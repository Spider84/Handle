/*
 * flash_config.c
 *
 *  Created on: 11 июн. 2026 г.
 *      Author: Spider
 */

#include "flash_config.h"
#include "n32g430_flash.h"
#include "project_config.h"
#include "crc16.h"
#include "debug.h"
#include <string.h>

/**
 * @brief Массив конфигураций во flash (размещается в секции .flash_config)
 * @details Размер секции определяется символами из линкера:
 *          __flash_config_start и __flash_config_end
 *          Размер структуры: sizeof(flash_config_t) байт
 *          Количество записей вычисляется автоматически
 */
extern uint32_t __flash_config_start;
extern uint32_t __flash_config_end;

// Размер структуры уже кратен 4 байтам (20 байт), выравнивание не требуется
#define FLASH_CONFIG_MAX_RECORDS ((FLASH_CONFIG_SECTION_SIZE) / sizeof(flash_config_t))

// Порог для стирания страницы при приближении к переполнению sequence_number
#define FLASH_CONFIG_SEQUENCE_THRESHOLD 0xFFFF0000

// Массив максимального возможного размера (вычисляется автоматически)
// volatile необходим для предотвращения оптимизации чтения из flash компилятором
__attribute__((section(".flash_config"))) static volatile flash_config_t flash_config_storage[FLASH_CONFIG_MAX_RECORDS];
#define FLASH_CONFIG_BASE_ADDR ((uint32_t)&flash_config_storage)

/**
 * @brief Стирание страницы flash
 * @return true если стирание успешно, false в случае ошибки
 */
static bool flash_erase_page(void)
{
    FLASH_STS status;

    // Разблокировка flash
    FLASH_Unlock();

    // Стирание страницы
    status = FLASH_One_Page_Erase(FLASH_CONFIG_BASE_ADDR);

    // Блокировка flash
    FLASH_Lock();

    return (status == FLASH_EOP);
}

/**
 * @brief Программирование слова во flash
 * @param address адрес для записи (должен быть выровнен на 4 байта)
 * @param data данные для записи
 * @return true если программирование успешно, false в случае ошибки
 */
static bool flash_program_word(uint32_t address, uint32_t data)
{
    FLASH_STS status;

    // Разблокировка flash
    FLASH_Unlock();

    // Программирование слова
    status = FLASH_Word_Program(address, data);

    // Блокировка flash
    FLASH_Lock();

    return (status == FLASH_EOP);
}

/**
 * @brief Проверка валидности записи конфигурации
 * @param config указатель на структуру конфигурации (volatile для чтения из flash)
 * @return true если запись валидна, false в противном случае
 */
static bool is_config_valid(volatile const flash_config_t *config)
{
    uint32_t computed_crc;

    // Проверка магического числа
    if (config->magic != FLASH_CONFIG_MAGIC)
    {
        return false;
    }

    // Проверка размера структуры
    if (config->size != sizeof(flash_config_t))
    {
        return false;
    }

    // Вычисление и проверка CRC16
    // Явное приведение volatile указателя к обычному для CRC16_Fast
    computed_crc = CRC16_Fast((uint8_t*)(uintptr_t)config, offsetof(flash_config_t, crc16));
    if (computed_crc != config->crc16)
    {
        return false;
    }

    return true;
}

/**
 * @brief Поиск индекса последней валидной записи (с максимальным sequence_number)
 * @return индекс последней записи или -1 если валидных записей нет
 */
static int find_latest_record_index(void)
{
    int latest_index = -1;
    uint32_t max_sequence = 0;
    int i;

    for (i = 0; i < FLASH_CONFIG_MAX_RECORDS; i++)
    {
        if (is_config_valid(&flash_config_storage[i]))
        {
            if (flash_config_storage[i].sequence_number > max_sequence)
            {
                max_sequence = flash_config_storage[i].sequence_number;
                latest_index = i;
            }
        }
    }

    return latest_index;
}

/**
 * @brief Поиск индекса следующей свободной ячейки для записи
 * @return индекс свободной ячейки или -1 если нет свободного места
 */
