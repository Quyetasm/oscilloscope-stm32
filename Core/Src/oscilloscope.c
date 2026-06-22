/**
 * @file    oscilloscope.c
 * @brief   Oscilloscope state machine, control tables and acquisition flow.
 */
#include <stddef.h>
#include "oscilloscope.h"
#include "adc_dma.h"
#include "encoder.h"
#include "trigger.h"
#include "display_ui.h"
#include "measurements.h"
#include "ili9488.h"

OscConfig_t osc;

/** Practical ADC ceiling; faster timebases are clamped to this (~2 MSa/s).  */
#define OSC_MAX_SAMPLE_RATE   2000000UL

/** Trigger-level step per encoder detent, in volts. */
#define OSC_TRIG_STEP_V       0.05f

/** Hold time on ENC1 to toggle RUN/STOP (handled via Encoder btn_held). */

/* ----------------------------------------------------------------------- */
/* Control tables                                                           */
/* ----------------------------------------------------------------------- */

/*
 * Timebase table. With one sample per pixel and GRID_DIV_X (40) px/division,
 * the sample rate equals 40 / (Time/div). Rates above OSC_MAX_SAMPLE_RATE are
 * clamped when applied (see osc_apply_timebase) — see README "Limitations".
 */
static const TimebaseEntry_t timebase_tbl[] = {
    { 40000000UL, GRID_DIV_X, "1us"   },
    { 20000000UL, GRID_DIV_X, "2us"   },
    {  8000000UL, GRID_DIV_X, "5us"   },
    {  4000000UL, GRID_DIV_X, "10us"  },
    {  2000000UL, GRID_DIV_X, "20us"  },
    {   800000UL, GRID_DIV_X, "50us"  },
    {   400000UL, GRID_DIV_X, "100us" },
    {   200000UL, GRID_DIV_X, "200us" },
    {    80000UL, GRID_DIV_X, "500us" },
    {    40000UL, GRID_DIV_X, "1ms"   },
    {    20000UL, GRID_DIV_X, "2ms"   },
    {     8000UL, GRID_DIV_X, "5ms"   },
    {     4000UL, GRID_DIV_X, "10ms"  },
    {     2000UL, GRID_DIV_X, "20ms"  },
    {      800UL, GRID_DIV_X, "50ms"  },
    {      400UL, GRID_DIV_X, "100ms" },
    {      200UL, GRID_DIV_X, "200ms" },
    {       80UL, GRID_DIV_X, "500ms" },
    {       40UL, GRID_DIV_X, "1s"    },
};

/*
 * Voltdiv table. scale = pixels-per-ADC-count = GRID_DIV_Y / (counts per div).
 * counts per div = v_per_div * ADC_MAX / ADC_VREF, so
 * scale = GRID_DIV_Y * ADC_VREF / (ADC_MAX * v_per_div).
 * The front-end bias already lands at ADC_MID, so offset_center = 0.
 */
static const VoltdivEntry_t voltdiv_tbl[] = {
    { 0.1f, 0.24170f, 0.0f, "100mV" },
    { 0.2f, 0.12085f, 0.0f, "200mV" },
    { 0.5f, 0.04834f, 0.0f, "500mV" },
    { 1.0f, 0.02417f, 0.0f, "1V"    },
    { 2.0f, 0.01209f, 0.0f, "2V"    },
    { 5.0f, 0.00483f, 0.0f, "5V"    },
};

const uint8_t TIMEBASE_COUNT = (uint8_t)(sizeof(timebase_tbl) / sizeof(timebase_tbl[0]));
const uint8_t VOLTDIV_COUNT  = (uint8_t)(sizeof(voltdiv_tbl) / sizeof(voltdiv_tbl[0]));

const TimebaseEntry_t *Osc_CurrentTimebase(void) { return &timebase_tbl[osc.timebase_idx]; }
const VoltdivEntry_t  *Osc_CurrentVoltdiv(void)  { return &voltdiv_tbl[osc.voltdiv_idx]; }

