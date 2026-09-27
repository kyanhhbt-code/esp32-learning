#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define ENCODER_CLK_GPIO GPIO_NUM_4
#define ENCODER_DT_GPIO  GPIO_NUM_5

static const char* TAG = "ENCODER_PCNT";

void app_main(void)
{
    // 1. Configure internal pull-up resistors for encoder inputs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_CLK_GPIO) | (1ULL << ENCODER_DT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // 2. Set up PCNT unit configuration and bounds
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit = -32768,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // 3. Configure glitch filter to suppress mechanical contact bouncing
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, // Filter out pulses narrower than 1us
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 4. Initialize Channel A: sample edge on CLK, level on DT
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_CLK_GPIO,
        .level_gpio_num = ENCODER_DT_GPIO,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // 5. Initialize Channel B: sample edge on DT, level on CLK
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_DT_GPIO,
        .level_gpio_num = ENCODER_CLK_GPIO,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // 6. Set edge and level actions for full quadrature 4X decoding
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // 7. Enable and start the hardware counter
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    ESP_LOGI(TAG, "PCNT rotary encoder driver initialized successfully");

    int cur_count = 0;
    int last_count = 0;

    while (1)
    {
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &cur_count));

        if (cur_count != last_count)
        {
            // Standard mechanical encoders (e.g., KY-040) generate 4 edges per detent
            int steps = cur_count / 4;
            ESP_LOGI(TAG, "Raw Count: %d | Steps: %d", cur_count, steps);
            last_count = cur_count;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
