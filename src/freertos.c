/*
 * freertos.c
 *
 *  Created on: Oct 25, 2025
 *      Author: Spider
 */

#include <stdlib.h>
#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>
#include "debug.h"

void vConfigureTimerForRunTimeStats(void);
unsigned long vGetTimerForRunTimeStats(void);

void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
	//__builtin_trap();
	DEBUG_PRINTF("StackOverflow: %s\r\n", pcTaskName);
	configASSERT(0);
}
