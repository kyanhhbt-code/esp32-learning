#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "I2C_SCANNER";

#define I2C_PORT_NUM       I2C_NUM_0
#define TOUCH_PIN_SDA      4
#define TOUCH_PIN_SCL      5
#define TOUCH_PIN_RST      6
#define I2C_MASTER_FREQ_HZ 400000

static i2c_master_bus_handle_t bus_handle = NULL;

static void i2c_touch_hardware_reset(void)
{
    // Pull FT6336U reset pin HIGH for normal operation
    if (TOUCH_PIN_RST >= 0)
    {
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << TOUCH_PIN_RST),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);

        ESP_LOGI(TAG, "Resetting touch controller on GPIO %d...", TOUCH_PIN_RST);
        gpio_set_level(TOUCH_PIN_RST, 0); // Pull LOW to reset
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(TOUCH_PIN_RST, 1); // Pull HIGH to wake up
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void i2c_bus_init(void)
{
    i2c_touch_hardware_reset();

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    ESP_LOGI(TAG, "I2C Master Bus initialized successfully on SDA: GPIO %d, SCL: GPIO %d",
             TOUCH_PIN_SDA, TOUCH_PIN_SCL);
}

static void i2c_scan_bus(void)
{
    uint8_t devices_found = 0;

    printf("\n==================== I2C BUS SCANNER (0x01 - 0x7F) ====================\n");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (uint8_t i = 0; i < 128; i += 16)
    {
        printf("%02x: ", i);
        for (uint8_t j = 0; j < 16; j++)
        {
            uint8_t address = i + j;

            // Standard valid 7-bit I2C address range is 0x01 to 0x7F (0x00 is General Call)
            if (address == 0 || address > 0x7F)
            {
                printf("   ");
                continue;
            }

            // Probe to check for ACK response from slave
            esp_err_t ret = i2c_master_probe(bus_handle, address, 20);

            if (ret == ESP_OK)
            {
                printf("%02x ", address);
                devices_found++;
            }
            else
            {
                printf("-- ");
            }
        }
        printf("\n");
    }

    printf("-----------------------------------------------------------------------\n");
    printf("Total devices discovered: %d\n", devices_found);

    // Identify known devices based on detected addresses
    for (uint8_t address = 1; address <= 0x7F; address++)
    {
        if (i2c_master_probe(bus_handle, address, 20) == ESP_OK)
        {
            if (address == 0x38)
            {
                ESP_LOGI(TAG, "-> [0x38]: FT6336U Capacitive Touch Controller (FocalTech) DETECTED!");
            }
            else if (address == 0x68 || address == 0x69)
            {
                ESP_LOGI(TAG, "-> [0x%02X]: MPU6050 IMU Accelerometer/Gyro DETECTED!", address);
            }
            else if (address == 0x3C || address == 0x3D)
            {
                ESP_LOGI(TAG, "-> [0x%02X]: SSD1306 / SH1106 OLED Display DETECTED!", address);
            }
            else
            {
                ESP_LOGI(TAG, "-> [0x%02X]: Unknown I2C Device DETECTED!", address);
            }
        }
    }

    if (devices_found == 0)
    {
        ESP_LOGW(TAG, "Warning: No I2C devices detected! Please check:");
        ESP_LOGW(TAG, "1. SDA (GPIO %d) and SCL (GPIO %d) connections.", TOUCH_PIN_SDA, TOUCH_PIN_SCL);
        ESP_LOGW(TAG, "2. Are pull-up resistors connected (2.2k - 4.7k ohm)?");
        ESP_LOGW(TAG, "3. Is TOUCH_PIN_RST (GPIO %d) pulled HIGH?", TOUCH_PIN_RST);
    }
    printf("=======================================================================\n\n");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting I2C Scanner Lab for Session 7...");
    i2c_bus_init();

    while (1)
    {
        i2c_scan_bus();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
