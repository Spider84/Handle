/**
 * @file buttons.h
 * @brief Модуль обработки кнопок на базе FSM и FreeRTOS (Static Memory).
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "n32g430.h"
#include "project_config.h"

/**
 * @brief Типы событий кнопок.
 */
typedef enum {
    BUTTON_EVENT_DOWN,          /**< Кнопка нажата */
    BUTTON_EVENT_UP,            /**< Кнопка отпущена */
    BUTTON_EVENT_CLICK,         /**< Короткий клик */
#ifdef BUTTON_LONG_PRESS_MS
    BUTTON_EVENT_LONG_PRESS,    /**< Длинное нажатие */
#endif
#ifdef BUTTON_DOUBLE_CLICK_MS
    BUTTON_EVENT_DOUBLE_CLICK   /**< Двойной клик */
#endif
} ButtonEvent_t;

/**
 * @brief Структура сообщения, передаваемого в очередь.
 */
typedef struct {
    uint8_t button_id;          /**< Идентификатор кнопки */
    ButtonEvent_t event;        /**< Тип произошедшего события */
} ButtonMsg_t;

/**
 * @brief Конфигурация подписки на кнопку.
 */
typedef struct {
    GPIO_Module* port;         /**< Порт GPIO (напр. GPIOA) */
    uint32_t pin;               /**< Пин GPIO (напр. LL_GPIO_PIN_0) */
    bool inverted;              /**< true, если нажатие - логический 0 */
    uint8_t button_id;          /**< Уникальный ID для идентификации в очереди */
    QueueHandle_t event_queue;  /**< Очередь, куда слать события */
} ButtonConfig_t;

/**
 * @brief Инициализация модуля кнопок. Создает задачу обработки.
 * @return pdPASS если задача создана успешно.
 */
BaseType_t Buttons_Init(void);

/**
 * @brief Регистрация кнопки и подписка на события.
 * @param config Указатель на структуру конфигурации.
 * @return pdPASS если регистрация успешна.
 */
BaseType_t Buttons_Register(const ButtonConfig_t *config);

#endif // BUTTONS_H
