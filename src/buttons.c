#include "n32g430_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "project_config.h"
#include "buttons.h"
#include "debug.h"

/**
 * @brief Внутренние состояния FSM.
 */
typedef enum {
    ST_IDLE,
    ST_DEBOUNCE_PRESS,
    ST_PRESSED,
#ifdef BUTTON_LONG_PRESS_MS
    ST_LONG_PRESSED,
#endif
#ifdef BUTTON_DOUBLE_CLICK_MS
    ST_WAIT_SECOND_CLICK,
#endif
    ST_DEBOUNCE_RELEASE
} ButtonState_t;

typedef struct {
    ButtonConfig_t config;
    ButtonState_t state;
    uint32_t timer;
    bool is_active;
} ButtonInternal_t;

/* Статическое выделение памяти для FreeRTOS */
static StaticTask_t xButtonsTaskBuffer;
static StackType_t xButtonsStack[configMINIMAL_STACK_SIZE * 6];
static ButtonInternal_t buttons_list[MAX_BUTTONS];
static uint8_t buttons_count = 0;

/* Вспомогательная функция чтения физического уровня */
static bool is_button_pressed(ButtonInternal_t *btn) {
    return GPIO_Input_Pin_Data_Get(btn->config.port, btn->config.pin) == (btn->config.inverted)?PIN_RESET:PIN_SET;
}

/* Отправка события в очередь подписчика */
static void send_event(ButtonInternal_t *btn, ButtonEvent_t event) {
    ButtonMsg_t msg = { .button_id = btn->config.button_id, .event = event };
    if (btn->config.event_queue != NULL) {
        if (xQueueSend(btn->config.event_queue, &msg, 0) != pdPASS) {
            DEBUG_PRINTF("[Buttons] Error: Queue full for ID %d\n", btn->config.button_id);
        }
    }

    /* Логирование в RTT */
    const char* evt_names[] = {"DOWN", "UP", "CLICK",
#ifdef BUTTON_LONG_PRESS_MS
    "LONG_PRESS",
#endif
#ifdef BUTTON_DOUBLE_CLICK_MS
    "DBL_CLICK",
#endif
    };
    (void)evt_names;
    DEBUG_PRINTF("[Buttons] Time: %lu ID: %d Event: %s\n", xTaskGetTickCount(), btn->config.button_id, evt_names[event]);
}

/**
 * @brief Основной FSM обработчик.
 */
static void Buttons_ProcessFSM(ButtonInternal_t *btn) {
    bool pressed = is_button_pressed(btn);

    switch (btn->state) {
        case ST_IDLE:
            if (pressed) {
                btn->state = ST_DEBOUNCE_PRESS;
                btn->timer = 0;
            }
            break;

        case ST_DEBOUNCE_PRESS:
            btn->timer += BUTTON_POLL_RATE_MS;
            if (!pressed) btn->state = ST_IDLE;
            else if (btn->timer >= BUTTON_DEBOUNCE_MS) {
                btn->state = ST_PRESSED;
                btn->timer = 0;
                send_event(btn, BUTTON_EVENT_DOWN);
            }
            break;

        case ST_PRESSED:
            btn->timer += BUTTON_POLL_RATE_MS;
            if (!pressed) {
                send_event(btn, BUTTON_EVENT_UP);
#ifdef BUTTON_DOUBLE_CLICK_MS
#ifdef BUTTON_LONG_PRESS_MS
                if (btn->timer < BUTTON_LONG_PRESS_MS) {
                    btn->state = ST_WAIT_SECOND_CLICK;
                    btn->timer = 0;
                } else {
                    btn->state = ST_IDLE;
                }
#else
                btn->state = ST_WAIT_SECOND_CLICK;
                btn->timer = 0;
#endif
#else
                btn->state = ST_IDLE;
#endif
#ifdef BUTTON_LONG_PRESS_MS
            } else if (btn->timer >= BUTTON_LONG_PRESS_MS) {
                btn->state = ST_LONG_PRESSED;
                send_event(btn, BUTTON_EVENT_LONG_PRESS);
#endif
            }
            break;

#ifdef BUTTON_LONG_PRESS_MS
        case ST_LONG_PRESSED:
            if (!pressed) {
                btn->state = ST_IDLE;
                send_event(btn, BUTTON_EVENT_UP);
            }
            break;
#endif

#ifdef BUTTON_DOUBLE_CLICK_MS
        case ST_WAIT_SECOND_CLICK:
            btn->timer += BUTTON_POLL_RATE_MS;
            if (pressed) {
                // Второе нажатие обнаружено
                btn->state = ST_DEBOUNCE_RELEASE; // Ждем отпускания для DBL Click
                send_event(btn, BUTTON_EVENT_DOUBLE_CLICK);
            } else if (btn->timer >= BUTTON_DOUBLE_CLICK_MS) {
                // Время вышло - значит это был обычный клик
                btn->state = ST_IDLE;
                send_event(btn, BUTTON_EVENT_CLICK);
            }
            break;
#endif

        case ST_DEBOUNCE_RELEASE:
            if (!pressed) {
                btn->state = ST_IDLE;
                send_event(btn, BUTTON_EVENT_UP);
            }
            break;
    }
}

/**
 * @brief Задача FreeRTOS для опроса кнопок.
 */
static void Buttons_Task(void *pvParameters) {
    (void)pvParameters;
    // DEBUG_PRINTF("[Buttons] Task Started\n");

    for (;;) {
        for (uint8_t i = 0; i < buttons_count; i++) {
            Buttons_ProcessFSM(&buttons_list[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_RATE_MS));
    }
}

BaseType_t Buttons_Init(void) {
    TaskHandle_t xHandle = xTaskCreateStatic(
        Buttons_Task,
        "Buttons",
        sizeof(xButtonsStack)/sizeof(xButtonsStack[0]),
        NULL,
        configMAX_PRIORITIES - 2,
        xButtonsStack,
        &xButtonsTaskBuffer
    );
    return (xHandle != NULL) ? pdPASS : pdFAIL;
}

BaseType_t Buttons_Register(const ButtonConfig_t *config) {
    if (buttons_count >= MAX_BUTTONS) return pdFAIL;

    buttons_list[buttons_count].config = *config;
    buttons_list[buttons_count].state = ST_IDLE;
    buttons_list[buttons_count].is_active = true;
    buttons_count++;

    DEBUG_PRINTF("[Buttons] Registered ID:%u Pin:%lu\n", config->button_id, config->pin);
    return pdPASS;
}
