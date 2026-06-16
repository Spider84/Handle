/**
 * @file cpu_temp_sensor.c
 * @brief Реализация модуля опроса температуры процессора
 */

#include "cpu_temp_sensor.h"
#include "adc_manager.h"
#include "n32g430_flash.h"
#include "project_config.h"

/* ============================================================================
 * Локальные переменные
 * ============================================================================ */
static uint16_t last_adc_value = 0;       /* Последнее значение ADC с датчика температуры */
static int16_t last_temperature_c = 250;  /* Последнее значение температуры в 0.1°C */

/* Калибровочные значения из NVR для датчика температуры */
static uint16_t vts_cal_value = 1430;     /* VTS: напряжение датчика при калибровочной температуре (мВ) */
static int16_t t_cal_value = 25;         /* T: калибровочная температура (°C) */
static uint8_t cal_initialized = 0;      /* Флаг инициализации калибровки */

/* ============================================================================
 * Локальные функции
 * ============================================================================ */

/**
 * @brief Чтение калибровочных значений из NVR
 * @details Читает VTS и T из Non-Volatile Register для точного расчёта температуры
 */
static void read_temp_sensor_calibration(void) {
    uint32_t nvr_data;

    /* Отключение и сброс кэша FLASH для доступа к NVR */
    FLASH_ICache_Disable();
    FLASH_ICache_Reset();

    /* Чтение VTS (напряжение датчика при калибровочной температуре) */
    if (Get_NVR(NVR_ADDR_VTS, &nvr_data) == CMD_CR_SUCCESS) {
        vts_cal_value = (uint16_t)(nvr_data >> 16);  /* Старшие 16 бит */
    }

    /* Чтение T (калибровочная температура) */
    if (Get_NVR(NVR_ADDR_T, &nvr_data) == CMD_CR_SUCCESS) {
        t_cal_value = (int16_t)(nvr_data >> 16);  /* Старшие 16 бит */
    }

    /* Включение кэша FLASH */
    FLASH_ICache_Enable();

    cal_initialized = 1;
}

/* ============================================================================
 * Реализация API
 * ============================================================================ */

/**
 * @brief Инициализация модуля датчика температуры процессора
 */
void cpu_temp_sensor_init(void) {
    /* Чтение калибровочных значений из NVR */
    read_temp_sensor_calibration();

    last_adc_value = 0;
    last_temperature_c = 250;
}

/**
 * @brief Обновление данных из DMA буфера
 */
void cpu_temp_sensor_update(void) {
    const uint16_t* adc_buffer = adc_manager_get_buffer();

    /* Чтение значения температуры из буфера (индекс 2) */
    last_adc_value = adc_buffer[ADC_CHANNEL_TEMP];

    /*
     * Формула из документации N32G430:
     * Temperature = (VTS - V_sense * 1000) / (Avg_Slope * 1000) + T_cal / 1000 - 1.25
     * где:
     * - VTS = калибровочное напряжение из NVR (мВ)
     * - V_sense = текущее напряжение датчика (В)
     * - Avg_Slope = 4.3 мВ/°C
     * - T_cal = калибровочная температура из NVR (мК)
     * - 1.25 = смещение (°C)
     *
     * Целочисленная реализация (результат в 0.1°C):
     * voltage_mv = last_adc_value * 3300 / 4095
     * temp_01c = ((vts - voltage_mv) * 10) / 43 + (t_cal_mk / 100) - 125
     */

    /* Используем калибровочные значения из NVR, если они были прочитаны */
    uint16_t vts = cal_initialized ? vts_cal_value : 1430;  /* Значение по умолчанию 1.43V в мВ */
    int16_t t_cal_mk = cal_initialized ? t_cal_value : 25000;  /* Значение по умолчанию 25°C в мК */

    /* Вычисление напряжения в мВ с использованием 32-битной арифметики */
    uint32_t voltage_mv = ((uint32_t)last_adc_value * 3300) / 4095;

    /* Вычисление температуры в 0.1°C согласно формуле из документации */
    int32_t temp_01c = ((int32_t)(vts - voltage_mv) * 10) / 43 + (t_cal_mk / 100) - 125;

    last_temperature_c = (int16_t)temp_01c;
}

/**
 * @brief Чтение последнего значения температуры процессора
 * @return Температура процессора в дециградусах (0.1°C)
 */
int16_t cpu_temp_read_c(void) {
    return last_temperature_c;
}

/**
 * @brief Чтение последнего сырого значения датчика температуры процессора
 */
uint16_t cpu_temp_read_raw(void) {
    return last_adc_value;
}
