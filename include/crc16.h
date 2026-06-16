/**
 * @file crc16.h
 * @brief Модуль расчёта CRC16 (полином CCITT 0x1021)
 */

#ifndef __CRC16_H__
#define __CRC16_H__

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Быстрый расчёт CRC16 с полиномом CCITT (0x1021)
 * @param data указатель на данные
 * @param length длина данных в байтах
 * @return CRC16 значение
 */
uint16_t CRC16_Fast(uint8_t* data, size_t length);

#endif /* __CRC16_H__ */
