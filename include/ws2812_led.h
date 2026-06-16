/**
 * @file ws2812_led.h
 * @brief Module for controlling a single WS2812E LED via TIM3+DMA on STM32F411.
 */

#ifndef WS2812_LED_H
#define WS2812_LED_H

#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "ws2812e_dma.h"

/** @brief Macro to create RGB color */
#define RGB_COLOR(r, g, b) ((ws2812e_dma_color_t){g, r, b})

/** @brief Standard Colors */
#define COLOR_RED     RGB_COLOR(255, 0, 0)
#define COLOR_GREEN   RGB_COLOR(0, 255, 0)
#define COLOR_BLUE    RGB_COLOR(0, 0, 255)
#define COLOR_WHITE   RGB_COLOR(255, 255, 255)
#define COLOR_YELLOW  RGB_COLOR(255, 255, 0)
#define COLOR_MAGENTA RGB_COLOR(255, 0, 255)
#define COLOR_CYAN    RGB_COLOR(0, 255, 255)
#define COLOR_OFF     RGB_COLOR(0, 0, 0)

/**
 * @brief Initializes the WS2812 module and starts the control task.
 */
void WS2812_Init(void);

/**
 * @brief Sets a constant color for the LED.
 * @param rgb 24-bit RGB color.
 */
void WS2812_SetColor(ws2812e_dma_color_t rgb);

/**
 * @brief Blinks the LED with specified parameters.
 * @param rgb Color to blink.
 * @param interval_ms Full period interval in ms.
 * @param duration_ms Duration of the "ON" state in ms.
 * @param count Number of blinks (0 for infinite).
 */
void WS2812_Blink(ws2812e_dma_color_t rgb, uint32_t interval_ms, uint32_t duration_ms, uint32_t count);

/**
 * @brief Turns off the LED.
 */
void WS2812_Off(void);

#endif // WS2812_LED_H
