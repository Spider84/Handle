/*
 * modbus.c
 *
 *  Created on: 3 апр. 2026 г.
 *      Author: Spider
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include "mb.h"
#include "mbport.h"
#include "mbutils.h"
#include "modbus.h"
#include "debug.h"
#include "ws2812_led.h"
#include "buzzer.h"
#include "keys.h"
#include "flash_config.h"
#include "flash_storage.h"
#include "project_config.h"
#include <flashdb.h>
#include <fal.h>
#include <sfud.h>
#include <string.h>
#include "n32g430.h"
#include "n32g430_iwdg.h"

#ifdef DEBUG
#ifdef DEBUG_PRINTF
#undef DEBUG_PRINTF
#define DEBUG_PRINTF(...)
#endif
#endif

MB_StorageInput_t MB_StorageInput = {
	.fw_version = FW_VERSION,
	.hw_version = HW_VERSION,
};

MB_StorageHolding_t MB_StorageHolding;

/**
 * @brief Тип запроса для асинхронной обработки
 */
typedef enum {
	ASYNC_REQ_RI = 0,
	ASYNC_REQ_SERVICE = 1,
	ASYNC_REQ_BAUDRATE = 2,
	ASYNC_REQ_RELOAD_FROM_FLASH = 3,
	ASYNC_REQ_SAVE_CYCLES_COUNT = 4,
	ASYNC_REQ_SAVE_GEAR_UNIT_INFO = 5,
	ASYNC_REQ_SAVE_CUTTING_TOOL_INFO = 6,
	ASYNC_REQ_SAVE_SPINDLE_UNIT_INFO = 7,
	ASYNC_REQ_SAVE_ARCHIVE_CURRENT_TOOL = 8,
	ASYNC_REQ_SAVE_MAINTENANCE_RECORD = 9,
	ASYNC_REQ_LED_SELF_TEST = 10,
	ASYNC_REQ_MODBUS_ADDRESS = 11
} AsyncReqType_t;

/**
 * @brief Структура запроса для асинхронной обработки
 */
typedef struct {
	AsyncReqType_t type;
	uint16_t index;
} AsyncRequest_t;

/* Очередь для асинхронной обработки запросов */
static QueueHandle_t xAsyncQueue = NULL;
static StaticQueue_t xAsyncQueueBuffer;
static uint8_t ucAsyncQueueStorage[5 * sizeof(AsyncRequest_t)];

/* Отложенное значение baudrate для применения после завершения транзакции */
static uint16_t pending_baudrate = 0;

/* Отложенное значение modbus_address для применения после завершения транзакции */
static uint16_t pending_modbus_address = 0;

/* Флаг переинициализации ModBus */
static volatile bool reinit_requested = false;

/* Флаг перехода в BootLoader */
static volatile bool bootloader_requested = false;

/**
 * @brief Переход в BootLoader по адресу 0x08000000
 * @details Функция выполняет полный сброс MCU и переход на вектор таблицу BootLoader
 */
static void jump_to_bootloader(void)
{
	typedef void (*pFunction)(void);

	// 1. Отключение всех прерываний
	__disable_irq();

	// 2. Сброс настроек NVIC - отключение всех прерываний в ICER
	for (uint32_t i = 0; i < 8; i++)
	{
		NVIC->ICER[i] = 0xFFFFFFFF;
	}

	// 3. Отключение и сброс всей периферии через RCC
	// Сброс всех APB1 периферий
	RCC->APB1PRST = 0xFFFFFFFF;
	RCC->APB1PRST = 0x00000000;

	// Сброс всех APB2 периферий
	RCC->APB2PRST = 0xFFFFFFFF;
	RCC->APB2PRST = 0x00000000;

	// Сброс всех AHB периферий
	RCC->AHBPRST = 0xFFFFFFFF;
	RCC->AHBPRST = 0x00000000;

	// Сброс настроек RCC к значениям по умолчанию
	RCC->CTRL |= RCC_CTRL_HSIEN;
	RCC->CFG &= ~(RCC_CFG_SCLKSW | RCC_CFG_AHBPRES | RCC_CFG_APB1PRES | RCC_CFG_APB2PRES);
	RCC->CTRL &= ~(RCC_CTRL_HSEEN | RCC_CTRL_PLLEN);
	RCC->CFG &= ~(RCC_CFG_PLLSRC | RCC_CFG_PLLMULFCT);

	// 4. Сброс SysTick
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	// 4.1 Перенастройка IWDG на максимальное время
	// Максимальный prescaler DIV256 и reload 0xFFF дают ~26 секунд при LSI 40 кГц
	IWDG_Write_Protection_Disable();
	IWDG_Prescaler_Division_Set(IWDG_CONFIG_PRESCALER_DIV256);
	IWDG_Counter_Reload(0x0FFF);
	IWDG_Key_Reload();

	// 5. Перенос VTOR на адрес 0x08000000
	SCB->VTOR = FLASH_BASE;

	// 6. Перенастройка векторов и стека как у только что запущенного MCU
	// Чтение начального значения стека из вектора сброса
	uint32_t reset_vector = *(uint32_t*)FLASH_BASE;
	__set_MSP(reset_vector);

	// Чтение адреса Reset_Handler
	uint32_t reset_handler = *(uint32_t*)(FLASH_BASE+4);

	// 7. Включение глобальных прерываний
	__enable_irq();

	// 8. JUMP на адрес 0x08000004 (Reset_Handler)
	pFunction jump_to_app = (pFunction)reset_handler;
	jump_to_app();

	// Функция не должна возвращаться
	while(1);
}

/**
 * @brief Задача асинхронного чтения данных из KVDB
 * @param pvArg аргумент задачи (не используется)
 */
