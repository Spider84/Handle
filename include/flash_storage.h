/**
 * @file flash_storage.h
 * @brief Модуль управления FlashDB и SPI Flash хранилищем
 * @details Предоставляет функции для инициализации, деинициализации и работы с KVDB
 */

#ifndef __FLASH_STORAGE_H__
#define __FLASH_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Инициализация/реинициализация Flash и KVDB
 * @details Вызывается при старте системы или при hotswap физического носителя
 * @return true при успешной инициализации, false при ошибке
 */
bool flash_storage_reinit(void);

/**
 * @brief Деинициализация FlashDB и SPI Flash
 * @details Вызывается перед hotswap физического носителя
 */
void flash_storage_deinit(void);

/**
 * @brief Проверка инициализированности FlashDB
 * @return true если FlashDB инициализирован, false в противном случае
 */
bool flash_storage_is_initialized(void);

/**
 * @brief Установка флага инициализации FlashDB
 * @param initialized состояние инициализации
 */
void flash_storage_set_initialized(bool initialized);

/**
 * @brief Получение указателя на KVDB
 * @return указатель на структуру fdb_kvdb
 */
struct fdb_kvdb* flash_storage_get_kvdb(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_STORAGE_H__ */
