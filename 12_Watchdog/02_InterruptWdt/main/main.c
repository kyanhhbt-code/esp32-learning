#include <inttypes.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_GPIO      GPIO_NUM_0 // BOOT button, active-low
#define DEBOUNCE_MS      30
#define LONG_PRESS_MS    1000

// CONFIG_ESP_INT_WDT_TIMEOUT_MS is 300 ms, so blocking for 500 ms is guaranteed to trip it
#define BLOCKING_TIME_US (500 * 1000)

static const char *TAG = "int_wdt_lab";

static volatile int64_t s_press_start_us = 0;
static volatile bool s_fault_injected = false;
static TaskHandle_t s_fault_task = NULL;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_SW:
            return "SW (esp_restart)";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT (other watchdog)";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_USB:
            return "USB (reset via USB-Serial/JTAG)";
        default:
            return "OTHER";
    }
}

/*
 * GPIO ISR on both edges. The press duration is only known on release (rising edge),
 * so that is where the scenario is chosen.
 */
static void button_isr(void* arg)
{
    int64_t now_us = esp_timer_get_time();

    if (gpio_get_level(BUTTON_GPIO) == 0)
    {
        s_press_start_us = now_us; // Falling edge: button pressed
        return;
    }

    if (s_press_start_us == 0 || s_fault_injected)
    {
        return;
    }
    int64_t duration_us = now_us - s_press_start_us;
    s_press_start_us = 0;
    if (duration_us < DEBOUNCE_MS * 1000)
    {
        return; // Contact bounce
    }
    s_fault_injected = true;

    if (duration_us < LONG_PRESS_MS * 1000)
    {
        // Scenario A: an ISR that runs far too long. Interrupts of the same or lower level
        // (including the FreeRTOS tick) cannot run on this CPU until we return.
        esp_rom_printf("ISR: busy-waiting %d ms inside the interrupt handler...\n", BLOCKING_TIME_US / 1000);
        esp_rom_delay_us(BLOCKING_TIME_US);
    }
    else
    {
        // Scenario B: hand the fault over to a task on the other CPU
        BaseType_t higher_prio_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_fault_task, &higher_prio_woken);
        portYIELD_FROM_ISR(higher_prio_woken);
    }
}

static void fault_task(void* arg)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ESP_LOGW(TAG, "fault_task: spinning %d ms inside portENTER_CRITICAL() on CPU %d...", BLOCKING_TIME_US / 1000,
             xPortGetCoreID());

    // A critical section masks interrupts on this CPU, so its tick stops just like in scenario A
    portENTER_CRITICAL(&s_mux);
    esp_rom_delay_us(BLOCKING_TIME_US);
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGE(TAG, "fault_task: you should never see this line");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(esp_reset_reason()));

    // Pinned to CPU 1 so scenario B reports "CPU1" while scenario A (ISR installed from CPU 0) reports "CPU0"
    xTaskCreatePinnedToCore(fault_task, "fault_task", 3072, NULL, 5, &s_fault_task, 1);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    // app_main runs on CPU 0, so the GPIO interrupt is allocated on CPU 0
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL));

    ESP_LOGI(TAG, "BOOT short press: long ISR on CPU 0 | BOOT long press (>= %d ms): critical section on CPU 1",
             LONG_PRESS_MS);

    uint32_t seconds = 0;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "heartbeat %" PRIu32 " s", ++seconds);
    }
}
