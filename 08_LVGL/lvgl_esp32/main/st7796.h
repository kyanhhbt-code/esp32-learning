#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Fallback configuration if Kconfig is not used
#ifndef CONFIG_LCD_MOSI_GPIO
#define CONFIG_LCD_MOSI_GPIO 11
#endif
#ifndef CONFIG_LCD_SCLK_GPIO
#define CONFIG_LCD_SCLK_GPIO 12
#endif
#ifndef CONFIG_LCD_CS_GPIO
#define CONFIG_LCD_CS_GPIO 10
#endif
#ifndef CONFIG_LCD_DC_GPIO
#define CONFIG_LCD_DC_GPIO 9
#endif
#ifndef CONFIG_LCD_RST_GPIO
#define CONFIG_LCD_RST_GPIO 14
#endif
#ifndef CONFIG_LCD_BLK_GPIO
#define CONFIG_LCD_BLK_GPIO 2
#endif
#ifndef CONFIG_LCD_WIDTH
#define CONFIG_LCD_WIDTH 480
#endif
#ifndef CONFIG_LCD_HEIGHT
#define CONFIG_LCD_HEIGHT 320
#endif
#ifndef CONFIG_LCD_SPI_CLOCK_HZ
#define CONFIG_LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)
#endif

// 16-bit RGB565 Colors
#define ST7796_BLACK   0x0000
#define ST7796_BLUE    0x001F
#define ST7796_GREEN   0x07E0
#define ST7796_CYAN    0x07FF
#define ST7796_RED     0xF800
#define ST7796_MAGENTA 0xF81F
#define ST7796_YELLOW  0xFFE0
#define ST7796_WHITE   0xFFFF

    /**
     * @brief Initialize SPI bus and ST7796 display controller
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t st7796_init(void);

    /**
     * @brief Send 1-byte command (D/C = LOW)
     * @param cmd Command byte
     */
    void st7796_send_cmd(uint8_t cmd);

    /**
     * @brief Send data buffer (D/C = HIGH)
     * @param data Data pointer
     * @param len Length in bytes
     */
    void st7796_send_data(const uint8_t* data, size_t len);

    /**
     * @brief Set drawing window area (CASET & RASET)
     * @param x0 Start X coordinate
     * @param y0 Start Y coordinate
     * @param x1 End X coordinate
     * @param y1 End Y coordinate
     */
    void st7796_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    /**
     * @brief Fill entire screen with a solid color
     * @param color 16-bit RGB565 color
     */
    void st7796_fill_screen(uint16_t color);

    /**
     * @brief Draw filled rectangle
     * @param x Top-left X coordinate
     * @param y Top-left Y coordinate
     * @param w Width
     * @param h Height
     * @param color 16-bit RGB565 color
     */
    void st7796_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

    /**
     * @brief Draw single pixel
     * @param x X coordinate
     * @param y Y coordinate
     * @param color 16-bit RGB565 color
     */
    void st7796_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

#ifdef __cplusplus
}
#endif
