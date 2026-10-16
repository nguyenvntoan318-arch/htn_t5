#include <stdint.h>

#include "stm32f103xb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* STM32F103C8T6 - t5b1
 * LED1: PA0 / TIM2_CH1
 * LED2: PA1 / TIM2_CH2
 * LED3: PA2 / TIM2_CH3
 * SELECT: PB12, UP: PB13, DOWN: PB14 (active-low)
 */

#define LED_COUNT          3U
#define BUTTON_SELECT      12U
#define BUTTON_UP          13U
#define BUTTON_DOWN        14U
#define PWM_PERIOD         999U
#define PWM_PRESCALER      71U
#define BRIGHTNESS_STEP    10U
#define DEBOUNCE_SAMPLES   3U
#define BUTTON_SAMPLE_MS   10U

uint32_t SystemCoreClock = 8000000U;

static QueueHandle_t ledQueue[LED_COUNT];

static void Clock_Init(void);
static void GPIO_Init(void);
static void PWM_Init(void);
static uint8_t Button_IsPressed(uint32_t pin);
static void LED_SetBrightness(uint8_t led, uint8_t brightness);
static void LED_Task_Common(uint8_t led, QueueHandle_t queue);
static void LED1_Task(void *argument);
static void LED2_Task(void *argument);
static void LED3_Task(void *argument);
static void Button_Task(void *argument);

void SystemInit(void)
{
    /* Clock is configured in Clock_Init() before starting FreeRTOS. */
}

void SystemCoreClockUpdate(void)
{
    /* SystemCoreClock is updated in Clock_Init(). */
}

static void Clock_Init(void)
{
    /* Enable HSE (8 MHz). */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    /* Flash prefetch and two wait states for 72 MHz. */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /* AHB /1, APB1 /2, APB2 /1, PLL source HSE, PLL x9. */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }

    SystemCoreClock = 72000000U;
}

static void GPIO_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;

    /* PA0, PA1, PA2: alternate-function push-pull, 50 MHz (MODE=11, CNF=10). */
    GPIOA->CRL &= ~((0xFU << 0U) | (0xFU << 4U) | (0xFU << 8U));
    GPIOA->CRL |=  ((0xBU << 0U) | (0xBU << 4U) | (0xBU << 8U));

    /* PB12, PB13, PB14: input with pull-up/pull-down (CNF=10, MODE=00). */
    GPIOB->CRH &= ~((0xFU << 16U) | (0xFU << 20U) | (0xFU << 24U));
    GPIOB->CRH |=  ((0x8U << 16U) | (0x8U << 20U) | (0x8U << 24U));

    /* ODR=1 selects pull-up. */
    GPIOB->ODR |= (1U << BUTTON_SELECT) | (1U << BUTTON_UP) | (1U << BUTTON_DOWN);
}

static void PWM_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->CR1 = 0U;
    TIM2->PSC = PWM_PRESCALER; /* 72 MHz / (71+1) = 1 MHz */
    TIM2->ARR = PWM_PERIOD;    /* 1 MHz / (999+1) = 1 kHz */

    /* PWM mode 1 and preload enable for CH1 and CH2. */
    TIM2->CCMR1 &= ~(TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE |
                     TIM_CCMR1_OC2M | TIM_CCMR1_OC2PE);
    TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE |
                   TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2PE;

    /* PWM mode 1 and preload enable for CH3. */
    TIM2->CCMR2 &= ~(TIM_CCMR2_OC3M | TIM_CCMR2_OC3PE);
    TIM2->CCMR2 |= TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3PE;

    /* Active-high outputs; enable channels 1, 2, 3. */
    TIM2->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC2P | TIM_CCER_CC3P);
    TIM2->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E;

    TIM2->CCR1 = 0U;
    TIM2->CCR2 = 0U;
    TIM2->CCR3 = 0U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_ARPE;
    TIM2->CR1 |= TIM_CR1_CEN;
}

/* Returns 1 while pressed; buttons connect their pin to GND when pressed. */
static uint8_t Button_IsPressed(uint32_t pin)
{
    return ((GPIOB->IDR & (1U << pin)) == 0U) ? 1U : 0U;
}

static void LED_SetBrightness(uint8_t led, uint8_t brightness)
{
    uint32_t compare_value;

    if (brightness > 100U) {
        brightness = 100U;
    }

    compare_value = ((uint32_t)brightness * (PWM_PERIOD + 1U)) / 100U;

    switch (led) {
        case 0U: TIM2->CCR1 = compare_value; break;
        case 1U: TIM2->CCR2 = compare_value; break;
        case 2U: TIM2->CCR3 = compare_value; break;
        default: break;
    }
}

