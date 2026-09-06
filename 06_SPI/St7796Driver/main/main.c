#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

// Official esp_lcd framework and ST7796 driver
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"

static const char* TAG = "ST7796_DRIVER";

#ifndef CONFIG_LCD_MOSI_GPIO
#define CONFIG_LCD_MOSI_GPIO 11
#endif
#ifndef CONFIG_LCD_MISO_GPIO
#define CONFIG_LCD_MISO_GPIO 13
#endif
#ifndef CONFIG_LCD_SCLK_GPIO
#define CONFIG_LCD_SCLK_GPIO 12
#endif
#ifndef CONFIG_LCD_CS_GPIO
#define CONFIG_LCD_CS_GPIO 10
#endif
#ifndef CONFIG_LCD_DC_GPIO
#define CONFIG_LCD_DC_GPIO 9
#endif
#ifndef CONFIG_LCD_RST_GPIO
#define CONFIG_LCD_RST_GPIO 14
#endif
#ifndef CONFIG_LCD_BLK_GPIO
#define CONFIG_LCD_BLK_GPIO 2
#endif
#ifndef CONFIG_LCD_H_RES
#define CONFIG_LCD_H_RES 480
#endif
#ifndef CONFIG_LCD_V_RES
#define CONFIG_LCD_V_RES 320
#endif
#ifndef CONFIG_LCD_PIXEL_CLOCK_HZ
#define CONFIG_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#endif

// 16-bit RGB565 Colors
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

#define LCD_HOST   SPI2_HOST
#define CHUNK_ROWS 40

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static SemaphoreHandle_t s_trans_done_sem = NULL;
static uint16_t* s_draw_buf = NULL;

static bool
on_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done_sem, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

/**
 * @brief Synchronous draw bitmap to avoid DMA queue overflow
 */
static void lcd_draw_bitmap_sync(int x_start, int y_start, int x_end, int y_end, const void* color_data)
{
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel_handle, x_start, y_start, x_end, y_end, color_data);
    if (ret == ESP_OK)
    {
        xSemaphoreTake(s_trans_done_sem, portMAX_DELAY);
    }
    else
    {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Initialize ST7796 display using ESP-IDF esp_lcd framework
 */
static esp_err_t init_st7796_display(void)
{
    ESP_LOGI(TAG,
             "Initializing SPI bus (MOSI=%d, MISO=%d, SCLK=%d, CS=%d, DC=%d, BLK=%d)...",
             CONFIG_LCD_MOSI_GPIO,
             CONFIG_LCD_MISO_GPIO,
             CONFIG_LCD_SCLK_GPIO,
             CONFIG_LCD_CS_GPIO,
             CONFIG_LCD_DC_GPIO,
             CONFIG_LCD_BLK_GPIO);

    s_trans_done_sem = xSemaphoreCreateBinary();
    assert(s_trans_done_sem != NULL);

    // 1. Initialize SPI Bus with DMA
    spi_bus_config_t buscfg = {
        .sclk_io_num = CONFIG_LCD_SCLK_GPIO,
        .mosi_io_num = CONFIG_LCD_MOSI_GPIO,
        .miso_io_num = CONFIG_LCD_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_LCD_H_RES * CHUNK_ROWS * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Install Panel IO SPI driver
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = CONFIG_LCD_DC_GPIO,
        .cs_gpio_num = CONFIG_LCD_CS_GPIO,
        .pclk_hz = CONFIG_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) LCD_HOST, &io_config, &s_io_handle));

    // 3. Install ST7796 panel driver
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_LCD_RST_GPIO,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(s_io_handle, &panel_config, &s_panel_handle));

    // 4. Initialize hardware panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));

    // Set rotation: Landscape (MV=1) with BGR color order
    uint8_t madctl_val = 0x20 | 0x80 | 0x40 | 0x08;
    esp_lcd_panel_io_tx_param(s_io_handle, 0x36, &madctl_val, 1);

    // 5. Turn on backlight (GPIO 2)
    if (CONFIG_LCD_BLK_GPIO >= 0)
    {
        gpio_set_direction(CONFIG_LCD_BLK_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(CONFIG_LCD_BLK_GPIO, 1);
    }

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // Allocate draw buffer in DMA-capable RAM
    s_draw_buf = (uint16_t*) heap_caps_malloc(CONFIG_LCD_H_RES * CHUNK_ROWS * sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(s_draw_buf != NULL);

    ESP_LOGI(TAG,
             "ST7796 display initialized successfully (%dx%d @ %d MHz)",
             CONFIG_LCD_H_RES,
             CONFIG_LCD_V_RES,
             CONFIG_LCD_PIXEL_CLOCK_HZ / (1000 * 1000));
    return ESP_OK;
}

/**
 * @brief Fill entire screen with a solid color
 */
static void lcd_fill_screen(uint16_t color)
{
    uint16_t color_be = (color >> 8) | (color << 8);
    size_t chunk_pixels = CONFIG_LCD_H_RES * CHUNK_ROWS;
    for (size_t i = 0; i < chunk_pixels; i++)
    {
        s_draw_buf[i] = color_be;
    }

    for (int y = 0; y < CONFIG_LCD_V_RES; y += CHUNK_ROWS)
    {
        int rows = (y + CHUNK_ROWS > CONFIG_LCD_V_RES) ? (CONFIG_LCD_V_RES - y) : CHUNK_ROWS;
        lcd_draw_bitmap_sync(0, y, CONFIG_LCD_H_RES, y + rows, s_draw_buf);
    }
}

/**
 * @brief Fill rectangular region with a solid color
 */
static void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x >= CONFIG_LCD_H_RES || y >= CONFIG_LCD_V_RES || w <= 0 || h <= 0)
    {
        return;
    }
    if ((x + w) > CONFIG_LCD_H_RES)
    {
        w = CONFIG_LCD_H_RES - x;
    }
    if ((y + h) > CONFIG_LCD_V_RES)
    {
        h = CONFIG_LCD_V_RES - y;
    }

    uint16_t color_be = (color >> 8) | (color << 8);
    size_t total_pixels = w * h;
    size_t max_buf_pixels = CONFIG_LCD_H_RES * CHUNK_ROWS;

    if (total_pixels <= max_buf_pixels)
    {
        for (size_t i = 0; i < total_pixels; i++)
        {
            s_draw_buf[i] = color_be;
        }
        lcd_draw_bitmap_sync(x, y, x + w, y + h, s_draw_buf);
    }
    else
    {
        for (int i = 0; i < w; i++)
        {
            s_draw_buf[i] = color_be;
        }
        for (int row = y; row < y + h; row++)
        {
            lcd_draw_bitmap_sync(x, row, x + w, row + 1, s_draw_buf);
        }
    }
}

