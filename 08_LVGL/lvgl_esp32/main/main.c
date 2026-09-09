#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "lv_conf.h"
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "st7796.h"
#include "ft6336.h"

#define LCD_H_RES 480
#define LCD_V_RES 320

static lv_color_t buf[LCD_H_RES * 20];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map)
{
    st7796_set_window(area->x1, area->y1, area->x2, area->y2);
    const uint8_t* src = (const uint8_t*) color_map;
    size_t row_bytes = (size_t) (area->x2 - area->x1 + 1) * sizeof(lv_color_t);
    for (int y = area->y1; y <= area->y2; ++y)
    {
        st7796_send_data(src, row_bytes);
        src += row_bytes;
    }
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t* drv, lv_indev_data_t* data)
{
    (void) drv;
    ft6336_touch_data_t touch = {0};
    if (ft6336_read_touch(&touch) == ESP_OK && touch.touch_count > 0)
    {
        data->state = LV_INDEV_STATE_PR;
        // Landscape 0 deg:   int x = 480 - touch.y; int y = touch.x;
        // Landscape 180 deg: int x = touch.y;       int y = 320 - touch.x;
        int x = touch.y;
        int y = 320 - touch.x;
        data->point.x = (x < 0) ? 0 : (x >= LCD_H_RES ? LCD_H_RES - 1 : x);
        data->point.y = (y < 0) ? 0 : (y >= LCD_V_RES ? LCD_V_RES - 1 : y);
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void tick_cb(void* arg)
{
    (void) arg;
    lv_tick_inc(1);
}

void app_main(void)
{
    st7796_init();
    ft6336_init();

    lv_init();

    // Display
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_H_RES * 20);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = disp_flush;
    lv_disp_drv_register(&disp_drv);

    // Touch
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    // 1ms Tick Timer
    const esp_timer_create_args_t timer_args = {.callback = tick_cb, .name = "lv_tick"};
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_periodic(timer, 1000);

    // Start Demo Widgets
    // lv_demo_widgets();
    lv_demo_music();

    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