static void vTaskAsyncHandler(void *pvArg)
{
	AsyncRequest_t request;
	char key_name[16];
	struct fdb_blob blob;
#ifdef DEBUG
	const char *task_name = pcTaskGetName(NULL);
#endif

	while (1)
	{
		// Ожидание запроса из очереди
		if (xQueueReceive(xAsyncQueue, &request, portMAX_DELAY))
		{
			switch (request.type)
			{
			case ASYNC_REQ_RI:
				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					uint16_t archive_ri_count = 0;

					// Чтение текущего значения счётчика archive_ri
					if (fdb_kv_get_blob(flash_storage_get_kvdb(), KVDB_KEY_ARCHIVE_RI, fdb_blob_make(&blob, &archive_ri_count, sizeof(archive_ri_count))) == sizeof(archive_ri_count))
					{
						// Счётчик содержит количество записей (последний индекс = archive_ri_count)
						//archive_ri_count;
					}
					else
					{
						// Если счётчик не существует, записей нет
						archive_ri_count = 0;
					}

					// Вычисление реального индекса для чтения с конца (0 -> последняя запись)
					uint16_t real_index = archive_ri_count - 1 - request.index;
					if (sizeof(MB_StorageHolding.archive_ri.seq_no)>sizeof(uint16_t))
					{
						MB_StorageHolding.archive_ri.seq_no = __REV16( request.index);
					}
					else
					{
						MB_StorageHolding.archive_ri.seq_no = request.index;
					}

					// Проверка, что запрашиваемый индекс не превышает количество записей
					if (request.index >= archive_ri_count)
					{
						DEBUG_PRINTF( "[%s] RI index %u out of range (total: %u)\r\n", task_name, request.index, archive_ri_count);
						memset(&MB_StorageHolding.archive_ri.archive, 0xff, sizeof(MB_StorageHolding.archive_ri.archive));
						MB_StorageHolding.archive_ri.status = 0;
						MB_StorageInput.fault_code = FAULT_CODE_INDEX_NOT_FOUND;
						MB_StorageInput.device_status |= DEVICE_FAULT;
						break;
					}

					// Формирование ключа для KVDB
					snprintf(key_name, sizeof(key_name), KVDB_KEY_RI_TEMPLATE, real_index);

					// Чтение данных РИ из KVDB
					if (fdb_kv_get_blob(flash_storage_get_kvdb(), key_name, fdb_blob_make(&blob, &MB_StorageHolding.archive_ri.archive, sizeof(MB_StorageHolding.archive_ri.archive))) == sizeof(MB_StorageHolding.archive_ri.archive))
					{
						MB_StorageHolding.archive_ri.status = 1;
						DEBUG_PRINTF( "[%s] Loaded RI request index: %u, real index: %u\r\n", task_name, request.index, real_index);
						break;
					}
					DEBUG_PRINTF( "[%s] RI real index %u not found in KVDB\r\n", task_name, real_index);
					memset(&MB_StorageHolding.archive_ri.archive, 0xff, sizeof(MB_StorageHolding.archive_ri.archive));
					MB_StorageHolding.archive_ri.status = 0;
					MB_StorageInput.fault_code = FAULT_CODE_INDEX_NOT_FOUND;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					break;
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, filling RI with 0xFF\r\n", task_name, request.index);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}
				// Заполнение полей archive_ri значением 0xFFFF при недоступности FlashDB
				memset(&MB_StorageHolding.archive_ri, 0xFF, sizeof(MB_StorageHolding.archive_ri));
				break;

			case ASYNC_REQ_SERVICE:
				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					uint16_t archive_service_count = 0;

					// Чтение текущего значения счётчика archive_service
					if (fdb_kv_get_blob(flash_storage_get_kvdb(), KVDB_KEY_ARCHIVE_SERVICE, fdb_blob_make(&blob, &archive_service_count, sizeof(archive_service_count))) == sizeof(archive_service_count))
					{
						// Счётчик содержит количество записей (последний индекс = archive_service_count)
					}
					else
					{
						// Если счётчик не существует, записей нет
						archive_service_count = 0;
					}

					// Вычисление реального индекса для чтения с конца (0 -> последняя запись)
					uint16_t real_index = archive_service_count - 1 - request.index;
					if (sizeof(MB_StorageHolding.archive_service.number)<=sizeof(uint16_t))
						MB_StorageHolding.archive_service.number = request.index;
					else
						MB_StorageHolding.archive_service.number = __REV16(request.index);

					// Проверка, что запрашиваемый индекс не превышает количество записей
					if (request.index >= archive_service_count)
					{
						DEBUG_PRINTF( "[%s] Service index %u out of range (total: %u)\r\n", task_name, request.index, archive_service_count);
						MB_StorageHolding.archive_service.status = 0;
						memset(&MB_StorageHolding.archive_service.service_info, 0xFF, sizeof(MB_StorageHolding.archive_service.service_info));
						MB_StorageInput.fault_code = FAULT_CODE_INDEX_NOT_FOUND;
						MB_StorageInput.device_status |= DEVICE_FAULT;
						break;
					}

					// Формирование ключа для KVDB
					snprintf(key_name, sizeof(key_name), KVDB_KEY_SERVICE_TEMPLATE, real_index);

					new_service_info_t archive_service = {0xFF};

					// Чтение данных сервисного обслуживания из KVDB
					if (fdb_kv_get_blob(flash_storage_get_kvdb(), key_name, fdb_blob_make(&blob, &archive_service, sizeof(archive_service))) == sizeof(archive_service))
					{
						MB_StorageHolding.archive_service.status = 1;
						memcpy(&MB_StorageHolding.archive_service.service_info, &archive_service, sizeof(archive_service));
						DEBUG_PRINTF( "[%s] Loaded Service request index: %u, real index: %u\r\n", task_name, request.index, real_index);
						break;
					}
					DEBUG_PRINTF( "[%s] Service real index %u not found in KVDB\r\n", task_name, real_index);
					MB_StorageHolding.archive_service.status = 0;
					memset(&MB_StorageHolding.archive_service.service_info, 0xFF, sizeof(archive_service));
					MB_StorageInput.fault_code = FAULT_CODE_INDEX_NOT_FOUND;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					break;
				}
				else
				{
					// DEBUG_PRINTF( "[%s] FlashDB not available, filling Service with 0xFF\r\n", task_name, request.index);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}
				// Заполнение полей archive_service значением 0xFFFF при недоступности FlashDB
				memset(&MB_StorageHolding.archive_service, 0xFF, sizeof(MB_StorageHolding.archive_service));
				break;

			case ASYNC_REQ_BAUDRATE:
			{
				// Сохранение нового baudrate и установка флага переинициализации
				uint32_t new_baudrate = (uint32_t)request.index * 100;

				// Сохранение во внутреннюю flash
				if (flash_config_set_baudrate(new_baudrate))
				{
					DEBUG_PRINTF( "[%s] Baudrate %lu saved to flash\r\n", task_name, new_baudrate);
					reinit_requested = true;
				}
				else
				{
					DEBUG_PRINTF( "[%s] Failed to save baudrate to flash\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_CONFIG_WRITE_ERROR;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}
				break;
			}

			case ASYNC_REQ_MODBUS_ADDRESS:
			{
				// Сохранение нового адреса Modbus и установка флага переинициализации
				uint8_t new_address = (uint8_t)request.index;

				// Сохранение во внутреннюю flash
				if (flash_config_set_modbus_address(new_address))
				{
					DEBUG_PRINTF( "[%s] Modbus address %u saved to flash\r\n", task_name, new_address);
					reinit_requested = true;
				}
				else
				{
					DEBUG_PRINTF( "[%s] Failed to save modbus address to flash\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_CONFIG_WRITE_ERROR;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}
				break;
			}

			case ASYNC_REQ_RELOAD_FROM_FLASH:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping ReloadFromFlash\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] ReloadFromFlash started\r\n", task_name);

				// Деинициализация FlashDB
				flash_storage_deinit();

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] ReloadFromFlash completed\r\n", task_name);
				break;

			case ASYNC_REQ_SAVE_GEAR_UNIT_INFO:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping SaveGearUnitInfo\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveGearUnitInfo started\r\n", task_name);

				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					// Сохранение в KVDB (без поля resource)
					struct fdb_blob blob;
					if (fdb_kv_set_blob(flash_storage_get_kvdb(), KVDB_KEY_GEAR, fdb_blob_make(&blob, &MB_StorageHolding.gear, sizeof(MB_StorageHolding.gear))) == FDB_NO_ERR)
					{
						DEBUG_PRINTF( "[%s] Gear unit info saved\r\n", task_name);
					}
					else
					{
						DEBUG_PRINTF( "[%s] Failed to save gear unit info\r\n", task_name);
						MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
						MB_StorageInput.device_status |= DEVICE_FAULT;
					}
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, cannot save gear unit info\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveGearUnitInfo completed\r\n", task_name);

			case ASYNC_REQ_SAVE_CYCLES_COUNT:
save_cycles_count:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping SaveCyclesCount\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveCyclesCount started\r\n", task_name);

				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					// Формирование структуры ресурса привода
					const cycles_count_t resource = {
						.gear_total_resource = MB_StorageHolding.resource.total_resource,
						.gear_work_resource = MB_StorageHolding.resource.work_resource,
						.ri_total_resource = MB_StorageHolding.ri_info.archive.total_resource,
					};

					// Сохранение в KVDB
					struct fdb_blob blob;
					if (fdb_kv_set_blob(flash_storage_get_kvdb(), KVDB_KEY_GEAR_RESOURCE, fdb_blob_make(&blob, &resource, sizeof(resource))) == FDB_NO_ERR)
					{
						DEBUG_PRINTF( "[%s] Gear resource saved: total=%lu, work=%u\r\n", task_name, resource.gear_total_resource, resource.gear_work_resource);
					}
					else
					{
						DEBUG_PRINTF( "[%s] Failed to save gear resource\r\n", task_name);
						MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
						MB_StorageInput.device_status |= DEVICE_FAULT;
					}
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, cannot save cycles count\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveCyclesCount completed\r\n", task_name);
				break;

			case ASYNC_REQ_SAVE_CUTTING_TOOL_INFO:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping SaveCuttingToolInfo\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveCuttingToolInfo started\r\n", task_name);

				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					// Сохранение в KVDB
					struct fdb_blob blob;
					if (fdb_kv_set_blob(flash_storage_get_kvdb(), KVDB_KEY_CUT_TOOL, fdb_blob_make(&blob, &MB_StorageHolding.ri_info, sizeof(MB_StorageHolding.ri_info))) == FDB_NO_ERR)
					{
						DEBUG_PRINTF( "[%s] Cutting tool info saved\r\n", task_name);
					}
					else
					{
						DEBUG_PRINTF( "[%s] Failed to save cutting tool info\r\n", task_name);
						MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
						MB_StorageInput.device_status |= DEVICE_FAULT;
					}
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, cannot save cutting tool info\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveCuttingToolInfo completed\r\n", task_name);

				goto save_cycles_count;

			case ASYNC_REQ_SAVE_SPINDLE_UNIT_INFO:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping SaveSpindleUnitInfo\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveSpindleUnitInfo started\r\n", task_name);

				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					// Сохранение в KVDB
					struct fdb_blob blob;
					if (fdb_kv_set_blob(flash_storage_get_kvdb(), KVDB_KEY_SPINDEL, fdb_blob_make(&blob, &MB_StorageHolding.spindel, sizeof(MB_StorageHolding.spindel))) == FDB_NO_ERR)
					{
						DEBUG_PRINTF( "[%s] Spindle info saved\r\n", task_name);
					}
					else
					{
						DEBUG_PRINTF( "[%s] Failed to save spindle info\r\n", task_name);
						MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
						MB_StorageInput.device_status |= DEVICE_FAULT;
					}
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, cannot save spindle info\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveSpindleUnitInfo completed\r\n", task_name);
				break;

			case ASYNC_REQ_SAVE_ARCHIVE_CURRENT_TOOL:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping SaveArchiveCurrentTool\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveArchiveCurrentTool started\r\n", task_name);

				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					uint16_t archive_ri_count = 0;
					struct fdb_blob blob;

					// Чтение текущего значения счётчика archive_ri
					if (fdb_kv_get_blob(flash_storage_get_kvdb(), KVDB_KEY_ARCHIVE_RI, fdb_blob_make(&blob, &archive_ri_count, sizeof(archive_ri_count))) == sizeof(archive_ri_count))
					{
						// Увеличение счётчика на 1
						++archive_ri_count;
					}
					else
					{
						// Если счётчик не существует, начинаем с 1
						archive_ri_count = 1;
					}

					// Формирование ключа для нового индекса
					char key_name[sizeof(KVDB_KEY_RI_TEMPLATE)+5];
					snprintf(key_name, sizeof(key_name), KVDB_KEY_RI_TEMPLATE, archive_ri_count-1);

					// Сохранение данных РИ в KVDB
					if (fdb_kv_set_blob(flash_storage_get_kvdb(), key_name, fdb_blob_make(&blob, &MB_StorageHolding.ri_info.archive, sizeof(MB_StorageHolding.ri_info.archive))) == FDB_NO_ERR)
					{
						DEBUG_PRINTF( "[%s] RI archived with index: %u\r\n", task_name, archive_ri_count-1);

						// Сохранение обновлённого счётчика
						if (fdb_kv_set_blob(flash_storage_get_kvdb(), KVDB_KEY_ARCHIVE_RI, fdb_blob_make(&blob, &archive_ri_count, sizeof(archive_ri_count))) == FDB_NO_ERR)
						{
							MB_StorageInput.tool_count = archive_ri_count;
							DEBUG_PRINTF( "[%s] Archive RI count updated: %u\r\n", task_name, archive_ri_count);
						}
						else
						{
							DEBUG_PRINTF( "[%s] Failed to update archive RI count\r\n", task_name);
							MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
							MB_StorageInput.device_status |= DEVICE_FAULT;
						}
					}
					else
					{
						DEBUG_PRINTF( "[%s] Failed to archive RI data\r\n", task_name);
						MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
						MB_StorageInput.device_status |= DEVICE_FAULT;
					}
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, cannot archive current tool\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveArchiveCurrentTool completed\r\n", task_name);
				break;

			case ASYNC_REQ_SAVE_MAINTENANCE_RECORD:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping SaveMaintenanceRecord\r\n", task_name);
					break;
				}
				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveMaintenanceRecord started\r\n", task_name);

				// Проверка доступности FlashDB
				if (IS_FLASHDB_AVAILABLE())
				{
					uint16_t archive_service_count = 0;
					struct fdb_blob blob;

					// Чтение текущего значения счётчика archive_service
					if (fdb_kv_get_blob(flash_storage_get_kvdb(), KVDB_KEY_ARCHIVE_SERVICE, fdb_blob_make(&blob, &archive_service_count, sizeof(archive_service_count))) == sizeof(archive_service_count))
					{
						// Увеличение счётчика на 1
						++archive_service_count;
					}
					else
					{
						// Если счётчик не существует, начинаем с 0
						archive_service_count = 1;
					}

					// Формирование ключа для нового индекса
					char key_name[sizeof(KVDB_KEY_SERVICE_TEMPLATE)+5];
					snprintf(key_name, sizeof(key_name), KVDB_KEY_SERVICE_TEMPLATE, archive_service_count-1);

					// Сохранение данных сервисного обслуживания в KVDB
					if (fdb_kv_set_blob(flash_storage_get_kvdb(), key_name, fdb_blob_make(&blob, &MB_StorageHolding.new_service, sizeof(MB_StorageHolding.new_service))) == FDB_NO_ERR)
					{
						DEBUG_PRINTF( "[%s] Service record archived with index: %u\r\n", task_name, archive_service_count-1);

						// Сохранение обновлённого счётчика
						if (fdb_kv_set_blob(flash_storage_get_kvdb(), KVDB_KEY_ARCHIVE_SERVICE, fdb_blob_make(&blob, &archive_service_count, sizeof(archive_service_count))) == FDB_NO_ERR)
						{
							MB_StorageInput.service_count = archive_service_count;
							DEBUG_PRINTF( "[%s] Archive service count updated: %u\r\n", task_name, archive_service_count);
						}
						else
						{
							DEBUG_PRINTF( "[%s] Failed to update archive service count\r\n", task_name);
							MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
							MB_StorageInput.device_status |= DEVICE_FAULT;
						}
					}
					else
					{
						DEBUG_PRINTF( "[%s] Failed to archive service data\r\n", task_name);
						MB_StorageInput.fault_code = FAULT_CODE_DATA_WRITE_ERROR;
						MB_StorageInput.device_status |= DEVICE_FAULT;
					}
				}
				else
				{
					DEBUG_PRINTF( "[%s] FlashDB not available, cannot save maintenance record\r\n", task_name);
					MB_StorageInput.fault_code = FAULT_CODE_FLASHDB_UNAVAILABLE;
					MB_StorageInput.device_status |= DEVICE_FAULT;
				}

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] SaveMaintenanceRecord completed\r\n", task_name);
				break;

			case ASYNC_REQ_LED_SELF_TEST:
				// Проверка флага занятости flash
				if (MB_StorageInput.device_status & DEVICE_WRITE_BUSY)
				{
					MB_StorageInput.fault_code = FAULT_CODE_DEVICE_BUSY;
					MB_StorageInput.device_status |= DEVICE_FAULT;
					DEBUG_PRINTF( "[%s] Device busy, skipping LedSelfTest\r\n", task_name);
					break;
				}
				// Самотестирование светодиода
				DEBUG_PRINTF( "[%s] LedSelfTest started\r\n", task_name);

				// Установка флага занятости flash
				MB_StorageInput.device_status |= DEVICE_WRITE_BUSY;

				// Белый цвет на 3 секунды
				WS2812_SetColor(COLOR_WHITE);
				vTaskDelay(pdMS_TO_TICKS(3000));

				// Перебор базовых цветов с интервалом 1 секунда
				ws2812e_dma_color_t base_colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN};
				for (int i = 0; i < 6; i++)
				{
					WS2812_SetColor(base_colors[i]);
					vTaskDelay(pdMS_TO_TICKS(1000));
				}

				// Перелив цветового спектра (5 секунд)
				// Используем HSV-like перебор через RGB
				const int spectrum_steps = 50;
				const int spectrum_delay = 5000 / spectrum_steps;
				for (int i = 0; i < spectrum_steps; i++)
				{
					float hue = (float)i / spectrum_steps;
					uint8_t r, g, b;

					// Конвертация hue в RGB (простой алгоритм)
					int sector = hue * 6;
					float fraction = hue * 6 - sector;
					uint8_t p = 0;
					uint8_t q = (uint8_t)(255 * (1 - fraction));
					uint8_t t = (uint8_t)(255 * fraction);

					switch (sector % 6)
					{
					case 0: r = 255; g = t; b = p; break;
					case 1: r = q; g = 255; b = p; break;
					case 2: r = p; g = 255; b = t; break;
					case 3: r = p; g = q; b = 255; break;
					case 4: r = t; g = p; b = 255; break;
					case 5: r = 255; g = p; b = q; break;
					default: r = 0; g = 0; b = 0; break;
					}

					WS2812_SetColor(RGB_COLOR(r, g, b));
					vTaskDelay(pdMS_TO_TICKS(spectrum_delay));
				}

				// Быстрое мигание 3 секунды (интервал 100мс, длительность 50мс)
				WS2812_Blink(COLOR_WHITE, 100, 50, 0);
				vTaskDelay(pdMS_TO_TICKS(3000));
				WS2812_Off();

				// Выключение
				WS2812_Off();

				// Сброс флага занятости flash
				MB_StorageInput.device_status &= ~DEVICE_WRITE_BUSY;

				DEBUG_PRINTF( "[%s] LedSelfTest completed\r\n", task_name);
				break;
			}
		}
	}
}

