/**
 * @file adc_manager.c
 * @brief Реализация модуля управления ADC
 */

#include "adc_manager.h"
#include "debug.h"
#include "n32g430_adc.h"
#include "n32g430_rcc.h"
#include "n32g430_tim.h"
#include "n32g430_dma.h"
#include "gpio.h"
#include "project_config.h"
#include <stddef.h>
#include "FreeRTOS.h"
// #include "SEGGER_SYSVIEW_FreeRTOS.h"

/* ============================================================================
 * Локальные переменные
 * ============================================================================ */
static uint16_t adc_dma_buffer[ADC_CHANNEL_COUNT];  /* Буфер DMA: [NTC, Temp, PB0] */
static volatile uint8_t data_ready = 0;              /* Флаг готовности новых данных */

/* ============================================================================
 * Параметры таймера-триггера ADC (TIM4)
 * ============================================================================ */
/*
 * Преобразования ADC запускаются аппаратно от TIM4 (канал CC4) каждые 30 мс.
 * TIM4 расположен на шине APB1 и тактируется частотой 64 МГц
 * (SYSCLK 128 МГц, APB1 = HCLK/4 = 32 МГц, таймерный множитель x2).
 * Период = (PSC + 1) * (ARR + 1) / 64 МГц = 64 * 30000 / 64e6 = 30 мс.
 */

/* ============================================================================
 * Локальные функции
 * ============================================================================ */

/**
 * @brief Настройка TIM4 как источника аппаратного триггера ADC каждые 30 мс
 * @details Канал TIM4_CC4 в режиме PWM формирует событие сравнения, которое
 *          через линию T4_CC4 аппаратно запускает преобразование ADC.
 *          Физический вывод не задействован: GPIO канала не настроен на AF.
 */
static void adc_trigger_timer_init(void) {
    TIM_TimeBaseInitType TIM_TimeBaseStruct;
    OCInitType TIM_OCStruct;

    /* Включение тактирования TIM4 */
    RCC_APB1_Peripheral_Clock_Enable(RCC_APB1_PERIPH_TIM4);

    /* Базовая настройка таймера: период 30 мс, счёт вверх */
    TIM_Base_Struct_Initialize(&TIM_TimeBaseStruct);
    TIM_TimeBaseStruct.Period = ADC_TRIG_TIM_ARR;
    TIM_TimeBaseStruct.Prescaler = ADC_TRIG_TIM_PSC;
    TIM_TimeBaseStruct.ClkDiv = TIM_CLK_DIV1;
    TIM_TimeBaseStruct.CntMode = TIM_CNT_MODE_UP;
    TIM_Base_Initialize(TIM4, &TIM_TimeBaseStruct);

    /* Канал CC4 в режиме PWM1 — формирует периодический триггер для ADC */
    TIM_Output_Channel_Struct_Initialize(&TIM_OCStruct);
    TIM_OCStruct.OcMode = TIM_OCMODE_PWM1;
    TIM_OCStruct.OutputState = TIM_OUTPUT_STATE_ENABLE;
    TIM_OCStruct.Pulse = (ADC_TRIG_TIM_ARR + 1) / 2;
    TIM_OCStruct.OcPolarity = TIM_OC_POLARITY_HIGH;
    TIM_Output_Channel4_Initialize(TIM4, &TIM_OCStruct);
}

/* ============================================================================
 * Реализация API
 * ============================================================================ */

/**
 * @brief Инициализация ADC с DMA и таймером-триггером
 */
