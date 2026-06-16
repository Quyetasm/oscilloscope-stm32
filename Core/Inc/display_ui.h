/**
 * @file    display_ui.h
 * @brief   Oscilloscope user-interface rendering on the ILI9488.
 *
 * Layout (landscape 480x320):
 *   - Header row:    y 0..19
 *   - Graticule:     x 40..439, y 20..259 (10 x 8 divisions of 40x30 px)
 *   - Status bar:    y 270..319
 */
#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdint.h>
#include "ili9488.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Screen geometry -------------------------------------------------- */
#define SCREEN_W        480
#define SCREEN_H        320
#define GRID_X          40      /**< Left margin (Y labels).            */
#define GRID_Y          20      /**< Top margin.                        */
#define GRID_W          400     /**< Waveform width (10 divisions).     */
#define GRID_H          240     /**< Waveform height (8 divisions).     */
#define GRID_DIV_X      40      /**< Pixels per horizontal division.    */
#define GRID_DIV_Y      30      /**< Pixels per vertical division.      */
#define STATUS_Y        270     /**< Top of the status bar.             */

/* Derived helpers. */
#define GRID_RIGHT      (GRID_X + GRID_W)            /**< 440 */
#define GRID_BOTTOM     (GRID_Y + GRID_H)            /**< 260 */
#define GRID_CENTER_Y   (GRID_Y + GRID_H / 2)        /**< 140 (0 V line) */
#define HEADER_Y        2                            /**< Header text baseline. */

/** @brief Initialise the UI (clears screen, draws static chrome). */
void UI_Init(void);

/** @brief Draw the dotted 10x8 graticule with solid centre axes. */
void UI_DrawGrid(void);

/**
 * @brief  Render a waveform, erasing only changed columns.
 * @param  buf      sample buffer (ADC counts).
 * @param  len      number of valid samples in @p buf.
 * @param  trig_pos sample index to centre horizontally (trigger point).
 * @param  v_scale  pixels per ADC count (vertical gain).
 * @param  v_offset additional vertical pixel offset (0 = centred).
 */
void UI_DrawWaveform(const uint16_t *buf, uint32_t len,
                     int32_t trig_pos, float v_scale, float v_offset);

/** @brief Draw the horizontal trigger-level line. */
void UI_DrawTriggerLine(uint16_t level_raw, float v_scale);

/** @brief Redraw the bottom status bar from the global osc/trigger state. */
void UI_DrawStatusBar(void);

/** @brief Draw time-axis (X) labels. */
void UI_DrawTimeLabel(void);

/** @brief Draw voltage-axis (Y) labels. */
void UI_DrawVoltLabel(void);

/** @brief Draw the measurement readouts (Vpp, Freq, DC, ...). */
void UI_DrawMeasurements(const float *meas);

/** @brief Clear only the waveform area (not the whole screen). */
void UI_ClearWaveformArea(void);

/** @brief Draw a vertical cursor (e.g. the trigger position). */
void UI_DrawCursor(uint16_t x, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UI_H */
