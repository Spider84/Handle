/**
 * @file keys.h
 * @brief Определения ключей для KVDB FlashDB
 *
 * ВАЖНО ДЛЯ БУДУЩЕГО ИИ:
 * При добавлении новых ключей убедитесь, что длина ключа не превышает
 * FDB_KV_NAME_MAX (определено в fdb_def.h, по умолчанию 64 символа).
 * Используйте snprintf с размером буфера не более FDB_KV_NAME_MAX
 * при формировании ключей из шаблонов.
 */

#ifndef __KEYS_H
#define __KEYS_H

#include <stdint.h>

/* Ключи для хранения заголовков */
#define KVDB_KEY_HEADER_0         "HEADER_0"
#define KVDB_KEY_HEADER_1         "HEADER_1"

/* Ключи для хранения счётчиков */
#define KVDB_KEY_ARCHIVE_SERVICE  "archive_service"
#define KVDB_KEY_ARCHIVE_RI       "archive_ri"

#define KVDB_KEY_GEAR             "gear"
#define KVDB_KEY_GEAR_RESOURCE    "gear_resource"
#define KVDB_KEY_CUT_TOOL         "cut_tool"
#define KVDB_KEY_SPINDEL          "spindel"

/* Шаблон для формирования ключей заголовков */
#define KVDB_KEY_HEADER_TEMPLATE  "HEADER_%d"

/* Шаблон для формирования ключей данных РИ по индексу */
#define KVDB_KEY_RI_TEMPLATE      "RI_%d"

/* Шаблон для формирования ключей данных сервисного обслуживания по индексу */
#define KVDB_KEY_SERVICE_TEMPLATE "SERVICE_%d"

#endif /* __KEYS_H */
