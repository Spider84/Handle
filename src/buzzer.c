/*
 * buzzer.c
 *
 *  Created on: 11 июн. 2026 г.
 *      Author: Spider
 */

#include "buzzer.h"
#include "n32g430.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "project_config.h"

/* Текущий источник тактирования */
static beeper_clksel_t current_clksel = BEEPER_CLKSEL_LSI;

/* FreeRTOS Static Allocation */
static StackType_t xBuzzerStack[configMINIMAL_STACK_SIZE * 3];
static StaticTask_t xBuzzerTaskBuffer;
static SemaphoreHandle_t xBuzzerMutex;
static StaticSemaphore_t xMutexBuffer;

/* Состояние buzzer */
typedef enum {
    BUZZER_STATE_IDLE = 0,
    BUZZER_STATE_BEEP_ON,
    BUZZER_STATE_BEEP_OFF
} buzzer_state_t;

static struct {
    BuzzerConfig_t config;
    buzzer_state_t state;
    int32_t remaining_beeps;
} buzzer_state = {
    .config = {
        .beep_count = 0,
        .beep_duration_ms = 0,
        .pause_duration_ms = 0
    },
    .state = BUZZER_STATE_IDLE,
    .remaining_beeps = 0
};

/**
 * @brief Расчёт делителей для получения нужной частоты
 * @param src_freq_hz Частота источника тактирования
 * @param target_freq_hz Желаемая частота
 * @param psc Указатель для хранения прескалера
 * @param beepdiv Указатель для хранения делителя
 */
static void calculate_dividers(uint32_t src_freq_hz, uint32_t target_freq_hz,
                               uint32_t *psc, uint32_t *beepdiv) {
    /* BEEPER выходная частота = src_freq / (2 * (psc + 1) * (beepdiv + 1)) */
    /* src_freq / (2 * (psc + 1) * (beepdiv + 1)) = target_freq */
    /* (psc + 1) * (beepdiv + 1) = src_freq / (2 * target_freq) */

    uint32_t total_div = src_freq_hz / (2 * target_freq_hz);

    /* Ограничиваем максимальное значение делителя */
    if (total_div > 4096) {
        total_div = 4096;
    }

    /* Оптимальное распределение: psc = beepdiv = sqrt(total_div) */
    uint32_t div = (total_div > 0) ? total_div : 1;
    *psc = div - 1;
    *beepdiv = div - 1;

    /* Ограничиваем значения по регистрам */
    if (*psc > 63) *psc = 63;        /* PSC: 6 бит */
    if (*beepdiv > 511) *beepdiv = 511; /* BEEPDIV: 9 бит */
}

/**
 * @brief Задача FreeRTOS для управления buzzer
 */
