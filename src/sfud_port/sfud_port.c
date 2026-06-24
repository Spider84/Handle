/*
 * This file is part of the Serial Flash Universal Driver Library.
 *
 * Copyright (c) 2016-2018, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for each platform.
 * Created on: 2016-04-23
 */

#include <sfud.h>
#include <stdarg.h>
#include "adc_manager.h"
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "debug.h"
#include "gpio.h"
#include "project_config.h"
#include "n32g430_gpio.h"
#include "n32g430_rcc.h"
#include "n32g430_spi.h"

/* Глобальный флаг активности SPI NSS на PB0 */
volatile bool pb0_spi_nss_active = false;

static StaticSemaphore_t xMutexBuffer;
static SemaphoreHandle_t mutex;

/**
 * SPI write data then read data
 */
static sfud_err spi_write_read(const sfud_spi *spi, const uint8_t *write_buf, size_t write_size, uint8_t *read_buf, size_t read_size) {
    sfud_err result = SFUD_SUCCESS;
    uint8_t send_data, read_data;

	if (write_size) {
		SFUD_ASSERT(write_buf);
	}
	if (read_size) {
		SFUD_ASSERT(read_buf);
	}

	/* Очистка буфера чтения перед передачей */
	while (SPI_I2S_Flag_Status_Get(SPI1, SPI_I2S_FLAG_RNE)!=RESET) {
		volatile uint16_t temp = SPI_I2S_Data_Get(SPI1);
		(void)temp;
	}

	GPIO_Pins_Reset(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin);

	/* 锟斤拷始锟斤拷写锟斤拷锟斤拷 */
	for (size_t i = 0, retry_times; i < write_size + read_size; i++) {
		/* 锟斤拷写锟斤拷锟斤拷锟斤拷锟叫碉拷锟斤拷锟捷碉拷 SPI 锟斤拷锟竭ｏ拷锟斤拷锟斤拷写锟斤拷锟斤拷锟叫� dummy(0xFF) 锟斤拷 SPI 锟斤拷锟斤拷 */
		send_data = (i < write_size)?*write_buf++:SFUD_DUMMY_DATA;
		/* 锟斤拷锟斤拷锟斤拷锟斤拷 */
		retry_times = 10000;


		while (SPI_I2S_Flag_Status_Get(SPI1, SPI_I2S_FLAG_TE)==RESET) {
			SFUD_RETRY_PROCESS(NULL, retry_times, result);
		}
		if (result != SFUD_SUCCESS) {
			goto exit;
		}
		SPI_I2S_Data_Transmit(SPI1, send_data);
		/* 锟斤拷锟斤拷锟斤拷锟斤拷 */
		retry_times = 10000;
		while (SPI_I2S_Flag_Status_Get(SPI1, SPI_I2S_FLAG_RNE)==RESET) {
			SFUD_RETRY_PROCESS(NULL, retry_times, result);
		}
		if (result != SFUD_SUCCESS) {
			goto exit;
		}
		read_data = SPI_I2S_Data_Get(SPI1);
		/* 写锟斤拷锟斤拷锟斤拷锟叫碉拷锟斤拷锟捷凤拷锟斤拷锟斤拷俣锟饺� SPI 锟斤拷锟斤拷锟叫碉拷锟斤拷锟捷碉拷锟斤拷锟斤拷锟斤拷锟斤拷 */
		if (i >= write_size) {
			*read_buf++ = read_data;
		}
	}

exit:
	GPIO_Pins_Set(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin);

    return result;
}

static void spi_lock(const sfud_spi *spi) {
	if (spi->user_data)
    	xSemaphoreTake((SemaphoreHandle_t)spi->user_data, portMAX_DELAY);

	/* Отключение ADC канала PB0 перед SPI операцией */
    adc_manager_disable_pb0_channel();
    pb0_spi_nss_active = true;

	// DEBUG_PRINTF("[SPI] Locked\r\n");
}

static void spi_unlock(const sfud_spi *spi) {
	/* Включение ADC канала PB0 после SPI операции */
    pb0_spi_nss_active = false;
    adc_manager_enable_pb0_channel();

	// DEBUG_PRINTF("[SPI] Unlocked\r\n");

	if (spi->user_data)
    	xSemaphoreGive((SemaphoreHandle_t)spi->user_data);
}

/* about 100 microsecond delay using DWT cycle counter */
static void retry_delay_100us(void) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemClockFrequency * 100) / 1000000UL;  /* 100us в циклах */
    while ((DWT->CYCCNT - start) < cycles);
}

