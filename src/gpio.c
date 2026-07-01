/**
 * @file gpio.c
 * @brief Реализация инициализации GPIO для N32G430
 * @details Централизованная инициализация всех пинов проекта
 */

#include "gpio.h"

/**
 * @brief Инициализация всех пинов GPIO
 */
void gpio_init(void) {
    GPIO_InitType GPIO_InitStructure;

    /* Включение тактирования GPIOA, GPIOB и AFIO */
    RCC_AHB_Peripheral_Clock_Enable(RCC_AHB_PERIPH_GPIOA);
    RCC_AHB_Peripheral_Clock_Enable(RCC_AHB_PERIPH_GPIOB);
    RCC_APB2_Peripheral_Clock_Enable(RCC_APB2_PERIPH_AFIO);

    /* Сброс AFIO для корректной работы при обычном запуске */
    RCC_APB2_Peripheral_Reset(RCC_APB2_PERIPH_AFIO);

    /* Отключение remapping SPI1 NSS для корректной работы PB0 */
    AFIO->RMP_CFG &= ~AFIO_RMP_CFG_SPI1_NSS;

    /* Включение 5V tolerance для PB0 */
    AFIO_5V_Tolerance_Disable(PB0_5V_TOLERANCE);

    /* ============================================================================
     * Настройка PA0 (NTC_SENSOR) как аналоговый вход для ADC
     * ============================================================================ */
    GPIO_InitStructure.Pin = NTC_SENSOR_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_ANALOG;
    GPIO_InitStructure.GPIO_Pull = GPIO_NO_PULL;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_SLEW_RATE_SLOW;
    GPIO_InitStructure.GPIO_Current = GPIO_DS_12MA;
    GPIO_InitStructure.GPIO_Alternate = 0;
    GPIO_Peripheral_Initialize(NTC_SENSOR_PORT, &GPIO_InitStructure);

    /* ============================================================================
     * Настройка кнопок PA7, PA8, PA10 как входы без подтяжек
     * ============================================================================ */
    GPIO_InitStructure.Pin = BUTTON_BIG_PIN | BUTTON_1_PIN | BUTTON_2_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_SLEW_RATE_FAST;
    GPIO_Peripheral_Initialize(BUTTON_BIG_PORT, &GPIO_InitStructure);

    /* ============================================================================
     * Настройка PA1 (UART2 RTS для RE/DE) как GPIO выход для ручного управления
     * ============================================================================ */
    GPIO_InitStructure.Pin = UART2_RTS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_OUT_PP;
    GPIO_Peripheral_Initialize(UART2_RTS_PORT, &GPIO_InitStructure);

    /* Установить RE/DE в LOW (режим приёма) */
    GPIO_Pins_Reset(UART2_RTS_PORT, UART2_RTS_PIN);

    /* ============================================================================
     * Настройка PA2 (UART2 TX) как альтернативная функция AF1
     * ============================================================================ */
    GPIO_InitStructure.Pin = UART2_TX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.GPIO_Alternate = UART2_TX_AF;
    GPIO_Peripheral_Initialize(UART2_TX_PORT, &GPIO_InitStructure);

    /* ============================================================================
     * Настройка PA3 (UART2 RX) как альтернативная функция AF1
     * ============================================================================ */
    GPIO_InitStructure.Pin = UART2_RX_PIN;
    GPIO_InitStructure.GPIO_Alternate = UART2_RX_AF;
    GPIO_Peripheral_Initialize(UART2_RX_PORT, &GPIO_InitStructure);

    /* ============================================================================
     * Настройка PB6 (BEEPER) как альтернативная функция AF12
     * ============================================================================ */
    GPIO_InitStructure.Pin = BEEPER_PIN;
    GPIO_InitStructure.GPIO_Alternate = GPIO_AF12_BEEPER;
    GPIO_Peripheral_Initialize(BEEPER_PORT, &GPIO_InitStructure);

    /* ============================================================================
     * Настройка PB1 (WS2812E) как альтернативная функция AF2 (TIM3_CH4)
     * ============================================================================ */
    GPIO_InitStructure.Pin = WS2812E_PIN;
    GPIO_InitStructure.GPIO_Alternate = WS2812E_AF;
    GPIO_Peripheral_Initialize(WS2812E_PORT, &GPIO_InitStructure);

    /* ============================================================================
     * Настройка PB0 (CHIP_CS) как аналоговый вход для детекции чипа
     * ============================================================================ */
    // chip_select_init();
    /* Настройка GPIO: PB3-SCK, PB4-MISO, PB5-MOSI как AF PP, PB0-CS как выход */
	// GPIO_InitType GPIO_InitStruct;

	// /* SCK (PB3) - AF push-pull */
	// GPIO_InitStruct.Pin           = SPI1_SCK_PIN;
	// GPIO_InitStruct.GPIO_Mode     = GPIO_MODE_AF_PP;
	// GPIO_InitStruct.GPIO_Slew_Rate = GPIO_SLEW_RATE_FAST;
	// GPIO_InitStruct.GPIO_Current  = GPIO_DS_12MA;
	// GPIO_InitStruct.GPIO_Alternate = SPI1_SCK_AF;
	// GPIO_Peripheral_Initialize(SPI1_SCK_PORT, &GPIO_InitStruct);

	// /* MOSI (PB5) - AF push-pull */
	// GPIO_InitStruct.Pin           = SPI1_MOSI_PIN;
	// GPIO_InitStruct.GPIO_Alternate = SPI1_MOSI_AF;
	// GPIO_Peripheral_Initialize(SPI1_MOSI_PORT, &GPIO_InitStruct);

	// /* MISO (PB4) - AF input для мастера */
	// GPIO_InitStruct.Pin           = SPI1_MISO_PIN;
	// GPIO_InitStruct.GPIO_Alternate = SPI1_MISO_AF;
	// GPIO_Peripheral_Initialize(SPI1_MISO_PORT, &GPIO_InitStruct);

    chip_select_deinit();
}

