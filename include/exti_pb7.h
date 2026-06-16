/**
 * @file exti_pb7.h
 * @brief Драйвер внешнего прерывания на PB7 для N32G430
 * @details Прерывание на PB7 для будущих нужд
 */

#ifndef __EXTI_PB7_H__
#define __EXTI_PB7_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Инициализация внешнего прерывания на PB7
 * @details Настраивает EXTI7 по падающему фронту для PB7
 */
void exti_pb7_init(void);

/**
 * @brief Обработчик прерывания EXTI7 (PB7)
 * @details Вызывается при срабатывании прерывания на PB7
 */
void exti_pb7_irq_handler(void);

/**
 * @brief Проверка флага прерывания PB7
 * @return 1 если было прерывание, 0 если нет
 */
uint8_t exti_pb7_get_irq_flag(void);

#ifdef __cplusplus
}
#endif

#endif /* __EXTI_PB7_H__ */
