/*
 * buzzer.h
 *
 *  Created on: 11 июн. 2026 г.
 *      Author: Spider
 */

#ifndef BUZZER_H_
#define BUZZER_H_

#include <stdint.h>

typedef enum {
    BEEPER_CLKSEL_LSE = 0,    /* LSE clock (32.768 kHz) */
    BEEPER_CLKSEL_LSI = 1,    /* LSI clock (40 kHz) */
    BEEPER_CLKSEL_HSI = 2,    /* HSI clock (8 MHz) */
} beeper_clksel_t;

/**
 * @brief Режимы работы buzzer (устаревший, для обратной совместимости)
 */
typedef enum {
	BUZZER_MODE_OFF = 0,      ///< Выкл - бизер молчит
	BUZZER_MODE_SINGLE = 1,    ///< Одиночный пик 0.5 с - одно срабатывание
	BUZZER_MODE_TRIPLE = 2,    ///< Три пика по 0.5 с с паузой 0.25 с - зациклено
	BUZZER_MODE_CRITICAL = 3   ///< Критический пик 3 с с паузой 1 сек - пока не заменят другим состоянием
} BuzzerMode_t;

/**
 * @brief Конфигурация режима работы buzzer
 */
typedef struct {
    int32_t beep_count;       ///< Количество писков: 0 = выкл, -1 = бесконечно, >0 = конкретное число
    uint32_t beep_duration_ms;   ///< Длительность одного писка в мс
    uint32_t pause_duration_ms;  ///< Длительность паузы между писками в мс
} BuzzerConfig_t;

/**
 * @brief Инициализация buzzer
 */
void Buzzer_Init(void);

/**
 * @brief Установка конфигурации buzzer
 * @param config указатель на структуру конфигурации
 */
void Buzzer_SetConfig(const BuzzerConfig_t *config);

/**
 * @brief Получение текущей конфигурации buzzer
 * @param config указатель на структуру для сохранения текущей конфигурации
 */
void Buzzer_GetConfig(BuzzerConfig_t *config);

/**
 * @brief Установка режима работы buzzer (устаревший, для обратной совместимости)
 * @param mode режим работы buzzer
 */
void Buzzer_SetMode(BuzzerMode_t mode);

/**
 * @brief Получение текущего режима работы buzzer (устаревший, для обратной совместимости)
 * @return текущий режим работы buzzer
 */
BuzzerMode_t Buzzer_GetMode(void);

/**
 * @brief Выключение buzzer
 */
void Buzzer_Off(void);

#endif /* BUZZER_H_ */