/* ----------------------------------------------------------------------- */
/* Helpers                                                                  */
/* ----------------------------------------------------------------------- */

/** @brief Input-referred channel gain (attenuator factor). */
static inline float osc_atten_gain(void)
{
    return (osc.attenuator != 0U) ? 10.0f : 1.0f;
}

/** @brief Apply the current timebase sample rate to the ADC (clamped). */
static void osc_apply_timebase(void)
{
    uint32_t rate = timebase_tbl[osc.timebase_idx].sample_rate;
    if (rate > OSC_MAX_SAMPLE_RATE) {
        rate = OSC_MAX_SAMPLE_RATE;
    }
    ADC_SetSampleRate(rate);
}

/** @brief Recompute trigger.level_raw from osc.trig_level (input volts). */
static void osc_apply_trigger_level(void)
{
    float pin_v = osc.trig_level / osc_atten_gain() + ADC_OFFSET_V;
    if (pin_v < 0.0f)      { pin_v = 0.0f; }
    if (pin_v > ADC_VREF)  { pin_v = ADC_VREF; }
    trigger.level_volts = osc.trig_level;
    trigger.level_raw   = ADC_VOLT_TO_RAW(pin_v);
}

/* ----------------------------------------------------------------------- */
/* Initialisation                                                           */
/* ----------------------------------------------------------------------- */

void Oscilloscope_Init(void)
{
    osc.state        = OSC_RUNNING;
    osc.timebase_idx = 9U;   /* 1ms/div */
    osc.voltdiv_idx  = 3U;   /* 1V/div  */
    osc.trig_mode    = TRIG_AUTO;
    osc.trig_edge    = TRIG_RISING;
    osc.trig_level   = 0.0f; /* centre (input-referred) */
    osc.show_meas    = 1U;
    osc.attenuator   = 0U;

    trigger.mode = osc.trig_mode;
    trigger.edge = osc.trig_edge;
    osc_apply_trigger_level();

    UI_Init();
    UI_DrawTriggerLine(trigger.level_raw, Osc_CurrentVoltdiv()->scale);

    osc_apply_timebase();
    ADC_StartCapture();
}

/* ----------------------------------------------------------------------- */
/* Encoder handling                                                         */
/* ----------------------------------------------------------------------- */

void Oscilloscope_HandleEnc1(int32_t delta, uint8_t btn)
{
    static uint8_t prev_btn  = 0U;
    static uint8_t hold_done = 0U;

    /* Rotation: change Time/div. */
    if (delta != 0) {
        int32_t idx = (int32_t)osc.timebase_idx + delta;
        if (idx < 0) { idx = 0; }
        if (idx >= (int32_t)TIMEBASE_COUNT) { idx = (int32_t)TIMEBASE_COUNT - 1; }
        osc.timebase_idx = (uint8_t)idx;
        osc_apply_timebase();
        UI_DrawStatusBar();
    }

    /* Hold: toggle RUN/STOP once per hold. */
    if (btn && enc1.btn_held && !hold_done) {
        hold_done = 1U;
        if (osc.state == OSC_RUNNING) {
            osc.state = OSC_STOPPED;
            ADC_StopCapture();
        } else {
            osc.state = OSC_RUNNING;
            ADC_StartCapture();
        }
        UI_DrawStatusBar();
    }

    /* Release: short click cycles trigger mode (AUTO->NORMAL->SINGLE->AUTO). */
    if (prev_btn && !btn) {
        if (!hold_done) {
            osc.trig_mode = (TriggerMode_t)((osc.trig_mode + 1) % 3);
            trigger.mode = osc.trig_mode;
            if (osc.state == OSC_STOPPED) {
                osc.state = OSC_RUNNING;
                ADC_StartCapture();
            }
            UI_DrawStatusBar();
        }
        hold_done = 0U;
    }
    prev_btn = btn;
}