/**
 * @brief Обработка команды управления светодиодом
 * @param control_code код режима работы светодиода
 */
static void process_led_control(uint16_t control_code)
{
	switch (control_code)
	{
		case 0:
			WS2812_Off();
			break;
		case 1:
			WS2812_SetColor(COLOR_GREEN);
			break;
		case 2:
			WS2812_SetColor(COLOR_BLUE);
			break;
		case 3:
			WS2812_SetColor(COLOR_RED);
			break;
		case 4:
			WS2812_SetColor(COLOR_YELLOW);
			break;
		case 5:
			WS2812_SetColor(COLOR_WHITE);
			break;
		case 11:
			WS2812_Blink(COLOR_GREEN, 400, 200, 0);
			break;
		case 12:
			WS2812_Blink(COLOR_BLUE, 400, 200, 0);
			break;
		case 13:
			WS2812_Blink(COLOR_RED, 1000, 500, 0);
			break;
		case 14:
			WS2812_Blink(COLOR_YELLOW, 1000, 500, 0);
			break;
		case 15:
			WS2812_Blink(COLOR_WHITE, 1000, 500, 0);
			break;
		default:
			// Неизвестный код - ничего не делаем
			break;
	}
}

/**
 * @brief Callback функция для сброса buzzer_control после завершения конечного действия
 */
