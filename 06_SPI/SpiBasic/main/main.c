#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "SPI_BASIC";

// ESP32-S3 SPI Master Configuration
#ifndef CONFIG_SPI_MOSI_GPIO // Tx
#define CONFIG_SPI_MOSI_GPIO 11
#endif
#ifndef CONFIG_SPI_MISO_GPIO // Rx
#define CONFIG_SPI_MISO_GPIO 13
#endif
#ifndef CONFIG_SPI_SCLK_GPIO
#define CONFIG_SPI_SCLK_GPIO 12
#endif
#ifndef CONFIG_SPI_CS_GPIO
#define CONFIG_SPI_CS_GPIO 10
#endif
#ifndef CONFIG_SPI_CLOCK_SPEED_HZ
#define CONFIG_SPI_CLOCK_SPEED_HZ (10 * 1000 * 1000)
#endif
#ifndef CONFIG_SPI_MODE_SELECT
#define CONFIG_SPI_MODE_SELECT 0
#endif

static spi_device_handle_t s_spi_handle = NULL;

/**
 * @brief Initialize SPI Master bus and register SPI device
 */
static esp_err_t spi_master_init(void)
{
    ESP_LOGI(TAG,
             "Configuring SPI Master (MOSI=%d, MISO=%d, SCLK=%d, CS=%d, %d MHz, Mode %d)",
             CONFIG_SPI_MOSI_GPIO,
             CONFIG_SPI_MISO_GPIO,
             CONFIG_SPI_SCLK_GPIO,
             CONFIG_SPI_CS_GPIO,
             CONFIG_SPI_CLOCK_SPEED_HZ / (1000 * 1000),
             CONFIG_SPI_MODE_SELECT);

    // 1. Bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_SPI_MISO_GPIO,
        .sclk_io_num = CONFIG_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Device interface configuration
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = CONFIG_SPI_CLOCK_SPEED_HZ,
        .mode = CONFIG_SPI_MODE_SELECT,
        .spics_io_num = CONFIG_SPI_CS_GPIO,
        .queue_size = 7,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPI Master initialized successfully.");
    return ESP_OK;
}

/**
 * @brief Demo 1: Short payload polling transfer
 */
static void spi_demo_polling_loopback(void)
{
    uint8_t tx_buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rx_buf[4] = {0};

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = sizeof(tx_buf) * 8; // 32 bits
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;

    esp_err_t ret = spi_device_polling_transmit(s_spi_handle, &t);
    if (ret == ESP_OK)
    {
        if (memcmp(tx_buf, rx_buf, sizeof(tx_buf)) == 0)
        {
            ESP_LOGI(TAG,
                     "[Polling] Loopback SUCCESS: [0x%02X, 0x%02X, 0x%02X, 0x%02X]",
                     rx_buf[0],
                     rx_buf[1],
                     rx_buf[2],
                     rx_buf[3]);
        }
        else
        {
            ESP_LOGW(TAG,
                     "[Polling] Data mismatch! Check MOSI (GPIO %d) -> MISO (GPIO %d)",
                     CONFIG_SPI_MOSI_GPIO,
                     CONFIG_SPI_MISO_GPIO);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Polling transmit failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Demo 2: DMA string buffer transfer
 */
static void spi_demo_dma_string_loopback(void)
{
    const char* message = "Hello ESP32-S3 SPI Master!";
    size_t len = strlen(message);

    static uint8_t tx_str[64];
    static uint8_t rx_str[64];

    memset(tx_str, 0, sizeof(tx_str));
    memset(rx_str, 0, sizeof(rx_str));
    memcpy(tx_str, message, len);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = tx_str;
    t.rx_buffer = rx_str;

    esp_err_t ret = spi_device_transmit(s_spi_handle, &t);
    if (ret == ESP_OK)
    {
        if (memcmp(tx_str, rx_str, len) == 0)
        {
            ESP_LOGI(TAG, "[DMA] String Loopback SUCCESS: \"%s\"", (char*) rx_str);
        }
        else
        {
            ESP_LOGW(TAG, "[DMA] Data mismatch!");
        }
    }
    else
    {
        ESP_LOGE(TAG, "DMA transmit failed: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    ESP_LOGI(
        TAG, "Starting SPI Loopback Demo (Connect GPIO %d to GPIO %d)", CONFIG_SPI_MOSI_GPIO, CONFIG_SPI_MISO_GPIO);

    if (spi_master_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Aborting due to SPI initialization error.");
        return;
    }

    while (1)
    {
        // spi_demo_polling_loopback();
        // vTaskDelay(pdMS_TO_TICKS(500));

        spi_demo_dma_string_loopback();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
