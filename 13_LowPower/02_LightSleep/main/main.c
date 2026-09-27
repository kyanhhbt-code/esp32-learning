#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/uart_pins.h"

#define BOOT_BUTTON_GPIO     GPIO_NUM_0 // Safe here: light sleep wakeup is not a chip reset, strapping is not re-sampled
#define CONSOLE_UART         CONFIG_ESP_CONSOLE_UART_NUM
#define UART_WAKEUP_EDGES    3 // Number of RX edges needed to wake up (the characters themselves are lost)
#define SLEEP_TIME_MS        5000
#define TICKER_PERIOD_MS     1000

static const char *TAG = "light_sleep_lab";

// Normal SRAM: retained in light sleep, so this keeps counting across sleeps
static uint32_t s_sleep_cycles = 0;

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

static const char *wakeup_cause_str(esp_sleep_wakeup_cause_t cause)
{
    switch (cause)
    {
        case ESP_SLEEP_WAKEUP_TIMER:
            return "TIMER";
        case ESP_SLEEP_WAKEUP_GPIO:
            return "GPIO (BOOT button)";
        case ESP_SLEEP_WAKEUP_UART:
            return "UART (key pressed in monitor)";
        default:
            return "OTHER";
    }
}

// Runs every second while the chip is awake. During light sleep it is frozen like every other task.
static void ticker_task(void* arg)
{
    uint32_t ticks = 0;
    while (1)
    {
        ESP_LOGI(TAG, "  ticker %" PRIu32 " (t=%" PRId64 " ms)", ++ticks, esp_timer_get_time() / 1000);
        vTaskDelay(pdMS_TO_TICKS(TICKER_PERIOD_MS));
    }
}

static void register_wakeup_sources(void)
{
    // 1. Timer
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_MS * 1000));

    // 2. GPIO: any GPIO can wake from light sleep, level-triggered only
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
    ESP_ERROR_CHECK(gpio_wakeup_enable(BOOT_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    // 3. UART: the RX pin must stay an input with pull-up while sleeping, or it may float and wake at random
    ESP_ERROR_CHECK(gpio_sleep_set_direction(U0RXD_GPIO_NUM, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_sleep_set_pull_mode(U0RXD_GPIO_NUM, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(uart_set_wakeup_threshold(CONSOLE_UART, UART_WAKEUP_EDGES));
    ESP_ERROR_CHECK(esp_sleep_enable_uart_wakeup(CONSOLE_UART));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(esp_reset_reason()));
    register_wakeup_sources();
    xTaskCreate(ticker_task, "ticker", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "Light sleep lab: wake by timer (%d ms), BOOT button, or typing in the monitor", SLEEP_TIME_MS);
    vTaskDelay(pdMS_TO_TICKS(3000)); // Let the ticker run a few times first

    while (1)
    {
        ESP_LOGI(TAG, "Entering light sleep (cycle %" PRIu32 ")...", s_sleep_cycles);

        // Light sleep suspends the UART: without this, the rest of the line only comes out AFTER wakeup
        uart_wait_tx_idle_polling(CONSOLE_UART);

        int64_t t_before_us = esp_timer_get_time();
        esp_light_sleep_start(); // Blocks here. RAM, registers and tasks are kept.
        int64_t t_after_us = esp_timer_get_time();

        s_sleep_cycles++;
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(TAG, "Woke up: %s, slept %" PRId64 " ms, cycle counter still = %" PRIu32, wakeup_cause_str(cause),
                 (t_after_us - t_before_us) / 1000, s_sleep_cycles);

        if (cause == ESP_SLEEP_WAKEUP_GPIO)
        {
            // Level-triggered: sleeping again while the button is held would wake up instantly, forever
            while (gpio_get_level(BOOT_BUTTON_GPIO) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Release bounce
        }

        // Stay awake for a moment so the ticker gets to run between sleeps
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
