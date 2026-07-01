/**
 * @file iwdg.c
 * @brief Реализация модуля управления IWDG (Independent Watchdog)
 */

#include "iwdg.h"
#include "n32g430_iwdg.h"
#include "FreeRTOS.h"
#include "task.h"

/* ============================================================================
 * Локальные переменные
 * ============================================================================ */
static StaticTask_t xTaskBuffer;
static StackType_t xStack[configMINIMAL_STACK_SIZE * 2];

/* ============================================================================
 * Локальные функции
 * ============================================================================ */

/**
 * @brief Задача для периодического сброса IWDG
 * @param pArg не используется
 */
static void reset_iwdg(void *pArg)
{
	while (1)
	{
		IWDG_Key_Reload();
		vTaskDelay(pdMS_TO_TICKS(250));
	}
}

/* ============================================================================
 * Глобальные функции
 * ============================================================================ */

/**
 * @brief Инициализация и запуск IWDG
 * @details Настраивает IWDG на таймаут 1 секунду (LSI = 40 кГц)
 *          Prescaler = DIV64, Reload = 624
 *          Timeout = (624 + 1) * 64 / 40000 = 1.0 сек
 */
void IWDG_Init(void)
{
	// Отключение защиты записи для настройки регистров
	IWDG_Write_Protection_Disable();

	// Установка prescaler = DIV64 (делитель на 64)
	IWDG_Prescaler_Division_Set(IWDG_CONFIG_PRESCALER_DIV64);

	// Установка reload value = 624 для таймаута 1 секунды при LSI = 40 кГц
	// Timeout = (RELV + 1) * (PREDIV + 1) / LSI = (624 + 1) * 64 / 40000 = 1.0 сек
	IWDG_Counter_Reload(624);

	// Включение watchdog
	IWDG_Enable();

	// Создание задачи для периодического сброса IWDG
	// xTaskCreateStatic(reset_iwdg, "IWDGReset", sizeof(xStack)/sizeof(xStack[0]), NULL, configMAX_PRIORITIES-1, xStack, &xTaskBuffer);
}
