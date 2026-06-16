/*
 * flash_config.h
 *
 *  Created on: 11 июн. 2026 г.
 *      Author: Spider
 */

#ifndef INC_FLASH_CONFIG_H_
#define INC_FLASH_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Структура конфигурации, хранящаяся во внутренней flash
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           ///< Магическое число для валидации (0x4D4F4442 = "MODB")
    uint16_t size;            ///< Размер структуры для валидации
    uint16_t baudrate;        ///< Скорость Modbus в бодах (хранится как скорость/100, т.е. 9600 как 96, 115200 как 1152)
    uint8_t modbus_address;   ///< Адрес Modbus устройства
    uint8_t reserved[5];      ///< Резерв для выравнивания (размер структуры = 20 байт, кратен 4)
    uint32_t sequence_number; ///< Порядковый номер записи для wear leveling
    uint16_t crc16;           ///< CRC16 контрольная сумма структуры
} flash_config_t;

/**
 * @brief Размер секции конфигурации во flash (в байтах)
 */
#define FLASH_CONFIG_SECTION_SIZE  0x800  // 2048 байт (одна страница)

/**
 * @brief Магическое число для валидации конфигурации
 */
#define FLASH_CONFIG_MAGIC  0x4D4F4442  // "MODB"

/**
 * @brief Инициализация модуля конфигурации flash
 * @return true если конфигурация валидна, false если нужно использовать значения по умолчанию
 */
bool flash_config_init(void);

/**
 * @brief Чтение baudrate из flash
 * @return скорость в бодах (значение по умолчанию 9600 если конфигурация невалидна)
 */
uint32_t flash_config_get_baudrate(void);

/**
 * @brief Сохранение baudrate во внутреннюю flash
 * @param baudrate новая скорость в бодах
 * @return true если сохранение успешно, false в случае ошибки
 */
bool flash_config_set_baudrate(uint32_t baudrate);

/**
 * @brief Чтение адреса Modbus устройства из flash
 * @return адрес устройства (значение по умолчанию 10 если конфигурация невалидна)
 */
uint8_t flash_config_get_modbus_address(void);

/**
 * @brief Сохранение адреса Modbus устройства во внутреннюю flash
 * @param address новый адрес устройства
 * @return true если сохранение успешно, false в случае ошибки
 */
bool flash_config_set_modbus_address(uint8_t address);

/**
 * @brief Сброс конфигурации flash к значениям по умолчанию
 * @return true если сброс успешен, false в случае ошибки
 */
bool flash_config_reset(void);

/**
 * @brief Деинициализация FlashDB и SPI Flash
 * @details Вызывается перед hotswap физического носителя
 */
void flash_storage_deinit(void);

/**
 * @brief Инициализация/реинициализация Flash и KVDB
 * @details Вызывается при старте системы или при hotswap физического носителя
 * @return true при успешной инициализации, false при ошибке
 */
bool flash_storage_reinit(void);

#endif /* INC_FLASH_CONFIG_H_ */