void Oscilloscope_HandleEnc2(int32_t delta, uint8_t btn)
{
    static uint8_t prev_btn = 0U;
    static uint8_t used_mod = 0U;

    if (delta != 0) {
        if (btn) {
            /* Button held while rotating: adjust trigger level. */
            osc.trig_level += (float)delta * OSC_TRIG_STEP_V;
            osc_apply_trigger_level();
            used_mod = 1U;
            UI_DrawTriggerLine(trigger.level_raw, Osc_CurrentVoltdiv()->scale);
        } else {
            /* Normal rotation: change Volt/div. */
            int32_t step = (delta > 0) ? 1 : -1;
            int32_t idx = (int32_t)osc.voltdiv_idx + step;
            if (idx < 0) { idx = 0; }
            if (idx >= (int32_t)VOLTDIV_COUNT) { idx = (int32_t)VOLTDIV_COUNT - 1; }
            osc.voltdiv_idx = (uint8_t)idx;
            osc_apply_trigger_level();
            UI_DrawVoltLabel();
            UI_DrawStatusBar();
        }
    }

    /* Release without having been used as a modifier: cycle trigger edge. */
    if (prev_btn && !btn) {
        if (!used_mod) {
            osc.trig_edge = (osc.trig_edge == TRIG_RISING) ? TRIG_FALLING : TRIG_RISING;
            trigger.edge = osc.trig_edge;
            UI_DrawStatusBar();
        }
        used_mod = 0U;
    }
    prev_btn = btn;
}

/* ----------------------------------------------------------------------- */
/* Acquisition / render loop                                                */
/* ----------------------------------------------------------------------- */

/** @brief Process one freshly captured buffer and refresh the display. */
static void osc_process_frame(void)
{
    uint8_t ready;
    __disable_irq();
    ready = adc_buf_ready;
    adc_capture_done = 0U;
    __enable_irq();

    const uint16_t *buf = adc_buf[ready];
    const uint32_t  len = ADC_BUF_SIZE;

    /* Trigger search. */
    int32_t trig_pos;
    uint8_t draw = 1U;
    if (osc.trig_mode == TRIG_AUTO) {
        trig_pos = Trigger_Find(buf, len);
        if (trig_pos < 0) {
            trig_pos = (int32_t)(len / 2U); /* free-run on timeout */
        }
        trigger.triggered = 1U;
    } else { /* NORMAL or SINGLE */
        trig_pos = Trigger_Find(buf, len);
        if (trig_pos < 0) {
            trigger.triggered = 0U;
            draw = 0U;               /* hold previous frame */
        } else {
            trigger.triggered = 1U;
        }
    }
    trigger.position = trig_pos;

    if (!draw) {
        return;
    }

    const VoltdivEntry_t *vd = Osc_CurrentVoltdiv();

    /* Measurements. */
    Measurements_Calculate(buf, len, osc_atten_gain(), ADC_OFFSET_V, &osc.meas);

    /* Waveform + overlays. */
    UI_DrawWaveform(buf, len, trig_pos, vd->scale, vd->offset_center);
    UI_DrawTriggerLine(trigger.level_raw, vd->scale);

    if (osc.show_meas) {
        float m[3] = { osc.meas.vpp, osc.meas.freq_hz, osc.meas.vdc };
        UI_DrawMeasurements(m);
    }

    /* SINGLE mode: stop after one trigger. */
    if (osc.trig_mode == TRIG_SINGLE && trigger.triggered) {
        osc.state = OSC_STOPPED;
        ADC_StopCapture();
        UI_DrawStatusBar();
    }
}

void Oscilloscope_Run(void)
{
    /* Encoders are sampled in the 1 ms SysTick ISR (see HAL_IncTick in
     * encoder.c); here we only consume the accumulated movement. */
    Oscilloscope_HandleEnc1(Encoder_GetDelta(&enc1), enc1.btn_pressed);
    Oscilloscope_HandleEnc2(Encoder_GetDelta(&enc2), enc2.btn_pressed);

    /* Render when running and a new buffer is available. */
    if (osc.state == OSC_RUNNING) {
        uint8_t done;
        __disable_irq();
        done = adc_capture_done;
        __enable_irq();
        if (done) {
            osc_process_frame();
        }
    }
}
