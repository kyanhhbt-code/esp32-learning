#include <inttypes.h>
#include <stdbool.h>
#include <sys/time.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---- Pins ----
#define WAKEUP_BUTTON_GPIO  GPIO_NUM_5    // Button to GND: "report now"
#define LED_GPIO            GPIO_NUM_4    // Blinks only while "transmitting"
#define SENSOR_POWER_GPIO   GPIO_NUM_6    // Powers the potentiometer only while measuring
#define SENSOR_ADC_CHANNEL  ADC_CHANNEL_0 // ADC1_CH0 = GPIO1 on ESP32-S3

// ---- Application timing ----
#define SAMPLE_PERIOD_S     10
#define BATCH_SIZE          6    // Samples collected before one "radio" report
#define SENSOR_SETTLE_US    5000 // Busy-wait: pdMS_TO_TICKS(5) would be 0 ticks at CONFIG_FREERTOS_HZ=100
#define ADC_SAMPLES         16
#define REPORT_TX_MS        200  // Simulated radio transmission time

// ---- Energy model: replace with YOUR measured values (see README) ----
#define I_ACTIVE_MA         40.0f   // Current while awake
#define I_SLEEP_UA          10.0f   // Current in deep sleep (chip ~8 uA, a DevKit board is much higher)
#define BATTERY_MAH         2000.0f // e.g. one 18650 cell, ignoring regulator losses and self-discharge

static const char *TAG = "sensor_node";

// Survive deep sleep. Reloaded with their initial value (0) after power-on.
static RTC_DATA_ATTR uint16_t s_batch_mv[BATCH_SIZE];
static RTC_DATA_ATTR uint32_t s_batch_count = 0;
static RTC_DATA_ATTR uint32_t s_wake_count = 0;
static RTC_DATA_ATTR struct timeval s_last_sleep_entry; // RTC time: keeps running across deep sleep
static RTC_DATA_ATTR bool s_have_sleep_entry = false;
static RTC_DATA_ATTR uint64_t s_total_awake_us = 0;     // Sum over measured cycles
static RTC_DATA_ATTR uint32_t s_measured_cycles = 0;

static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON:
            return "POWERON";
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

static int64_t timeval_diff_us(const struct timeval* later, const struct timeval* earlier)
{
    return (int64_t)(later->tv_sec - earlier->tv_sec) * 1000000 + (later->tv_usec - earlier->tv_usec);
}

static void output_pin_init(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, 0);
    // Set a known level first, then release the hold that kept the pin low during deep sleep
    ESP_ERROR_CHECK(gpio_hold_dis(pin));
}

static int read_sensor_mv(void)
{
    gpio_set_level(SENSOR_POWER_GPIO, 1);
    esp_rom_delay_us(SENSOR_SETTLE_US); // Let the divider and the ADC input settle

    adc_oneshot_unit_handle_t adc = NULL;
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, SENSOR_ADC_CHANNEL, &chan_cfg));

    adc_cali_handle_t cali = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    bool calibrated = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) == ESP_OK);

    int sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++)
    {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc, SENSOR_ADC_CHANNEL, &raw));
        int mv = raw;
        if (calibrated)
        {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali, raw, &mv));
        }
        sum += mv;
    }

    if (calibrated)
    {
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(cali));
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc));
    gpio_set_level(SENSOR_POWER_GPIO, 0); // Sensor off: a 10 kOhm pot on 3.3 V would draw 330 uA all the time

    if (!calibrated)
    {
        ESP_LOGW(TAG, "No ADC calibration data, value is a raw code");
    }
    return sum / ADC_SAMPLES;
}

