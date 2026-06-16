/**
 * @file pb0_sensor.h
 * @brief Модуль опроса напряжения на входе PB0 через ADC
 * @details PB0 подключен к ADC каналу 9.
 *          Данные обновляются автоматически каждые 30 мс через DMA.
 */

#ifndef __PB0_SENSOR_H__
#define __PB0_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * API модуля
 * ============================================================================ */

/**
 * @brief Инициализация модуля PB0 сенсора
 * @details Регистрирует модуль в adc_manager для получения данных.
 *          Должен вызываться после adc_manager_init().
 */
void pb0_sensor_init(void);

/**
 * @brief Обновление данных из DMA буфера
 * @details Должен вызываться в основном цикле или по прерыванию
 *          для обновления локальных значений из adc_manager.
 */
void pb0_sensor_update(void);

/**
 * @brief Чтение последнего значения напряжения на PB0 в мВ
 * @return Напряжение на PB0 в милливольтах
 */
uint16_t pb0_read_voltage_mv(void);

/**
 * @brief Чтение последнего сырого значения ADC с PB0
 * @return Сырое значение ADC (0-4095)
 */
uint16_t pb0_read_raw(void);

/**
 * @brief Глобальный флаг активности SPI NSS на PB0
 * @details Устанавливается в true когда PB0 используется как SPI NSS,
 *          в это время ADC канал PB0 отключен и чтение напряжения недостоверно
 */
extern volatile bool pb0_spi_nss_active;

#ifdef __cplusplus
}
#endif

#endif /* __PB0_SENSOR_H__ */
