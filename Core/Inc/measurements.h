/**
 * @file    measurements.h
 * @brief   Automatic signal measurements over a captured buffer.
 */
#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Computed signal measurements (all voltages referred to the input). */
typedef struct {
    float vpp;       /**< Peak-to-peak (max - min).      */
    float vmax;      /**< Maximum.                       */
    float vmin;      /**< Minimum.                       */
    float vdc;       /**< Mean (DC component).           */
    float vrms;      /**< RMS value.                     */
    float freq_hz;   /**< Frequency (zero-crossing).     */
    float period_ms; /**< Period in milliseconds.        */
} Measurements_t;

/**
 * @brief  Compute measurements for a raw ADC buffer.
 * @param  buf       sample buffer (ADC counts).
 * @param  len       sample count.
 * @param  v_scale   channel gain: input volts per pin volt (incl. attenuator).
 * @param  v_offset  pin bias subtracted to centre the signal (e.g. 1.65 V).
 * @param  out       result structure (filled by this function).
 *
 * Frequency is estimated by counting crossings of the signal mean with a 5%
 * (of Vpp) hysteresis band to reject noise; the period derives from the sample
 * rate exposed by the ADC module.
 */
void Measurements_Calculate(const uint16_t *buf, uint32_t len,
                            float v_scale, float v_offset,
                            Measurements_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENTS_H */