static int find_next_free_slot(void)
{
    int i;

    for (i = 0; i < FLASH_CONFIG_MAX_RECORDS; i++)
    {
        // Ячейка считается свободной если magic не равен FLASH_CONFIG_MAGIC
        if (flash_config_storage[i].magic != FLASH_CONFIG_MAGIC)
        {
            return i;
        }
    }

    return -1;  // Нет свободного места
}

/**
 * @brief Получение следующего порядкового номера с обработкой переполнения
 * @param current_index индекс текущей записи
 * @return следующий порядковый номер
 */
static uint32_t get_next_sequence_number(int current_index)
{
    uint32_t current_seq;

    if (current_index >= 0 && current_index < FLASH_CONFIG_MAX_RECORDS)
    {
        current_seq = flash_config_storage[current_index].sequence_number;

        // Проверка на приближение к переполнению
        if (current_seq >= FLASH_CONFIG_SEQUENCE_THRESHOLD)
        {
            DEBUG_PRINTF("[FlashConfig] Sequence number near overflow (0x%08X), erasing page\r\n", current_seq);

            // Стирание страницы и сброс sequence_number
            if (flash_erase_page())
            {
                return 1;  // Начинаем с 1 после стирания
            }
            else
            {
                // Если стирание не удалось, продолжаем с текущим значением
                DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[FlashConfig] Failed to erase page, continuing with current sequence\r\n"RTT_CTRL_RESET);
            }
        }

        return current_seq + 1;
    }
    return 1;  // Первая запись
}

bool flash_config_init(void)
{
    // Поиск последней валидной записи
    int latest_index = find_latest_record_index();
    return (latest_index >= 0);
}

uint32_t flash_config_get_baudrate(void)
{
    int latest_index = find_latest_record_index();

    if (latest_index >= 0)
    {
        return (uint32_t)flash_config_storage[latest_index].baudrate * 100;
    }

    // Возврат значения по умолчанию при невалидной конфигурации
    return FLASH_CONFIG_DEFAULT_BAUDRATE * 100;
}

bool flash_config_set_baudrate(uint32_t baudrate)
{
    flash_config_t new_config;
    int latest_index;
    int next_slot;
    uint32_t next_sequence;
    uint32_t i;
    uint32_t write_addr;

    // Чтение текущего адреса устройства для сохранения
    uint8_t current_address = flash_config_get_modbus_address();

    // Поиск последней записи и следующего порядкового номера
    latest_index = find_latest_record_index();
    next_sequence = get_next_sequence_number(latest_index);

    // Поиск следующей свободной ячейки
    next_slot = find_next_free_slot();

    // Если нет свободного места - стираем страницу и пишем в начало
    if (next_slot < 0)
    {
        if (!flash_erase_page())
        {
            return false;
        }
        next_slot = 0;
        next_sequence = 1;  // Сброс порядкового номера
    }

    // Подготовка новой конфигурации
    new_config.magic = FLASH_CONFIG_MAGIC;
    new_config.size = sizeof(flash_config_t);
    new_config.sequence_number = next_sequence;
    new_config.baudrate = (uint16_t)(baudrate / 100);
    new_config.modbus_address = current_address;
    memset(new_config.reserved, 0, sizeof(new_config.reserved));
    new_config.crc16 = CRC16_Fast((uint8_t*)&new_config, offsetof(flash_config_t, crc16));

    // Запись конфигурации по словам (32 бита)
    write_addr = FLASH_CONFIG_BASE_ADDR + (next_slot * sizeof(flash_config_t));
    for (i = 0; i < (sizeof(flash_config_t) + 3) / sizeof(uint32_t); i++)
    {
        uint32_t word_data = ((uint32_t*)&new_config)[i];
        if (!flash_program_word(write_addr + (i * 4), word_data))
        {
            return false;
        }
    }

    // Проверка записи
    if (flash_config_storage[next_slot].baudrate != new_config.baudrate ||
        flash_config_storage[next_slot].magic != FLASH_CONFIG_MAGIC)
    {
        return false;
    }

    return true;
}

