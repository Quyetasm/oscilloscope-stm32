/**
 * @file    encoder.c
 * @brief   EC11 quadrature decoding and button debouncing.
 *
 * Each encoder keeps a 4-bit history of its (A<<1|B) gray-code state. On every
 * update the new 2-bit state is shifted in and the two most recent states form
 * a 4-bit index into encoder_table[] which yields -1, 0 or +1. Accumulated
 * quarter-steps are divided down to one count per mechanical detent (4 steps).
 */
#include "encoder.h"

Encoder_t enc1;
Encoder_t enc2;

/**
 * @brief Quadrature transition table indexed by (prev<<2 | curr).
 *
 * Valid single-step transitions yield +/-1; invalid or no-change yield 0.
 */
static const int8_t encoder_table[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

/** Steps accumulated per detent (EC11 emits 4 quadrature steps per click). */
#define ENC_STEPS_PER_DETENT  4

/**
 * @brief Per-encoder internal decoding context (not exposed in the header).
 */
typedef struct {
    Encoder_t *pub;        /**< Public state structure.                */
    GPIO_TypeDef *port;    /**< GPIO port for A/B/SW.                  */
    uint16_t a_pin;        /**< Channel A pin mask.                    */
    uint16_t b_pin;        /**< Channel B pin mask.                   */
    uint16_t sw_pin;       /**< Switch pin mask (active LOW).         */
    uint8_t  ab_state;     /**< Last 4-bit (prev<<2|curr) gray index. */
    int8_t   sub_count;    /**< Quarter-step accumulator.             */
    uint8_t  sw_shift;     /**< Debounce shift register for button.   */
    uint8_t  sw_stable;    /**< Last debounced button level.          */
} EncCtx_t;

static EncCtx_t ctx1;
static EncCtx_t ctx2;

/** @brief Read raw (A<<1|B) gray code for one encoder. */
static uint8_t enc_read_ab(const EncCtx_t *c)
{
    uint8_t a = (HAL_GPIO_ReadPin(c->port, c->a_pin) == GPIO_PIN_SET) ? 1U : 0U;
    uint8_t b = (HAL_GPIO_ReadPin(c->port, c->b_pin) == GPIO_PIN_SET) ? 1U : 0U;
    return (uint8_t)((a << 1) | b);
}

/** @brief Initialise one decoding context. */
static void enc_ctx_init(EncCtx_t *c, Encoder_t *pub, GPIO_TypeDef *port,
                         uint16_t a, uint16_t b, uint16_t sw)
{
    c->pub = pub;
    c->port = port;
    c->a_pin = a;
    c->b_pin = b;
    c->sw_pin = sw;
    c->ab_state = enc_read_ab(c);
    c->sub_count = 0;
    c->sw_shift = 0xFFU;   /* released = HIGH (pull-up) */
    c->sw_stable = 1U;     /* 1 = released */

    pub->position = 0;
    pub->delta = 0;
    pub->btn_pressed = 0U;
    pub->btn_held = 0U;
    pub->btn_time = 0U;
}

/** @brief Decode rotation and button for a single encoder. */
static void enc_update_one(EncCtx_t *c)
{
    /* ---- Rotation ---- */
    uint8_t curr = enc_read_ab(c);
    c->ab_state = (uint8_t)(((c->ab_state << 2) | curr) & 0x0FU);
    int8_t step = encoder_table[c->ab_state];
    if (step != 0) {
        c->sub_count = (int8_t)(c->sub_count + step);
        if (c->sub_count >= ENC_STEPS_PER_DETENT) {
            c->sub_count = (int8_t)(c->sub_count - ENC_STEPS_PER_DETENT);
            c->pub->position++;
            c->pub->delta++;
        } else if (c->sub_count <= -ENC_STEPS_PER_DETENT) {
            c->sub_count = (int8_t)(c->sub_count + ENC_STEPS_PER_DETENT);
            c->pub->position--;
            c->pub->delta--;
        }
    }

    /* ---- Button (active LOW) with shift-register debounce ---- */
    uint8_t raw = (HAL_GPIO_ReadPin(c->port, c->sw_pin) == GPIO_PIN_SET) ? 1U : 0U;
    c->sw_shift = (uint8_t)((c->sw_shift << 1) | raw);

    /* All ones -> stable released; all zeros -> stable pressed. */
    if (c->sw_shift == 0xFFU && c->sw_stable == 0U) {
        c->sw_stable = 1U;                 /* released */
        c->pub->btn_pressed = 0U;
        c->pub->btn_held = 0U;
    } else if (c->sw_shift == 0x00U && c->sw_stable == 1U) {
        c->sw_stable = 0U;                 /* pressed */
        c->pub->btn_pressed = 1U;
        c->pub->btn_time = HAL_GetTick();
    }

    /* Held detection while pressed. */
    if (c->pub->btn_pressed && !c->pub->btn_held) {
        if ((HAL_GetTick() - c->pub->btn_time) >= ENC_BTN_HOLD_MS) {
            c->pub->btn_held = 1U;
        }
    }
}

void Encoder_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* A/B channels and switches: inputs with pull-ups (open detents -> HIGH). */
    gpio.Pin   = ENC1_A_PIN | ENC1_B_PIN | ENC1_SW_PIN |
                 ENC2_A_PIN | ENC2_B_PIN | ENC2_SW_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(ENC_PORT, &gpio);

    enc_ctx_init(&ctx1, &enc1, ENC_PORT, ENC1_A_PIN, ENC1_B_PIN, ENC1_SW_PIN);
    enc_ctx_init(&ctx2, &enc2, ENC_PORT, ENC2_A_PIN, ENC2_B_PIN, ENC2_SW_PIN);
}

void Encoder_Update(void)
{
    enc_update_one(&ctx1);
    enc_update_one(&ctx2);
}

int32_t Encoder_GetDelta(Encoder_t *e)
{
    int32_t d;
    __disable_irq();
    d = e->delta;
    e->delta = 0;
    __enable_irq();
    return d;
}

/*
 * Poll the encoders from the 1 ms SysTick interrupt by overriding the weak
 * HAL_IncTick(). This guarantees a steady sample rate even while the main loop
 * is busy streaming a full frame to the display over SPI -- otherwise the
 * polling stalls during rendering and quadrature edges are missed, making the
 * encoders feel dead or erratic. Assumes the default 1 kHz tick.
 */
void HAL_IncTick(void)
{
    extern volatile uint32_t uwTick;
    uwTick++;
    Encoder_Update();
}