static void buzzer_completion_callback(void)
{
	MB_StorageHolding.buzzer_control = 0;
}

/**
 * @brief Обработка команды управления buzzer
 * @param control_code код режима работы buzzer
 */
static void process_buzzer_control(uint16_t control_code)
{
	switch (control_code)
	{
		case 0:
			Buzzer_SetMode(BUZZER_MODE_OFF);
			Buzzer_SetCompletionCallback(NULL);
			break;
		case 1:
			Buzzer_SetMode(BUZZER_MODE_SINGLE);
			Buzzer_SetCompletionCallback(buzzer_completion_callback);
			break;
		case 2:
			Buzzer_SetMode(BUZZER_MODE_TRIPLE);
			Buzzer_SetCompletionCallback(buzzer_completion_callback);
			break;
		case 3:
			Buzzer_SetMode(BUZZER_MODE_CRITICAL);
			Buzzer_SetCompletionCallback(NULL);
			break;
		default:
			// Неизвестный код - выключаем buzzer
			Buzzer_SetMode(BUZZER_MODE_OFF);
			Buzzer_SetCompletionCallback(NULL);
			break;
	}
}

static void
vTaskMODBUS( void *pvArg )
{
    const UCHAR     ucSlaveID[] = { 0xAA, 0xBB, 0xCC };
    eMBErrorCode    eStatus;
    uint32_t current_baudrate;
    uint8_t current_address;

	ModBus_Async_Init();

    while (1)
    {
		// Чтение baudrate и адреса из внутренней flash при каждой инициализации
		current_baudrate = flash_config_get_baudrate();
		current_address = flash_config_get_modbus_address();

		MB_StorageHolding.baudrate = current_baudrate/100;
		MB_StorageHolding.modbus_address = current_address;
		DEBUG_PRINTF( "[ModBus] Init with baudrate: %lu, address: %u\r\n", current_baudrate, current_address);

		if( MB_ENOERR != ( eStatus = eMBInit( MB_RTU, current_address, 2, current_baudrate, MB_PAR_NONE, 1 ) ) )
		{
			/* Can not initialize. Add error handling code here. */
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		if( MB_ENOERR != ( eStatus = eMBSetSlaveID( 0x34, TRUE, ucSlaveID, 3 ) ) )
		{
			/* Can not set slave id. Check arguments */
			eMBClose();
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		if( MB_ENOERR != ( eStatus = eMBEnable(  ) ) )
		{
			/* Enable failed. */
			eMBDisable();
			eMBClose();
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		while(1)
		{
			IWDG_Key_Reload();
			eMBErrorCode poll_status = eMBPoll();

			// Проверка критических ошибок, требующих переинициализации
			if (poll_status == MB_EILLSTATE || poll_status == MB_EPORTERR ||
			    /*poll_status == MB_EIO || */poll_status == MB_ETIMEDOUT)
			{
				DEBUG_PRINTF( "[ModBus] Critical error: %d, triggering reinit\r\n", poll_status);
				break; // Выход из внутреннего цикла для переинициализации
			}

			// Проверка флага переинициализации
			if (reinit_requested)
			{
				DEBUG_PRINTF( "[ModBus] Reinit triggered by flag\r\n");
				reinit_requested = false;
				break; // Выход из внутреннего цикла для переинициализации
			}

			// Проверка флага перехода в BootLoader
			if (bootloader_requested)
			{
				DEBUG_PRINTF( "[ModBus] BootLoader jump triggered\r\n");
				bootloader_requested = false;
				eMBDisable();
				eMBClose();
				jump_to_bootloader();
				// Функция не должна возвращаться
				while(1);
			}
		}
		eMBDisable();
		eMBClose();
    }
}

eMBErrorCode
eMBRegInputCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;

    //DEBUG_PRINTF( RTT_CTRL_TEXT_BRIGHT_BLACK"[MB Input CP] Addr: %u Regs: %u\r\n"RTT_CTRL_RESET, usAddress, usNRegs);

    if( ( usAddress >= 1 ) && ( usAddress + usNRegs <= 1+(sizeof(MB_StorageInput)/2) ) )
    {
        unsigned int iRegIndex = ( int )( usAddress - 1 );
        while( usNRegs > 0 )
        {
        	uint16_t value;
        	// Если читается поле uptime, берём значение из tick count
        	if (iRegIndex == REG_INDEX(MB_StorageInput_t, uptime)) {
        		uint32_t tick_count = xTaskGetTickCount();
        		value = (uint16_t)(tick_count >> 16);
        	} else if (iRegIndex == REG_INDEX(MB_StorageInput_t, uptime) + 1) {
        		uint32_t tick_count = xTaskGetTickCount();
        		value = (uint16_t)(tick_count & 0xFFFF);
        	} else {
        		value = MB_StorageInput.array[iRegIndex];
        	}
            *pucRegBuffer++ = ( unsigned char )( value >> 8 );
            *pucRegBuffer++ = ( unsigned char )( value & 0xFF );
            iRegIndex++;
            usNRegs--;
        }
    }
    else
    {
        eStatus = MB_ENOREG;
		DEBUG_PRINTF("Invalid Input address: %d\r\n", usAddress);
    }

    return eStatus;
}

eMBErrorCode
eMBRegHoldingCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs, eMBRegisterMode eMode )
{
    eMBErrorCode    eStatus = MB_ENOERR;

    // DEBUG_PRINTF( RTT_CTRL_TEXT_BRIGHT_BLACK"[MB Holding CP] Addr: %u Regs: %u\r\n"RTT_CTRL_RESET, usAddress, usNRegs);

    if( ( usAddress >= 1 ) && ( usAddress + usNRegs <= 1+(sizeof(MB_StorageHolding)/2) ) )
    {
        int iRegIndex = ( int )( usAddress - 1 );
        switch ( eMode )
        {
        case MB_REG_READ:
            while( usNRegs > 0 )
            {
				uint16_t val = MB_StorageHolding.array[iRegIndex];
				*pucRegBuffer++ = ( unsigned char )( val >> 8 );
				*pucRegBuffer++ = ( unsigned char )( val & 0xFF );
				iRegIndex++;
				usNRegs--;
            }
            break;

        case MB_REG_WRITE:
            while( usNRegs > 0 )
            {
            	uint16_t value;
            	value = *pucRegBuffer++ << 8;
            	value |= *pucRegBuffer++;
            	// Обработка команд управления при записи в регистры
            	switch (iRegIndex)
            	{
            	case REG_INDEX(MB_StorageHolding_t, led_control):
            		MB_StorageHolding.array[iRegIndex] = value;
            		process_led_control(value);
            		break;
            	case REG_INDEX(MB_StorageHolding_t, buzzer_control):
            		MB_StorageHolding.array[iRegIndex] = value;
            		process_buzzer_control(value);
            		break;
            	case REG_INDEX(MB_StorageHolding_t, ri_index):
            		// Валидация индекса РИ
            		if (value > MAX_RI_INDEX) {
            			DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[MB] Invalid RI index: %u (max: %d)\r\n"RTT_CTRL_RESET, value, MAX_RI_INDEX);
            			MB_StorageInput.fault_code = FAULT_CODE_INVALID_INDEX;
            			break; // Не сохраняем значение и не отправляем в очередь
            		}
            		MB_StorageHolding.array[iRegIndex] = value;
            		// Отправка индекса РИ в очередь для асинхронной обработки
            		if (xAsyncQueue != NULL)
            		{
            			AsyncRequest_t req = { .type = ASYNC_REQ_RI, .index = value };
            			if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send RI index to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
            		}
            		break;
            	case REG_INDEX(MB_StorageHolding_t, service_index):
            		// Валидация индекса сервисного обслуживания
            		if (value > MAX_SERVICE_INDEX) {
            			DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[MB] Invalid service index: %u (max: %d)\r\n"RTT_CTRL_RESET, value, MAX_SERVICE_INDEX);
            			MB_StorageInput.fault_code = FAULT_CODE_INVALID_INDEX;
            			break; // Не сохраняем значение и не отправляем в очередь
            		}
            		MB_StorageHolding.array[iRegIndex] = value;
            		// Отправка индекса сервисного обслуживания в очередь для асинхронной обработки
            		if (xAsyncQueue != NULL)
            		{
            			AsyncRequest_t req = { .type = ASYNC_REQ_SERVICE, .index = value };
            			if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send service index to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
            		}
            		break;
            	case REG_INDEX(MB_StorageHolding_t, baudrate):
            		// Валидация baudrate (допустимые значения: 96-1152 для 9600-115200)
            		if (value < MIN_BAUDRATE || value > MAX_BAUDRATE) {
            			DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[MB] Invalid baudrate: %u (valid range: %d-%d)\r\n"RTT_CTRL_RESET, value, MIN_BAUDRATE, MAX_BAUDRATE);
            			MB_StorageInput.fault_code = FAULT_CODE_INVALID_BAUDRATE;
            			break; // Не сохраняем значение
            		}
            		MB_StorageHolding.array[iRegIndex] = value;
            		// Сохранение значения baudrate для применения после завершения транзакции
            		pending_baudrate = value;
            		break;
            	case REG_INDEX(MB_StorageHolding_t, modbus_address):
            		// Валидация modbus_address (допустимый диапазон: 1-247)
            		if (value < MIN_MODBUS_ADDRESS || value > MAX_MODBUS_ADDRESS) {
            			DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[MB] Invalid modbus address: %u (valid range: %d-%d)\r\n"RTT_CTRL_RESET, value, MIN_MODBUS_ADDRESS, MAX_MODBUS_ADDRESS);
            			MB_StorageInput.fault_code = FAULT_CODE_INVALID_ADDRESS;
            			break; // Не сохраняем значение
            		}
            		MB_StorageHolding.array[iRegIndex] = value;
            		// Сохранение значения modbus_address для применения после завершения транзакции
            		pending_modbus_address = value;
            		break;
            	case REG_INDEX(MB_StorageHolding_t, led_color):
            	case REG_INDEX(MB_StorageHolding_t, led_color) + 1:
            		// Сохраняем значение и устанавливаем цвет LED
            		MB_StorageHolding.array[iRegIndex] = value;
            		// Вызываем WS2812_SetColor при записи в любой из двух регистров led_color
            		WS2812_SetColor(MB_StorageHolding.led_color);
            		break;
            	default:
            		// Для остальных регистров просто сохраняем значение
            		MB_StorageHolding.array[iRegIndex] = value;
            		break;
            	}
                iRegIndex++;
                usNRegs--;
            }

            // Применение отложенного изменения baudrate после завершения обработки всех регистров
            if (pending_baudrate != 0 && xAsyncQueue != NULL)
            {
            	AsyncRequest_t req = { .type = ASYNC_REQ_BAUDRATE, .index = pending_baudrate };
            	if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
				{
					DEBUG_PRINTF("[MB] Failed to send baudrate to queue\r\n");
					MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
				}
            	pending_baudrate = 0;
            }

            // Применение отложенного изменения modbus_address после завершения обработки всех регистров
            if (pending_modbus_address != 0 && xAsyncQueue != NULL)
            {
            	AsyncRequest_t req = { .type = ASYNC_REQ_MODBUS_ADDRESS, .index = pending_modbus_address };
            	if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
				{
					DEBUG_PRINTF("[MB] Failed to send modbus address to queue\r\n");
					MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
				}
            	pending_modbus_address = 0;
            }
        }
    }
    else
    {
        eStatus = MB_ENOREG;
		DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"Invalid Holding address: %d\r\n"RTT_CTRL_RESET, usAddress);
    }
    return eStatus;
}


