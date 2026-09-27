#include <inttypes.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BUTTON_GPIO        GPIO_NUM_0 // BOOT button, active-low
#define BUTTON_POLL_MS     10
#define DEBOUNCE_MS        30
#define LONG_PRESS_MS      1000

#define TWDT_TIMEOUT_MS    3000
#define TWDT_TRIGGER_PANIC false // Exercise: set to true, rebuild and watch the reboot

#define WORKER_PERIOD_MS   200
#define WORKER_PRIORITY    5 // Higher than IDLE (0) so a busy loop starves IDLE0
#define BUTTON_PRIORITY    3

typedef enum
{
    FAULT_NONE,
    FAULT_BLOCKED,   // Short press: worker waits forever, CPU stays free
    FAULT_BUSY_LOOP, // Long press: worker spins forever, CPU 0 is monopolized
} fault_t;

static const char *TAG = "task_wdt_lab";

static volatile fault_t s_fault = FAULT_NONE;
static SemaphoreHandle_t s_never_given;

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

// Blocks until the button is pressed and released, returns the press duration in ms.
static uint32_t wait_for_press_ms(void)
{
    while (1)
    {
        while (gpio_get_level(BUTTON_GPIO) == 1)
        {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        }
        TickType_t start = xTaskGetTickCount();
        while (gpio_get_level(BUTTON_GPIO) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        }
        uint32_t duration_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start);
        if (duration_ms >= DEBOUNCE_MS)
        {
            return duration_ms;
        }
    }
}

static void button_task(void* arg)
{
    uint32_t duration_ms = wait_for_press_ms();

    if (duration_ms < LONG_PRESS_MS)
    {
        ESP_LOGW(TAG, "Short press (%" PRIu32 " ms) -> inject FAULT_BLOCKED", duration_ms);
        s_fault = FAULT_BLOCKED;
    }
    else
    {
        ESP_LOGW(TAG, "Long press (%" PRIu32 " ms) -> inject FAULT_BUSY_LOOP", duration_ms);
        s_fault = FAULT_BUSY_LOOP;
    }

    // One fault per boot keeps the log easy to read. Press RST to try the other one.
    vTaskDelete(NULL);
}

static void worker_task(void* arg)
{
    // Subscribe this task: from now on it must call esp_task_wdt_reset() within TWDT_TIMEOUT_MS
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint32_t cycle = 0;
    while (1)
    {
        if (s_fault == FAULT_BLOCKED)
        {
            ESP_LOGW(TAG, "worker: waiting for a semaphore nobody will give...");
            xSemaphoreTake(s_never_given, portMAX_DELAY); // Blocked: IDLE still runs
        }
        else if (s_fault == FAULT_BUSY_LOOP)
        {
            ESP_LOGW(TAG, "worker: spinning on CPU 0 without yielding...");
            volatile uint32_t spin = 0;
            while (1)
            {
                spin++; // Ready and running forever: IDLE0 never gets CPU time
            }
        }

        // One unit of "real work" done -> tell the watchdog we made progress
        cycle++;
        if (cycle % 10 == 0)
        {
            ESP_LOGI(TAG, "worker: cycle %" PRIu32 " done, feeding TWDT", cycle);
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(WORKER_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(esp_reset_reason()));

    // TWDT is already initialized by ESP-IDF (CONFIG_ESP_TASK_WDT_INIT=y), so reconfigure it, do not init again
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = TWDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1), // Watch IDLE0 and IDLE1
        .trigger_panic = TWDT_TRIGGER_PANIC,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    s_never_given = xSemaphoreCreateBinary();

    // Worker on CPU 0, button reader on CPU 1: the button must keep working while CPU 0 is monopolized
    xTaskCreatePinnedToCore(worker_task, "worker", 3072, NULL, WORKER_PRIORITY, NULL, 0);
    xTaskCreatePinnedToCore(button_task, "button", 3072, NULL, BUTTON_PRIORITY, NULL, 1);

    ESP_LOGI(TAG, "TWDT timeout %d ms, panic=%s", TWDT_TIMEOUT_MS, TWDT_TRIGGER_PANIC ? "true" : "false");
    ESP_LOGI(TAG, "BOOT short press: worker blocks | BOOT long press (>= %d ms): worker busy-loops", LONG_PRESS_MS);
}
