/**
 * @file main.c
 * @brief Главный файл проекта для N32G430G8
 * @details Настройка тактирования от HSE через PLL до 128MHz
 */

#include <FreeRTOS.h>
#include <task.h>
#include <flashdb.h>
#include <semphr.h>
#include <sys/param.h>
#include "debug.h"
#include "projdefs.h"
#include "ws2812_led.h"
#include "buzzer.h"
#include "gpio.h"
#include "buttons.h"
#include "modbus.h"
#include "buttons.h"
#include "temp.h"
#include "pb0_sensor.h"
#include "flash_config.h"
#include "project_config.h"
#include "iwdg.h"
#include "flash_storage.h"
#include "sfud_def.h"

/* Состояния FSM для управления Flash */
typedef enum {
	FLASH_STATE_DISCONNECTED,   /* Flash не подключена */
	FLASH_STATE_DEBOUNCE,       /* Ожидание debounce после обнаружения */
	FLASH_STATE_CONNECTED,     /* Flash подключена и работает */
	FLASH_STATE_INIT_FAILED    /* Ожидание паузы после неудачной инициализации */
} FlashState_t;

/**
 * @brief Инициализация Flash и KVDB
 * @param arg аргумент задачи (не используется)
 */
static void Main_Task(void *arg)
{
	/* Инициализация WS2812E на PB1 через DMA+PWM */
    WS2812_Init();
	WS2812_SetColor(COLOR_RED);
	Buzzer_Init();

	Temp_Task_Init();

	ModBus_Init();

	StaticQueue_t xStaticQueue;
	uint8_t ucQueueStorage[MSG_QUEUE_LEN * sizeof(ButtonMsg_t)];
	QueueHandle_t hButtonQueue = xQueueCreateStatic(MSG_QUEUE_LEN, sizeof(ButtonMsg_t), ucQueueStorage, &xStaticQueue);
#ifdef DEBUG
	vQueueAddToRegistry(hButtonQueue, "Buttons");
#endif

	const ButtonConfig_t key[3] = {
		{
			.port = BUTTON_BIG_PORT,
			.pin = BUTTON_BIG_PIN,
			.inverted = true, // Pull-up
			.button_id = 0,
			.event_queue = hButtonQueue
		},
		{
			.port = BUTTON_1_PORT,
			.pin = BUTTON_1_PIN,
			.inverted = true, // Pull-up
			.button_id = 1,
			.event_queue = hButtonQueue
		},
		{
			.port = BUTTON_2_PORT,
			.pin = BUTTON_2_PIN,
			.inverted = true, // Pull-up
			.button_id = 2,
			.event_queue = hButtonQueue
		}
	};

	Buttons_Init();
	Buttons_Register(key);
	Buttons_Register(key+1);
	Buttons_Register(key+2);

	MB_StorageInput.device_status = 0;

	/* Задержка для заряда внешней цепи на PB0 */
	vTaskDelay(pdMS_TO_TICKS(200));

	/* Переменные FSM */
	FlashState_t flash_state = FLASH_STATE_DISCONNECTED;
	TickType_t last_check_time = 0;
	TickType_t debounce_start_time = 0;
	TickType_t init_fail_time = 0;

	vTaskDelay(pdMS_TO_TICKS(100));
	WS2812_Off();

	// sfud_err sfud_spi_port_init(sfud_flash *flash);
	// extern sfud_flash sfud_norflash0;
	// sfud_err sfud_spi_port_init(sfud_flash *flash);
	// sfud_spi_port_init(&sfud_norflash0);

	while(1) {
		uint16_t current_pb0_voltage = pb0_read_voltage_mv();
		TickType_t current_time = xTaskGetTickCount();

		MB_StorageInput.pb0_voltage = current_pb0_voltage;
		if (pb0_spi_nss_active)
			MB_StorageInput.pb0_falgs |= 0x8000;
		else
		{
			MB_StorageInput.pb0_falgs &= ~0x8000;
		}

		switch (flash_state) {
			case FLASH_STATE_DISCONNECTED:
				/* Проверка PB0 каждые 100мс */
				if ((current_time - last_check_time) >= pdMS_TO_TICKS(100)) {
					// WS2812_SetColor(COLOR_GREEN);
					last_check_time = current_time;

					if (!pb0_spi_nss_active && current_pb0_voltage >= PB0_HIGH_THRESHOLD) {
						/* Обнаружен высокий уровень - переходим в debounce */
						flash_state = FLASH_STATE_DEBOUNCE;
						debounce_start_time = current_time;
						DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_YELLOW"[FSM] PB0 high detected, entering debounce\n"RTT_CTRL_RESET);
					}
				}
				break;

			case FLASH_STATE_DEBOUNCE:
				// WS2812_SetColor(COLOR_YELLOW);
				/* Проверяем, что уровень остаётся высоким в течение debounce */
				if (current_pb0_voltage < PB0_LOW_THRESHOLD) {
					/* Уровень упал - отмена */
					flash_state = FLASH_STATE_DISCONNECTED;
					DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_YELLOW"[FSM] Debounce cancelled (PB0 low)\n"RTT_CTRL_RESET);
				} else if ((current_time - debounce_start_time) >= pdMS_TO_TICKS(REINIT_DEBOUNCE_MS)) {
					/* Debounce прошёл успешно - пробуем инициализировать Flash */
					DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_GREEN"[FSM] Debounce passed, initializing Flash\n"RTT_CTRL_RESET);
					MB_StorageInput.device_status |= DEVICE_MEM_DETECTED;

					if (flash_storage_reinit()) {
						flash_storage_set_initialized(true);
						flash_state = FLASH_STATE_CONNECTED;
						last_check_time = current_time;
						DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_GREEN"[FSM] Flash initialized successfully\n"RTT_CTRL_RESET);
					} else {
						/* Инициализация не удалась - переходим в состояние ожидания */
						flash_state = FLASH_STATE_INIT_FAILED;
						init_fail_time = current_time;
						DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[FSM] Flash initialization failed, waiting 500ms\n"RTT_CTRL_RESET);
					}
				}
				break;

			case FLASH_STATE_INIT_FAILED:
				// WS2812_SetColor(COLOR_RED);
				/* Ожидание паузы 500мс после неудачной инициализации */
				if ((current_time - init_fail_time) >= pdMS_TO_TICKS(500)) {
					flash_state = FLASH_STATE_DISCONNECTED;
					DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_YELLOW"[FSM] Init fail timeout expired, retrying\n"RTT_CTRL_RESET);
				}
				break;

			case FLASH_STATE_CONNECTED:
				/* Проверка целостности FlashDB каждую секунду */
				if ((current_time - last_check_time) >= pdMS_TO_TICKS(1000)) {
					// WS2812_SetColor(COLOR_BLUE);

					last_check_time = current_time;

					if (fdb_kvdb_check(flash_storage_get_kvdb()) != FDB_NO_ERR) {
						/* Ошибка доступа к Flash - деинициализация */
						MB_StorageInput.device_status &= ~(DEVICE_FLASH_VALID | DEVICE_MEM_MOUNTED | DEVICE_ARCHIVE_VALID);
						flash_storage_deinit();
						flash_storage_set_initialized(false);
						flash_state = FLASH_STATE_DISCONNECTED;
						DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[FSM] FlashDB check failed, disconnecting\n"RTT_CTRL_RESET);
					} else {
						/* Flash валиден */
						MB_StorageInput.device_status |= DEVICE_FLASH_VALID;
					}
				}

				/* Дополнительная проверка: если FlashDB валиден, но PB0 низкий - деинициализация */
				if ((MB_StorageInput.device_status & DEVICE_FLASH_VALID) && !pb0_spi_nss_active) {
					if (current_pb0_voltage < PB0_LOW_THRESHOLD) {
						MB_StorageInput.device_status &= ~(DEVICE_MEM_DETECTED|DEVICE_ARCHIVE_VALID);
						flash_storage_deinit();
						flash_storage_set_initialized(false);
						flash_state = FLASH_STATE_DISCONNECTED;
						DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[FSM] PB0 low while connected, disconnecting\n"RTT_CTRL_RESET);
					}
				}
				break;
		}

		ButtonMsg_t received_msg;
		if (xQueueReceive(hButtonQueue, &received_msg, pdMS_TO_TICKS(10))) {
			WS2812_Off();
			if (received_msg.event==BUTTON_EVENT_DOWN)
			{
				MB_StorageInput.buttons_mask |= (1<<received_msg.button_id);
			}
			else
			if (received_msg.event==BUTTON_EVENT_UP)
			{
				MB_StorageInput.buttons_mask &= ~(1<<received_msg.button_id);
			}
			MB_StorageInput.buttons_mask &= 0b111;

		}
	}

	vTaskSuspend(NULL);
}

