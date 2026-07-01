/*
 * modbus.h
 *
 *  Created on: 6 апр. 2026 г.
 *      Author: Spider
 */

#ifndef INC_MODBUS_H_
#define INC_MODBUS_H_

#include "ws2812_led.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Определение макросов для packed структур */
#ifndef __PACKED_STRUCT
#define __PACKED_STRUCT struct __attribute__((packed))
#endif

#ifndef __PACKED_UNION
#define __PACKED_UNION union __attribute__((packed))
#endif

#define FW_VERSION 0x0108
#define HW_VERSION 0x0001

/**
 * @brief Макрос для расчёта индекса поля в массиве uint16_t
 * @param type тип структуры
 * @param field имя поля в структуре
 */
#define REG_INDEX(type, field) (offsetof(type, field) / sizeof(uint16_t))

#define DEVICE_READY         (1<<0)
#define DEVICE_MEM_DETECTED  (1<<1)
#define DEVICE_MEM_MOUNTED   (1<<2)
#define DEVICE_FLASH_VALID   (1<<3)
#define DEVICE_ARCHIVE_VALID (1<<4)
#define DEVICE_WRITE_BUSY    (1<<5)
#define DEVICE_FAULT         (1<<6)

/**
 * @brief Коды ошибок устройства
 */
typedef enum {
	FAULT_CODE_NONE = 0,              // Нет ошибки
	FAULT_CODE_DEVICE_BUSY = 1,       // Устройство занято
	FAULT_CODE_INVALID_INDEX = 2,     // Недопустимый индекс
	FAULT_CODE_INVALID_BAUDRATE = 3,  // Недопустимый baudrate
	FAULT_CODE_INVALID_ADDRESS = 4,   // Недопустимый адрес
	FAULT_CODE_FLASHDB_UNAVAILABLE = 5, // FlashDB недоступен
	FAULT_CODE_INDEX_NOT_FOUND = 6,   // Индекс не найден в KVDB
	FAULT_CODE_CONFIG_WRITE_ERROR = 7, // Ошибка записи конфигурации
	FAULT_CODE_DATA_WRITE_ERROR = 8,   // Ошибка записи данных
	FAULT_CODE_QUEUE_FULL = 9         // Очередь переполнена
} fault_code_t;

/**
 * @brief Макрос для проверки доступности FlashDB
 * @details Возвращает true если SPI flash обнаружен и FlashDB примонтирован
 */
#define IS_FLASHDB_AVAILABLE() ((MB_StorageInput.device_status & (DEVICE_MEM_DETECTED | DEVICE_MEM_MOUNTED)) == (DEVICE_MEM_DETECTED | DEVICE_MEM_MOUNTED))

typedef union __attribute__((packed)) {
	struct __attribute__((packed)) {
		union {
			uint16_t buttons_mask;  //30001
			struct {
				bool button_0:1;
				bool button_1:1;
				bool button_2:1;
			};
		};
		union {
			uint16_t device_status; //30002
			struct {
				bool ready:1;
				bool mem_detected:1;
				bool mem_mounted:1;
				bool flash_valid:1;
				bool archive_valid:1;
				bool write_busy:1;
				bool archive_fault:1;
			};
		};
		int16_t temper;        //30003
		uint16_t fault_code;    //30004
		uint16_t fw_version;	//30005
		uint16_t hw_version;	//30006
		uint16_t tool_count;	//30007
		uint16_t service_count; //30008
		int16_t cpu_temp;       //30009
		uint32_t uptime;        //30010-30011
		uint16_t pb0_voltage;   //30012
		uint16_t pb0_falgs;     //30013
	};
	uint16_t array[13];
} MB_StorageInput_t;

/* Typedef для дочерних структур Holding регистров */
typedef __PACKED_STRUCT {
	uint16_t gear_work_resource;
	uint32_t gear_total_resource;
	uint16_t ri_total_resource;
} cycles_count_t;

typedef __PACKED_STRUCT {
	uint16_t work_resource; 				//40043
	uint32_t total_resource; 				//40044 - 40045
} gear_resource_t;

typedef __PACKED_STRUCT {
	char ERPID[30];     					//40010 - 40024
	char serial[30];    					//40025 - 40039
	uint16_t version;						//40040
	uint16_t date;      					//40041
	uint16_t service_resource; 				//40042
} gear_info_t;

typedef __PACKED_STRUCT {
	char ERPID[30];    						//40050 - 40064
	char serial[30];   						//40065 - 40079
	uint16_t sharp;    						//40080
	uint16_t service_resource;    			//40081
	uint16_t total_resource;    			//40082
} archive_ri_t;

typedef __PACKED_STRUCT {
	archive_ri_t archive;					//40049 - 40082
	int16_t dril_distance;    				//40083
	int16_t countersink_distance;    		//40084
} ri_info_t;

typedef __PACKED_STRUCT {
	char ERPID[30];    						//40090 - 40104
	char serial[30];   						//40105 - 40119
	char installer[30];						//40120 - 40134
} spindle_info_t;

typedef __PACKED_STRUCT {
	archive_ri_t archive;                   //40150 - 40182
	uint32_t seq_no;		    			//40183 - 40184
	uint16_t status;		    			//40185
} archive_ri_info_t;

typedef __PACKED_STRUCT {
	uint16_t date;    							//40200
	uint16_t cycle_cnt;    						//40201
} new_service_info_t;

typedef __PACKED_STRUCT {
	uint16_t number;    					//40190
	new_service_info_t service_info;        //40191 - 40192
	uint16_t status;		    			//40193
} archive_service_info_t;

typedef __PACKED_UNION {
	__PACKED_STRUCT {
		uint16_t led_control;                       //40001
		uint16_t buzzer_control;                    //40002
		uint16_t ri_index;							//40003
		uint16_t service_index;                     //40004
		uint16_t baudrate;                          //40005
		uint16_t modbus_address;                    //40006
		uint16_t reserved[1];                       //40007
		ws2812e_dma_color_t led_color;              //40008 - 40009
		gear_info_t gear;							//40010 - 40042
		gear_resource_t resource;                   //40043 - 40045
		uint16_t gear_reserved[4];					//40046 - 40049
		ri_info_t ri_info;							//40050 - 40084
		uint16_t ri_reserved[5];					//40085 - 40089
		spindle_info_t spindel;                     //40090 - 40134
		uint16_t spindel_reserved[15];				//40135 - 40149
		__PACKED_STRUCT {
			archive_ri_info_t archive_ri;			//40150 - 40185
			uint16_t archive_ri_reserved[4];		//40186 - 40189
			archive_service_info_t archive_service;	//40190 - 40193
			uint16_t archive_service_reserved[6];	//40194 - 40199
		};
		new_service_info_t new_service;             //40200 - 40201
	};
	uint16_t array[203];
} MB_StorageHolding_t;

extern MB_StorageInput_t MB_StorageInput;
extern MB_StorageHolding_t MB_StorageHolding;

void ModBus_Init(void);
/**
 * @brief Инициализация задачи асинхронной обработки Modbus
 */
void ModBus_Async_Init(void);

/**
 * @brief Сброс Holding регистров на 0xFFFF кроме указанных исключений
 * @details Исключения: led_control, buzzer_control, baudrate, modbus_address
 */
void ModBus_ResetHoldingRegisters(void);

#endif /* INC_MODBUS_H_ */