/**
 * @brief Обработка чтения/записи Coils
 * @param pucRegBuffer буфер данных Modbus
 * @param usAddress адрес первого coil (начинается с 1)
 * @param usNCoils количество coils
 * @param eMode режим операции (чтение/запись)
 * @return код ошибки Modbus
 * @note Coils 00002-00010 используются для команд управления
 */
eMBErrorCode
eMBRegCoilsCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNCoils, eMBRegisterMode eMode )
{
	eMBErrorCode    eStatus = MB_ENOERR;
	int             iIntAddress = ( int )usAddress - 1;
	USHORT          usBitOffset = 0;

	// DEBUG_PRINTF( RTT_CTRL_TEXT_BRIGHT_BLACK"[MB Coils CP] Addr: %u Regs: %u\r\n"RTT_CTRL_RESET, usAddress, usNCoils);

	// Отдельная проверка на Coil 0xBEAF для перехода в BootLoader
	if (usAddress == 0xBEAF && usNCoils == 1 && eMode == MB_REG_WRITE)
	{
		UCHAR ucValue = xMBUtilGetBits(pucRegBuffer, 0, 1);
		if (ucValue == 1)
		{
			// Установка флага для перехода в BootLoader после завершения транзакции
			bootloader_requested = true;
			DEBUG_PRINTF("[MB] BootLoader requested via Coil 0xBEAF\r\n");
		}
		return MB_ENOERR;
	}

	// Проверка диапазона адресов coils (00001-00010)
	if( ( iIntAddress >= 0 ) && ( iIntAddress + usNCoils <= 10 ) )
	{
		switch ( eMode )
		{
		case MB_REG_READ:
			// Чтение coils (заглушка - всегда возвращаем 0)
			while( usNCoils > 0 )
			{
				xMBUtilSetBits( pucRegBuffer, usBitOffset, 1, 0 );
				iIntAddress++;
				usBitOffset++;
				usNCoils--;
			}
			break;

		case MB_REG_WRITE:
			// Запись coils - обработка команд
			while( usNCoils > 0 )
			{
				UCHAR ucValue = xMBUtilGetBits( pucRegBuffer, usBitOffset, 1 );

				// Обработка команд при установке coil в 1
				if (ucValue && xAsyncQueue != NULL)
				{
					AsyncRequest_t req = { .type = 0, .index = 0 };

					switch (iIntAddress)
					{
					case 1: // Coil 00002 - ReloadFromFlash
						req.type = ASYNC_REQ_RELOAD_FROM_FLASH;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send reload from flash to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					case 2: // Coil 00003 - SaveCyclesCount
						req.type = ASYNC_REQ_SAVE_CYCLES_COUNT;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send save cycles count to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					case 3: // Coil 00004 - SaveGearUnitInfo
						req.type = ASYNC_REQ_SAVE_GEAR_UNIT_INFO;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send save gear unit info to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}

						break;
					case 4: // Coil 00005 - SaveCuttingToolInfo
						req.type = ASYNC_REQ_SAVE_CUTTING_TOOL_INFO;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send save cutting tool info to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					case 5: // Coil 00006 - SaveSpindleUnitInfo
						req.type = ASYNC_REQ_SAVE_SPINDLE_UNIT_INFO;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send save spindle unit info to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					case 6: // Coil 00007 - SaveArchiveCurrentTool
						req.type = ASYNC_REQ_SAVE_ARCHIVE_CURRENT_TOOL;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send save archive current tool to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					case 7: // Coil 00008 - SaveMaintenanceRecord
						req.type = ASYNC_REQ_SAVE_MAINTENANCE_RECORD;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send save maintenance record to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					case 8: // Coil 00009 - ClearFault (синхронная обработка)
						if (ucValue)
						{
							MB_StorageInput.fault_code = 0;
							MB_StorageInput.device_status &= ~DEVICE_FAULT;
							// DEBUG_PRINTF( "[Coil Handler] ClearFault command\r\n");
						}
						break;
					case 9: // Coil 00010 - LedSelfTest
						req.type = ASYNC_REQ_LED_SELF_TEST;
						if (xQueueSend(xAsyncQueue, &req, 0) != pdPASS)
						{
							DEBUG_PRINTF("[MB] Failed to send led self test to queue\r\n");
							MB_StorageInput.fault_code = FAULT_CODE_QUEUE_FULL;
						}
						break;
					default:
						// Coil 00001 - не используется
						break;
					}
				}

				iIntAddress++;
				usBitOffset++;
				usNCoils--;
			}
			break;
		}
	}
	else
	{
		eStatus = MB_ENOREG;
		DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"Invalid Coils address: %d\r\n"RTT_CTRL_RESET, usAddress);
	}

	return eStatus;
}

