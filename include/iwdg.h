/**
 * @file iwdg.h
 * @brief Модуль управления IWDG (Independent Watchdog)
 * @details Предоставляет задачу для периодического сброса IWDG
 */

#ifndef __IWDG_H__
#define __IWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Инициализация модуля IWDG
 * @details Создаёт задачу для периодического сброса IWDG
 */
void IWDG_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __IWDG_H__ */
