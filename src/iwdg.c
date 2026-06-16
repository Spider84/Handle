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

void IWDG_Init(void)
{
	xTaskCreateStatic(reset_iwdg, "IWDGReset", sizeof(xStack)/sizeof(xStack[0]), NULL, configMAX_PRIORITIES-1, xStack, &xTaskBuffer);
}
