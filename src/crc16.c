/**
 * @file crc16.c
 * @brief Реализация модуля расчёта CRC16 (полином CCITT 0x1021)
 */

#include "crc16.h"
#include "project_config.h"

uint16_t CRC16_Fast(uint8_t* data, size_t length) {
    uint16_t crc = CRC16_INITIAL_VALUE;
    while (length--) {
        crc ^= (uint16_t)*data++ << 8;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
