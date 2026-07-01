/**
 * @file ws2812e_dma.h
 * @brief Драйвер для управления WS2812E через DMA+PWM (TIM3_CH4 на PB1)
 * @details Оптимизирован для 1 диода, использует DMA для передачи данных
 */

#ifndef __WS2812E_DMA_H__
#define __WS2812E_DMA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Структура цвета RGB */
typedef union __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t g;  /* Green (WS2812E использует порядок GRB) */
        uint8_t r;  /* Red */
        uint8_t b;  /* Blue */
    };
    uint32_t dec;
} ws2812e_dma_color_t;

/**
 * @brief Инициализация драйвера WS2812E с DMA+PWM
 * @note Настраивает TIM3_CH4 на PB1 (AF2) и DMA1_CH7
 */
void ws2812e_dma_init(void);

/**
 * @brief Установка цвета одному светодиоду
 * @param color Структура цвета RGB
 * @note Запускает DMA передачу, функция возвращает сразу
 */
void ws2812e_dma_set_color(ws2812e_dma_color_t color);

/**
 * @brief Проверка завершения передачи DMA
 * @return 1 - передача завершена, 0 - передача в процессе
 */
uint8_t ws2812e_dma_is_complete(void);

/**
 * @brief Очистка (выключение) светодиода
 */
void ws2812e_dma_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812E_DMA_H__ */
