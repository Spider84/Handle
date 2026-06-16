#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG

#include <stdarg.h>
#include <stdint.h>
#include "SEGGER_RTT.h"

/**
 * Функция для вывода отладочных сообщений с указанием файла и строки
 * @param file имя файла
 * @param line номер строки
 * @param format формат строки
 * @param args список аргументов
 */
void debug_log_debug(const char *file, const long line, const char *format, ...);

/**
 * Функция для вывода информационных сообщений
 * @param format формат строки
 * @param args список аргументов
 */
void debug_log_info(const char *format, ...);

/**
 * Функция для вывода отладочных сообщений (аналог DEBUG_RTT_printf)
 * @param format формат строки
 * @param ... аргументы
 */
void debug_printf(const char *format, ...);

/**
 * Функция для вывода строки (аналог DEBUG_RTT_WriteString)
 * @param str строка для вывода
 */
void debug_write_string(const char *str);

#define DEBUG_PRINTF debug_printf

#else

#define DEBUG_PRINTF(...)

#endif //DEBUG

#endif // DEBUG_H