/* ============================================================================
 * Управление CHIP_DETECT/CHIP_SELECT (PB0)
 * ============================================================================ */

/**
 * @brief Переключение PB0 в режим CS активен (OUTPUT LOW)
 */
void chip_select_init(void) {
    GPIO_InitType GPIO_InitStructure;

    /* Переключение в OUTPUT PUSH-PULL HIGH (неактивен) */
    GPIO_InitStructure.Pin = CHIP_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_OUT_PP;
    GPIO_InitStructure.GPIO_Pull = GPIO_NO_PULL;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_SLEW_RATE_FAST;
    GPIO_InitStructure.GPIO_Current = GPIO_DS_12MA;
    GPIO_InitStructure.GPIO_Alternate = 0;
    GPIO_Peripheral_Initialize(CHIP_CS_PORT, &GPIO_InitStructure);

    /* Установить HI (CS не активен) */
    GPIO_Pins_Set(CHIP_CS_PORT, CHIP_CS_PIN);
}

/**
 * @brief Переключение PB0 в режим CS неактивен (INPUT для детекции)
 */
void chip_select_deinit(void) {
    GPIO_InitType GPIO_InitStructure;

    /* Переключение в аналоговый вход для детекции */
    GPIO_InitStructure.Pin = CHIP_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_ANALOG;
    GPIO_InitStructure.GPIO_Pull = GPIO_NO_PULL;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_SLEW_RATE_SLOW;
    GPIO_InitStructure.GPIO_Current = GPIO_DS_12MA;
    GPIO_InitStructure.GPIO_Alternate = 0;
    GPIO_Peripheral_Initialize(CHIP_CS_PORT, &GPIO_InitStructure);
}

/**
 * @brief Проверка наличия чипа через ADC
 * @return true если чип есть (напряжение > 1.5В), false если нет
 */
bool chip_is_present(void) {
    extern uint16_t ntc_read_pb0_voltage_mv(void);

    uint16_t voltage = ntc_read_pb0_voltage_mv();
    return (voltage > CHIP_DETECT_THRESHOLD_MV);
}
