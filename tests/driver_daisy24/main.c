/*
 * Copyright (C) 2016 Antonio Galea
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup tests
 * @{
 *
 * @file
 * @brief       Test application for the Daisy24 I2C 16x2 LCD monitor
 *
 * @author      Antonio Galea <antonio.galea@gmail.com>
 *
 * @}
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daisy24.h"
#include "shell.h"

#define BUFSIZE        (128U)

#define LCD_ADDR 0x3E
#define EXT_ADDR 0x3F

static daisy24_t dev;

int cmd_init(int argc, char **argv)
{
    int res;

    res = daisy24_init(&dev, I2C_0, LCD_ADDR, EXT_ADDR);
    if (res) {
        puts("Error: Init: Given device not available");
        return 1;
    }
    printf("Daisy24 successfully initialized.\n");
    return 0;
}

int cmd_backlight(int argc, char **argv)
{
    int res;

    if (argc < 2) {
        puts("Error: not enough arguments given");
        printf("Usage:\n%s [0 | 1]\n", argv[0]);
        return 1;
    }

    res = daisy24_backlight(&dev, atoi(argv[1]) ? true : false);
    if (res < 0) {
        puts("Error: no bytes were written");
        return 1;
    }
    return 0;
}

int cmd_clear(int argc, char **argv)
{
    int res;

    res = daisy24_clear(&dev);
    if (res < 0) {
        puts("Error: no bytes were written");
        return 1;
    }
    return 0;
}

int cmd_set_position(int argc, char **argv)
{
    int res;

    if (argc < 3) {
        puts("Error: not enough arguments given");
        printf("Usage:\n%s X Y\n", argv[0]);
        return 1;
    }
    res = daisy24_set_position(&dev, atoi(argv[1]), atoi(argv[2]));
    if (res < 0) {
        puts("Error: no bytes were written");
        return 1;
    }
    return 0;
}

int cmd_write(int argc, char **argv)
{
    int res;
    uint8_t length;

    if (argc < 2) {
        puts("Error: not enough arguments given");
        printf("Usage:\n%s: STRING\n", argv[0]);
        return 1;
    }

    length = strlen(argv[1]); 
    res = daisy24_write(&dev, argv[1], length);
    if (res < 0) {
        puts("Error: no bytes were written");
        return 1;
    }
    return 0;
}

int cmd_button_states(int argc, char **argv)
{
    int res;

    res = daisy24_read_button_states(&dev);
    if (res < 1) {
        puts("Error: no bytes were read");
        return 1;
    }
    printf("button states: A=%d B=%d C=%d D=%d\n",
        dev.buttons[0], dev.buttons[1], dev.buttons[2], dev.buttons[3]
    );

    return 0;
}


static const shell_command_t shell_commands[] = {
    { "i", "initialize lcd", cmd_init },
    { "b", "set backlight on or off", cmd_backlight },
    { "c", "clear screen", cmd_clear },
    { "p", "set position", cmd_set_position },
    { "w", "write string", cmd_write },
    { "s", "read button states", cmd_button_states },
    { NULL, NULL, NULL }
};

int main(void)
{
    puts("Test for the Daisy24 I2C 16x2 LCD monitor");

    /* define own shell commands */
    char line_buf[SHELL_DEFAULT_BUFSIZE];

    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
