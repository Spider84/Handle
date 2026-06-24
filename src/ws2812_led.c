/**
 * @file ws2812_led.c
 */

#include "ws2812e_dma.h"
#include "ws2812_led.h"
#include "debug.h"

/* FreeRTOS Static Allocation */
static StaticTask_t xTaskBuffer;
static StackType_t xStack[configMINIMAL_STACK_SIZE * 2];
static SemaphoreHandle_t xMutex;
static StaticSemaphore_t xMutexBuffer;

typedef enum {
    LED_MODE_STATIC,
    LED_MODE_BLINK
} led_mode_t;

static struct {
    uint32_t interval;
    uint32_t duration;
    uint32_t count;
    uint32_t remaining;
    led_mode_t mode;
    ws2812e_dma_color_t color;
} led_state;

static void WS2812_Task(void *pvParameters) {
    (void)pvParameters;

    ws2812e_dma_init();

    while (1) {
        xSemaphoreTake(xMutex, portMAX_DELAY);

        if (led_state.mode == LED_MODE_STATIC) {
            // DEBUG_RTT_printf(0, "[%lu] WS2812: LED ON\n", xTaskGetTickCount());
            ws2812e_dma_set_color(led_state.color);
            xSemaphoreGive(xMutex);
            vTaskDelay(pdMS_TO_TICKS(100)); // Low freq update
        }
        else if (led_state.mode == LED_MODE_BLINK) {
            if (led_state.count == 0 || led_state.remaining > 0) {
                // DEBUG_RTT_printf(0, "[%lu] WS2812: LED BLINK\n", xTaskGetTickCount());
                ws2812e_dma_set_color(led_state.color);
                xSemaphoreGive(xMutex);
                vTaskDelay(pdMS_TO_TICKS(led_state.duration));

                xSemaphoreTake(xMutex, portMAX_DELAY);
                // DEBUG_RTT_printf(0, "[%lu] WS2812: LED OFF\n", xTaskGetTickCount());
                ws2812e_dma_set_color(COLOR_OFF);

                if (led_state.count > 0) led_state.remaining--;

                xSemaphoreGive(xMutex);

                if (led_state.remaining>0 && led_state.interval>led_state.duration)
                {
                	vTaskDelay(pdMS_TO_TICKS(led_state.interval - led_state.duration));
                }
            } else {
                led_state.mode = LED_MODE_STATIC;
                led_state.color = COLOR_OFF;
                // DEBUG_RTT_printf(0, "[%lu] WS2812: LED OFF\n", xTaskGetTickCount());
                ws2812e_dma_set_color(led_state.color);
                xSemaphoreGive(xMutex);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}

void WS2812_Init(void) {
    xMutex = xSemaphoreCreateBinaryStatic(&xMutexBuffer);
    xSemaphoreGive(xMutex);
#ifdef DEBUG
	vQueueAddToRegistry(xMutex, "LED");
#endif

    xTaskCreateStatic(WS2812_Task, "WS2812", sizeof(xStack)/sizeof(xStack[0]),
                      NULL, configMAX_PRIORITIES - 2, xStack, &xTaskBuffer);

    DEBUG_PRINTF("WS2812: Module Initialized\n");
}

void WS2812_SetColor(ws2812e_dma_color_t rgb) {
    if (xTaskGetSchedulerState()==taskSCHEDULER_RUNNING)
        xSemaphoreTake(xMutex, portMAX_DELAY);
    led_state.color = rgb;
    led_state.mode = LED_MODE_STATIC;
    if (xTaskGetSchedulerState()==taskSCHEDULER_RUNNING)
        xSemaphoreGive(xMutex);
    DEBUG_PRINTF("WS2812: Static Color 0x%06X\n", rgb.dec & 0xFFFFFF);
}

void WS2812_Blink(ws2812e_dma_color_t rgb, uint32_t interval_ms, uint32_t duration_ms, uint32_t count) {
    if (xTaskGetSchedulerState()==taskSCHEDULER_RUNNING)
        xSemaphoreTake(xMutex, portMAX_DELAY);
    led_state.color = rgb;
    led_state.interval = interval_ms;
    led_state.duration = (duration_ms < interval_ms) ? duration_ms : (interval_ms / 2);
    led_state.count = count;
    led_state.remaining = count;
    led_state.mode = LED_MODE_BLINK;
    if (xTaskGetSchedulerState()==taskSCHEDULER_RUNNING)
        xSemaphoreGive(xMutex);
    DEBUG_PRINTF("WS2812: Blink Mode Start, Color 0x%06X, Count %d\n", rgb, count);
}

void WS2812_Off(void) {
    WS2812_SetColor(COLOR_OFF);
}
