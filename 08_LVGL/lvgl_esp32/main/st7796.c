#include "st7796.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "ST7796_BASIC";

static spi_device_handle_t s_spi = NULL;

void st7796_send_cmd(uint8_t cmd)
{
    if (CONFIG_LCD_DC_GPIO >= 0)
    {
        gpio_set_level(CONFIG_LCD_DC_GPIO, 0); // D/C = 0: Command
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;

    spi_device_polling_transmit(s_spi, &t);
}

void st7796_send_data(const uint8_t* data, size_t len)
{
    if (len == 0)
    {
        return;
    }

    if (CONFIG_LCD_DC_GPIO >= 0)
    {
        gpio_set_level(CONFIG_LCD_DC_GPIO, 1); // D/C = 1: Data
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;

    spi_device_polling_transmit(s_spi, &t);
}

void st7796_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Column Address Set (CASET)
    st7796_send_cmd(0x2A);
    uint8_t data_x[4] = {(uint8_t) (x0 >> 8), (uint8_t) (x0 & 0xFF), (uint8_t) (x1 >> 8), (uint8_t) (x1 & 0xFF)};
    st7796_send_data(data_x, sizeof(data_x));

    // Row Address Set (RASET)
    st7796_send_cmd(0x2B);
    uint8_t data_y[4] = {(uint8_t) (y0 >> 8), (uint8_t) (y0 & 0xFF), (uint8_t) (y1 >> 8), (uint8_t) (y1 & 0xFF)};
    st7796_send_data(data_y, sizeof(data_y));

    // Memory Write (RAMWR)
    st7796_send_cmd(0x2C);
}

void st7796_fill_screen(uint16_t color)
{
    st7796_fill_rect(0, 0, CONFIG_LCD_WIDTH, CONFIG_LCD_HEIGHT, color);
}

void st7796_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((x >= CONFIG_LCD_WIDTH) || (y >= CONFIG_LCD_HEIGHT) || (w == 0) || (h == 0))
    {
        return;
    }
    if ((x + w - 1) >= CONFIG_LCD_WIDTH)
    {
        w = CONFIG_LCD_WIDTH - x;
    }
    if ((y + h - 1) >= CONFIG_LCD_HEIGHT)
    {
        h = CONFIG_LCD_HEIGHT - y;
    }

    st7796_set_window(x, y, x + w - 1, y + h - 1);

    uint16_t color_be = (color >> 8) | (color << 8);

    static uint16_t line_buffer[480];
    uint16_t line_len = (w > 480) ? 480 : w;
    for (int i = 0; i < line_len; i++)
    {
        line_buffer[i] = color_be;
    }

    for (int row = 0; row < h; row++)
    {
        st7796_send_data((uint8_t*) line_buffer, w * sizeof(uint16_t));
    }
}

void st7796_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= CONFIG_LCD_WIDTH) || (y >= CONFIG_LCD_HEIGHT))
    {
        return;
    }
    st7796_set_window(x, y, x, y);
    uint8_t data[2] = {(uint8_t) (color >> 8), (uint8_t) (color & 0xFF)};
    st7796_send_data(data, sizeof(data));
}

esp_err_t st7796_init(void)
{
    ESP_LOGI(TAG,
             "Initializing ST7796 (MOSI=%d, SCLK=%d, CS=%d, DC=%d, RST=%d, BLK=%d)...",
             CONFIG_LCD_MOSI_GPIO,
             CONFIG_LCD_SCLK_GPIO,
             CONFIG_LCD_CS_GPIO,
             CONFIG_LCD_DC_GPIO,
             CONFIG_LCD_RST_GPIO,
             CONFIG_LCD_BLK_GPIO);

    // 1. Configure control GPIOs
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    uint64_t pin_mask = 0;
    if (CONFIG_LCD_DC_GPIO >= 0)
        pin_mask |= (1ULL << CONFIG_LCD_DC_GPIO);
    if (CONFIG_LCD_RST_GPIO >= 0)
        pin_mask |= (1ULL << CONFIG_LCD_RST_GPIO);
    if (CONFIG_LCD_BLK_GPIO >= 0)
        pin_mask |= (1ULL << CONFIG_LCD_BLK_GPIO);

    if (pin_mask > 0)
    {
        io_conf.pin_bit_mask = pin_mask;
        gpio_config(&io_conf);
    }

    if (CONFIG_LCD_BLK_GPIO >= 0)
    {
        gpio_set_level(CONFIG_LCD_BLK_GPIO, 1);
    }

    // 2. Initialize SPI Master Bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = CONFIG_LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_LCD_WIDTH * 2 * sizeof(uint16_t),
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Register SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = CONFIG_LCD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_LCD_CS_GPIO,
        .queue_size = 7,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Hardware reset
    if (CONFIG_LCD_RST_GPIO >= 0)
    {
        gpio_set_level(CONFIG_LCD_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(CONFIG_LCD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // 5. ST7796 initialization sequence
    st7796_send_cmd(0x01); // Software reset
    vTaskDelay(pdMS_TO_TICKS(150));

    st7796_send_cmd(0x11); // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    st7796_send_cmd(0x3A); // Color mode: 16-bit RGB565
    uint8_t colmod = 0x55;
    st7796_send_data(&colmod, 1);

    st7796_send_cmd(0x36); // MADCTL: Landscape + BGR mode
    // uint8_t madctl = 0x20 | 0x80 | 0x40 | 0x08; // Landscape 0 deg (MV | MY | MX | BGR)
    uint8_t madctl = 0x20 | 0x08; // Landscape 180 deg (MV | BGR)
    st7796_send_data(&madctl, 1);

    st7796_send_cmd(0x21); // Display inversion ON
    vTaskDelay(pdMS_TO_TICKS(10));

    st7796_send_cmd(0x13); // Normal display ON
    vTaskDelay(pdMS_TO_TICKS(10));

    st7796_send_cmd(0x29); // Display ON
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG,
             "ST7796 initialized successfully (%dx%d @ %d MHz).",
             CONFIG_LCD_WIDTH,
             CONFIG_LCD_HEIGHT,
             CONFIG_LCD_SPI_CLOCK_HZ / (1000 * 1000));
    return ESP_OK;
}