void adc_manager_init(void) {
    ADC_InitType ADC_InitStructure;

    /* Включение тактирования ADC и DMA */
    RCC_AHB_Peripheral_Clock_Enable(RCC_AHB_PERIPH_ADC);
    RCC_AHB_Peripheral_Clock_Enable(RCC_AHB_PERIPH_DMA);

    /* Настройка тактирования ADC - AHB mode с делителем */
    ADC_Clock_Mode_Config(ADC_CKMOD_AHB, RCC_ADCHCLK_DIV8);

    /* Настройка ADC1MCLK - источник HSI с делителем */
    RCC_ADC_1M_Clock_Config(RCC_ADC1MCLK_SRC_HSI, RCC_ADC1MCLK_DIV8);

    /* Включение внутреннего датчика температуры и Vrefint */
    ADC_Temperature_Sensor_And_Vrefint_Channel_Enable();

    /* Настройка ADC */
    ADC_Initializes_Structure(&ADC_InitStructure);
    ADC_InitStructure.MultiChEn = ENABLE;            /* Мультиканальный режим */
    ADC_InitStructure.ContinueConvEn = DISABLE;      /* Одиночное преобразование на каждый триггер */
    ADC_InitStructure.ExtTrigSelect = ADC_EXT_TRIGCONV_REGULAR_T4_CC4;  /* Аппаратный триггер от TIM4_CC4 */
    ADC_InitStructure.DatAlign = ADC_DAT_ALIGN_R;   /* Выравнивание вправо */
    ADC_InitStructure.ChsNumber = ADC_REGULAR_LEN_3;                /* 3 канала */
    ADC_Initializes(&ADC_InitStructure);

    /* Настройка времени выборки каналов - максимальное для точности */
    ADC_Channel_Sample_Time_Config(ADC_CH_1, ADC_SAMP_TIME_239CYCLES5);   /* PA0 - NTC */
    ADC_Channel_Sample_Time_Config(ADC_CH_TEMP_SENSOR, ADC_SAMP_TIME_239CYCLES5); /* Температура процессора */
    ADC_Channel_Sample_Time_Config(ADC_CH_9, ADC_SAMP_TIME_239CYCLES5);   /* PB0 */

    /* Настройка регулярной последовательности - 3 канала */
    ADC_Regular_Sequence_Conversion_Number_Config(ADC_CH_1, ADC_REGULAR_NUMBER_1);   /* Позиция 1: NTC */
    ADC_Regular_Sequence_Conversion_Number_Config(ADC_CH_TEMP_SENSOR, ADC_REGULAR_NUMBER_2); /* Позиция 2: Температура */
    ADC_Regular_Sequence_Conversion_Number_Config(ADC_CH_9, ADC_REGULAR_NUMBER_3);   /* Позиция 3: PB0 */

    /* ============================================================================
     * Настройка DMA CH1 для ADC
     * ============================================================================ */
    DMA_InitType DMA_InitStruct;
    DMA_Structure_Initializes(&DMA_InitStruct);
    DMA_InitStruct.PeriphAddr = (uint32_t)&ADC->DAT;          /* Адрес регистра данных ADC */
    DMA_InitStruct.MemAddr = (uint32_t)adc_dma_buffer;       /* Адрес буфера в памяти */
    DMA_InitStruct.Direction = DMA_DIR_PERIPH_SRC;           /* Периферия -> Память */
    DMA_InitStruct.BufSize = ADC_CHANNEL_COUNT;              /* 3 значения (NTC, PB0, Temp) */
    DMA_InitStruct.PeriphInc = DMA_PERIPH_INC_MODE_DISABLE;  /* Адрес периферии не инкрементируется */
    DMA_InitStruct.MemoryInc = DMA_MEM_INC_MODE_ENABLE;     /* Адрес памяти инкрементируется */
    DMA_InitStruct.PeriphDataSize = DMA_PERIPH_DATA_WIDTH_HALFWORD; /* 16 бит */
    DMA_InitStruct.MemDataSize = DMA_MEM_DATA_WIDTH_HALFWORD;     /* 16 бит */
    DMA_InitStruct.CircularMode = DMA_CIRCULAR_MODE_ENABLE;  /* Циклический режим */
    DMA_InitStruct.Priority = DMA_CH_PRIORITY_HIGH;          /* Высокий приоритет */
    DMA_InitStruct.Mem2Mem = DMA_MEM2MEM_DISABLE;           /* Не Mem2Mem */
    DMA_Initializes(DMA_CH1, &DMA_InitStruct);

    /* Ремап DMA запроса для ADC */
    DMA_Channel_Request_Remap(DMA_CH1, DMA_REMAP_ADC);

    /* Настройка приоритета прерывания DMA в NVIC */
    NVIC_SetPriority(DMA_Channel1_IRQn, 1);
    NVIC_EnableIRQ(DMA_Channel1_IRQn);

    /* Включение прерывания завершения передачи DMA */
    DMA_Interrupts_Enable(DMA_CH1, DMA_INT_TXC);

    /* Включение DMA в ADC */
    ADC_DMA_Transfer_Enable();

    /* Прерывание ADC не нужно - данные передаются через DMA */

    /* Включение ADC */
    ADC_ON();

    /* Check ADC Ready */
    while(ADC_Flag_Status_Get(ADC_RD_FLAG, ADC_FLAG_ENDC, ADC_FLAG_RDY) == RESET);

    /* Калибровка ADC */
    ADC_Calibration_Operation(ADC_CALIBRATION_ENABLE);
    while (ADC_Calibration_Operation(ADC_CALIBRATION_STS) == SET);

    /* Разрешение запуска преобразований по внешнему триггеру (от таймера) */
    ADC_External_Trigger_Conversion_Config(ADC_EXTTRIGCONV_REGULAR_ENABLE);

    /* Включение DMA канала */
    DMA_Channel_Enable(DMA_CH1);

    /* Настройка и запуск таймера: преобразования стартуют каждые 30 мс */
    adc_trigger_timer_init();
    TIM_On(TIM4);
}

