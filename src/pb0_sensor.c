/**
 * @file pb0_sensor.c
 * @brief Реализация модуля опроса напряжения на PB0
 */

#include "pb0_sensor.h"
#include "adc_manager.h"

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
    last_adc_value = adc_buffer[ADC_CHANNEL_PB0];
    
    /* Пересчет ADC в напряжение: V = adc * Vref / 4095 */
    last_voltage_mv = (uint16_t)((uint32_t)last_adc_value * ADC_VREF_MV / ADC_MAX_VALUE);
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
