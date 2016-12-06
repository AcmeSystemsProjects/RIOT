/*
 * Copyright (C) 2016 Antonio Galea
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     drivers_daisy24
 * @{
 *
 * @file
 * @brief       Driver for the Daisy24 I2C 16x2 LCD monitor
 *
 * @author      Antonio Galea <antonio.galea@gmail.com>
 *
 * @}
 */

#include <stdint.h>
#include <stdbool.h>
#include "periph/i2c.h"
#include "daisy24.h"

#define ENABLE_DEBUG    (0)
#include "debug.h"

int daisy24_init(daisy24_t *dev, i2c_t i2c, uint8_t lcd_address, uint8_t ext_address)
{
    int i, res;
    uint8_t data[2];
    uint8_t lcd_init[] = { 0x38, 0x39, 0x14, 0x72, 0x54, 0x6F, 0x0C };

    /* write device descriptor */
    dev->i2c = i2c;
    dev->lcd_addr = lcd_address;
    dev->ext_addr = ext_address;
    dev->initialized = false;

    i2c_acquire(dev->i2c);
    res = i2c_init_master(dev->i2c, I2C_SPEED_NORMAL);
    if (res == -1) {
        DEBUG("Error: Init: Given device not available\n");
        i2c_release(dev->i2c);
        return -1;
    }
    else if (res == -2) {
        DEBUG("Error: Init: Unsupported speed value I2C_SPEED_NORMAL\n");
        i2c_release(dev->i2c);
        return -1;
    }
    DEBUG("I2C_0 successfully initialized as master!\n");
    i2c_release(dev->i2c);

    /* LCD init */
    i2c_acquire(dev->i2c);
    for(i=0;i<sizeof(lcd_init);i++){
      data[0] = 0;
      data[1] = lcd_init[i];
      res = i2c_write_bytes(dev->i2c, dev->lcd_addr, data, 2);
      if (res != 2) {
        DEBUG("Error: Init: cannot write initial config to device\n");
        i2c_release(dev->i2c);
        return -1;
      }
    }
    i2c_release(dev->i2c);

    dev->initialized = true;
    return 0;
}

int daisy24_backlight(daisy24_t *dev, bool state)
{
    int res;

    if (!dev->initialized) {
        DEBUG("Error: no I2C device initialized\n");
        return -1;
    }
    i2c_acquire(dev->i2c);
    res = i2c_write_byte(dev->i2c, dev->ext_addr, state ? 16 : 0);
    if (res < 0) {
        DEBUG("Error: no bytes were written\n");
        i2c_release(dev->i2c);
        return -1;
    }
    i2c_release(dev->i2c);
    return 0;
}

int daisy24_clear(daisy24_t *dev)
{
    int res;
    char data[2];

    if (!dev->initialized) {
        DEBUG("Error: no I2C device initialized\n");
        return -1;
    }
    /* LCD clear */
    data[0] = 0;
    data[1] = 0x01;
    i2c_acquire(dev->i2c);
    res = i2c_write_bytes(dev->i2c, dev->lcd_addr, data, 2);
    if (res < 0) {
        DEBUG("Error: no bytes were written\n");
        i2c_release(dev->i2c);
        return -1;
    }
    i2c_release(dev->i2c);
    return 0;
}

int daisy24_set_position(daisy24_t *dev, uint8_t x, uint8_t y)
{
    int res;
    char data[2];

    if (!dev->initialized) {
        DEBUG("Error: no I2C device initialized\n");
        return -1;
    }

    data[0] = 0;
    data[1] = 0x80 + (y*0x40) + x;
    i2c_acquire(dev->i2c);
    res = i2c_write_bytes(dev->i2c, dev->lcd_addr, data, 2);
    if (res < 0) {
        DEBUG("Error: no bytes were written\n");
        i2c_release(dev->i2c);
        return -1;
    }
    i2c_release(dev->i2c);
    return 0;
}

int daisy24_write(daisy24_t *dev, char *str, uint8_t len)
{
    int res;
    uint8_t i;
    uint8_t data[2];

    if (!dev->initialized) {
        DEBUG("Error: no I2C device initialized\n");
        return -1;
    }
    i2c_acquire(dev->i2c);
    for(i=0;i<len;i++){
        data[0] = 0x40;
        data[1] = str[i];
        res = i2c_write_bytes(dev->i2c, dev->lcd_addr, data, 2);
        if (res < 0) {
            DEBUG("Error: no bytes were written\n");
            i2c_release(dev->i2c);
            return -1;
        }
    }
    i2c_release(dev->i2c);
    return 0;
}

int daisy24_read_button_states(daisy24_t *dev)
{
    int res;
    uint8_t state;

    if (!dev->initialized) {
        DEBUG("Error: no I2C device initialized\n");
        return -1;
    }
    state = 0xFF;
    i2c_acquire(dev->i2c);
    res = i2c_write_byte(dev->i2c, dev->ext_addr, state);
    if (res < 0) {
        DEBUG("Error: no bytes were written\n");
        i2c_release(dev->i2c);
        return -1;
    }
    res = i2c_read_byte(dev->i2c, dev->ext_addr, &state);
    if (res < 1) {
        DEBUG("Error: no bytes were written\n");
        i2c_release(dev->i2c);
        return -1;
    }
    i2c_release(dev->i2c);
    state &= 0x0F;
    state ^= 0x0F;
    dev->buttons[0] = state&1?1:0;
    dev->buttons[1] = state&2?1:0;
    dev->buttons[2] = state&8?1:0;
    dev->buttons[3] = state&4?1:0;
    DEBUG("button states: A=%d B=%d C=%d D=%d\n",
        dev->buttons[0], dev->buttons[1], dev->buttons[2], dev->buttons[3]
    );
    return 0;
}