/**
 * @brief Получение указателя на DMA буфер с результатами ADC
 */
const uint16_t* adc_manager_get_buffer(void) {
    return adc_dma_buffer;
}

/**
 * @brief Проверка готовности новых данных
 */
uint8_t adc_manager_data_ready(void) {
    return data_ready;
}

/**
 * @brief Сброс флага готовности данных
 */
void adc_manager_clear_data_ready(void) {
    data_ready = 0;
}

/**
 * @brief Отключение канала PB0 из регулярной последовательности ADC
 * @details Изменяет количество каналов с 3 на 2 и размер DMA буфера
 *          Также переключает PB0 в режим GPIO output для SPI NSS
 */
void adc_manager_disable_pb0_channel(void) {
    /* Остановка ADC */
    TIM_Off(TIM4);
    ADC_OFF();
    /* Обновление размера буфера DMA */
    DMA_Channel_Disable(DMA_CH1);

    /* Изменение количества каналов с 3 на 2 */
    ADC_Regular_Channels_Number_Config(ADC_REGULAR_LEN_2);

    DMA_Buffer_Size_Config(DMA_CH1, 2);
    DMA_Channel_Enable(DMA_CH1);

    /* Переключение PB0 в режим GPIO output (SPI NSS) */
    chip_select_init();

    /* Возобновление преобразований */
    ADC_ON();
    TIM_On(TIM4);

    // DEBUG_PRINTF("[ADC] adc_manager_disable_pb0_channel\r\n");
}

/**
 * @brief Включение канала PB0 в регулярную последовательность ADC
 * @details Восстанавливает 3 канала и размер DMA буфера
 *          Также переключает PB0 в режим аналоговый вход для ADC
 */
void adc_manager_enable_pb0_channel(void) {
    /* Переключение PB0 в режим аналоговый вход (ADC) */
    chip_select_deinit();

    /* Остановка ADC */
    TIM_Off(TIM4);
    ADC_OFF();

    /* Обновление размера буфера DMA */
    DMA_Channel_Disable(DMA_CH1);

    /* Изменение количества каналов с 2 на 3 */
    ADC_Regular_Channels_Number_Config(ADC_REGULAR_LEN_3);

    DMA_Buffer_Size_Config(DMA_CH1, ADC_CHANNEL_COUNT);
    DMA_Channel_Enable(DMA_CH1);

    /* Возобновление преобразований */
    ADC_ON();
    TIM_On(TIM4);

    // DEBUG_PRINTF("[ADC] adc_manager_enable_pb0_channel\r\n");
}

/**
 * @brief Обработчик прерывания DMA
 * @details Вызывается при завершении передачи DMA (раз в 30 мс).
 *          Устанавливает флаг готовности данных.
 */
void DMA_Channel1_IRQHandler(void) {
    traceISR_ENTER();

    if (DMA_Flag_Status_Get(DMA, DMA_INTSTS_TXCF1)) {
        DMA_Flag_Status_Clear(DMA, DMA_INTSTS_TXCF1);
        data_ready = 1;
    }

    traceISR_EXIT();
}
