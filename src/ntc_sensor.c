/**
 * @file ntc_sensor.c
 * @brief Реализация модуля опроса температуры NTC термистора
 */

#include "ntc_sensor.h"
#include "adc_manager.h"
#include "project_config.h"
#include <stddef.h>

/* ============================================================================
 * Локальные переменные
 * ============================================================================ */
static uint32_t filtered_adc_value = 0;      /* Отфильтрованное значение ADC для NTC */
static uint8_t filter_initialized = 0;      /* Флаг инициализации фильтра */
static uint16_t last_voltage_mv = 0;        /* Последнее значение напряжения NTC в мВ */
static uint16_t last_resistance_ohm = 0;     /* Последнее значение сопротивления NTC в Ом */
static int16_t last_temperature_c = 250;     /* Последнее значение температуры NTC в 0.1°C */
static volatile uint8_t data_ready = 0;      /* Флаг готовности новых данных NTC */
static ntc_calibration_t calibration = {0, 100};  /* Калибровочные коэффициенты (offset=0, scale=1.00) */

/* ============================================================================
 * Таблица сопротивлений NTC (B57703-M-103-G40, B=3950, R25=10k)
 * ============================================================================ */
/* Таблица: сопротивление -> температура (°C) для диапазона -40...+100°C */
typedef struct {
    uint32_t resistance;
    int16_t temperature;
} ntc_entry_t;

/* Генерация массива из макроса */
#define NTC_ENTRY(resistance, temp) {resistance, temp},
static const ntc_entry_t ntc_lookup_table[] = {
    NTC_LOOKUP_TABLE(NTC_ENTRY)
};
#undef NTC_ENTRY

#define NTC_LOOKUP_TABLE_SIZE  (sizeof(ntc_lookup_table) / sizeof(ntc_lookup_table[0]))


/* ============================================================================
 * Реализация API
 * ============================================================================ */


/**
 * @brief Инициализация модуля NTC сенсора
 */
void ntc_sensor_init(void) {
    /* Сброс фильтра */
    ntc_filter_reset();
}

/**
 * @brief Обновление значений из отфильтрованного ADC
 * @details Вычисляет напряжение, сопротивление и температуру
 */
static void ntc_update_values(uint16_t adc_value) {
    uint16_t voltage_mv;
    uint16_t resistance_ohm;
    
    /* Пересчет ADC в напряжение: V = adc * Vref / 4095 */
    voltage_mv = (uint16_t)((uint32_t)adc_value * NTC_VREF_MV / NTC_ADC_MAX);
    
    /* Расчет сопротивления NTC из формулы делителя:
     * V_ntc = Vref * R_ntc / (R_top + R_ntc)
     * R_ntc = V_ntc * R_top / (Vref - V_ntc)
     */
    if (voltage_mv >= NTC_VREF_MV) {
        resistance_ohm = NTC_R_TOP;  /* Защита от деления на ноль */
    } else {
        resistance_ohm = (uint16_t)((uint32_t)voltage_mv * NTC_R_TOP / (NTC_VREF_MV - voltage_mv));
    }
    
    /* Расчёт температуры по таблице поиска с линейной интерполяцией */
    int16_t temp_01c = 250;  /* Значение по умолчанию в 0.1°C */

    /* Поиск в таблице: находим два соседних значения для интерполяции */
    for (size_t i = 0; i < NTC_LOOKUP_TABLE_SIZE - 1; i++) {
        if (resistance_ohm >= ntc_lookup_table[i + 1].resistance &&
            resistance_ohm <= ntc_lookup_table[i].resistance) {

            /* Линейная интерполяция между двумя точками */
            uint32_t r_diff = ntc_lookup_table[i].resistance - ntc_lookup_table[i + 1].resistance;
            uint32_t r_offset = ntc_lookup_table[i].resistance - resistance_ohm;
            int16_t t_diff = ntc_lookup_table[i + 1].temperature - ntc_lookup_table[i].temperature;

            /* Интерполяция в °C, затем конвертация в 0.1°C */
            temp_01c = (ntc_lookup_table[i].temperature * 10) +
                      (int16_t)(((int32_t)t_diff * r_offset / r_diff) * 10);
            break;
        }
    }

    /* Защита от выхода за пределы таблицы */
    if (resistance_ohm > ntc_lookup_table[0].resistance) {
        temp_01c = (ntc_lookup_table[0].temperature - 5) * 10;  /* Ниже минимума */
    } else if (resistance_ohm < ntc_lookup_table[NTC_LOOKUP_TABLE_SIZE - 1].resistance) {
        temp_01c = (ntc_lookup_table[NTC_LOOKUP_TABLE_SIZE - 1].temperature + 5) * 10;  /* Выше максимума */
    }

    /* Применение калибровки: T_calibrated = offset + (scale / 100) * T_measured */
    /* offset в °C, temp_01c в 0.1°C, поэтому offset умножаем на 10 */
    int16_t temp_01c_calib = (calibration.offset * 10) + ((calibration.scale * temp_01c) / 100);

    last_temperature_c = temp_01c_calib;
    
    /* Сохранение значений */
    last_voltage_mv = voltage_mv;
    last_resistance_ohm = resistance_ohm;
    data_ready = 1;
}

