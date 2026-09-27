#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TIMER_PERIOD_MS     500         // 500 ms period for both timers
#define TIMER_PERIOD_US     (TIMER_PERIOD_MS * 1000)

static const char *TAG = "timer_compare";

static volatile uint32_t s_gptimer_count = 0;
static volatile uint32_t s_esptimer_count = 0;

/*
 * 1. Hardware Timer (GPTimer) Callback
 * Execution context: Hardware ISR (Direct vector interrupt)
 */
static bool IRAM_ATTR gptimer_cb(gptimer_handle_t timer, 
                                 const gptimer_alarm_event_data_t *edata, 
                                 void *user_ctx)
{
    s_gptimer_count++;
    return false;
}

/*
 * 2. High-Resolution Software Timer (esp_timer) Callback
 * Execution context: Task Context (Dispatched inside background esp_timer task)
 */
static void esptimer_cb(void *arg)
{
    s_esptimer_count++;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting experiment: GPTimer (Hardware) vs esp_timer (Software)...");

    // -------------------------------------------------------------
    // Step A: Initialize Hardware Timer (GPTimer)
    // -------------------------------------------------------------
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t gptimer_cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz -> 1 us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&gptimer_cfg, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = gptimer_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = TIMER_PERIOD_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_cfg));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    // -------------------------------------------------------------
    // Step B: Initialize Software Timer (esp_timer)
    // -------------------------------------------------------------
    esp_timer_handle_t esptimer = NULL;
    esp_timer_create_args_t esptimer_args = {
        .callback = esptimer_cb,
        .name = "periodic_esptimer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&esptimer_args, &esptimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(esptimer, TIMER_PERIOD_US));

    // -------------------------------------------------------------
    // Step C: Main Superloop (while(1))
    // -------------------------------------------------------------
    int cycle = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        cycle++;

        ESP_LOGI(TAG, "[REPORT] GPTimer (HW): %lu ticks | esp_timer (SW): %lu ticks",
                 s_gptimer_count, s_esptimer_count);

        // Every 3 seconds, simulate a heavy compute loop blocking CPU for 100 ms
        if (cycle % 3 == 0) {
            ESP_LOGW(TAG, ">>> [BUSY DELAY] CPU busy processing for 100 ms in while(1)! <<<");
            esp_rom_delay_us(100000); // 100,000 us = 100 ms busy-wait starvation
        }
    }
}