void freertos_tasks_init(void)
{
	static StaticTask_t xAppTaskBuffer;
	static StackType_t xAppStack[ configMINIMAL_STACK_SIZE * 12 ];
	xTaskCreateStatic(Main_Task, "Application", sizeof(xAppStack)/sizeof(xAppStack[0]), NULL, tskIDLE_PRIORITY+1, xAppStack, &xAppTaskBuffer);
}

/**
 * @brief Главная функция программы
 */
int main(void)
{
    __disable_irq();

	MB_StorageInput.pb0_falgs = (RCC->CTRLSTS & 0xFF200000) >>21;
	// uint32_t reset_flags = RCC_Flag_Status_Get(RCC_FLAG_PORRST);

	RCC_Reset_Flag_Clear();

	// if (reset_flags)
	// {
	// 	NVIC_SystemReset();
	// }

#ifndef DEBUG
	IWDG_Init();
#endif

	portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();

// #ifndef DEBUG
//     /* Проверка и установка защиты от чтения (RDP Level 1) для RELEASE сборки */
//     if (FLASH_Read_Out_Protection_Status_Get() == RESET) {
//         /* Защита от чтения не установлена - устанавливаем RDP Level 1 */
//         FLASH_Read_Out_Protection_L1_Enable();
// 		NVIC_SystemReset();
//     }
// #endif

	System_Clock_Frequency_Update();

    /* Инициализация GPIO */
    gpio_init();

#ifdef DEBUG
  	SEGGER_RTT_Init();
  	traceSTART();

	// DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_GREEN"[Main] Start\n"RTT_CTRL_RESET);
#endif

    __enable_irq();

	if (!flash_config_init())
	{
		// DEBUG_PRINTF(RTT_CTRL_TEXT_BRIGHT_RED"[Main] No config from MCU flash, using default...\r\n"RTT_CTRL_RESET);
		flash_config_reset();
	}

    vTaskStartScheduler();

    return 0;
}
