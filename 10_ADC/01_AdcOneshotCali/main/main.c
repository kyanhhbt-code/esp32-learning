#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"        // ESP-IDF ADC Calibration driver interface
#include "esp_adc/adc_cali_scheme.h" // ESP-IDF Calibration schemes (Curve Fitting / Line Fitting)
#include "esp_adc/adc_oneshot.h"     // ESP-IDF New ADC Oneshot Mode driver
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Select ADC Unit 1 (Hardware Unit 1 has no Wi-Fi resource conflict on ESP32-S3)
#define ADC_UNIT_USED ADC_UNIT_1
// Select Channel 2 on Unit 1 (Mapped to physical GPIO1 on ESP32-S3)
#define ADC_CHANNEL_USED ADC_CHANNEL_2
// Select 12 dB attenuation (Extends measurable input range up to ~2900 mV per datasheet)
#define ADC_ATTEN_USED ADC_ATTEN_DB_12
// Number of multisampling readings to smooth random noise
#define MULTISAMPLE_COUNT 32U

static const char* TAG = "adc_oneshot_cali";

/*
 * Initialize Curve Fitting calibration scheme based on SoC eFuse factory data.
 * Returns true if eFuse calibration data is present and initialized successfully.
 */
static bool calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t* out_handle)
{
    // Configuration structure for the Curve Fitting calibration scheme
    adc_cali_curve_fitting_config_t config = {
        .unit_id = unit,                  // Hardware ADC unit identifier (ADC_UNIT_1)
        .atten = atten,                   // Attenuation level being calibrated (ADC_ATTEN_DB_12)
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit default output resolution (0-4095)
    };

    // Allocate and initialize Curve Fitting scheme using burned eFuse characteristics
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&config, out_handle);
    if (err == ESP_OK)
    {
        // Calibration handle successfully created
        return true;
    }

    // Reset handle pointer to NULL on failure
    *out_handle = NULL;
    if (err == ESP_ERR_NOT_SUPPORTED)
    {
        // Chip eFuse does not contain the required calibration curve data
        ESP_LOGW(TAG, "Calibration eFuse data is not available on this chip!");
        return false;
    }

    // Check and panic if unexpected hardware error occurred
    ESP_ERROR_CHECK(err);
    return false;
}

void app_main(void)
{
    // =========================================================================
    // STEP 1: Initialize ADC Oneshot Hardware Unit
    // =========================================================================
    // Declare opaque handle to manage the hardware ADC unit instance
    adc_oneshot_unit_handle_t adc_handle = NULL;

    // Configuration structure for the ADC Oneshot Unit
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_USED,         // Target hardware unit (ADC_UNIT_1)
        .ulp_mode = ADC_ULP_MODE_DISABLE, // Disable ULP co-processor mode (standard CPU task control)
    };
    // Allocate hardware ADC unit from the system pool and acquire power management lock
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    // =========================================================================
    // STEP 2: Configure ADC Channel and Attenuation Network
    // =========================================================================
    // Configuration structure for individual channel analog front-end
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_USED,          // 12dB attenuation to measure up to ~2900 mV safely
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Set 12-bit output resolution (yields codes 0 - 4095)
    };
    // Apply channel configuration to Channel 0 (GPIO1) on the allocated unit
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_USED, &channel_config));

    // =========================================================================
    // STEP 3: Initialize Curve Fitting Calibration Handle
    // =========================================================================
    // Declare opaque handle for storing calibration scheme context
    adc_cali_handle_t cali_handle = NULL;

    // Query eFuse calibration parameters and create curve-fitting converter
    bool calibration_available = calibration_init(ADC_UNIT_USED, ADC_ATTEN_USED, &cali_handle);

    ESP_LOGI(TAG, "ADC Oneshot system ready. Starting data acquisition...");

    // =========================================================================
    // STEP 4: Periodic Multisampling Loop
    // =========================================================================
    while (1)
    {
        uint32_t raw_sum = 0;
        uint32_t voltage_sum_mv = 0;

        // Take 32 consecutive samples to reduce random noise variance by sqrt(32) ≈ 5.6x
        for (uint32_t i = 0; i < MULTISAMPLE_COUNT; ++i)
        {
            int raw = 0;

            // Trigger one hardware SAR conversion on Channel 0 and read 12-bit raw code (0-4095)
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_USED, &raw));
            // Accumulate raw codes for averaging
            raw_sum += (uint32_t) raw;

            if (calibration_available)
            {
                int voltage_mv = 0;

                // Convert uncalibrated raw code to millivolts using polynomial curve fitting
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv));
                // Accumulate calibrated millivolt readings
                voltage_sum_mv += (uint32_t) voltage_mv;
            }
        }

        // Calculate arithmetic mean of raw codes across 32 samples
        uint32_t raw_average = raw_sum / MULTISAMPLE_COUNT;

        if (calibration_available)
        {
            // Calculate arithmetic mean of calibrated millivolts
            uint32_t voltage_average_mv = voltage_sum_mv / MULTISAMPLE_COUNT;
            ESP_LOGI(TAG,
                     "raw_avg=%" PRIu32 ", voltage_avg=%" PRIu32 " mV (%.3f V)",
                     raw_average,
                     voltage_average_mv,
                     voltage_average_mv / 1000.0f);
        }
        else
        {
            ESP_LOGI(TAG, "raw_avg=%" PRIu32 " (No eFuse calibration available)", raw_average);
        }

        // Delay for 500 ms before next measurement block
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // =========================================================================
    // STEP 5: Clean Up and Release Resources on Exit
    // =========================================================================
    if (cali_handle != NULL)
    {
        // Free calibration scheme heap memory
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(cali_handle));
    }
    // Release hardware ADC unit back to system pool and unlock power management
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
}
