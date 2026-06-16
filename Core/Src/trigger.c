/**
 * @file    trigger.c
 * @brief   Software level-trigger search.
 */
#include <stddef.h>
#include "trigger.h"
#include "adc_dma.h"

Trigger_t trigger;

void Trigger_Init(void)
{
    trigger.mode        = TRIG_AUTO;
    trigger.edge        = TRIG_RISING;
    trigger.level_volts = ADC_OFFSET_V;                 /* mid-scale (1.65 V) */
    trigger.level_raw   = ADC_VOLT_TO_RAW(ADC_OFFSET_V);
    trigger.position    = -1;
    trigger.triggered   = 0U;
    trigger.timeout_ms  = TRIG_AUTO_TIMEOUT_MS;
}

int32_t Trigger_Find(const uint16_t *buf, uint32_t len)
{
    if (buf == NULL || len <= (uint32_t)TRIG_PRE_SAMPLES) {
        return -1;
    }

    uint16_t level = trigger.level_raw;

    for (uint32_t i = (uint32_t)TRIG_PRE_SAMPLES; i < len; i++) {
        uint16_t prev = buf[i - 1U];
        uint16_t curr = buf[i];

        if (trigger.edge == TRIG_RISING) {
            if (prev < level && curr >= level) {
                return (int32_t)i;
            }
        } else { /* TRIG_FALLING */
            if (prev > level && curr <= level) {
                return (int32_t)i;
            }
        }
    }
    return -1;
}
