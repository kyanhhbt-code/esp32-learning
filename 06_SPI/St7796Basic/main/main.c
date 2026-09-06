#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "st7796.h"

static const char* TAG = "MAIN_APP";

static void draw_color_palette(void)
{
    uint16_t w = CONFIG_LCD_WIDTH / 2;
    uint16_t h = CONFIG_LCD_HEIGHT / 2;

    st7796_fill_rect(0, 0, w, h, ST7796_RED);
    st7796_fill_rect(w, 0, w, h, ST7796_GREEN);
    st7796_fill_rect(0, h, w, h, ST7796_BLUE);
    st7796_fill_rect(w, h, w, h, ST7796_YELLOW);

    uint16_t center_w = 80;
    uint16_t center_h = 50;
    uint16_t cx = (CONFIG_LCD_WIDTH - center_w) / 2;
    uint16_t cy = (CONFIG_LCD_HEIGHT - center_h) / 2;
    st7796_fill_rect(cx, cy, center_w, center_h, ST7796_WHITE);
}

static void run_bouncing_box_animation(int duration_seconds)
{
    st7796_fill_screen(ST7796_BLACK);

    int box_size = 40;
    int x = 20;
    int y = 20;
    int dx = 5;
    int dy = 4;

    int total_frames = duration_seconds * 50;
    for (int i = 0; i < total_frames; i++)
    {
        st7796_fill_rect(x, y, box_size, box_size, ST7796_BLACK);

        x += dx;
        y += dy;

        if (x <= 0 || (x + box_size) >= CONFIG_LCD_WIDTH)
        {
            dx = -dx;
        }
        if (y <= 0 || (y + box_size) >= CONFIG_LCD_HEIGHT)
        {
            dy = -dy;
        }

        st7796_fill_rect(x, y, box_size, box_size, ST7796_CYAN);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ST7796 Basic Driver Demo...");

    if (st7796_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Display initialization failed! Aborting.");
        return;
    }

    while (1)
    {
        ESP_LOGI(TAG, "Demo: Color Cycling...");
        st7796_fill_screen(ST7796_RED);
        vTaskDelay(pdMS_TO_TICKS(800));

        st7796_fill_screen(ST7796_GREEN);
        vTaskDelay(pdMS_TO_TICKS(800));

        st7796_fill_screen(ST7796_BLUE);
        vTaskDelay(pdMS_TO_TICKS(800));

        ESP_LOGI(TAG, "Demo: Color Palette...");
        draw_color_palette();
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Demo: Bouncing Animation...");
        run_bouncing_box_animation(5);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