static void Buzzer_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        xSemaphoreTake(xBuzzerMutex, portMAX_DELAY);

        /* Проверка на выключение */
        if (buzzer_state.config.beep_count == 0) {
            BEEPER_Disable();
            buzzer_state.state = BUZZER_STATE_IDLE;
            buzzer_state.remaining_beeps = 0;
            xSemaphoreGive(xBuzzerMutex);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Единый FSM для всех режимов */
        switch (buzzer_state.state) {
            case BUZZER_STATE_IDLE:
                /* Начало цикла писков */
                buzzer_state.state = BUZZER_STATE_BEEP_ON;
                buzzer_state.remaining_beeps = buzzer_state.config.beep_count;
                BEEPER_Enable();
                xSemaphoreGive(xBuzzerMutex);
                vTaskDelay(pdMS_TO_TICKS(buzzer_state.config.beep_duration_ms));
                break;

            case BUZZER_STATE_BEEP_ON:
                BEEPER_Disable();
                buzzer_state.state = BUZZER_STATE_BEEP_OFF;
                if (buzzer_state.remaining_beeps > 0) {
                    buzzer_state.remaining_beeps--;
                }
                xSemaphoreGive(xBuzzerMutex);

                /* Проверка на завершение */
                if (buzzer_state.remaining_beeps == 0) {
                    /* Для отрицательного beep_count - бесконечный режим, начинаем новый цикл */
                    if (buzzer_state.config.beep_count < 0) {
                        vTaskDelay(pdMS_TO_TICKS(buzzer_state.config.pause_duration_ms));
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(buzzer_state.config.pause_duration_ms));
                }
                break;

            case BUZZER_STATE_BEEP_OFF:
                if (buzzer_state.remaining_beeps == 0) {
                    if (buzzer_state.config.beep_count < 0) {
                        /* Бесконечный режим - начинаем новый цикл */
                        buzzer_state.state = BUZZER_STATE_BEEP_ON;
                        buzzer_state.remaining_beeps = buzzer_state.config.beep_count;
                        BEEPER_Enable();
                        xSemaphoreGive(xBuzzerMutex);
                        vTaskDelay(pdMS_TO_TICKS(buzzer_state.config.beep_duration_ms));
                    } else {
                        /* Все писки завершены */
                        buzzer_state.state = BUZZER_STATE_IDLE;
                        buzzer_state.config.beep_count = 0;
                        xSemaphoreGive(xBuzzerMutex);
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                } else {
                    /* Следующий пик */
                    buzzer_state.state = BUZZER_STATE_BEEP_ON;
                    BEEPER_Enable();
                    xSemaphoreGive(xBuzzerMutex);
                    vTaskDelay(pdMS_TO_TICKS(buzzer_state.config.beep_duration_ms));
                }
                break;

            default:
                BEEPER_Disable();
                buzzer_state.state = BUZZER_STATE_IDLE;
                xSemaphoreGive(xBuzzerMutex);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

/**
 * @brief Инициализация buzzer
 */
void Buzzer_Init(void)
{
	buzzer_state.config.beep_count = 0;
	buzzer_state.state = BUZZER_STATE_IDLE;
	buzzer_state.remaining_beeps = 0;

	/* Инициализация пищалки на PB6 */
    RCC_LSI_Enable();
    RCC_LSI_Stable_Wait();

	/* Включение тактирования BEEPER */
    RCC_APB2_Peripheral_Clock_Enable(RCC_APB2_PERIPH_BEEPER);

    /* GPIO инициализируется в gpio_init() */

    /* Сохраняем источник тактирования */
    current_clksel = BEEPER_CLKSEL_LSI;

    /* Определяем частоту источника */
    uint32_t src_freq;
    uint32_t clock_source;
    switch (current_clksel) {
        case BEEPER_CLKSEL_LSE:
            src_freq = LSE_FREQ_HZ;
            clock_source = BEEPER_CLK_SOURCE_LSE;
            break;
        case BEEPER_CLKSEL_LSI:
            src_freq = LSI_FREQ_HZ;
            clock_source = BEEPER_CLK_SOURCE_LSI;
            break;
        case BEEPER_CLKSEL_HSI:
            src_freq = HSI_VALUE;
            clock_source = BEEPER_CLK_SOURCE_APB;
            break;
        default:
            src_freq = LSE_FREQ_HZ;
            clock_source = BEEPER_CLK_SOURCE_LSE;
            break;
    }

    /* Расчёт делителей */
    uint32_t psc, beepdiv;
    calculate_dividers(src_freq, BUZZER_FREQ_HZ, &psc, &beepdiv);

    /* Настройка BEEPER через Std Periph Driver */
    BEEPER_Initialize(clock_source, psc, beepdiv);

    /* Создаём мьютекс */
    xBuzzerMutex = xSemaphoreCreateBinaryStatic(&xMutexBuffer);
    xSemaphoreGive(xBuzzerMutex);
#ifdef DEBUG
    vQueueAddToRegistry(xBuzzerMutex, "Buzzer");
#endif

    /* Создаём задачу buzzer */
    xTaskCreateStatic(Buzzer_Task, "Buzzer", sizeof(xBuzzerStack)/sizeof(xBuzzerStack[0]),
                      NULL, configMAX_PRIORITIES - 2, xBuzzerStack, &xBuzzerTaskBuffer);

    /* Выключаем по умолчанию */
    BEEPER_Disable();
}

/**
 * @brief Установка конфигурации buzzer
 * @param config указатель на структуру конфигурации
 */
void Buzzer_SetConfig(const BuzzerConfig_t *config)
{
	xSemaphoreTake(xBuzzerMutex, portMAX_DELAY);
	buzzer_state.config = *config;
	buzzer_state.state = BUZZER_STATE_IDLE;
	buzzer_state.remaining_beeps = 0;
	xSemaphoreGive(xBuzzerMutex);
}

/**
 * @brief Получение текущей конфигурации buzzer
 * @param config указатель на структуру для сохранения текущей конфигурации
 */
void Buzzer_GetConfig(BuzzerConfig_t *config)
{
	xSemaphoreTake(xBuzzerMutex, portMAX_DELAY);
	*config = buzzer_state.config;
	xSemaphoreGive(xBuzzerMutex);
}

/**
 * @brief Установка режима работы buzzer (устаревший, для обратной совместимости)
 * @param mode режим работы buzzer
 */
void Buzzer_SetMode(BuzzerMode_t mode)
{
	BuzzerConfig_t config;

	switch (mode) {
		case BUZZER_MODE_SINGLE:
			config.beep_count = 1;
			config.beep_duration_ms = BUZZER_SINGLE_LENGTH;
			config.pause_duration_ms = BUZZER_SINGLE_PAUSE;
			break;
		case BUZZER_MODE_TRIPLE:
			config.beep_count = 3; /* 3 писка с повторением цикла */
			config.beep_duration_ms = BUZZER_TRIPLE_LENGTH;
			config.pause_duration_ms = BUZZER_TRIPLE_PAUSE;
			break;
		case BUZZER_MODE_CRITICAL:
			config.beep_count = 1; /* 1 пик с повторением цикла */
			config.beep_duration_ms = BUZZER_CRITICAL_LENGTH;
			config.pause_duration_ms = BUZZER_CRITICAL_PAUSE;
			break;
        case BUZZER_MODE_OFF:
		default:
			config.beep_count = 0;
			config.beep_duration_ms = 0;
			config.pause_duration_ms = 0;
			break;
	}

	Buzzer_SetConfig(&config);
}

/**
 * @brief Получение текущего режима работы buzzer (устаревший, для обратной совместимости)
 * @return текущий режим работы buzzer
 */
BuzzerMode_t Buzzer_GetMode(void)
{
	BuzzerConfig_t config;
	BuzzerMode_t mode;

	Buzzer_GetConfig(&config);

	if (config.beep_count == 0) {
		mode = BUZZER_MODE_OFF;
	} else if (config.beep_count == 1) {
		mode = BUZZER_MODE_SINGLE;
	} else if (config.beep_count == -1 && config.beep_duration_ms == 500 && config.pause_duration_ms == 250) {
		mode = BUZZER_MODE_TRIPLE;
	} else if (config.beep_count == -1 && config.beep_duration_ms == 3000 && config.pause_duration_ms == 1000) {
		mode = BUZZER_MODE_CRITICAL;
	} else {
		mode = BUZZER_MODE_OFF;
	}

	return mode;
}

/**
 * @brief Выключение buzzer
 */
void Buzzer_Off(void)
{
	BuzzerConfig_t config = {0};
	Buzzer_SetConfig(&config);
}
