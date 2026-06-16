/**
 * @file flash_storage.c
 * @brief Реализация модуля управления FlashDB и SPI Flash хранилищем
 */

#include "flash_storage.h"
#include "flashdb.h"
#include "keys.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "sysview_events.h"
#include "debug.h"
#include "modbus.h"
#include <string.h>

/* ============================================================================
 * Глобальные переменные
 * ============================================================================ */
struct fdb_kvdb kvdb = { 0 };

/* Глобальный мьютекс для FlashDB */
static StaticSemaphore_t xFlashDBMutexBuffer;
static SemaphoreHandle_t xFlashDBMutex = NULL;

/* Флаг инициализации FlashDB */
static bool flashdb_initialized = false;

/* ============================================================================
 * Локальные функции
 * ============================================================================ */

/**
 * @brief Метод блокировки библиотеки для межпоточной синхронизации
 */
static void fdb_lock(fdb_db_t db)
{
	SYSVIEW_RecordVoid(SYSVIEW_MODULE_EVENT(SYSVIEW_MODULE_FLASHDB, SYSVIEW_EVT_FLASHDB_LOCK));
	xSemaphoreTake((SemaphoreHandle_t)db->user_data, portMAX_DELAY);
}

/**
 * @brief Метод разблокировки библиотеки для межпоточной синхронизации
 */
static void fdb_unlock(fdb_db_t db)
{
	xSemaphoreGive((SemaphoreHandle_t)db->user_data);
	SYSVIEW_RecordVoid(SYSVIEW_MODULE_EVENT(SYSVIEW_MODULE_FLASHDB, SYSVIEW_EVT_FLASHDB_UNLOCK));
}

/* ============================================================================
 * Глобальные функции
 * ============================================================================ */

bool flash_storage_reinit(void)
{
	extern int spi_flash_init(void);

	SYSVIEW_RecordVoid(SYSVIEW_MODULE_EVENT(SYSVIEW_MODULE_FLASHDB, SYSVIEW_EVT_FLASHDB_INIT));

	/* Сброс флагов статуса устройства */
	MB_StorageInput.device_status &= ~(DEVICE_MEM_MOUNTED | DEVICE_READY);

	/* Создание мьютекса для FlashDB */
	if (xFlashDBMutex == NULL) {
		xFlashDBMutex = xSemaphoreCreateBinaryStatic(&xFlashDBMutexBuffer);
		if (xFlashDBMutex == NULL) {
			return false;
		}
#ifdef DEBUG
		vQueueAddToRegistry(xFlashDBMutex, "FlashDB");
#endif
		xSemaphoreGive(xFlashDBMutex);
	}

	/* Инициализация SPI Flash */
	if (spi_flash_init() != 0) {
		MB_StorageInput.device_status &= ~DEVICE_FLASH_VALID;
		return false;
	}

	MB_StorageInput.device_status |= DEVICE_FLASH_VALID;

	/* Инициализация KVDB */
	fdb_kvdb_control(&kvdb, FDB_KVDB_CTRL_SET_LOCK, (void *)fdb_lock);
	fdb_kvdb_control(&kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)fdb_unlock);
	if (fdb_kvdb_init(&kvdb, "kvdb", "kvdb", NULL, (void *)xFlashDBMutex) != FDB_NO_ERR) {
		MB_StorageInput.device_status |= DEVICE_ARCHIVE_FAULT;
		return false;
	}

	MB_StorageInput.device_status &= ~DEVICE_ARCHIVE_FAULT;
	MB_StorageInput.device_status |= DEVICE_MEM_MOUNTED;

	struct fdb_blob blob;

	memset(&MB_StorageHolding.gear, 0xFF, sizeof(MB_StorageHolding.gear)+sizeof(MB_StorageHolding.cut)+sizeof(MB_StorageHolding.spindel));

	/* Использование выбранного заголовка */
	gear_info_t gear = {0};
	if (fdb_kv_get_blob(&kvdb, KVDB_KEY_GEAR, fdb_blob_make(&blob, &gear, sizeof(gear)-sizeof(gear.resource))) == sizeof(gear)-sizeof(gear.resource)) {
		memcpy(&MB_StorageHolding.gear, &gear, sizeof(gear)-sizeof(gear.resource));
	}
	else
	{
		DEBUG_PRINTF(RTT_CTRL_TEXT_YELLOW"[WARN] No Gear Info in flash. New device?\r\n"RTT_CTRL_RESET);
		return false;
	}

	MB_StorageInput.device_status |= DEVICE_READY;

	gear_resource_t resource = {0};
	if (fdb_kv_get_blob(&kvdb, KVDB_KEY_GEAR_RESOURCE, fdb_blob_make(&blob, &resource, sizeof(resource))) == sizeof(resource)) {
		MB_StorageHolding.gear.resource.work_resource = resource.work_resource;
		MB_StorageHolding.gear.resource.total_resource = resource.total_resource;
	}

	cut_tool_info_t cut_tool = {0};
	if (fdb_kv_get_blob(&kvdb, KVDB_KEY_CUT_TOOL, fdb_blob_make(&blob, &cut_tool, sizeof(cut_tool))) == sizeof(cut_tool)) {
		memcpy(&MB_StorageHolding.cut, &cut_tool, sizeof(cut_tool));
	}

	spindle_info_t spindel = {0};
	if (fdb_kv_get_blob(&kvdb, KVDB_KEY_SPINDEL, fdb_blob_make(&blob, &spindel, sizeof(spindel))) == sizeof(spindel)) {
		memcpy(&MB_StorageHolding.spindel, &spindel, sizeof(spindel));
	}

	/* Чтение счётчиков из KVDB */
	uint16_t service_count = 0xFFFF;
	uint16_t tool_count = 0xFFFF;
	if (fdb_kv_get_blob(&kvdb, KVDB_KEY_ARCHIVE_SERVICE, fdb_blob_make(&blob, &service_count, sizeof(service_count))) == sizeof(service_count)) {
		MB_StorageInput.service_count = service_count;
	} else {
		MB_StorageInput.service_count = 0;
	}
	if (fdb_kv_get_blob(&kvdb, KVDB_KEY_ARCHIVE_RI, fdb_blob_make(&blob, &tool_count, sizeof(tool_count))) == sizeof(tool_count)) {
		MB_StorageInput.tool_count = tool_count;
	} else {
		MB_StorageInput.tool_count = 0;
	}

	return true;
}

void flash_storage_deinit(void)
{
	fdb_lock(&kvdb.parent);

	/* Деинициализация KVDB */
	fdb_kvdb_deinit(&kvdb);

	/* Сброс флагов статуса устройства */
	MB_StorageInput.device_status &= ~(DEVICE_MEM_MOUNTED | DEVICE_MEM_DETECTED | DEVICE_FLASH_VALID | DEVICE_READY);

	/* Сброс Holding регистров на 0xFFFF кроме исключений */
	ModBus_ResetHoldingRegisters();
	
	fdb_unlock(&kvdb.parent);

	/* Очистка мьютекса */
	if (xFlashDBMutex != NULL) {
		vSemaphoreDelete(xFlashDBMutex);
		xFlashDBMutex = NULL;
	}
}

bool flash_storage_is_initialized(void)
{
	return flashdb_initialized;
}

void flash_storage_set_initialized(bool initialized)
{
	flashdb_initialized = initialized;
}

struct fdb_kvdb* flash_storage_get_kvdb(void)
{
	return &kvdb;
}
