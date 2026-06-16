#include "debug.h"
#include "SEGGER_RTT.h"
#include "portmacro.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <stdio.h>
#include <string.h>

#ifdef DEBUG

static StaticSemaphore_t xLogMutexBuffer;
static SemaphoreHandle_t xLogMutex = NULL;

static SemaphoreHandle_t debug_log_mutex_get(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
        return NULL;

    if (xLogMutex == NULL) {
        taskENTER_CRITICAL();
        if (xLogMutex == NULL) {
            xLogMutex = xSemaphoreCreateBinaryStatic(&xLogMutexBuffer);
            xSemaphoreGive(xLogMutex);
#ifdef DEBUG
    		vQueueAddToRegistry(xLogMutex, "LOG");
#endif
        }
        taskEXIT_CRITICAL();
    }

    return xLogMutex;
}

__attribute__((weak)) int _write(int file, char *ptr, int len) {
    return SEGGER_RTT_WriteString(0, ptr);
}

/**
 * Функция для вывода отладочных сообщений с указанием файла и строки
 */
void debug_log_debug(const char *file, const long line, const char *format, ...) {
    va_list args;
    SemaphoreHandle_t mutex = debug_log_mutex_get();

    if (mutex != NULL) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }

    SEGGER_RTT_printf(0,"[%lu][DEBUG](%s:%ld) ", xTaskGetTickCount(), file, line);
    /* must use vsnprintf to format */
    va_start(args, format);
    SEGGER_RTT_vprintf(0, format, &args);
    va_end(args);
    SEGGER_RTT_PutChar(0, '\n');

    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

/**
 * Функция для вывода информационных сообщений
 */
void debug_log_info(const char *format, ...) {
    va_list args;
    SemaphoreHandle_t mutex = debug_log_mutex_get();

    if (mutex != NULL) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }

    SEGGER_RTT_printf(0, "[%lu][INFO] ", xTaskGetTickCount());
    /* must use vsnprintf to format */
    va_start(args, format);
    SEGGER_RTT_vprintf(0, format, &args);
    va_end(args);
    SEGGER_RTT_PutChar(0, '\n');

    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

/**
 * Функция для вывода отладочных сообщений (аналог DEBUG_RTT_printf)
 */
void debug_printf(const char *format, ...) {
    va_list args;
    SemaphoreHandle_t mutex = debug_log_mutex_get();
    BaseType_t xHigherPriorityTaskWoken;

    if (mutex != NULL) {
        if (!xPortIsInsideInterrupt())
            xSemaphoreTake(mutex, portMAX_DELAY);
        else
            xSemaphoreTakeFromISR(mutex, &xHigherPriorityTaskWoken);
    }

    va_start(args, format);
    // vprintf(format, args);
    SEGGER_RTT_vprintf(0, format, &args);
    va_end(args);

    if (mutex != NULL) {
        if (!xPortIsInsideInterrupt())
            xSemaphoreGive(mutex);
        else
        {
            xSemaphoreGiveFromISR(mutex, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

/**
 * Функция для вывода строки (аналог DEBUG_RTT_WriteString)
 */
void debug_write_string(const char *str) {
    SemaphoreHandle_t mutex = debug_log_mutex_get();

    if (mutex != NULL) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }

    // fputs(str, stdout);
    SEGGER_RTT_WriteString(0, str);

    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

#endif