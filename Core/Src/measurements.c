/**
 * @file    measurements.c
 * @brief   Vpp / Vmax / Vmin / DC / RMS / frequency from a raw ADC buffer.
 */
#include <stddef.h>
#include <math.h>
#include "measurements.h"
#include "adc_dma.h"

/** Hysteresis as a fraction of Vpp used to reject noise on zero-crossings. */
#define MEAS_HYSTERESIS_FRAC   0.05f

/** @brief Convert a raw ADC sample to an input-referred voltage. */
static inline float meas_raw_to_volt(uint16_t raw, float v_scale, float v_offset)
{
    return (ADC_RAW_TO_VOLT(raw) - v_offset) * v_scale;
}

void Measurements_Calculate(const uint16_t *buf, uint32_t len,
                            float v_scale, float v_offset,
                            Measurements_t *out)
{
    if (buf == NULL || out == NULL || len == 0U) {
        if (out != NULL) {
            out->vpp = out->vmax = out->vmin = 0.0f;
            out->vdc = out->vrms = out->freq_hz = out->period_ms = 0.0f;
        }
        return;
    }

    /* ---- First pass: min / max / sum / sum-of-squares (in volts) -------- */
    uint16_t raw_min = buf[0];
    uint16_t raw_max = buf[0];
    double   sum  = 0.0;
    double   sumsq = 0.0;

    for (uint32_t i = 0U; i < len; i++) {
        uint16_t r = buf[i];
        if (r < raw_min) { raw_min = r; }
        if (r > raw_max) { raw_max = r; }
        float v = meas_raw_to_volt(r, v_scale, v_offset);
        sum   += (double)v;
        sumsq += (double)v * (double)v;
    }

    out->vmax = meas_raw_to_volt(raw_max, v_scale, v_offset);
    out->vmin = meas_raw_to_volt(raw_min, v_scale, v_offset);
    out->vpp  = out->vmax - out->vmin;
    out->vdc  = (float)(sum / (double)len);
    out->vrms = (float)sqrt(sumsq / (double)len);

    /* ---- Second pass: mean-crossing counting with hysteresis ----------- */
    float mean_v = out->vdc;
    float hyst   = out->vpp * MEAS_HYSTERESIS_FRAC * 0.5f;

    uint32_t crossings = 0U;
    int      state     = 0;     /* -1 below low band, +1 above high band, 0 unknown */
    int32_t  first_idx = -1;
    int32_t  last_idx  = -1;

    for (uint32_t i = 0U; i < len; i++) {
        float v = meas_raw_to_volt(buf[i], v_scale, v_offset);
        if (v > mean_v + hyst) {
            if (state == -1) {
                crossings++;            /* rising crossing of the mean */
                if (first_idx < 0) { first_idx = (int32_t)i; }
                last_idx = (int32_t)i;
            }
            state = 1;
        } else if (v < mean_v - hyst) {
            if (state == 1) {
                crossings++;            /* falling crossing of the mean */
                if (first_idx < 0) { first_idx = (int32_t)i; }
                last_idx = (int32_t)i;
            }
            state = -1;
        }
    }

    /* ---- Frequency / period -------------------------------------------- */
    uint32_t fs = adc_sample_rate;
    out->freq_hz   = 0.0f;
    out->period_ms = 0.0f;

    if (fs > 0U && crossings >= 2U && last_idx > first_idx) {
        /* Use the span between the first and last detected crossing for a more
         * stable estimate than len/crossings. Each full period == 2 crossings. */
        float full_cycles = (float)(crossings - 1U) / 2.0f;
        float span_samples = (float)(last_idx - first_idx);
        if (full_cycles > 0.0f && span_samples > 0.0f) {
            float period_samples = span_samples / full_cycles;
            out->freq_hz   = (float)fs / period_samples;
            out->period_ms = 1000.0f / out->freq_hz;
        }
    }
}
