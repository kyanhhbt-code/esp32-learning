#include <inttypes.h>
#include <sys/time.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WAKEUP_BUTTON_GPIO GPIO_NUM_0 // BOOT button on the board. Any RTC GPIO (0..21 on ESP32-S3) works for ext0
#define LED_GPIO           GPIO_NUM_4 // "Awake" indicator: ON while the CPU is running
#define AWAKE_TIME_MS      3000
#define SLEEP_TIME_S       10

static const char* TAG = "deep_sleep_lab";

// RTC slow memory: survives deep sleep (not power loss)
static RTC_DATA_ATTR uint32_t s_boot_count = 0;
static RTC_DATA_ATTR struct timeval s_sleep_enter_time;

// Normal SRAM: powered down in deep sleep, so this is always 1 after every wakeup
static uint32_t s_ram_counter = 0;

static const char* reset_reason_str(esp_reset_reason_t reason)
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

static const char* wakeup_cause_str(esp_sleep_wakeup_cause_t cause)
{
    switch (cause)
    {
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            return "UNDEFINED (not a deep sleep wakeup)";
        case ESP_SLEEP_WAKEUP_TIMER:
            return "TIMER";
        case ESP_SLEEP_WAKEUP_EXT0:
            return "EXT0 (button)";
        case ESP_SLEEP_WAKEUP_EXT1:
            return "EXT1";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            return "TOUCHPAD";
        case ESP_SLEEP_WAKEUP_ULP:
            return "ULP";
        default:
            return "OTHER";
    }
}

void app_main(void)
{
    struct timeval now;
    gettimeofday(&now, NULL); // The RTC timer keeps counting during deep sleep

    s_boot_count++;
    s_ram_counter++;

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG,
             "Boot #%" PRIu32 " | RAM counter = %" PRIu32 " | reset reason: %s",
             s_boot_count,
             s_ram_counter,
             reset_reason_str(esp_reset_reason()));
    ESP_LOGI(TAG, "Wakeup cause: %s", wakeup_cause_str(cause));

    if (cause != ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        int64_t slept_ms = (int64_t) (now.tv_sec - s_sleep_enter_time.tv_sec) * 1000 +
                           (now.tv_usec - s_sleep_enter_time.tv_usec) / 1000;
        ESP_LOGI(TAG, "Slept for %" PRId64 " ms", slept_ms);
    }

    // LED on while awake
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LED_GPIO, 1);

    // Button as a normal input for now, only to check it has been released
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << WAKEUP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    ESP_LOGI(TAG, "Awake for %d ms (this is where a real device does its work)...", AWAKE_TIME_MS);
    vTaskDelay(pdMS_TO_TICKS(AWAKE_TIME_MS));

    // ext0 is level-triggered: sleeping while the button is still pressed means waking up immediately
    while (gpio_get_level(WAKEUP_BUTTON_GPIO) == 0)
    {
        ESP_LOGW(TAG, "Release the button to go to sleep...");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Contacts bounce on release: a bounce during sleep entry is a false EXT0 wakeup

    // Wakeup source 1: RTC timer
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t) SLEEP_TIME_S * 1000000ULL));

    // Wakeup source 2: ext0 = one RTC GPIO, wake when it reads 0.
    // The digital GPIO pull-up is lost in deep sleep, so use the RTC pull-up instead.
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_BUTTON_GPIO, 0));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(WAKEUP_BUTTON_GPIO));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(WAKEUP_BUTTON_GPIO));

    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "Entering deep sleep: wake in %d s or when GPIO%d goes low", SLEEP_TIME_S, WAKEUP_BUTTON_GPIO);
    gettimeofday(&s_sleep_enter_time, NULL);

    esp_deep_sleep_start(); // Never returns: the next wakeup starts again from the bootloader
}
