#include <stdio.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_PWM_GPIO            GPIO_NUM_7          // Target LED pin: GPIO 7
#define LEDC_TIMER_MODE         LEDC_LOW_SPEED_MODE // Low speed mode for ESP32-S3
#define LEDC_TIMER_SEL          LEDC_TIMER_0        // Use LEDC Timer 0
#define LEDC_CHANNEL_SEL        LEDC_CHANNEL_0      // Use LEDC Channel 0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT   // 10-bit resolution -> Duty ranges from 0 to 1023 (2^10 - 1)
#define LEDC_MAX_DUTY           ((1 << 10) - 1)     // 1023 (100% duty cycle)
#define LEDC_FREQUENCY_HZ       5000                // 5 kHz PWM frequency (inaudible & flicker-free)

static const char *TAG = "PWM_LED";

void app_main(void)
{
    ESP_LOGI(TAG, "Configuring LEDC Hardware PWM on GPIO %d...", LED_PWM_GPIO);

    // 1. Configure the LEDC Timer
    // Specifies PWM frequency and duty resolution
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_TIMER_MODE,
        .timer_num        = LEDC_TIMER_SEL,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Configure the LEDC Channel
    // Binds the configured timer to target GPIO pin and sets initial duty cycle
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_TIMER_MODE,
        .channel        = LEDC_CHANNEL_SEL,
        .timer_sel      = LEDC_TIMER_SEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_PWM_GPIO,
        .duty           = 0, // Initial duty cycle: 0% (LED OFF)
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "LEDC PWM initialized successfully! Frequency: %d Hz, Max Duty: %d", 
             LEDC_FREQUENCY_HZ, LEDC_MAX_DUTY);

    /*
     * 3. Main Superloop:
     * Generate a smooth breathing LED effect (Fade-in and Fade-out).
     * Note: Once the duty is updated in the hardware register,
     * the hardware peripheral generates the PWM signal continuously with 0% CPU load!
     */
    while (1) {
        // Fade in: increase duty from 0% to 100%
        for (int duty = 0; duty <= LEDC_MAX_DUTY; duty += 15) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_TIMER_MODE, LEDC_CHANNEL_SEL, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_TIMER_MODE, LEDC_CHANNEL_SEL));
            vTaskDelay(pdMS_TO_TICKS(15));
        }

        // Fade out: decrease duty from 100% to 0%
        for (int duty = LEDC_MAX_DUTY; duty >= 0; duty -= 15) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_TIMER_MODE, LEDC_CHANNEL_SEL, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_TIMER_MODE, LEDC_CHANNEL_SEL));
            vTaskDelay(pdMS_TO_TICKS(15));
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
