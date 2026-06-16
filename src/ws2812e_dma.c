/**
 * @file ws2812e_dma.c
 * @brief Реализация драйвера WS2812E через DMA+PWM (TIM3_CH4 на PB1)
 * @details Оптимизирован для 1 диода
 */

#include "ws2812e_dma.h"
#include "n32g430.h"
#include <FreeRTOS.h>

/* Конфигурация */
#define WS2812E_TIM           TIM3
#define WS2812E_DMA_CH        DMA_CH7

/* Размер буфера DMA: 25 битов (24 + NOP) + 50us reset (~125 периодов) */
#define WS2812E_BUFFER_SIZE   (25 + 125)

/* Тайминг PWM для периода 1.25us при 64MHz (Prescaler=0, Period=79 = 80 тиков) */
/* При PWM1 + HIGH: CNT < CCR = LOW, CNT >= CCR = HIGH */
/* T0H: 350ns HIGH (22 тика), CCR = 79 - 22 + 1 = 58 */
/* T1H: 700ns HIGH (45 тиков), CCR = 79 - 45 + 1 = 35 */
#define PWM_PERIOD            79
#define PWM_T0H_PULSE         22
#define PWM_T1H_PULSE         44

/* Буфер DMA */
static uint16_t dma_buffer[WS2812E_BUFFER_SIZE];
static volatile uint8_t transfer_complete = 1;

/**
 * @brief Прерывание завершения DMA
 */
void DMA_Channel7_IRQHandler(void) {
    traceISR_ENTER();

    if (DMA_Flag_Status_Get(DMA, DMA_INTSTS_TXCF7)) {
        DMA_Flag_Status_Clear(DMA, DMA_INTSTS_TXCF7);
        transfer_complete = 1;
        /* Остановка таймера */
        TIM_Off(TIM3);
        /* Сброс DMA канала для следующей передачи */
        DMA_Channel_Disable(DMA_CH7);
    }

    traceISR_EXIT();
}

/**
 * @brief Инициализация драйвера WS2812E с DMA+PWM
 */
void ws2812e_dma_init(void) {
    /* Включение тактирования */
    RCC_APB1_Peripheral_Clock_Enable(RCC_APB1_PERIPH_TIM3);
    RCC_AHB_Peripheral_Clock_Enable(RCC_AHB_PERIPH_DMA);

    /* GPIO инициализируется в gpio_init() */

    /* Настройка TIM3 для PWM (64MHz, Period=79, период 1.25us) */
    TIM_TimeBaseInitType TIM_TimeBaseInitStruct;
    TIM_TimeBaseInitStruct.Period = PWM_PERIOD;
    TIM_TimeBaseInitStruct.Prescaler = 0;
    TIM_TimeBaseInitStruct.ClkDiv = 0;
    TIM_TimeBaseInitStruct.CntMode = TIM_CNT_MODE_UP;
    TIM_TimeBaseInitStruct.RepetCnt = 0;
    TIM_Base_Initialize(TIM3, &TIM_TimeBaseInitStruct);

    /* Настройка CH4 для PWM */
    OCInitType TIM_OCInitStruct;
    TIM_OCInitStruct.OcMode = TIM_OCMODE_PWM1;
    TIM_OCInitStruct.OutputState = TIM_OUTPUT_STATE_ENABLE;
    TIM_OCInitStruct.Pulse = 0;
    TIM_OCInitStruct.OcPolarity = TIM_OC_POLARITY_HIGH;
    TIM_Output_Channel4_Initialize(TIM3, &TIM_OCInitStruct);

    /* Включение буферизации CCR для канала 4 (обновление только при update event) */
    TIM_Output_Channel4_Preload_Set(TIM3, TIM_OC_PRELOAD_ENABLE);
    /* Включение буферизации регистров управления capture/compare */
    TIM_Capture_Compare_Control_Preload_Enable(TIM3);
    /* Включение буферизации ARR */
    TIM_Auto_Reload_Preload_Enable(TIM3);

    /* Настройка DMA CH7 */
    DMA_InitType DMA_InitStruct;
    DMA_Structure_Initializes(&DMA_InitStruct);
    DMA_InitStruct.PeriphAddr = (uint32_t)&TIM3->CCDAT4;
    DMA_InitStruct.MemAddr = (uint32_t)dma_buffer;
    DMA_InitStruct.Direction = DMA_DIR_PERIPH_DST;
    DMA_InitStruct.BufSize = WS2812E_BUFFER_SIZE;
    DMA_InitStruct.PeriphInc = DMA_PERIPH_INC_MODE_DISABLE;
    DMA_InitStruct.MemoryInc = DMA_MEM_INC_MODE_ENABLE;
    DMA_InitStruct.PeriphDataSize = DMA_PERIPH_DATA_WIDTH_HALFWORD;
    DMA_InitStruct.MemDataSize = DMA_MEM_DATA_WIDTH_HALFWORD;
    DMA_InitStruct.CircularMode = DMA_CIRCULAR_MODE_DISABLE;
    DMA_InitStruct.Priority = DMA_CH_PRIORITY_MEDIUM;
    DMA_InitStruct.Mem2Mem = DMA_MEM2MEM_DISABLE;
    DMA_Initializes(DMA_CH7, &DMA_InitStruct);

    /* Ремап DMA запроса для TIM3_CH4 */
    DMA_Channel_Request_Remap(DMA_CH7, DMA_REMAP_TIM3_CH4);

    /* Включение DMA запроса от TIM3_CH4 */
    TIM_Dma_Enable(TIM3, TIM_DMA_CC4);

    /* Включение прерывания DMA в NVIC */
    NVIC_EnableIRQ(DMA_Channel7_IRQn);

    /* Заполнение буфера нулями */
    for (int i = 0; i < WS2812E_BUFFER_SIZE; i++) {
        dma_buffer[i] = 0;
    }
}

