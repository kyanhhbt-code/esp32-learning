#include <inttypes.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// 0 = correct design: supervisor feeds only when every worker checked in
// 1 = anti-pattern:   a periodic timer feeds the watchdog no matter what
#define FEED_FROM_TIMER      0

#define BUTTON_GPIO          GPIO_NUM_0 // BOOT button, active-low
#define BUTTON_POLL_MS       10
#define DEBOUNCE_MS          30

#define TWDT_TIMEOUT_MS      5000
#define SUPERVISOR_PERIOD_MS 2000 // Must be >= the slowest worker period (logger, 1000 ms)
#define TIMER_FEED_PERIOD_US (1000 * 1000)

#define BIT_SENSOR           (1 << 0)
#define BIT_COMM             (1 << 1)
#define BIT_LOGGER           (1 << 2)
#define ALL_WORKER_BITS      (BIT_SENSOR | BIT_COMM | BIT_LOGGER)

typedef struct
{
    const char* name;
    EventBits_t bit;
    uint32_t period_ms;
} worker_desc_t;

static const worker_desc_t s_workers[] = {
    {"sensor", BIT_SENSOR, 100},
    {"comm", BIT_COMM, 500},
    {"logger", BIT_LOGGER, 1000},
};
#define WORKER_COUNT (sizeof(s_workers) / sizeof(s_workers[0]))

static const char *TAG = "progress_feed";

static EventGroupHandle_t s_checkin;
static SemaphoreHandle_t s_never_given;
static volatile bool s_comm_hang = false;

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

static void worker_task(void* arg)
{
    const worker_desc_t* desc = (const worker_desc_t*)arg;

    while (1)
    {
        if (desc->bit == BIT_COMM && s_comm_hang)
        {
            // Simulates a stuck driver: blocked forever, CPU is idle, IDLE tasks still run
            ESP_LOGW(TAG, "%s: stuck waiting for a response that never comes...", desc->name);
            xSemaphoreTake(s_never_given, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(desc->period_ms)); // One unit of simulated work
        xEventGroupSetBits(s_checkin, desc->bit);   // Check in: "I completed a cycle"
    }
}

static void supervisor_task(void* arg)
{
#if !FEED_FROM_TIMER
    // Only the supervisor is subscribed. It feeds on behalf of the whole application.
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
#endif

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_PERIOD_MS));

        // Atomic read-and-clear: returns the bits as they were before clearing
        EventBits_t bits = xEventGroupClearBits(s_checkin, ALL_WORKER_BITS) & ALL_WORKER_BITS;

        if (bits == ALL_WORKER_BITS)
        {
            ESP_LOGI(TAG, "supervisor: all %u workers checked in -> feed", (unsigned)WORKER_COUNT);
#if !FEED_FROM_TIMER
            esp_task_wdt_reset();
#endif
            continue;
        }

        for (size_t i = 0; i < WORKER_COUNT; i++)
        {
            if ((bits & s_workers[i].bit) == 0)
            {
                ESP_LOGE(TAG, "supervisor: '%s' missed its check-in", s_workers[i].name);
            }
        }
#if FEED_FROM_TIMER
        ESP_LOGE(TAG, "supervisor: ...but the timer keeps feeding, so nothing will ever reset");
#else
        ESP_LOGE(TAG, "supervisor: NOT feeding TWDT (reset in <= %d ms)", TWDT_TIMEOUT_MS);
#endif
    }
}

#if FEED_FROM_TIMER
static esp_task_wdt_user_handle_t s_timer_user;

// ANTI-PATTERN: this proves only that the esp_timer task is alive, not that the application works
static void timer_feed_cb(void* arg)
{
    esp_task_wdt_reset_user(s_timer_user);
}

static void start_timer_feed(void)
{
    ESP_ERROR_CHECK(esp_task_wdt_add_user("timer_feed", &s_timer_user));

    const esp_timer_create_args_t args = {
        .callback = timer_feed_cb,
        .name = "timer_feed",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, TIMER_FEED_PERIOD_US));
}
#endif

static void button_task(void* arg)
{
    while (1)
    {
        while (gpio_get_level(BUTTON_GPIO) == 1)
        {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(BUTTON_GPIO) == 0)
        {
            break;
        }
    }

    ESP_LOGW(TAG, "BOOT pressed -> 'comm' will hang on its next cycle");
    s_comm_hang = true;
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(esp_reset_reason()));

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = TWDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
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

    s_checkin = xEventGroupCreate();
    s_never_given = xSemaphoreCreateBinary();

    for (size_t i = 0; i < WORKER_COUNT; i++)
    {
        xTaskCreate(worker_task, s_workers[i].name, 3072, (void*)&s_workers[i], 4, NULL);
    }
    xTaskCreate(supervisor_task, "supervisor", 3072, NULL, 5, NULL);
    xTaskCreate(button_task, "button", 2048, NULL, 3, NULL);

#if FEED_FROM_TIMER
    start_timer_feed();
    ESP_LOGW(TAG, "Mode: FEED_FROM_TIMER (anti-pattern)");
#else
    ESP_LOGI(TAG, "Mode: supervisor feeds on progress");
#endif
    ESP_LOGI(TAG, "TWDT %d ms, supervisor check every %d ms. Press BOOT to hang 'comm'.", TWDT_TIMEOUT_MS,
             SUPERVISOR_PERIOD_MS);
}
