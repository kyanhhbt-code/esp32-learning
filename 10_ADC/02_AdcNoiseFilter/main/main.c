#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"        // ESP-IDF ADC Calibration driver interface
#include "esp_adc/adc_cali_scheme.h" // ESP-IDF Curve Fitting calibration scheme
#include "esp_adc/adc_oneshot.h"     // ESP-IDF New ADC Oneshot Mode driver
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Hardware ADC Unit 1 (Immune to Wi-Fi RF arbiter lock)
#define ADC_UNIT_USED ADC_UNIT_1
// ADC Channel 2 (Physical GPIO1 on ESP32-S3)
#define ADC_CHANNEL_USED ADC_CHANNEL_2
// 12 dB attenuation (Permits measurement range up to ~2900 mV)
#define ADC_ATTEN_USED ADC_ATTEN_DB_12
// Size of moving average window (16 samples)
#define FILTER_SIZE 16U

static const char* TAG = "adc_filter";

// Moving average filter structure with O(1) time complexity (no loop iterations)
typedef struct
{
    uint16_t samples[FILTER_SIZE]; // Circular buffer holding recent samples
    uint32_t sum;                  // Running sum of current window elements
    size_t index;                  // Pointer to the oldest sample in ring buffer
    size_t count;                  // Count of valid samples currently filled
} moving_average_t;

/*
 * O(1) Sliding Window Moving Average Filter:
 * Subtracts the oldest sample leaving the window, adds the newest sample,
 * updates ring buffer index, and divides running sum by sample count.
 */
static uint16_t moving_average_update(moving_average_t* filter, uint16_t sample)
{
    if (filter->count == FILTER_SIZE)
    {
        // Window full: subtract the oldest value from the running sum
        filter->sum -= filter->samples[filter->index];
    }
    else
    {
        // Warm-up phase: window is still filling up
        filter->count++;
    }

    // Store new sample into circular buffer replacing the oldest slot
    filter->samples[filter->index] = sample;
    // Add new sample into the running sum
    filter->sum += sample;
    // Advance circular buffer index with wrap-around
    filter->index = (filter->index + 1U) % FILTER_SIZE;

    // Return current average as a 16-bit integer
    return (uint16_t) (filter->sum / filter->count);
}

/*
 * Initialize Curve Fitting calibration handle using eFuse factory data.
 */
static bool calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t* out_handle)
{
    // Configure calibration parameters matching target hardware unit and attenuation
    adc_cali_curve_fitting_config_t config = {
        .unit_id = unit,                  // Hardware ADC Unit 1
        .atten = atten,                   // Attenuation level: 12 dB
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Resolution: 12-bit (0-4095)
    };

    // Attempt to create curve fitting scheme from eFuse burned coefficients
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&config, out_handle);
    return (err == ESP_OK);
}

void app_main(void)
{
    // =========================================================================
    // STEP 1: Initialize ADC Oneshot Unit and Channel
    // =========================================================================
    // Declare opaque handle for the hardware ADC unit
    adc_oneshot_unit_handle_t adc_handle = NULL;

    // Initialize unit configuration structure
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_USED,         // Target hardware unit: ADC_UNIT_1
        .ulp_mode = ADC_ULP_MODE_DISABLE, // Disable ULP mode (CPU-driven execution)
    };
    // Allocate ADC1 unit instance from hardware resources
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    // Configure analog front-end attenuation and resolution for Channel 0 (GPIO1)
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_USED,          // 12 dB attenuation (up to 2.9V input range)
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit output resolution (0-4095)
    };
    // Bind attenuation and bitwidth configuration to Channel 0
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_USED, &chan_config));

    // =========================================================================
    // STEP 2: Initialize Calibration Scheme
    // =========================================================================
    // Declare opaque calibration converter handle
    adc_cali_handle_t cali_handle = NULL;

    // Load factory calibration curve from eFuse memory
    bool cali_ok = calibration_init(ADC_UNIT_USED, ADC_ATTEN_USED, &cali_handle);

    // =========================================================================
    // STEP 3: Initialize Digital Filter State
    // =========================================================================
    moving_average_t filter = {0};

    ESP_LOGI(TAG, "ADC noise filter system initialized successfully!");

    // Tracking variables for Peak-to-Peak (P2P) noise quantification
    uint32_t loop_count = 0;
    int raw_min = 4096, raw_max = -1;
    int filter_min = 4096, filter_max = -1;

    // =========================================================================
    // STEP 4: Real-Time Sampling and Filtering Loop
    // =========================================================================
    while (1)
    {
        int raw = 0;

        // Trigger a single SAR hardware conversion on Channel 0 (GPIO1)
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_USED, &raw));

        // Push new raw sample into O(1) moving average filter
        uint16_t filtered_raw = moving_average_update(&filter, (uint16_t) raw);

        // Update minimum and maximum raw codes to calculate raw peak-to-peak ripple
        if (raw < raw_min)
            raw_min = raw;
        if (raw > raw_max)
            raw_max = raw;

        // Update minimum and maximum filtered codes to calculate filtered peak-to-peak ripple
        if (filtered_raw < filter_min)
            filter_min = filtered_raw;
        if (filtered_raw > filter_max)
            filter_max = filtered_raw;

        loop_count++;

        // Output statistical comparison report every 10 samples (every 500 ms)
        if (loop_count % 10 == 0)
        {
            int raw_mv = 0, filtered_mv = 0;
            if (cali_ok)
            {
                // Convert raw uncalibrated code to millivolts via polynomial curve fitting
                adc_cali_raw_to_voltage(cali_handle, raw, &raw_mv);

                // Convert filtered code to millivolts using the same calibration curve
                adc_cali_raw_to_voltage(cali_handle, filtered_raw, &filtered_mv);
            }

            // Calculate Peak-to-Peak (P2P) jitter amplitude: Peak = Max - Min
            int p2p_raw = raw_max - raw_min;
            int p2p_filtered = filter_max - filter_min;

            ESP_LOGI(TAG,
                     "Raw: %4d (%4d mV, P2P=%2d) | Filtered: %4d (%4d mV, P2P=%2d)",
                     raw,
                     raw_mv,
                     p2p_raw,
                     filtered_raw,
                     filtered_mv,
                     p2p_filtered);

            // Reset min/max trackers for the next statistical window
            raw_min = 4096;
            raw_max = -1;
            filter_min = 4096;
            filter_max = -1;
        }

        // Sampling delay: 50 ms per sample (Sampling rate fs = 20 Hz)
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // =========================================================================
    // STEP 5: Clean Up and Release Resources
    // =========================================================================
    if (cali_handle != NULL)
    {
        // Destroy calibration instance and free heap memory
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(cali_handle));
    }
    // Delete ADC unit and release hardware resources
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
}
