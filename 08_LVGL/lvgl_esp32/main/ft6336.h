#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Fallback configuration if Kconfig is not used
#ifndef CONFIG_TOUCH_SDA_GPIO
#define CONFIG_TOUCH_SDA_GPIO 4
#endif
#ifndef CONFIG_TOUCH_SCL_GPIO
#define CONFIG_TOUCH_SCL_GPIO 5
#endif
#ifndef CONFIG_TOUCH_RST_GPIO
#define CONFIG_TOUCH_RST_GPIO 6
#endif
#ifndef CONFIG_TOUCH_INT_GPIO
#define CONFIG_TOUCH_INT_GPIO 7
#endif
#ifndef CONFIG_TOUCH_I2C_PORT
#define CONFIG_TOUCH_I2C_PORT 0
#endif
#ifndef CONFIG_TOUCH_I2C_FREQ_HZ
#define CONFIG_TOUCH_I2C_FREQ_HZ 400000
#endif

#define FT6336_I2C_ADDR 0x38

// Register Map of FocalTech FT6336U
#define FT6336_REG_DEV_MODE  0x00
#define FT6336_REG_GEST_ID   0x01
#define FT6336_REG_TD_STATUS 0x02
#define FT6336_REG_P1_XH     0x03
#define FT6336_REG_P1_XL     0x04
#define FT6336_REG_P1_YH     0x05
#define FT6336_REG_P1_YL     0x06
#define FT6336_REG_LIB_VER_H 0xA1
#define FT6336_REG_LIB_VER_L 0xA2
#define FT6336_REG_CHIP_ID   0xA3 // Should be 0x02 / 0x64
#define FT6336_REG_FOCAL_ID  0xA8 // Should be 0x11 for FocalTech

typedef struct
{
    uint8_t touch_count;
    uint16_t x;
    uint16_t y;
    uint8_t event; // 0: Press Down, 1: Lift Up, 2: Contact, 3: No event
} ft6336_touch_data_t;

/**
 * @brief Initialize I2C bus and FT6336U touch controller
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ft6336_init(void);

/**
 * @brief Perform hardware reset on FT6336U
 */
void ft6336_hardware_reset(void);

/**
 * @brief Read touch status and coordinates (Burst read from 0x02)
 * @param touch Pointer to ft6336_touch_data_t structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ft6336_read_touch(ft6336_touch_data_t* touch);

/**
 * @brief Read a single 8-bit register
 * @param reg Register address
 * @param val Pointer to receive value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ft6336_read_reg(uint8_t reg, uint8_t* val);

/**
 * @brief Read consecutive bytes from a register
 * @param reg Register start address
 * @param buf Destination buffer
 * @param len Number of bytes to read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ft6336_read_bytes(uint8_t reg, uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif
