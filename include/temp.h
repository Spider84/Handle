#ifndef TEMP_H
#define TEMP_H

/**
 * @brief Инициализация модуля температуры (создание задач и примитивов)
 */
void Temp_Task_Init(void);

/**
 * @brief Получить текущую температуру NTC (в градусах Цельсия)
 */
int16_t Temp_GetNTC(void);

/**
 * @brief Получить текущую температуру кристалла процессора
 */
int16_t Temp_GetMCU(void);

#endif /* TEMP_H */