eMBErrorCode
eMBRegDiscreteCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNDiscrete )
{
	eMBErrorCode    eStatus = MB_ENOERR;
	int             iIntAddress = ( int )usAddress - 1;
	USHORT          usBitOffset = 0;

	// DEBUG_PRINTF( RTT_CTRL_TEXT_BRIGHT_BLACK"[MB Discrete CP] Addr: %u Regs: %u\r\n"RTT_CTRL_RESET, usAddress, usNDiscrete);

	// 1. Проверка диапазона запроса
	if( ( iIntAddress >= 0 ) && ( iIntAddress + usNDiscrete <= 3 ) )
	{
		while( usNDiscrete > 0 )
		{
			UCHAR ucByteValue = 0;

			// 2. Опрос физических пинов в зависимости от текущего iIntAddress
			ucByteValue = ( iIntAddress < 3)?((MB_StorageInput.buttons_mask & (iIntAddress+1)) >> iIntAddress):0;

			// 3. Упаковка считанного бита в буфер ответа Modbus
			// xMBUtilSetBits(буфер, смещение_в_битах, кол-во_бит, значение)
			xMBUtilSetBits( pucRegBuffer, usBitOffset, 1, ucByteValue );

			iIntAddress++;
			usBitOffset++;
			usNDiscrete--;
		}
	}
	else
	{
		// Если мастер запросил адрес, которого нет (например, 10-ю кнопку)
		eStatus = MB_ENOREG;
		DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"Invalid discrete address: %d\r\n"RTT_CTRL_RESET, usAddress);
	}

	return eStatus;
}

