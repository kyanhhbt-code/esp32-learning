#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO            GPIO_NUM_7
#define TIMER_RESOLUTION_HZ 1000000 // 1 MHz -> 1 tick = 1 microsecond (us)
#define ALARM_PERIOD_US     1000000 // 1,000,000 us = 1 second

static const char* TAG = "GPTIMER_BASIC";
static volatile uint32_t s_led_state = 0;

/*
 * Alarm event callback.
 * Note: This function executes in Hardware ISR context.
 * - IRAM_ATTR ensures code resides in internal SRAM.
 * - Do not call blocking APIs or printf from within this ISR.
 */
static bool IRAM_ATTR example_timer_on_alarm_cb(gptimer_handle_t timer,
                                                const gptimer_alarm_event_data_t* edata,
                                                void* user_ctx)
{
    // Toggle LED state
    s_led_state = !s_led_state;
    gpio_set_level(LED_GPIO, s_led_state);

    // Return false since no high-priority task needs immediate wake-up
    return false;
}

void app_main(void)
{
    // 1. Configure LED GPIO output
    ESP_LOGI(TAG, "Initializing LED GPIO: %d", LED_GPIO);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_GPIO, 0);

    // 2. Allocate and configure GPTimer (Resolution = 1 MHz)
    ESP_LOGI(TAG, "Initializing GPTimer handle (1 MHz)...");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,   // APB_CLK (80 MHz)
        .direction = GPTIMER_COUNT_UP,        // Count up
        .resolution_hz = TIMER_RESOLUTION_HZ, // 1 tick = 1 us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    // 3. Register alarm event callback
    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    // 4. Configure alarm action (1s period, auto-reload)
    ESP_LOGI(TAG, "Configuring alarm: %d us, auto-reload...", ALARM_PERIOD_US);
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = ALARM_PERIOD_US,     // 1,000,000 us = 1s
        .reload_count = 0,                  // Reset to 0 on alarm
        .flags.auto_reload_on_alarm = true, // Periodic timer
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    // 5. Enable and start timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_LOGI(TAG, "Starting GPTimer!");
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    // Periodic monitoring loop
    while (1)
    {
        uint64_t count_val = 0;
        gptimer_get_raw_count(gptimer, &count_val);
        ESP_LOGI(TAG, "Raw Counter: %llu us | LED: %s", count_val, s_led_state ? "ON" : "OFF");
        // Sample counter every 250 ms (shows counter incrementing from 0 to 1,000,000 us)
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