/**
 * @brief Обновление данных из DMA буфера
 * @details Читает данные из adc_manager и обновляет значения NTC.
 */
void ntc_sensor_update(void) {
    const uint16_t* adc_buffer = adc_manager_get_buffer();
    uint16_t raw_adc_ntc = adc_buffer[ADC_CHANNEL_NTC];

    /* Цифровой фильтр для NTC: экспоненциальное сглаживание */
    /* filtered = alpha * filtered + (1 - alpha) * new */
    /* При SHIFT=4: alpha = 15/16, (1-alpha) = 1/16 */
    if (!filter_initialized) {
        filtered_adc_value = raw_adc_ntc << NTC_FILTER_SHIFT;
        filter_initialized = 1;
    } else {
        filtered_adc_value = filtered_adc_value - (filtered_adc_value >> NTC_FILTER_SHIFT) + raw_adc_ntc;
    }

    /* Обновление значений напряжения, сопротивления и температуры NTC */
    ntc_update_values((uint16_t)(filtered_adc_value >> NTC_FILTER_SHIFT));
}

/**
 * @brief Чтение последнего отфильтрованного значения напряжения с NTC в мВ
 */
uint16_t ntc_read_voltage_mv(void) {
    return last_voltage_mv;
}

/**
 * @brief Чтение последнего отфильтрованного значения сопротивления NTC в Ом
 */
uint16_t ntc_read_resistance_ohm(void) {
    return last_resistance_ohm;
}

/**
 * @brief Чтение последнего значения температуры в градусах Цельсия
 */
int16_t ntc_read_temperature_c(void) {
    return last_temperature_c;
}

/**
 * @brief Проверка готовности новых данных
 * @return 1 если доступны новые данные с момента последнего опроса, 0 иначе
 */
uint8_t ntc_data_ready(void) {
    if (data_ready) {
        data_ready = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief Сброс фильтра (очистка истории)
 */
void ntc_filter_reset(void) {
    filtered_adc_value = 0;
    filter_initialized = 0;
    data_ready = 0;
}

/**
 * @brief Установка калибровочных коэффициентов
 * @param calib Указатель на структуру с калибровочными коэффициентами
 */
void ntc_set_calibration(const ntc_calibration_t *calib) {
    if (calib != NULL) {
        calibration.offset = calib->offset;
        calibration.scale = calib->scale;
    }
}

/**
 * @brief Получение текущих калибровочных коэффициентов
 * @param calib Указатель на структуру для записи текущих коэффициентов
 */
void ntc_get_calibration(ntc_calibration_t *calib) {
    if (calib != NULL) {
        calib->offset = calibration.offset;
        calib->scale = calibration.scale;
    }
}