eMBErrorCode    eMBRegFileCB( UCHAR * pucFileBuffer, USHORT usFileNumber,
                              USHORT usRecordNumber, USHORT usRecordLength,
							  eMBRegisterMode eMode )
{
	eMBErrorCode eStatus = MB_ENOERR;

	// Обработка только режима чтения
	if (eMode == MB_REG_READ)
	{
		// FileNumber 1 - чтение SFDP данных
		if (usFileNumber == 1)
		{
			// Проверка доступности NOR flash
			if (!IS_FLASHDB_AVAILABLE())
			{
				return MB_ENOREG;
			}

			// Получение устройства SFUD
			extern sfud_flash sfud_norflash0;
			sfud_flash *flash = &sfud_norflash0;

			if (!flash->init_ok || !flash->sfdp.available)
			{
				return MB_ENOREG;
			}

			// Вычисление смещения в структуре SFDP (record number * 2)
			uint32_t sfdp_offset = (uint32_t)usRecordNumber * 2;

			// Вычисление количества байт для чтения (record length * 2)
			size_t read_len = (size_t)usRecordLength * 2;

			// Проверка границ структуры SFDP
			if (sfdp_offset + read_len > sizeof(flash->sfdp))
			{
				return MB_ENOREG;
			}

			// Чтение данных из структуры SFDP
			uint8_t *sfdp_data = (uint8_t *)&flash->sfdp;

			// Преобразование байтов в регистры Modbus (big-endian)
			for (size_t i = 0; i < read_len; i++)
			{
				*pucFileBuffer++ = sfdp_data[sfdp_offset + i];
			}
		}
		// FileNumber 0 (или любой другой) - чтение RAW данных из flash
		else
		{
			// Проверка доступности NOR flash
			if (!IS_FLASHDB_AVAILABLE())
			{
				return MB_ENOREG;
			}

			// Получение устройства flash через FAL
			const struct fal_flash_dev *flash_dev = fal_flash_device_find(NOR_FLASH_DEV_NAME);
			if (flash_dev == NULL)
			{
				return MB_ENOREG;
			}

			// Вычисление адреса в байтах (record number * 2, так как record в регистрах)
			uint32_t flash_addr = (uint32_t)usRecordNumber * 2;

			// Вычисление количества байт для чтения (record length * 2)
			size_t read_len = (size_t)usRecordLength * 2;

			// Проверка границ (адрес + длина не должны превышать размер flash)
			if (flash_addr + read_len > flash_dev->len)
			{
				return MB_ENOREG;
			}

			// Временный буфер для чтения RAW данных
			static uint8_t temp_buffer[256];

			// Ограничение размера чтения размером временного буфера
			if (read_len > sizeof(temp_buffer))
			{
				read_len = sizeof(temp_buffer);
			}

			// Чтение данных из NOR flash через FAL
			int read_result = flash_dev->ops.read(flash_addr, temp_buffer, read_len);
			if (read_result < 0)
			{
				return MB_ENOREG;
			}

			// Преобразование байтов в регистры Modbus (big-endian)
			for (size_t i = 0; i < read_len; i += 2)
			{
				*pucFileBuffer++ = temp_buffer[i];     // Старший байт
				*pucFileBuffer++ = temp_buffer[i + 1]; // Младший байт
			}
		}
	}
	else
	{
		// Запись не поддерживается
		eStatus = MB_ENOREG;
		DEBUG_PRINTF(RTT_CTRL_TEXT_YELLOW"Write is not supported\r\n"RTT_CTRL_RESET);
	}

	return eStatus;
}

