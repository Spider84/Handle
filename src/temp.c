#include <stdint.h>
#include "freeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "temp.h"
#include "modbus.h"
#include "ntc_sensor.h"
#include "pb0_sensor.h"
#include "cpu_temp_sensor.h"
#include "adc_manager.h"

/* Настройки RTOS */
static StackType_t xTempStack[configMINIMAL_STACK_SIZE*3];
static StaticTask_t xTempTaskBuffer;
static SemaphoreHandle_t xTempMutex;
static StaticSemaphore_t xMutexBuffer;

/* Хранилище отфильтрованных данных */
static int16_t current_ntc_temp = INT16_MIN;
static int16_t current_mcu_temp = INT16_MIN;

/* Приватные функции */
static void Temp_Task(void *pvParameters);

void Temp_Task_Init(void) {
    xTempMutex = xSemaphoreCreateBinaryStatic(&xMutexBuffer);
    xSemaphoreGive(xTempMutex);
#ifdef DEBUG
	vQueueAddToRegistry(xTempMutex, "Temp");
#endif

    adc_manager_init();

    // /* Инициализация NTC сенсора температуры на PA0 */
    ntc_sensor_init();

    // /* Инициализация PB0 сенсора */
    pb0_sensor_init();

    // /* Инициализация датчика температуры процессора */
    cpu_temp_sensor_init();

    xTaskCreateStatic(Temp_Task, "TempScan", sizeof(xTempStack)/sizeof(xTempStack[0]), NULL, configMAX_PRIORITIES - 1, xTempStack, &xTempTaskBuffer);
}

static void Temp_Task(void *pvParameters) {
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // Обновляем раз в 100мс

        /* Обновление данных сенсоров при готовности новых данных */
        if (!adc_manager_data_ready()) {
            continue;
        }

        ntc_sensor_update();
        pb0_sensor_update();
        cpu_temp_sensor_update();
        adc_manager_clear_data_ready();

        /* Опрос температуры NTC сенсора */
        int16_t temperature = ntc_read_temperature_c();
        // uint16_t voltage = ntc_read_voltage_mv();
        // uint16_t resistance = ntc_read_resistance_ohm();

        /* Чтение напряжения на PB0 и температуры процессора */
        // uint16_t pb0_voltage = pb0_read_voltage_mv();
        int16_t cpu_temp = cpu_temp_read_c();

        if (xSemaphoreTake(xTempMutex, portMAX_DELAY) == pdTRUE) {
            current_ntc_temp = temperature;
            current_mcu_temp = cpu_temp;
            xSemaphoreGive(xTempMutex);
        }

        MB_StorageInput.temper = temperature;
        MB_StorageInput.cpu_temp = cpu_temp;
    }
}

int16_t Temp_GetNTC(void) {
    int16_t temp;
    xSemaphoreTake(xTempMutex, portMAX_DELAY);
    temp = current_ntc_temp;
    xSemaphoreGive(xTempMutex);
    return temp;
}

int16_t Temp_GetMCU(void) {
    int16_t temp;
    xSemaphoreTake(xTempMutex, portMAX_DELAY);
    temp = current_mcu_temp;
    xSemaphoreGive(xTempMutex);
    return temp;
}