static void LED_Task_Common(uint8_t led, QueueHandle_t queue)
{
    uint8_t brightness = 0U;
    uint8_t new_brightness;

    LED_SetBrightness(led, brightness);

    for (;;) {
        if (xQueueReceive(queue, &new_brightness, portMAX_DELAY) == pdPASS) {
            brightness = new_brightness;
            LED_SetBrightness(led, brightness);
        }
    }
}

static void LED1_Task(void *argument)
{
    (void)argument;
    LED_Task_Common(0U, ledQueue[0]);
}

static void LED2_Task(void *argument)
{
    (void)argument;
    LED_Task_Common(1U, ledQueue[1]);
}

static void LED3_Task(void *argument)
{
    (void)argument;
    LED_Task_Common(2U, ledQueue[2]);
}

static void Button_Task(void *argument)
{
    (void)argument;

    /* LED1 is selected at startup. SELECT advances LED1 -> LED2 -> LED3 -> LED1. */
    uint8_t selected_led = 0U;
    uint8_t brightness[LED_COUNT] = {0U, 0U, 0U};

    uint8_t stable_select = 0U;
    uint8_t stable_up = 0U;
    uint8_t stable_down = 0U;
    uint8_t count_select = 0U;
    uint8_t count_up = 0U;
    uint8_t count_down = 0U;

    for (;;) {
        uint8_t raw_select = Button_IsPressed(BUTTON_SELECT);
        uint8_t raw_up = Button_IsPressed(BUTTON_UP);
        uint8_t raw_down = Button_IsPressed(BUTTON_DOWN);

        /* Debounce SELECT: accept a changed state after 3 consecutive samples. */
        if (raw_select == stable_select) {
            count_select = 0U;
        } else if (++count_select >= DEBOUNCE_SAMPLES) {
            stable_select = raw_select;
            count_select = 0U;
            if (stable_select != 0U) {
                selected_led++;
                if (selected_led >= LED_COUNT) {
                    selected_led = 0U;
                }
            }
        }

        /* Debounce UP; change brightness once on the stable press edge. */
        if (raw_up == stable_up) {
            count_up = 0U;
        } else if (++count_up >= DEBOUNCE_SAMPLES) {
            stable_up = raw_up;
            count_up = 0U;
            if (stable_up != 0U) {
                if (brightness[selected_led] <= (100U - BRIGHTNESS_STEP)) {
                    brightness[selected_led] += BRIGHTNESS_STEP;
                } else {
                    brightness[selected_led] = 100U;
                }
                (void)xQueueOverwrite(ledQueue[selected_led], &brightness[selected_led]);
            }
        }

        /* Debounce DOWN; change brightness once on the stable press edge. */
        if (raw_down == stable_down) {
            count_down = 0U;
        } else if (++count_down >= DEBOUNCE_SAMPLES) {
            stable_down = raw_down;
            count_down = 0U;
            if (stable_down != 0U) {
                if (brightness[selected_led] >= BRIGHTNESS_STEP) {
                    brightness[selected_led] -= BRIGHTNESS_STEP;
                } else {
                    brightness[selected_led] = 0U;
                }
                (void)xQueueOverwrite(ledQueue[selected_led], &brightness[selected_led]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_SAMPLE_MS));
    }
}

/* Required when configCHECK_FOR_STACK_OVERFLOW is enabled in FreeRTOSConfig.h. */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

int main(void)
{
    Clock_Init();
    GPIO_Init();
    PWM_Init();

    ledQueue[0] = xQueueCreate(1U, sizeof(uint8_t));
    ledQueue[1] = xQueueCreate(1U, sizeof(uint8_t));
    ledQueue[2] = xQueueCreate(1U, sizeof(uint8_t));

    if ((ledQueue[0] == NULL) || (ledQueue[1] == NULL) || (ledQueue[2] == NULL)) {
        for (;;) {
        }
    }

    if (xTaskCreate(LED1_Task, "LED1", 128U, NULL, 2U, NULL) != pdPASS ||
        xTaskCreate(LED2_Task, "LED2", 128U, NULL, 2U, NULL) != pdPASS ||
        xTaskCreate(LED3_Task, "LED3", 128U, NULL, 2U, NULL) != pdPASS ||
        xTaskCreate(Button_Task, "Buttons", 192U, NULL, 3U, NULL) != pdPASS) {
        for (;;) {
        }
    }

    vTaskStartScheduler();
    for (;;) {
    }
}