sfud_err sfud_spi_port_init(sfud_flash *flash) {
    sfud_err result = SFUD_SUCCESS;

	assert(!strcmp(flash->spi.name, "SPI1"));

    /**
     * add your port spi bus and device object initialize code like this:
     * 1. rcc initialize
     * 2. gpio initialize
     * 3. spi device initialize
     * 4. flash->spi and flash->retry item initialize
     *    flash->spi.wr = spi_write_read; //Required
     *    flash->spi.qspi_read = qspi_read; //Required when QSPI mode enable
     *    flash->spi.lock = spi_lock;
     *    flash->spi.unlock = spi_unlock;
     *    flash->spi.user_data = &spix;
     *    flash->retry.delay = null;
     *    flash->retry.times = 10000; //Required
     */

	mutex = xSemaphoreCreateBinaryStatic(&xMutexBuffer);
	xSemaphoreGive(mutex);
#ifdef DEBUG
	vQueueAddToRegistry(mutex, "SPI Mutex");
#endif

	/* Включение тактирования SPI1, AFIO и GPIOB */
	RCC_APB2_Peripheral_Clock_Enable(RCC_APB2_PERIPH_SPI1 | RCC_APB2_PERIPH_AFIO);
	RCC_AHB_Peripheral_Clock_Enable(RCC_AHB_PERIPH_GPIOB);

	/* Сброс SPI1 */
	SPI_I2S_Reset(SPI1);

	/* Настройка GPIO: PB3-SCK, PB4-MISO, PB5-MOSI как AF PP, PB0-CS как выход */
	GPIO_InitType GPIO_InitStruct;

	/* SCK (PB3) - AF push-pull */
	GPIO_InitStruct.Pin           = SPI1_SCK_PIN;
	GPIO_InitStruct.GPIO_Mode     = GPIO_MODE_AF_PP;
	GPIO_InitStruct.GPIO_Slew_Rate = GPIO_SLEW_RATE_FAST;
	GPIO_InitStruct.GPIO_Current  = GPIO_DS_12MA;
	GPIO_InitStruct.GPIO_Alternate = SPI1_SCK_AF;
	GPIO_Peripheral_Initialize(SPI1_SCK_PORT, &GPIO_InitStruct);

	/* MOSI (PB5) - AF push-pull */
	GPIO_InitStruct.Pin           = SPI1_MOSI_PIN;
	GPIO_InitStruct.GPIO_Alternate = SPI1_MOSI_AF;
	GPIO_Peripheral_Initialize(SPI1_MOSI_PORT, &GPIO_InitStruct);

	/* MISO (PB4) - AF input для мастера */
	GPIO_InitStruct.Pin           = SPI1_MISO_PIN;
	GPIO_InitStruct.GPIO_Alternate = SPI1_MISO_AF;
	GPIO_Peripheral_Initialize(SPI1_MISO_PORT, &GPIO_InitStruct);

	/* CS (PB0) - программный GPIO выход, HIGH (неактивен) */
	/* Настраивается отдельно каждый раз в lock/unlock*/

	/* Настройка SPI1: мастер, full-duplex, 8 бит, CPOL=0, CPHA=0, MSB, NSS программный */
	SPI_InitType SPI_InitStruct;
	SPI_Initializes_Structure(&SPI_InitStruct);
	SPI_InitStruct.DataDirection = SPI_DIR_DOUBLELINE_FULLDUPLEX;
	SPI_InitStruct.SpiMode       = SPI_MODE_MASTER;
	SPI_InitStruct.DataLen       = SPI_DATA_SIZE_8BITS;
	SPI_InitStruct.CLKPOL        = SPI_CLKPOL_LOW;
	SPI_InitStruct.CLKPHA        = SPI_CLKPHA_FIRST_EDGE;
	SPI_InitStruct.NSS           = SPI_NSS_SOFT;
	SPI_InitStruct.BaudRatePres  = SPI_BR_PRESCALER_8;
	SPI_InitStruct.FirstBit      = SPI_FB_MSB;
	SPI_Initializes(SPI1, &SPI_InitStruct);

	// Жуткий костыль и шаманство. Без этого не работает
	SPI_Set_Nss_Level(SPI1, SPI_NSS_HIGH);

	/* Включение SPI1 */
	SPI_ON(SPI1);

	/* 同锟斤拷 Flash 锟斤拷植锟斤拷锟斤拷慕涌诩锟斤拷锟斤拷锟� */
	flash->spi.wr = spi_write_read;
	flash->spi.lock = spi_lock;
	flash->spi.unlock = spi_unlock;
	flash->spi.user_data = (void *)mutex;
	/* about 100 microsecond delay */
	flash->retry.delay = retry_delay_100us;
	/* adout 5 seconds timeout */
	flash->retry.times = 5 * 10000;

    return result;
}