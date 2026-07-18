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
#include <assert.h>
#include <stdbool.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

/* ----------------------- Modbus includes ----------------------------------*/
#include "port.h"
#include "mbport.h"

/* ----------------------- N32G430 includes ----------------------------------*/
#include "n32g430.h"
#include "n32g430_usart.h"
#include "n32g430_rcc.h"
#include "portmacro.h"
#include "project_config.h"

/* ----------------------- Debug includes -----------------------------------*/
#include "debug.h"

/* ----------------------- SEGGER SystemView includes -----------------------*/
#ifndef SEGGER_SYSVIEW_DISABLE
#include "SEGGER_SYSVIEW_FreeRTOS.h"
#endif

/* ----------------------- Static variables ---------------------------------*/
volatile bool rxEnabled = false;
volatile bool txEnabled = false;

void
vMBPortSerialEnable( BOOL xRxEnable, BOOL xTxEnable )
{
    rxEnabled = xRxEnable;
    txEnabled = xTxEnable;

    if( xRxEnable )
    {
        USART_Interrput_Enable(USART2, USART_INT_RXDNE);
    }
    else
    {
        USART_Interrput_Disable(USART2, USART_INT_RXDNE);
    }

    if( xTxEnable )
    {
        // Для RS485: поднимаем RTS для передачи
        GPIO_Pins_Set(UART2_RTS_PORT, UART2_RTS_PIN);
        USART_Interrput_Enable(USART2, USART_INT_TXDE);
    }
    else
    {
        // Для RS485: опускаем RTS для приёма
        GPIO_Pins_Reset(UART2_RTS_PORT, UART2_RTS_PIN);
        USART_Interrput_Disable(USART2, USART_INT_TXDE);
    }

#ifdef MB_TX_COMPLETE_EMPTY
    USART_Interrput_Disable(USART2, USART_INT_TXC);
#endif
}

BOOL
xMBPortSerialInit( UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity, UCHAR ucStopBits )
{
    USART_InitType USART_InitStructure;

    assert(ucPORT==2);

    /* Включение тактирования USART2 (APB1) */
    RCC_APB1_Peripheral_Clock_Enable(RCC_APB1_PERIPH_USART2);

    /* Настройка параметров UART2 */
    USART_Structure_Initializes(&USART_InitStructure);
    USART_InitStructure.BaudRate = ulBaudRate;
    USART_InitStructure.WordLength = (ucDataBits == 8) ? USART_WL_8B : USART_WL_9B;

    /* Настройка чётности */
    switch (eParity)
    {
        case MB_PAR_NONE:
            USART_InitStructure.Parity = USART_PE_NO;
            break;
        case MB_PAR_ODD:
            USART_InitStructure.Parity = USART_PE_ODD;
            break;
        case MB_PAR_EVEN:
            USART_InitStructure.Parity = USART_PE_EVEN;
            break;
    }

    USART_InitStructure.StopBits = (ucStopBits == 1) ? USART_STPB_1 : USART_STPB_2;
    USART_InitStructure.Mode = USART_MODE_TX | USART_MODE_RX;
    USART_InitStructure.HardwareFlowControl = USART_HFCTRL_NONE;
    USART_Initializes(USART2, &USART_InitStructure);

    /* Включение UART2 */
    USART_Enable(USART2);

    /* Настройка приоритета прерывания */
    NVIC_SetPriority(USART2_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY+1, 0));
    NVIC_EnableIRQ(USART2_IRQn);

    return TRUE;
}

void
vMBPortSerialClose( void )
{
    NVIC_DisableIRQ(USART2_IRQn);
    USART_Disable(USART2);
    RCC_APB1_Peripheral_Reset(RCC_APB1_PERIPH_USART2);
    RCC_APB1_Peripheral_Clock_Disable(RCC_APB1_PERIPH_USART2);
}

BOOL
xMBPortSerialPutByte( CHAR ucByte )
{
    /* Отправка байта */
    USART_Data_Send(USART2, ucByte);

    return TRUE;
}

BOOL
xMBPortSerialGetByte( CHAR * pucByte )
{
    *pucByte = (CHAR)USART_Data_Receive(USART2);

    // DEBUG_PRINTF("[MB] RX: %02X\r\n", *pucByte);
    return TRUE;
}

/**
 * @brief Обработчик прерывания USART2 для FreeModbus
 */
void USART2_IRQHandler(void)
{
    traceISR_ENTER();

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* Проверка прерывания приёма */
    if (USART_Interrupt_Status_Get(USART2, USART_INT_RXDNE) != RESET)
    {
        if (rxEnabled/*(USART2->CTRL1 & ~USART_MODE_MASK) & USART_MODE_RX*/)
        {
            xHigherPriorityTaskWoken = pxMBFrameCBByteReceived()?pdTRUE:pdFALSE;
        }
        else
            USART_Data_Receive(USART2);
    }

    /* Проверка прерывания передачи */
    if (USART_Interrupt_Status_Get(USART2, USART_INT_TXDE) != RESET)
    {
        if (txEnabled/*(USART2->CTRL1 & ~USART_MODE_MASK) & USART_MODE_TX*/)
        {
            xHigherPriorityTaskWoken = pxMBFrameCBTransmitterEmpty()?pdTRUE:pdFALSE;
#ifdef MB_TX_COMPLETE_EMPTY
            if (xHigherPriorityTaskWoken)
            {
                USART_Interrupt_Status_Clear(USART2, USART_INT_TXC);
                /* Включаем прерывание TC для последнего байта */
                USART_Interrput_Enable(USART2, USART_INT_TXC);
                USART_Interrput_Disable(USART2, USART_INT_TXDE);
            }
#endif
        }
    }

#ifdef MB_TX_COMPLETE_EMPTY
    /* Проверка прерывания завершения передачи */
    if (USART_Interrupt_Status_Get(USART2, USART_INT_TXC) != RESET)
    {
        if (txEnabled)
        {
            /* Отключаем прерывание TC */
            USART_Interrput_Disable(USART2, USART_INT_TXC);
            /* Вызываем callback для завершения передачи */
            xHigherPriorityTaskWoken = pxMBFrameCBTransmitterEmpty()?pdTRUE:pdFALSE;
        }
    }
#endif

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
