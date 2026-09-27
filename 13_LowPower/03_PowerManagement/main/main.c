#include <stdbool.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define LED_GPIO         GPIO_NUM_4

#define CPU_MAX_MHZ      160
#define CPU_MIN_MHZ      40 // = XTAL frequency, the usual choice (also what CONFIG_PM_DFS_INIT_AUTO uses)

#define BLINK_ON_MS      20
#define BLINK_PERIOD_MS  1000
#define STATS_PERIOD_MS  10000
#define BUTTON_POLL_MS   100 // Every poll is a wakeup: try 10 ms and compare the current (see README)

static const char *TAG = "pm_lab";

static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_SW:
            return "SW";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_USB:
            return "USB";
        default:
            return "OTHER";
    }
}

static esp_pm_lock_handle_t s_no_sleep_lock;
static esp_pm_lock_handle_t s_cpu_max_lock;
static bool s_busy = false;

// Mostly sleeping in vTaskDelay: while every task is blocked, the idle task lets the chip enter light sleep
static void blink_task(void* arg)
{
    while (1)
    {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS - BLINK_ON_MS));
    }
}

static void stats_task(void* arg)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(STATS_PERIOD_MS));
        ESP_LOGI(TAG, "Mode: %s. PM locks and time spent per mode:", s_busy ? "BUSY" : "IDLE (auto light sleep)");
        esp_pm_dump_locks(stdout); // Needs CONFIG_PM_PROFILING=y for the time columns
    }
}

// Toggles "busy mode". Holding ANY PM lock already prevents automatic light sleep;
// NO_LIGHT_SLEEP states the intent, CPU_FREQ_MAX additionally pins the CPU at max frequency.
static void button_task(void* arg)
{
    int last_level = 1;
    while (1)
    {
        int level = gpio_get_level(BOOT_BUTTON_GPIO);
        if (last_level == 1 && level == 0)
        {
            s_busy = !s_busy;
            if (s_busy)
            {
                ESP_ERROR_CHECK(esp_pm_lock_acquire(s_no_sleep_lock));
                ESP_ERROR_CHECK(esp_pm_lock_acquire(s_cpu_max_lock));
                ESP_LOGW(TAG, "BUSY: locks acquired -> no light sleep, CPU fixed at %d MHz", CPU_MAX_MHZ);
            }
            else
            {
                ESP_ERROR_CHECK(esp_pm_lock_release(s_cpu_max_lock));
                ESP_ERROR_CHECK(esp_pm_lock_release(s_no_sleep_lock));
                ESP_LOGI(TAG, "IDLE: locks released -> DFS + automatic light sleep allowed again");
            }
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(esp_reset_reason()));

    // Requires CONFIG_PM_ENABLE=y (and CONFIG_FREERTOS_USE_TICKLESS_IDLE=y for light sleep), see sdkconfig.defaults
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CPU_MAX_MHZ,
        .min_freq_mhz = CPU_MIN_MHZ,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "busy_no_sleep", &s_no_sleep_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "busy_cpu_max", &s_cpu_max_lock));

    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    xTaskCreate(blink_task, "blink", 2048, NULL, 4, NULL);
    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);
    xTaskCreate(stats_task, "stats", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "PM: %d..%d MHz, automatic light sleep ON. Press BOOT to toggle busy mode.", CPU_MIN_MHZ,
             CPU_MAX_MHZ);
}
