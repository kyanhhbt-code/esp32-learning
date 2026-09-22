#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"        // ESP-IDF ADC Calibration driver interface
#include "esp_adc/adc_cali_scheme.h" // ESP-IDF Curve Fitting calibration scheme
#include "esp_adc/adc_oneshot.h"     // ESP-IDF New ADC Oneshot Mode driver
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Hardware ADC Unit 1 (Safe from Wi-Fi radio conflict on ESP32-S3)
#define ADC_UNIT_USED ADC_UNIT_1
// Channel 0: General sensor / potentiometer input (Physical GPIO1)
#define SENSOR_ADC_CHANNEL ADC_CHANNEL_0
// Channel 1: 1S LiPo battery voltage divider input (Physical GPIO2)
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_1
// 12 dB attenuation: Effective measurement range up to ~2900 mV
#define ADC_ATTEN_USED ADC_ATTEN_DB_12

// Voltage divider resistors: R1 = 100k, R2 = 100k (Divides battery voltage by 2)
#define R1_OHM 100000ULL
#define R2_OHM 100000ULL

static const char* TAG = "adc_multichannel";

/*
 * Calculate actual battery voltage before the voltage divider:
 * Formula: Vin = Vadc * (R1 + R2) / R2
 * Casts to uint64_t before multiplying to prevent 32-bit integer overflow!
 */
static uint32_t battery_mv_from_adc(uint32_t adc_pin_mv)
{
    return (uint32_t) (((uint64_t) adc_pin_mv * (R1_OHM + R2_OHM)) / R2_OHM);
}

/*
 * Initialize Curve Fitting calibration scheme based on factory eFuse values.
 */
static bool calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t* out_handle)
{
    // Configure Curve Fitting calibration parameters
    adc_cali_curve_fitting_config_t config = {
        .unit_id = unit,                  // Hardware ADC Unit 1
        .atten = atten,                   // Attenuation: 12 dB
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit output resolution (0-4095)
    };

    // Create calibration converter instance from eFuse polynomial data
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&config, out_handle);
    return (err == ESP_OK);
}

void app_main(void)
{
    // =========================================================================
    // STEP 1: Initialize Single ADC1 Unit Shared Across Multiple Channels
    // =========================================================================
    // Declare opaque handle to manage the shared ADC1 hardware unit
    adc_oneshot_unit_handle_t adc_handle = NULL;

    // Configuration structure for the ADC Oneshot Unit
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_USED,         // Target hardware unit: ADC_UNIT_1
        .ulp_mode = ADC_ULP_MODE_DISABLE, // Disable ULP co-processor mode
    };
    // Allocate shared ADC1 hardware instance
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    // =========================================================================
    // STEP 2: Configure Individual Channels (Channel 0 & Channel 1)
    // =========================================================================
    // Channel front-end configuration structure
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_USED,          // 12 dB attenuation (up to 2.9V measurement range)
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit output resolution (yields 0 - 4095)
    };

    // Configure Channel 0 (GPIO1) for potentiometer sensor reading
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, SENSOR_ADC_CHANNEL, &chan_config));

    // Configure Channel 1 (GPIO2) for 1S LiPo battery divider reading
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &chan_config));

    // =========================================================================
    // STEP 3: Initialize Shared Curve Fitting Calibration Handle
    // Note: In ESP-IDF 5.2, channels sharing the same unit and attenuation
    // can share a single Curve Fitting calibration handle!
    // =========================================================================
    adc_cali_handle_t cali_handle = NULL;

    // Query eFuse and create the shared calibration converter
    bool cali_ok = calibration_init(ADC_UNIT_USED, ADC_ATTEN_USED, &cali_handle);

    ESP_LOGI(TAG, "Multi-channel ADC1 (Channel 0 & Channel 1) initialized successfully!");

    // =========================================================================
    // STEP 4: Sequential Multi-Channel Sampling Loop
    // =========================================================================
    while (1)
    {
        int raw_sensor = 0;
        int raw_battery = 0;

        // Perform sequential conversion: First sample Channel 0 (Sensor on GPIO1)
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, SENSOR_ADC_CHANNEL, &raw_sensor));

        // Perform sequential conversion: Second sample Channel 1 (Battery on GPIO2)
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw_battery));

        int sensor_mv = 0;
        int battery_pin_mv = 0;

        if (cali_ok)
        {
            // Convert Channel 0 raw code to millivolts using shared calibration scheme
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_sensor, &sensor_mv));

            // Convert Channel 1 raw code to millivolts using shared calibration scheme
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_battery, &battery_pin_mv));
        }

        // Reconstruct actual battery voltage from divider output (Vbat = 2 * Vadc_pin)
        uint32_t real_vbat_mv = battery_mv_from_adc((uint32_t) battery_pin_mv);
        float real_vbat_v = real_vbat_mv / 1000.0f;

        // Evaluate LiPo 1S battery operating status
        const char* bat_status = "Normal";
        if (real_vbat_mv >= 4150)
        {
            bat_status = "Battery Full";
        }
        else if (real_vbat_mv <= 3300)
        {
            bat_status = "WARNING: Low Battery!";
        }

        ESP_LOGI(TAG,
                 "[SENSOR CH0] %4d mV | [BATTERY CH1] Pin: %4d mV -> Real Vbat: %.3f V (%s)",
                 sensor_mv,
                 battery_pin_mv,
                 real_vbat_v,
                 bat_status);

        // Sample interval: 1000 ms (1 second)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // =========================================================================
    // STEP 5: Clean Up and Release Resources on Exit
    // =========================================================================
    if (cali_handle != NULL)
    {
        // Free shared calibration converter resources
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(cali_handle));
    }
    // Delete ADC unit and release hardware resources back to system pool
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
}