/**
 * @brief Подготовка буфера DMA из цвета
 */
static void prepare_buffer(ws2812e_dma_color_t color) {
    uint8_t data[3] = {color.g, color.r, color.b};  /* GRB order */
    uint16_t idx = 0;

    /* 24 бита данных */
    for (int byte = 0; byte < 3; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            if ((data[byte] >> bit) & 0x01) {
                dma_buffer[idx++] = PWM_T1H_PULSE;  /* Bit 1 */
            } else {
                dma_buffer[idx++] = PWM_T0H_PULSE;  /* Bit 0 */
            }
        }
    }

    /* 25-й NOP бит (всегда 0) */
    dma_buffer[idx++] = PWM_T0H_PULSE;

    /* Reset pulse (LOW level >50us) - при HIGH полярности это LOW уровень = 0 */
    for (; idx < WS2812E_BUFFER_SIZE; idx++) {
        dma_buffer[idx] = 0;
    }
}

/**
 * @brief Установка цвета одному светодиоду
 */
void ws2812e_dma_set_color(ws2812e_dma_color_t color) {
    while (!transfer_complete);  /* Ожидание завершения предыдущей передачи */

    prepare_buffer(color);

    transfer_complete = 0;
    /* Сброс флага прерывания перед запуском */
    DMA_Flag_Status_Clear(DMA, DMA_INTSTS_TXCF7);
    /* Включение прерывания завершения передачи DMA после включения канала */
    DMA_Interrupts_Enable(DMA_CH7, DMA_INT_TXC);
    /* Восстановление размера буфера DMA (обнуляется после отключения канала) */
    DMA_Buffer_Size_Config(DMA_CH7, WS2812E_BUFFER_SIZE);
    /* Сброс счётчика таймера ПЕРЕД включением DMA */
    TIM_Base_Count_Set(TIM3, 0);
    /* Включение канала DMA */
    DMA_Channel_Enable(DMA_CH7);
    /* Запуск таймера ПОСЛЕ включения DMA */
    TIM_On(TIM3);
}

/**
 * @brief Проверка завершения передачи DMA
 */
uint8_t ws2812e_dma_is_complete(void) {
    return transfer_complete;
}

/**
 * @brief Очистка (выключение) светодиода
 */
void ws2812e_dma_clear(void) {
    ws2812e_dma_color_t black = {0, 0, 0};
    ws2812e_dma_set_color(black);
}
