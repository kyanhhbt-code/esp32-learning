#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TIMER_RESOLUTION_HZ 1000000 // 1 MHz -> 1 tick = 1 us
#define ALARM_PERIOD_US     1000    // 1,000 us = 1 ms (1000 Hz interrupt rate)

static const char* TAG = "TIMER_FLAG_SYNC";

/*
 * 1. Status flag and system tick counter (SysTick ms).
 * MUST be declared 'volatile' because these variables are written inside the ISR
 * and read asynchronously in the app_main while(1) superloop.
 */
static volatile bool s_timer_flag = false;
static volatile uint32_t s_system_ticks_ms = 0;

/*
 * Get system uptime in milliseconds (equivalent to HAL_GetTick() on STM32 or millis() on Arduino)
 */
uint32_t get_system_ticks_ms(void)
{
    return s_system_ticks_ms;
}

/*
 * Timer ISR Callback:
 * - Executes strictly in Hardware ISR Context.
 * - Extremely minimal (takes only nanoseconds): increments tick and sets flag.
 * - Never execute heavy or blocking logic here; delegate everything to while(1).
 */
static bool IRAM_ATTR timer_1ms_alarm_cb(gptimer_handle_t timer,
                                         const gptimer_alarm_event_data_t* edata,
                                         void* user_ctx)
{
    s_system_ticks_ms++;
    s_timer_flag = true;

    // Return false: standard interrupt, no high-priority task context switch requested
    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing GPTimer with 1 ms period (Flag-based & Non-blocking model)...");

    // 1. Configure GPTimer with 1 MHz resolution (1 tick = 1 us)
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    // 2. Register alarm callback
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_1ms_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    // 3. Set alarm period to 1000 us (1 ms) and enable hardware auto-reload
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = ALARM_PERIOD_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    // 4. Enable and start hardware timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    ESP_LOGI(TAG, "GPTimer 1 ms started successfully!");

    uint32_t last_log_time = 0;

    /*
     * 5. Main Superloop (classic while(1) of embedded systems):
     * Simple, direct, without unnecessary RTOS complexity.
     */
    while (1)
    {
        // Pattern A: Flag-based execution
        if (s_timer_flag)
        {
            s_timer_flag = false; // Clear flag immediately upon servicing

            // Execute time-critical periodic servicing here if needed...
        }

        // Pattern B: Non-blocking software timer pattern (similar to HAL_GetTick() / millis())
        // Log system uptime periodically every 1000 ms without blocking CPU
        uint32_t current_time = get_system_ticks_ms();
        if (current_time - last_log_time >= 1000)
        {
            last_log_time = current_time;
            ESP_LOGI(TAG, "[Superloop] Uptime: %lu s (%lu ms)", current_time / 1000, current_time);
        }

        // Block for 100 ms so IDLE can run and feed the task watchdog.
        // Note: pdMS_TO_TICKS(1) would be 0 ticks at CONFIG_FREERTOS_HZ=100 and would not yield to IDLE.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
