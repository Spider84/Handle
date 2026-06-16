/*
 * FreeModbus Libary: Atmel AT91SAM3S Demo Application
 * Copyright (C) 2010 Christian Walter <cwalter@embedded-solutions.at>
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * IF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * File: $Id$
 */

/* ----------------------- System includes ----------------------------------*/
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

/* ----------------------- Modbus includes ----------------------------------*/
#include "port.h"
#include "mbport.h"

/* ----------------------- N32G430 includes ----------------------------------*/
#include "n32g430.h"
#include "n32g430_tim.h"
#include "n32g430_rcc.h"

/* ----------------------- SEGGER SystemView includes -----------------------*/
#ifndef SEGGER_SYSVIEW_DISABLE
#include "SEGGER_SYSVIEW_FreeRTOS.h"
#endif

/* ----------------------- Defines ------------------------------------------*/


/* ----------------------- Static variables ---------------------------------*/


/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortTimersInit( USHORT usTim1Timerout50us )
{
	// Включаем тактирование TIM6 (APB1)
	RCC_APB1_Peripheral_Clock_Enable(RCC_APB1_PERIPH_TIM6);
	RCC_APB1_Peripheral_Reset(RCC_APB1_PERIPH_TIM6);

	TIM_Off(TIM6);

	// Вычисляем частоту таймера
	// Для N32G430: TIM6 находится на APB1, частота таймера = HCLK (не зависит от делителя APB1)
	// SYSCLK = 128 МГц, HCLK = 128 МГц (по умолчанию)

	const uint32_t tim6_clk = (SystemClockFrequency / (1 << (((RCC->CFG & RCC_CFG_APB1PRES_DIV16) >> 8) - 3)))*((RCC->CFG & RCC_CFG_APB1PRES_DIV16)?2:1); // PCLK1 = 128 MHz / DIV

	// Рассчитываем Prescaler для получения шага в 50 мкс (20 кГц)
	const uint32_t prescaler = (tim6_clk / 20000) - 1;

	TIM_Base_Prescaler_Set(TIM6, (uint16_t)prescaler);

	// Устанавливаем период (usTim1Timerout50us — это количество "шагов" по 50 мкс)
	TIM_Base_Auto_Reload_Set(TIM6, usTim1Timerout50us);

	// Режим Up-counter (по умолчанию для TIM6)
	TIM_Base_Count_Mode_Set(TIM6, TIM_CNT_MODE_UP);

	// Настройка прерываний
	TIM_Interrupt_Enable(TIM6, TIM_INT_UPDATE);
	NVIC_SetPriority(TIM6_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY+2, 0));
	NVIC_EnableIRQ(TIM6_IRQn);

	return TRUE;
}

void
vMBPortTimerClose( void )
{
    NVIC_DisableIRQ( TIM6_IRQn );
    RCC_APB1_Peripheral_Clock_Disable(RCC_APB1_PERIPH_TIM6);
}

void
vMBPortTimersEnable(  )
{
	TIM_Base_Count_Set(TIM6,0);
	TIM_On(TIM6);
}

void
vMBPortTimersDisable(  )
{
	TIM_Off(TIM6);
}

void
vMBPortTimersDelay( USHORT usTimeOutMS )
{
    vTaskDelay( pdMS_TO_TICKS(usTimeOutMS) );
}

/**
 * @brief Обработчик прерывания таймера TIM6.
 * Вызывается, когда интервал T3.5 или T1.5 истек.
 */
void TIM6_IRQHandler(void)
{
    traceISR_ENTER();

    // Проверяем, что прерывание вызвано именно событием обновления (Update)
    if (TIM_Interrupt_Status_Get(TIM6, TIM_INT_UPDATE) != RESET)
    {
        // Обязательно сбрасываем флаг прерывания,
        // иначе процессор будет бесконечно входить в этот обработчик.
        TIM_Interrupt_Status_Clear(TIM6, TIM_INT_UPDATE);

        /* Вызываем функцию обратного вызова из стека FreeModbus.
         * Эта функция реализована внутри mb.c (или порта).
         * Она переведет состояние Modbus-машины в STATE_RX_IDLE
         * или оповестит о готовности фрейма.
         */
        portEND_SWITCHING_ISR(pxMBPortCBTimerExpired());

		// DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[MB] Timeout\r\n"RTT_CTRL_RESET);
    }
	else
	{
    	traceISR_EXIT();
	}
}
