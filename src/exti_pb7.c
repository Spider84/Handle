/**
 * @file exti_pb7.c
 * @brief Реализация драйвера внешнего прерывания на PB7 для N32G430
 * @details Прерывание на PB7 для будущих нужд
 */

#include "exti_pb7.h"
#include "n32g430.h"
#include "FreeRTOS.h"
// #include "SEGGER_SYSVIEW_FreeRTOS.h"

/* Флаг прерывания PB7 */
static volatile uint8_t exti_pb7_triggered = 0;

/**
 * @brief Инициализация внешнего прерывания на PB7
 */
void exti_pb7_init(void) {
    /* Включение тактирования AFIO */
    RCC_APB2_Peripheral_Clock_Enable(RCC_APB2_PERIPH_AFIO);

    /* Настройка AFIO: подключение PB7 к EXTI7 */
    AFIO->EXTI_CFG[1] &= ~(0xF << 12);  /* Очистка битов для EXTI7 */
    AFIO->EXTI_CFG[1] |= (0x1 << 12);   /* PB7 -> EXTI7 */

    /* Настройка EXTI7 на прерывание по падающему фронту */
    EXTI_InitType EXTI_InitStruct;
    EXTI_Structure_Initializes(&EXTI_InitStruct);
    EXTI_InitStruct.EXTI_Line = EXTI_LINE7;
    EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStruct.EXTI_LineCmd = ENABLE;
    EXTI_Peripheral_Initializes(&EXTI_InitStruct);

    /* Настройка приоритета прерывания EXTI9_5 */
    NVIC_SetPriority(EXTI9_5_IRQn, 5);

    /* Включение прерывания EXTI9_5 */
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}


/**
 * @brief Проверка флага прерывания PB7
 */
uint8_t exti_pb7_get_irq_flag(void) {
    if (exti_pb7_triggered) {
        exti_pb7_triggered = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief Обработчик прерывания EXTI9_5 (линии 5-9)
 * @details Вызывается из startup_n32g430_gnu.s
 */
void EXTI9_5_IRQHandler(void) {
    traceISR_ENTER();

    /* Проверка флага прерывания EXTI7 */
    if (EXTI_Flag_Status_Get(EXTI_LINE7) != RESET) {
        /* Сброс флага прерывания */
        EXTI_Flag_Status_Clear(EXTI_LINE7);

        /* Установка флага срабатывания прерывания */
        exti_pb7_triggered = 1;
    }

    traceISR_EXIT();
}
