/**
 * @file    adc_dma.h
 * @brief   Signal capture via ADC1 + DMA2 with ping-pong double buffering.
 *
 * The ADC is triggered by TIM3 TRGO (update event); DMA2 Stream0 Channel0
 * moves conversions into two alternating buffers. The half-transfer and
 * transfer-complete interrupts mark which buffer is ready.
 *
 * Pin: PA1 -> ADC1_IN1 (analog front-end biases the signal to 1.65 V).
 */
#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Capture buffers -------------------------------------------------- */
#define ADC_BUF_SIZE    2048U   /**< Samples per buffer. */
#define ADC_BUF_COUNT   2U      /**< Ping-pong buffer count. */

/* ---- Shared ADC <-> volts model (reused project-wide) ----------------- */
#define ADC_VREF        3.3f    /**< ADC reference voltage. */
#define ADC_MAX         4095    /**< 12-bit full scale.     */
#define ADC_MID         2048    /**< Mid-code (~VREF/2).    */
#define ADC_OFFSET_V    1.65f   /**< Front-end bias (VREF/2). */

/** @brief Convert a raw ADC code to volts at the ADC pin. */
#define ADC_RAW_TO_VOLT(raw)   (((float)(raw) / (float)ADC_MAX) * ADC_VREF)
/** @brief Convert a voltage at the ADC pin to the nearest raw code. */
#define ADC_VOLT_TO_RAW(v)     ((uint16_t)(((v) / ADC_VREF) * (float)ADC_MAX + 0.5f))

/*
 * NOTE: On STM32F4 the DMA controllers CANNOT access CCMRAM. The task asked for
 * these buffers in CCMRAM, but that would silently break DMA transfers, so they
 * are deliberately placed in normal SRAM (.bss). See adc_dma.c.
 */
extern uint16_t adc_buf[ADC_BUF_COUNT][ADC_BUF_SIZE];
extern volatile uint8_t adc_buf_ready;     /**< Index (0/1) of the filled buffer. */
extern volatile uint8_t adc_capture_done;  /**< Set when a fresh buffer is ready. */

/** Last sample rate programmed via ADC_SetSampleRate(), in Hz. */
extern volatile uint32_t adc_sample_rate;

/** @brief Initialise ADC1, DMA2 Stream0 and TIM3 (capture stays stopped). */
void ADC_DMA_Init(void);

/**
 * @brief  Program the sample clock (TIM3) for the requested rate.
 * @param  rate_hz desired sample rate in Hz.
 */
void ADC_SetSampleRate(uint32_t rate_hz);

/** @brief Start continuous (circular) DMA capture. */
void ADC_StartCapture(void);

/** @brief Stop DMA capture. */
void ADC_StopCapture(void);

#ifdef __cplusplus
}
#endif

#endif /* ADC_DMA_H */
