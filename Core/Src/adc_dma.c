/**
 * @file    adc_dma.c
 * @brief   ADC1 + DMA2 capture engine driven by TIM3 TRGO.
 *
 * TIM3 generates an update event (TRGO) at the desired sample rate; each event
 * starts one ADC1 conversion on channel IN1 (PA1). DMA2 Stream0 Channel0 moves
 * the results into a circular buffer spanning both ping-pong halves. The
 * half/complete IRQs publish which half just filled.
 */
#include "adc_dma.h"

/*
 * Capture buffers in NORMAL SRAM (.bss). DMA on STM32F4 cannot reach CCMRAM,
 * so despite the original request these must NOT be placed there.
 */
uint16_t adc_buf[ADC_BUF_COUNT][ADC_BUF_SIZE];
volatile uint8_t adc_buf_ready    = 0U;
volatile uint8_t adc_capture_done = 0U;
volatile uint32_t adc_sample_rate = 0U;

/** APB1 timer input clock (APB1=50 MHz, x2 for timers => 100 MHz). */
#define ADC_TIM_CLK_HZ   100000000UL

/* Peripheral handles owned by this module. */
static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;
static TIM_HandleTypeDef htim3;

void ADC_SetSampleRate(uint32_t rate_hz)
{
    if (rate_hz == 0U) {
        return;
    }
    uint32_t arr = (ADC_TIM_CLK_HZ / rate_hz);
    if (arr == 0U) {
        arr = 1U;
    }
    arr -= 1U;                 /* ARR = (clk / rate) - 1 */

    htim3.Instance->ARR = arr;
    htim3.Instance->PSC = 0U;  /* full timer clock, max resolution */
    htim3.Instance->EGR = TIM_EGR_UG; /* latch new ARR/PSC */
    adc_sample_rate = rate_hz;
}

/** @brief Configure GPIO PA1 as analog input for ADC1_IN1. */
static void adc_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin  = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/** @brief Configure DMA2 Stream0 Channel0 for ADC1 (circular, half-word). */
static void adc_dma_stream_init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_adc1.Instance                 = DMA2_Stream0;
    hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_CIRCULAR;
    hdma_adc1.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_adc1);

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/** @brief Configure TIM3 as the ADC sample clock (TRGO on update). */
static void adc_timer_init(void)
{
    TIM_MasterConfigTypeDef master = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 0U;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = ADC_TIM_CLK_HZ / 1000000UL - 1U; /* default 1 MHz */
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim3);

    master.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &master);
}

/** @brief Configure ADC1: 12-bit, single conversion triggered by TIM3 TRGO. */
static void adc_peripheral_init(void)
{
    ADC_ChannelConfTypeDef ch = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4; /* 100/4 = 25 MHz */
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.NbrOfConversion       = 1U;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T3_TRGO;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    HAL_ADC_Init(&hadc1);

    ch.Channel      = ADC_CHANNEL_1;          /* PA1 -> ADC1_IN1 */
    ch.Rank         = 1U;
    /* 3 cycles is fast; increase for high source impedance.
     * TODO: verify sampling time vs. analog front-end impedance on hardware. */
    ch.SamplingTime = ADC_SAMPLETIME_3CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &ch);

    HAL_NVIC_SetPriority(ADC_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(ADC_IRQn);
}

void ADC_DMA_Init(void)
{
    adc_gpio_init();
    adc_dma_stream_init();
    adc_timer_init();
    adc_peripheral_init();
    adc_buf_ready    = 0U;
    adc_capture_done = 0U;
}

void ADC_StartCapture(void)
{
    /* Single circular buffer spanning both ping-pong halves; the half/complete
     * callbacks select which half is the freshly completed one. */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE * ADC_BUF_COUNT);
    HAL_TIM_Base_Start(&htim3);
}

void ADC_StopCapture(void)
{
    HAL_TIM_Base_Stop(&htim3);
    HAL_ADC_Stop_DMA(&hadc1);
}

/* ---- Interrupt service routines --------------------------------------- */

/** @brief DMA2 Stream0 ISR -> HAL dispatcher. */
void DMA2_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}

/** @brief ADC1 ISR -> HAL dispatcher. */
void ADC_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&hadc1);
}

/**
 * @brief First half filled (buffer index 0 ready).
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        adc_buf_ready    = 0U;
        adc_capture_done = 1U;
    }
}

/**
 * @brief Second half filled (buffer index 1 ready).
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        adc_buf_ready    = 1U;
        adc_capture_done = 1U;
    }
}
