/*
 * Copyright (C) 2016 Antonio Galea
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    drivers_daisy24 Daisy24 I2C 16x2 LCD monitor
 * @ingroup     drivers_sensors
 * @brief       Driver for the Daisy24 I2C 16x2 LCD monitor
 *
 * @{
 *
 *
 * @file
 * @brief       Interface definition for the Daisy24 I2C 16x2 LCD driver.
 *
 * @author      Antonio Galea <antonio.galea@gmail.com>
 *
 */

#ifndef DAISY24_H
#define DAISY24_H

#include <stdint.h>
#include <stdbool.h>
#include "periph/i2c.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Device descriptor for Daisy24 LCD device.
 */
typedef struct {
    i2c_t i2c;              /**< I2C device, the sensor is connected to */
    uint8_t lcd_addr;       /**< the LCD's slave address on the I2C bus */
    uint8_t ext_addr;       /**< the extender's slave address on the I2C bus */
    bool initialized;       /**< monitor status, true if monitor is initialized */
    bool buttons[4];        /**< to keep the button state */
} daisy24_t;

/**
 * @brief Initialize the Daisy24 LCD driver.
 *
 * @param[out] dev          device descriptor of LCD to initialize
 * @param[in]  i2c          I2C bus the LCD is connected to
 * @param[in]  lcd_address  LCD's I2C slave address
 * @param[in]  ext_address  extender's I2C slave address
 *
 * @return                  0 on success
 * @return                  -1 on error
 */
int daisy24_init(daisy24_t *dev, i2c_t i2c, uint8_t lcd_address, uint8_t ext_address);

/**
 * @brief Control the Daisy24 LCD backlight.
 *
 * @param[out] dev          device descriptor of LCD
 * @param[in]  state        desired state
 *
 * @return                  0 on success
 * @return                  -1 on error
 */
int daisy24_backlight(daisy24_t *dev, bool state);

/**
 * @brief Clear the Daisy24 LCD.
 *
 * @param[out] dev          device descriptor of LCD
 *
 * @return                  0 on success
 * @return                  -1 on error
 */
int daisy24_clear(daisy24_t *dev);

/**
 * @brief Set cursor position on the Daisy24 LCD.
 *
 * @param[out] dev          device descriptor of LCD
 * @param[in]  x            x position [0-15]
 * @param[in]  y            y position [0-1]
 *
 * @return                  0 on success
 * @return                  -1 on error
 */
int daisy24_set_position(daisy24_t *dev, uint8_t x, uint8_t y);

/**
 * @brief Write a string to the Daisy24 LCD.
 *
 * @param[out] dev          device descriptor of LCD
 * @param[in]  str          buffer to write
 * @param[in]  len          buffer length (max 16)
 *
 * @return                  0 on success
 * @return                  -1 on error
 */
int daisy24_write(daisy24_t *dev, char *str, uint8_t len);

/**
 * @brief Read button states from the Daisy24 I2C extender.
 *
 * @param[out] dev          device descriptor of LCD
 *
 * @return                  0 on success
 * @return                  -1 on error
 */
int daisy24_read_button_states(daisy24_t *dev);

#ifdef __cplusplus
}
#endif

#endif
/** @} */
