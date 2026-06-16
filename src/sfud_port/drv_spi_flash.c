/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sfud.h>

sfud_flash sfud_norflash0 = {
        .name = NOR_FLASH_DEV_NAME,
        .spi.name = "SPI1",
};

int spi_flash_init(void)
{
    /* SFUD initialize */
    if (sfud_device_init(&sfud_norflash0) == SFUD_SUCCESS) {
        return 0;
    } else {
        return -1;
    }
}