static void send_report(void)
{
    // A real node would turn the radio on here. The LED stands in for the radio's current spike.
    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "REPORT: %" PRIu32 " samples:", s_batch_count);
    for (uint32_t i = 0; i < s_batch_count; i++)
    {
        ESP_LOGI(TAG, "  [%" PRIu32 "] %u mV", i, s_batch_mv[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(REPORT_TX_MS));
    gpio_set_level(LED_GPIO, 0);
    s_batch_count = 0;
}

/*
 * Awake time of one cycle = (this sleep entry - previous sleep entry) - timer sleep duration.
 * Both timestamps come from the RTC clock, so this includes ROM + bootloader + app startup,
 * which esp_timer_get_time() (started by the app) cannot see. Only valid for timer wakeups.
 */
static void account_awake_time(esp_sleep_wakeup_cause_t cause, const struct timeval* sleep_entry)
{
    if (cause == ESP_SLEEP_WAKEUP_TIMER && s_have_sleep_entry)
    {
        int64_t awake_us = timeval_diff_us(sleep_entry, &s_last_sleep_entry) - (int64_t)SAMPLE_PERIOD_S * 1000000;
        if (awake_us > 0)
        {
            s_total_awake_us += (uint64_t)awake_us;
            s_measured_cycles++;
        }
    }
    s_last_sleep_entry = *sleep_entry;
    s_have_sleep_entry = true;
}

static void print_battery_estimate(void)
{
    float avg_awake_s;
    if (s_measured_cycles > 0)
    {
        avg_awake_s = (float)s_total_awake_us / (float)s_measured_cycles / 1e6f;
    }
    else
    {
        // No full timer cycle measured yet: app time only, boot time missing -> lower bound
        avg_awake_s = (float)esp_timer_get_time() / 1e6f;
        ESP_LOGW(TAG, "Energy: first cycle, awake time excludes boot (estimate below is too optimistic)");
    }

    float sleep_s = (float)SAMPLE_PERIOD_S;
    float period_s = sleep_s + avg_awake_s;

    // Average current = charge per cycle / cycle length
    float avg_ma = (I_ACTIVE_MA * avg_awake_s + (I_SLEEP_UA / 1000.0f) * sleep_s) / period_s;
    float life_h = BATTERY_MAH / avg_ma;

    ESP_LOGI(TAG, "Energy: awake %.1f ms/cycle (%" PRIu32 " cycles measured), duty %.2f %%, I_avg = %.3f mA",
             avg_awake_s * 1000, s_measured_cycles, 100.0f * avg_awake_s / period_s, avg_ma);
    ESP_LOGI(TAG, "Estimated battery life with %.0f mAh: %.0f h = %.1f days", BATTERY_MAH, life_h, life_h / 24.0f);
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    s_wake_count++;

    output_pin_init(LED_GPIO);
    output_pin_init(SENSOR_POWER_GPIO);

    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        ESP_LOGI(TAG, "Cold start (reset reason %s): sampling every %d s, report every %d samples",
                 reset_reason_str(esp_reset_reason()), SAMPLE_PERIOD_S, BATCH_SIZE);
    }

    int mv = read_sensor_mv();
    if (s_batch_count < BATCH_SIZE)
    {
        s_batch_mv[s_batch_count++] = (uint16_t)mv;
    }
    ESP_LOGI(TAG, "Wake #%" PRIu32 " (%s): sensor = %d mV, batch %" PRIu32 "/%d", s_wake_count,
             cause == ESP_SLEEP_WAKEUP_EXT0 ? "button" : (cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : "power-on"), mv,
             s_batch_count, BATCH_SIZE);

    if (s_batch_count >= BATCH_SIZE || cause == ESP_SLEEP_WAKEUP_EXT0)
    {
        send_report();
    }

    // Wait for the button to be released, otherwise ext0 (level-triggered) wakes us up immediately
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << WAKEUP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
    if (gpio_get_level(WAKEUP_BUTTON_GPIO) == 0)
    {
        while (gpio_get_level(WAKEUP_BUTTON_GPIO) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Release bounce would be a false EXT0 wakeup
    }

    struct timeval sleep_entry;
    gettimeofday(&sleep_entry, NULL);
    account_awake_time(cause, &sleep_entry);
    print_battery_estimate();

    // Keep outputs LOW during deep sleep instead of letting them float
    ESP_ERROR_CHECK(gpio_hold_en(LED_GPIO));
    ESP_ERROR_CHECK(gpio_hold_en(SENSOR_POWER_GPIO));

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)SAMPLE_PERIOD_S * 1000000ULL));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_BUTTON_GPIO, 0));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(WAKEUP_BUTTON_GPIO));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(WAKEUP_BUTTON_GPIO));

    ESP_LOGI(TAG, "Deep sleep for %d s", SAMPLE_PERIOD_S);
    esp_deep_sleep_start();
}