/**
 * @brief Draw 4-quadrant color test pattern
 */
static void lcd_draw_test_pattern(void)
{
    int half_w = CONFIG_LCD_H_RES / 2;
    int half_h = CONFIG_LCD_V_RES / 2;

    lcd_fill_rect(0, 0, half_w, half_h, COLOR_RED);
    lcd_fill_rect(half_w, 0, half_w, half_h, COLOR_GREEN);
    lcd_fill_rect(0, half_h, half_w, half_h, COLOR_BLUE);
    lcd_fill_rect(half_w, half_h, half_w, half_h, COLOR_YELLOW);

    // Center white rectangle
    int center_w = 120;
    int center_h = 60;
    int cx = (CONFIG_LCD_H_RES - center_w) / 2;
    int cy = (CONFIG_LCD_V_RES - center_h) / 2;
    lcd_fill_rect(cx, cy, center_w, center_h, COLOR_WHITE);
}

/**
 * @brief Run smooth bouncing box animation
 */
static void lcd_run_bouncing_box(int duration_sec)
{
    lcd_fill_screen(COLOR_BLACK);

    int size = 40;
    int x = 20;
    int y = 20;
    int dx = 6;
    int dy = 5;

    int total_frames = duration_sec * 50;
    for (int i = 0; i < total_frames; i++)
    {
        // Erase old box
        lcd_fill_rect(x, y, size, size, COLOR_BLACK);

        x += dx;
        y += dy;

        if (x <= 0 || (x + size) >= CONFIG_LCD_H_RES)
        {
            dx = -dx;
        }
        if (y <= 0 || (y + size) >= CONFIG_LCD_V_RES)
        {
            dy = -dy;
        }

        // Draw new box
        lcd_fill_rect(x, y, size, size, COLOR_CYAN);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ST7796 esp_lcd Driver Demo...");

    if (init_st7796_display() != ESP_OK)
    {
        ESP_LOGE(TAG, "Display initialization failed!");
        return;
    }

    while (1)
    {
        ESP_LOGI(TAG, "Running demo: Full screen color cycling...");
        lcd_fill_screen(COLOR_RED);
        vTaskDelay(pdMS_TO_TICKS(800));

        lcd_fill_screen(COLOR_GREEN);
        vTaskDelay(pdMS_TO_TICKS(800));

        lcd_fill_screen(COLOR_BLUE);
        vTaskDelay(pdMS_TO_TICKS(800));

        ESP_LOGI(TAG, "Running demo: 4-Quadrant test pattern...");
        lcd_draw_test_pattern();
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Running demo: Smooth bouncing box animation...");
        lcd_run_bouncing_box(5);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
