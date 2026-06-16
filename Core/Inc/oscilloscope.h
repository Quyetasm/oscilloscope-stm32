/**
 * @file    oscilloscope.h
 * @brief   Top-level oscilloscope state machine and control tables.
 */
#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#include <stdint.h>
#include "trigger.h"
#include "measurements.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One timebase (Time/div) setting.
 */
typedef struct {
    uint32_t    sample_rate;  /**< ADC sample rate in Hz.            */
    uint32_t    spd;          /**< Samples per division.             */
    const char *label;        /**< Display label, e.g. "1us".        */
} TimebaseEntry_t;

/**
 * @brief One vertical sensitivity (Volt/div) setting.
 */
typedef struct {
    float       v_per_div;     /**< Volts per division (input-referred). */
    float       scale;         /**< Pixels per ADC count (raw -> px).    */
    float       offset_center; /**< Vertical centring offset (px).       */
    const char *label;         /**< Display label, e.g. "1V".            */
} VoltdivEntry_t;

/** @brief High-level acquisition state. */
typedef enum {
    OSC_RUNNING,  /**< Normal acquisition.                    */
    OSC_STOPPED,  /**< Single completed or user stop.         */
    OSC_MENU      /**< Quick menu (encoder button hold).      */
} OscState_t;

/** @brief Aggregate oscilloscope configuration / live state. */
typedef struct {
    OscState_t     state;
    uint8_t        timebase_idx;  /**< Index into the timebase table.  */
    uint8_t        voltdiv_idx;   /**< Index into the voltdiv table.   */
    TriggerMode_t  trig_mode;
    TriggerEdge_t  trig_edge;
    float          trig_level;    /**< Trigger level in volts.         */
    Measurements_t meas;
    uint8_t        show_meas;     /**< Show measurement readouts.      */
    uint8_t        attenuator;    /**< 0 = 1:1, 1 = 1:10 (informational). */
} OscConfig_t;

extern OscConfig_t osc;

/** Number of entries in the timebase / voltdiv tables. */
extern const uint8_t TIMEBASE_COUNT;
extern const uint8_t VOLTDIV_COUNT;

/** @brief Accessors for the currently selected control-table entries. */
const TimebaseEntry_t *Osc_CurrentTimebase(void);
const VoltdivEntry_t  *Osc_CurrentVoltdiv(void);

/** @brief Initialise oscilloscope state, peripherals and UI. */
void Oscilloscope_Init(void);

/** @brief Main super-loop body (called repeatedly from main). */
void Oscilloscope_Run(void);

/** @brief Handle ENC1 (Time/div + trigger mode + run/stop). */
void Oscilloscope_HandleEnc1(int32_t delta, uint8_t btn);

/** @brief Handle ENC2 (Volt/div + trigger level + trigger edge). */
void Oscilloscope_HandleEnc2(int32_t delta, uint8_t btn);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_H */
