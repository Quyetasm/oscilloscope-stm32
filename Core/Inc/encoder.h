/**
 * @file    encoder.h
 * @brief   Software handling of two EC11 rotary encoders with push buttons.
 *
 * Pin map (do not change):
 *   ENC1 (Time/div): PB3=A, PB4=B, PB5=SW (active LOW, internal pull-up)
 *   ENC2 (Volt/div): PB6=A, PB7=B, PB8=SW (active LOW, internal pull-up)
 *
 * Decoding uses a 2-bit gray-code state machine with a per-pin shift register
 * for debouncing. Encoder_Update() must be polled at a steady rate (~1 ms).
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Encoder pin assignment (PORTB) ----------------------------------- */
#define ENC_PORT        GPIOB
#define ENC1_A_PIN      GPIO_PIN_3
#define ENC1_B_PIN      GPIO_PIN_4
#define ENC1_SW_PIN     GPIO_PIN_5
#define ENC2_A_PIN      GPIO_PIN_6
#define ENC2_B_PIN      GPIO_PIN_7
#define ENC2_SW_PIN     GPIO_PIN_8

/* ---- Timing constants (in Encoder_Update tick units, ~1 ms) ----------- */
#define ENC_BTN_DEBOUNCE_MS   50U    /**< Button debounce window. */
#define ENC_BTN_HOLD_MS       500U   /**< Threshold for "held".   */

/**
 * @brief Runtime state of a single encoder.
 */
typedef struct {
    int32_t  position;      /**< Accumulated detent position.            */
    int32_t  delta;         /**< Change since last Encoder_GetDelta().   */
    uint8_t  btn_pressed;   /**< 1 while button held (debounced).        */
    uint8_t  btn_held;      /**< 1 once held longer than ENC_BTN_HOLD_MS.*/
    uint32_t btn_time;      /**< HAL_GetTick() at press instant.         */
} Encoder_t;

extern Encoder_t enc1;   /**< Time/div encoder. */
extern Encoder_t enc2;   /**< Volt/div encoder. */

/** @brief Configure encoder GPIO pins and reset state. */
void Encoder_Init(void);

/** @brief Sample both encoders; call at a steady ~1 ms cadence. */
void Encoder_Update(void);

/**
 * @brief  Atomically read and clear the accumulated rotation delta.
 * @param  e encoder to query.
 * @return signed detent count since the previous call.
 */
int32_t Encoder_GetDelta(Encoder_t *e);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_H */
