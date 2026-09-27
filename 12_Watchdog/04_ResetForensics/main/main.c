#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BUTTON_GPIO          GPIO_NUM_0 // BOOT button, active-low
#define BUTTON_POLL_MS       10
#define DEBOUNCE_MS          30

#define TWDT_TIMEOUT_MS      5000
#define SUPERVISOR_PERIOD_MS 2000

#define CRASH_MAGIC          0x57444F47 // "WDOG"
#define SAFE_MODE_THRESHOLD  3          // Consecutive crashes before entering safe mode
#define STABLE_CLEAR_MS      30000      // Healthy uptime needed to forget previous crashes

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

/*
 * Crash record kept in RTC slow memory.
 * RTC_NOINIT_ATTR: the startup code does not zero it, so it survives panic, watchdog and software resets.
 * It is NOT valid after power-on (random content), hence the magic number.
 */
typedef struct
{
    uint32_t magic;
    uint32_t crash_count; // Consecutive crashes
    uint32_t fault_armed; // Lab only: 1 = 'comm' hangs on every boot (a bug that always comes back)
    uint32_t uptime_ms;   // How long the crashed run lasted
    char culprit[16];     // First worker that stopped checking in
} crash_record_t;

static RTC_NOINIT_ATTR crash_record_t s_rec;

static const char *TAG = "reset_forensics";

static EventGroupHandle_t s_checkin;
static SemaphoreHandle_t s_never_given;
static bool s_safe_mode = false;

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

static bool is_crash(esp_reset_reason_t reason)
{
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT;
}

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void crash_record_boot(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(reason));

    // Power loss: RTC memory content is garbage, start from a clean record
    if (s_rec.magic != CRASH_MAGIC || reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT)
    {
        memset(&s_rec, 0, sizeof(s_rec));
        s_rec.magic = CRASH_MAGIC;
        ESP_LOGI(TAG, "Cold boot: crash record initialized");
        return;
    }

    if (is_crash(reason))
    {
        s_rec.crash_count++;
        ESP_LOGE(TAG, "=== Previous run crashed ===");
        ESP_LOGE(TAG, "  reason : %s", reset_reason_str(reason));
        ESP_LOGE(TAG, "  culprit: %s", s_rec.culprit[0] ? s_rec.culprit : "(unknown, not reported by supervisor)");
        ESP_LOGE(TAG, "  uptime : %" PRIu32 " ms", s_rec.uptime_ms);
        ESP_LOGE(TAG, "  consecutive crashes: %" PRIu32 "/%d", s_rec.crash_count, SAFE_MODE_THRESHOLD);
    }
    else
    {
        s_rec.crash_count = 0; // Intentional reset (e.g. esp_restart after an update)
    }

    // The report has been consumed, the next run starts a fresh one
    s_rec.culprit[0] = '\0';
    s_rec.uptime_ms = 0;

    s_safe_mode = (s_rec.crash_count >= SAFE_MODE_THRESHOLD);
}

static void worker_task(void* arg)
{
    const worker_desc_t* desc = (const worker_desc_t*)arg;

    while (1)
    {
        if (desc->bit == BIT_COMM && s_rec.fault_armed)
        {
            ESP_LOGW(TAG, "%s: stuck waiting for a response that never comes...", desc->name);
            xSemaphoreTake(s_never_given, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(desc->period_ms));
        xEventGroupSetBits(s_checkin, desc->bit);
    }
}

static void supervisor_task(void* arg)
{
    // In safe mode 'comm' is not started, so the supervisor must not wait for it
    const EventBits_t expected = s_safe_mode ? (ALL_WORKER_BITS & ~BIT_COMM) : ALL_WORKER_BITS;
    bool culprit_recorded = false;
    bool crash_count_cleared = false;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_PERIOD_MS));
        EventBits_t bits = xEventGroupClearBits(s_checkin, ALL_WORKER_BITS) & expected;

        if (bits == expected)
        {
            esp_task_wdt_reset();

            if (!crash_count_cleared && s_rec.crash_count > 0 && uptime_ms() >= STABLE_CLEAR_MS)
            {
                s_rec.crash_count = 0;
                crash_count_cleared = true;
                ESP_LOGI(TAG, "supervisor: healthy for %d ms -> consecutive crash counter cleared", STABLE_CLEAR_MS);
            }
            ESP_LOGI(TAG, "%ssupervisor: all expected workers checked in -> feed", s_safe_mode ? "[SAFE MODE] " : "");
            continue;
        }

        for (size_t i = 0; i < WORKER_COUNT; i++)
        {
            if ((expected & s_workers[i].bit) && (bits & s_workers[i].bit) == 0)
            {
                ESP_LOGE(TAG, "supervisor: '%s' missed its check-in", s_workers[i].name);

                // Write the evidence BEFORE the watchdog fires: after the reset it is too late
                if (!culprit_recorded)
                {
                    strlcpy(s_rec.culprit, s_workers[i].name, sizeof(s_rec.culprit));
                    s_rec.uptime_ms = uptime_ms();
                    culprit_recorded = true;
                }
            }
        }
        ESP_LOGE(TAG, "supervisor: NOT feeding TWDT (reset in <= %d ms)", TWDT_TIMEOUT_MS);
    }
}

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

    ESP_LOGW(TAG, "BOOT pressed -> fault armed: 'comm' will hang on every boot until power is removed");
    s_rec.fault_armed = 1;
    vTaskDelete(NULL);
}

void app_main(void)
{
    crash_record_boot();

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
        if (s_safe_mode && s_workers[i].bit == BIT_COMM)
        {
            ESP_LOGW(TAG, "SAFE MODE: '%s' is disabled after %" PRIu32 " consecutive crashes", s_workers[i].name,
                     s_rec.crash_count);
            continue;
        }
        xTaskCreate(worker_task, s_workers[i].name, 3072, (void*)&s_workers[i], 4, NULL);
    }
    xTaskCreate(supervisor_task, "supervisor", 3072, NULL, 5, NULL);
    xTaskCreate(button_task, "button", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "Mode: %s | fault_armed=%" PRIu32 " | Press BOOT to arm a recurring 'comm' hang",
             s_safe_mode ? "SAFE" : "NORMAL", s_rec.fault_armed);
}
