#include "ft6336.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "FT6336_BASIC";

static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

esp_err_t ft6336_read_reg(uint8_t reg, uint8_t* val)
{
    if (s_dev_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, val, 1, 100);
}

esp_err_t ft6336_read_bytes(uint8_t reg, uint8_t* buf, size_t len)
{
    if (s_dev_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, buf, len, 100);
}

void ft6336_hardware_reset(void)
{
    if (CONFIG_TOUCH_RST_GPIO >= 0)
    {
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << CONFIG_TOUCH_RST_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);

        ESP_LOGI(TAG, "Hardware Resetting FT6336U (Pin GPIO %d)...", CONFIG_TOUCH_RST_GPIO);
        gpio_set_level(CONFIG_TOUCH_RST_GPIO, 0); // Pull LOW
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(CONFIG_TOUCH_RST_GPIO, 1); // Pull HIGH to wake up chip
        vTaskDelay(pdMS_TO_TICKS(100));           // Wait for internal firmware initialization
    }
}

esp_err_t ft6336_init(void)
{
    ft6336_hardware_reset();

    i2c_master_bus_config_t bus_config = {
        .i2c_port = CONFIG_TOUCH_I2C_PORT,
        .sda_io_num = CONFIG_TOUCH_SDA_GPIO,
        .scl_io_num = CONFIG_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add FT6336U device to bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336_I2C_ADDR,
        .scl_speed_hz = CONFIG_TOUCH_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read & verify Vendor ID and Chip ID registers
    uint8_t vendor_id = 0;
    uint8_t chip_id = 0;

    esp_err_t err = ft6336_read_reg(FT6336_REG_FOCAL_ID, &vendor_id);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to communicate with FT6336U! Error: %s", esp_err_to_name(err));
        return err;
    }

    ft6336_read_reg(FT6336_REG_CHIP_ID, &chip_id);
    ESP_LOGI(TAG,
             "FT6336U detected! FocalTech Vendor ID: 0x%02X (Expected: 0x11), Chip ID: 0x%02X",
             vendor_id,
             chip_id);

    return ESP_OK;
}

esp_err_t ft6336_read_touch(ft6336_touch_data_t* touch)
{
    if (touch == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[6] = {0};

    // Read 5 consecutive bytes starting from register 0x02 (TD_STATUS)
    // [0] = TD_STATUS (Touch point count)
    // [1] = P1_XH (Event flag & X high 4 bits)
    // [2] = P1_XL (X low 8 bits)
    // [3] = P1_YH (Touch ID & Y high 4 bits)
    // [4] = P1_YL (Y low 8 bits)
    esp_err_t ret = ft6336_read_bytes(FT6336_REG_TD_STATUS, raw, 5);
    if (ret != ESP_OK)
    {
        return ret;
    }

    touch->touch_count = raw[0] & 0x0F;
    if (touch->touch_count > 2)
    {
        touch->touch_count = 0;
    }

    if (touch->touch_count > 0)
    {
        touch->event = (raw[1] >> 6) & 0x03;
        touch->x = ((uint16_t)(raw[1] & 0x0F) << 8) | raw[2];
        touch->y = ((uint16_t)(raw[3] & 0x0F) << 8) | raw[4];
    }
    else
    {
        touch->x = 0;
        touch->y = 0;
        touch->event = 3; // No touch
    }

    return ESP_OK;
}
