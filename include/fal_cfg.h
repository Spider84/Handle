/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-05-17     armink       the first version
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define NOR_FLASH_DEV_NAME             "norflash0"

#ifdef DEBUG
// #define FAL_DEBUG   1
#endif

#if !defined(FAL_DEBUG) || (FAL_DEBUG == 0)
#include "debug.h"
#define FAL_PRINTF  DEBUG_PRINTF
#else
#define FAL_PRINTF(...)
#endif

#define FAL_MALLOC                     malloc_show_error_here
#define FAL_CALLOC                     calloc_show_error_here
#define FAL_REALLOC                    realloc_show_error_here
#define FAL_FREE                       free_show_error_here

/* ===================== Flash device Configuration ========================= */
extern struct fal_flash_dev nor_flash0;

/* flash device table */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &nor_flash0,                                                     \
}
/* ====================== Partition Configuration ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG

/* partition table */
#define FAL_PART_TABLE                                                               \
{                                                                                    \
    {FAL_PART_MAGIC_WORD, "flash", NOR_FLASH_DEV_NAME, 0, 2*1024*1024, 0}, \
}
#endif /* FAL_PART_HAS_TABLE_CFG */

#endif /* _FAL_CFG_H_ */