void ModBus_Init(void)
{
	static StaticTask_t xMBTaskBuffer;
	static StackType_t xMBStack[ 128 ];
	xTaskCreateStatic(vTaskMODBUS, "ModBus", sizeof(xMBStack)/sizeof(xMBStack[0]), NULL, (configMAX_PRIORITIES-3) | portPRIVILEGE_BIT, xMBStack, &xMBTaskBuffer);
}

/**
 * @brief Инициализация задачи асинхронной обработки Modbus
 */
void ModBus_Async_Init(void)
{
	static StaticTask_t xAsyncTaskBuffer;
	static StackType_t xAsyncStack[ 384 ];

	// Создание очереди для асинхронных запросов (ёмкость 5 элементов)
	xAsyncQueue = xQueueCreateStatic(5, sizeof(AsyncRequest_t), ucAsyncQueueStorage, &xAsyncQueueBuffer);
#ifdef DEBUG
	vQueueAddToRegistry(xAsyncQueue, "MBAsync");
#endif

	// Создание задачи асинхронной обработки
	xTaskCreateStatic(vTaskAsyncHandler, "AsyncHandler", sizeof(xAsyncStack)/sizeof(xAsyncStack[0]), NULL, tskIDLE_PRIORITY+1, xAsyncStack, &xAsyncTaskBuffer);
}

/**
 * @brief Сброс Holding регистров на 0xFFFF кроме указанных исключений
 * @details Исключения: led_control (индекс 0), buzzer_control (индекс 1),
 *          baudrate (индекс 4), modbus_address (индекс 5)
 */
void ModBus_ResetHoldingRegisters(void)
{
	// Сохранение значений исключений
	uint16_t saved_led_control = MB_StorageHolding.led_control;
	uint16_t saved_buzzer_control = MB_StorageHolding.buzzer_control;
	uint16_t saved_baudrate = MB_StorageHolding.baudrate;
	uint16_t saved_modbus_address = MB_StorageHolding.modbus_address;

	// Сброс всех регистров на 0xFFFF
	memset(&MB_StorageHolding, 0xFF, sizeof(MB_StorageHolding));

	// Восстановление значений исключений
	MB_StorageHolding.led_control = saved_led_control;
	MB_StorageHolding.buzzer_control = saved_buzzer_control;
	MB_StorageHolding.baudrate = saved_baudrate;
	MB_StorageHolding.modbus_address = saved_modbus_address;

	DEBUG_PRINTF( RTT_CTRL_TEXT_YELLOW"[ModBus] Holding registers reset to 0xFFFF (except led_control, buzzer_control, baudrate, modbus_address)\r\n"RTT_CTRL_RESET);
}
