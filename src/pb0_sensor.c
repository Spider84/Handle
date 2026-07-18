/**
 * @file pb0_sensor.c
 * @brief Реализация модуля опроса напряжения на PB0
 */

#include <FreeRTOS.h>
#include "pb0_sensor.h"
#include "adc_manager.h"
#include "modbus.h"

/* ============================================================================
 * Локальные переменные
 * ============================================================================ */
static uint16_t last_adc_value = 0;       /* Последнее значение ADC с PB0 */
static uint16_t last_voltage_mv = 0;      /* Последнее значение напряжения в мВ */

/* ============================================================================
 * Реализация API
 * ============================================================================ */

/**
 * @brief Инициализация модуля PB0 сенсора
 */
void pb0_sensor_init(void) {
    /* Модуль использует общий ADC из adc_manager */
    /* Дополнительная инициализация не требуется */
    last_adc_value = 0;
    last_voltage_mv = 0;
}

/**
 * @brief Обновление данных из DMA буфера
 */
void pb0_sensor_update(void) {
    const uint16_t* adc_buffer = adc_manager_get_buffer();

    /* Чтение значения PB0 из буфера (индекс 1) */
    portENTER_CRITICAL();
    last_adc_value = adc_buffer[ADC_CHANNEL_PB0];
    portEXIT_CRITICAL();

    /* Пересчет ADC в напряжение с калибровкой по VREFINT */
    last_voltage_mv = (uint16_t)adc_manager_raw_to_mv(last_adc_value);

    // const uint16_t tmp = MB_StorageInput.pb0_falgs & 0x8000;
    // MB_StorageInput.pb0_falgs = ((MB_StorageInput.pb0_falgs & ~0x8000)+1) | tmp;
}

/**
 * @brief Чтение последнего значения напряжения на PB0 в мВ
 */
uint16_t pb0_read_voltage_mv(void) {
    return last_voltage_mv;
}

/**
 * @brief Чтение последнего сырого значения ADC с PB0
 */
uint16_t pb0_read_raw(void) {
    return last_adc_value;
}