uint8_t flash_config_get_modbus_address(void)
{
    int latest_index = find_latest_record_index();

    if (latest_index >= 0)
    {
        return flash_config_storage[latest_index].modbus_address;
    }

    // Возврат значения по умолчанию при невалидной конфигурации
    return FLASH_CONFIG_DEFAULT_ADDRESS;
}

bool flash_config_set_modbus_address(uint8_t address)
{
    flash_config_t new_config;
    int latest_index;
    int next_slot;
    uint32_t next_sequence;
    uint32_t i;
    uint32_t write_addr;

    // Чтение текущего baudrate для сохранения
    uint32_t current_baudrate = flash_config_get_baudrate();

    // Поиск последней записи и следующего порядкового номера
    latest_index = find_latest_record_index();
    next_sequence = get_next_sequence_number(latest_index);

    // Поиск следующей свободной ячейки
    next_slot = find_next_free_slot();

    // Если нет свободного места - стираем страницу и пишем в начало
    if (next_slot < 0)
    {
        if (!flash_erase_page())
        {
            return false;
        }
        next_slot = 0;
        next_sequence = 1;  // Сброс порядкового номера
    }

    // Подготовка новой конфигурации
    new_config.magic = FLASH_CONFIG_MAGIC;
    new_config.size = sizeof(flash_config_t);
    new_config.sequence_number = next_sequence;
    new_config.baudrate = (uint16_t)(current_baudrate / 100);
    new_config.modbus_address = address;
    memset(new_config.reserved, 0, sizeof(new_config.reserved));
    new_config.crc16 = CRC16_Fast((uint8_t*)&new_config, offsetof(flash_config_t, crc16));

    // Запись конфигурации по словам (32 бита)
    write_addr = FLASH_CONFIG_BASE_ADDR + (next_slot * sizeof(flash_config_t));
    for (i = 0; i < (sizeof(flash_config_t) + 3) / sizeof(uint32_t); i++)
    {
        uint32_t word_data = ((uint32_t*)&new_config)[i];
        if (!flash_program_word(write_addr + (i * 4), word_data))
        {
            return false;
        }
    }

    // Проверка записи
    if (flash_config_storage[next_slot].modbus_address != address ||
        flash_config_storage[next_slot].magic != FLASH_CONFIG_MAGIC)
    {
        return false;
    }

    return true;
}

bool flash_config_reset(void)
{
    flash_config_t new_config;
    uint32_t i;
    uint32_t write_addr;

    // Подготовка конфигурации по умолчанию
    new_config.magic = FLASH_CONFIG_MAGIC;
    new_config.size = sizeof(flash_config_t);
    new_config.sequence_number = 1;
    new_config.baudrate = FLASH_CONFIG_DEFAULT_BAUDRATE;
    new_config.modbus_address = FLASH_CONFIG_DEFAULT_ADDRESS;
    memset(new_config.reserved, 0, sizeof(new_config.reserved));
    new_config.crc16 = CRC16_Fast((uint8_t*)&new_config, offsetof(flash_config_t, crc16));

    // Стирание страницы
    if (!flash_erase_page())
    {
        return false;
    }

    // Запись конфигурации по словам (32 бита) в начало массива
    write_addr = FLASH_CONFIG_BASE_ADDR;
    for (i = 0; i < (sizeof(flash_config_t) + 3) / sizeof(uint32_t); i++)
    {
        uint32_t word_data = ((uint32_t*)&new_config)[i];
        if (!flash_program_word(write_addr + (i * 4), word_data))
        {
            DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"Error writing to MCU Flash\r\n"RTT_CTRL_RESET);
            return false;
        }
    }

    // Проверка записи
    if (flash_config_storage[0].baudrate != FLASH_CONFIG_DEFAULT_BAUDRATE ||
        flash_config_storage[0].modbus_address != FLASH_CONFIG_DEFAULT_ADDRESS ||
        flash_config_storage[0].magic != FLASH_CONFIG_MAGIC)
    {
        return false;
    }

    return true;
}
