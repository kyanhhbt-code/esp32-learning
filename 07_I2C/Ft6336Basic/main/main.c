#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "FT6336_BASIC";

// ==========================================
// 1. PIN CONFIGURATION & HARDWARE CONSTANTS
// ==========================================
#define I2C_PORT_NUM       I2C_NUM_0
#define TOUCH_PIN_SDA      4
#define TOUCH_PIN_SCL      5
#define TOUCH_PIN_RST      6
#define TOUCH_PIN_INT      7
#define I2C_MASTER_FREQ_HZ 400000

#define FT6336_I2C_ADDR 0x38

// Register Map of FocalTech FT6336U
#define FT6336_REG_DEV_MODE  0x00
#define FT6336_REG_GEST_ID   0x01
#define FT6336_REG_TD_STATUS 0x02
#define FT6336_REG_P1_XH     0x03
#define FT6336_REG_P1_XL     0x04
#define FT6336_REG_P1_YH     0x05
#define FT6336_REG_P1_YL     0x06
#define FT6336_REG_LIB_VER_H 0xA1
#define FT6336_REG_LIB_VER_L 0xA2
#define FT6336_REG_CHIP_ID   0xA3 // Should be 0x02 / 0x64
#define FT6336_REG_FOCAL_ID  0xA8 // Should be 0x11 for FocalTech

// Operating mode:
// 0 = Polling Mode (Periodic 20ms check)
// 1 = Interrupt Mode (Hardware INT pin + FreeRTOS Binary Semaphore)
#define USE_INTERRUPT_MODE 1

// ==========================================
// 2. DATA STRUCTURES & HANDLES
// ==========================================
typedef struct
{
    uint8_t touch_count;
    uint16_t x;
    uint16_t y;
    uint8_t event; // 0: Press Down, 1: Lift Up, 2: Contact, 3: No event
} ft6336_touch_data_t;

static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;
static SemaphoreHandle_t s_touch_sem = NULL;

// ==========================================
// 3. LOW-LEVEL I2C FUNCTIONS
// ==========================================
static esp_err_t ft6336_read_reg(uint8_t reg, uint8_t* val)
{
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, val, 1, 100);
}

static esp_err_t ft6336_read_bytes(uint8_t reg, uint8_t* buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, buf, len, 100);
}

// ==========================================
// 4. HARDWARE RESET & INITIALIZATION
// ==========================================
static void ft6336_hardware_reset(void)
{
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

        ESP_LOGI(TAG, "Hardware Resetting FT6336U (Pin GPIO %d)...", TOUCH_PIN_RST);
        gpio_set_level(TOUCH_PIN_RST, 0); // Pull LOW
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(TOUCH_PIN_RST, 1); // Pull HIGH to wake up chip
        vTaskDelay(pdMS_TO_TICKS(100));   // Wait for internal firmware initialization
    }
}

static esp_err_t ft6336_init(void)
{
    ft6336_hardware_reset();

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_bus_handle));

    // Add FT6336U device to bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle));

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
    ESP_LOGI(
        TAG, "FT6336U detected! FocalTech Vendor ID: 0x%02X (Expected: 0x11), Chip ID: 0x%02X", vendor_id, chip_id);

    return ESP_OK;
}

// ==========================================
// 5. TOUCH DATA PARSER (REGISTER BURST READ)
// ==========================================
static esp_err_t ft6336_read_touch(ft6336_touch_data_t* touch)
{
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
        touch->x = ((uint16_t) (raw[1] & 0x0F) << 8) | raw[2];
        touch->y = ((uint16_t) (raw[3] & 0x0F) << 8) | raw[4];
    }
    else
    {
        touch->x = 0;
        touch->y = 0;
        touch->event = 3; // No touch
    }

    return ESP_OK;
}

// ==========================================
// 6. INTERRUPT SERVICE ROUTINE (ISR)
// ==========================================
#if USE_INTERRUPT_MODE
static void IRAM_ATTR touch_gpio_isr_handler(void* arg)
{
    BaseType_t high_task_woken = pdFALSE;
    if (s_touch_sem != NULL)
    {
        xSemaphoreGiveFromISR(s_touch_sem, &high_task_woken);
    }
    if (high_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void touch_interrupt_init(void)
{
    s_touch_sem = xSemaphoreCreateBinary();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TOUCH_PIN_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Active-low INT pin, enable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // Trigger on falling edge when finger touches
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(TOUCH_PIN_INT, touch_gpio_isr_handler, NULL));

    ESP_LOGI(TAG, "Touch GPIO Interrupt installed on Pin GPIO %d (Active Low)", TOUCH_PIN_INT);
}
#endif

// ==========================================
// 7. TOUCH TASK
// ==========================================
static void touch_task(void* pvParameters)
{
    ft6336_touch_data_t touch;
    const char* event_str[] = {"PRESS_DOWN", "LIFT_UP", "CONTACT", "NO_EVENT"};

    while (1)
    {
#if USE_INTERRUPT_MODE
        // Interrupt Mode: Task sleeps with 0% CPU usage, waking up only on INT pin trigger
        if (xSemaphoreTake(s_touch_sem, portMAX_DELAY) == pdTRUE)
        {
            // Read coordinates continuously until finger is released
            while (1)
            {
                if (ft6336_read_touch(&touch) == ESP_OK)
                {
                    if (touch.touch_count > 0)
                    {
                        ESP_LOGI(TAG,
                                 "[INTERRUPT] Touch Detected -> Points: %d | Event: %-10s | X: %4d | Y: %4d",
                                 touch.touch_count,
                                 event_str[touch.event],
                                 touch.x,
                                 touch.y);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "[INTERRUPT] Touch Released.");
                        break;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(15)); // Smooth tracking while finger is held
            }
        }
#else
        // Polling Mode: Periodically wake up every 20ms
        if (ft6336_read_touch(&touch) == ESP_OK)
        {
            if (touch.touch_count > 0)
            {
                ESP_LOGI(TAG,
                         "[POLLING] Touch Detected -> Points: %d | Event: %-10s | X: %4d | Y: %4d",
                         touch.touch_count,
                         event_str[touch.event],
                         touch.x,
                         touch.y);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 50 Hz
#endif
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 FT6336U I2C Capacitive Touch Demo ===");

    ESP_ERROR_CHECK(ft6336_init());

#if USE_INTERRUPT_MODE
    ESP_LOGI(TAG, "Mode: INTERRUPT & FreeRTOS Semaphore (Zero-CPU Idle)");
    touch_interrupt_init();
#else
    ESP_LOGI(TAG, "Mode: POLLING (Periodic 20ms check)");
#endif

    xTaskCreatePinnedToCore(touch_task, "touch_task", 4096, NULL, 5, NULL, 1);
}
