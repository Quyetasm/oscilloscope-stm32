/**
 * @file    display_ui.c
 * @brief   Oscilloscope UI rendering with per-column partial waveform redraw.
 */
#include <stddef.h>
#include "display_ui.h"
#include "oscilloscope.h"
#include "adc_dma.h"
#include "trigger.h"

/* ---- Appearance ------------------------------------------------------- */
#define WAVE_COLOR       COLOR_YELLOW
#define DOT_SPACING      4U          /**< Dotted-line period in pixels. */
#define WAVE_TOP         (GRID_Y)        /**< Inclusive top clamp.     */
#define WAVE_BOTTOM      (GRID_BOTTOM - 1)/**< Inclusive bottom clamp.  */

/** Previous per-column waveform Y, for partial erase (-1 = none). */
static int16_t prev_y[GRID_W];
static uint8_t prev_valid = 0U;

/** @brief Is @p xrel a vertical division boundary column? */
static inline uint8_t ui_is_vdiv_col(int xrel)
{
    return (uint8_t)((xrel % GRID_DIV_X) == 0);
}

/**
 * @brief Repaint the background (black + graticule) for one waveform column.
 * @param xrel column index relative to GRID_X (0..GRID_W-1).
 */
static void ui_restore_column(int xrel)
{
    uint16_t px = (uint16_t)(GRID_X + xrel);

    /* Clear the column. */
    ILI9488_DrawFastVLine(px, GRID_Y, GRID_H, COLOR_BLACK);

    /* Vertical division line (dotted), centre column solid. */
    if (ui_is_vdiv_col(xrel)) {
        if (xrel == GRID_W / 2) {
            ILI9488_DrawFastVLine(px, GRID_Y, GRID_H, COLOR_GRID);
        } else {
            for (uint16_t y = GRID_Y; y < GRID_BOTTOM; y += DOT_SPACING) {
                ILI9488_DrawPixel(px, y, COLOR_GRID);
            }
        }
    }

    /* Horizontal division lines crossing this column. */
    for (int k = 0; k <= GRID_H / GRID_DIV_Y; k++) {
        uint16_t y = (uint16_t)(GRID_Y + k * GRID_DIV_Y);
        if (y == GRID_CENTER_Y) {
            ILI9488_DrawPixel(px, y, COLOR_GRID); /* centre axis */
        } else if ((xrel % DOT_SPACING) == 0) {
            ILI9488_DrawPixel(px, y, COLOR_GRID);
        }
    }
}

void UI_DrawGrid(void)
{
    /* Border. */
    ILI9488_DrawFastHLine(GRID_X, GRID_Y, GRID_W, COLOR_DARKGRAY);
    ILI9488_DrawFastHLine(GRID_X, GRID_BOTTOM, GRID_W, COLOR_DARKGRAY);
    ILI9488_DrawFastVLine(GRID_X, GRID_Y, GRID_H, COLOR_DARKGRAY);
    ILI9488_DrawFastVLine(GRID_RIGHT, GRID_Y, GRID_H, COLOR_DARKGRAY);

    /* Internal dotted divisions via per-column restore. */
    for (int xrel = 0; xrel < GRID_W; xrel++) {
        ui_restore_column(xrel);
    }
}

void UI_ClearWaveformArea(void)
{
    ILI9488_FillRect(GRID_X, GRID_Y, GRID_W, GRID_H, COLOR_BLACK);
    UI_DrawGrid();
    prev_valid = 0U;
}

/** @brief Map a raw sample to a clamped pixel Y inside the graticule. */
static int16_t ui_sample_to_y(uint16_t raw, float v_scale, float v_offset)
{
    int32_t y = (int32_t)GRID_CENTER_Y
              - (int32_t)((float)((int32_t)raw - ADC_MID) * v_scale)
              - (int32_t)v_offset;
    if (y < WAVE_TOP)    { y = WAVE_TOP; }
    if (y > WAVE_BOTTOM) { y = WAVE_BOTTOM; }
    return (int16_t)y;
}

void UI_DrawWaveform(const uint16_t *buf, uint32_t len,
                     int32_t trig_pos, float v_scale, float v_offset)
{
    if (buf == NULL || len == 0U) {
        return;
    }

    /* Centre the trigger point; one sample per pixel. */
    int32_t start = trig_pos - (GRID_W / 2);

    static int16_t new_y[GRID_W];

    for (int xrel = 0; xrel < GRID_W; xrel++) {
        int32_t idx = start + xrel;
        if (idx < 0) {
            idx = 0;
        } else if (idx >= (int32_t)len) {
            idx = (int32_t)len - 1;
        }
        new_y[xrel] = ui_sample_to_y(buf[idx], v_scale, v_offset);
    }

    for (int xrel = 0; xrel < GRID_W; xrel++) {
        uint8_t changed = (!prev_valid) ||
                          (new_y[xrel] != prev_y[xrel]) ||
                          (xrel > 0 && new_y[xrel - 1] != prev_y[xrel - 1]);
        if (!changed) {
            continue;
        }

        /* Erase old, repaint grid for this column. */
        ui_restore_column(xrel);

        /* Draw connected segment from the previous column's point. */
        uint16_t px = (uint16_t)(GRID_X + xrel);
        if (xrel == 0) {
            ILI9488_DrawPixel(px, (uint16_t)new_y[0], WAVE_COLOR);
        } else {
            int16_t y0 = new_y[xrel - 1];
            int16_t y1 = new_y[xrel];
            int16_t lo = (y0 < y1) ? y0 : y1;
            int16_t hi = (y0 < y1) ? y1 : y0;
            ILI9488_DrawFastVLine(px, (uint16_t)lo, (uint16_t)(hi - lo + 1), WAVE_COLOR);
        }
    }

    for (int xrel = 0; xrel < GRID_W; xrel++) {
        prev_y[xrel] = new_y[xrel];
    }
    prev_valid = 1U;
}

void UI_DrawTriggerLine(uint16_t level_raw, float v_scale)
{
    int16_t y = ui_sample_to_y(level_raw, v_scale, 0.0f);
    /* Dotted horizontal line in cyan. */
    for (uint16_t x = GRID_X; x < GRID_RIGHT; x += DOT_SPACING) {
        ILI9488_DrawPixel(x, (uint16_t)y, COLOR_CYAN);
    }
}

void UI_DrawCursor(uint16_t x, uint16_t color)
{
    if (x < GRID_X || x >= GRID_RIGHT) {
        return;
    }
    for (uint16_t y = GRID_Y; y < GRID_BOTTOM; y += DOT_SPACING) {
        ILI9488_DrawPixel(x, y, color);
    }
}

void UI_DrawVoltLabel(void)
{
    const VoltdivEntry_t *vd = Osc_CurrentVoltdiv();
    /* Labels at +/- divisions around the centre (4 divisions up/down). */
    for (int k = -4; k <= 4; k++) {
        uint16_t y = (uint16_t)(GRID_CENTER_Y - k * GRID_DIV_Y - 3);
        float v = (float)k * vd->v_per_div;
        ILI9488_FillRect(0, (uint16_t)(y - 1), GRID_X - 1, 9, COLOR_BLACK);
        ILI9488_DrawFloat(2, y, v, 1, COLOR_DARKGRAY, COLOR_BLACK, 1);
    }
}

void UI_DrawTimeLabel(void)
{
    const TimebaseEntry_t *tb = Osc_CurrentTimebase();
    char buf[24] = "T/div: ";
    /* Append the label manually (no libc string ops needed for short text). */
    int i = 0;
    while (buf[i] != '\0') { i++; }
    const char *l = tb->label;
    while (*l != '\0' && i < (int)sizeof(buf) - 1) { buf[i++] = *l++; }
    buf[i] = '\0';
    ILI9488_FillRect(GRID_X, GRID_BOTTOM + 1, 120, 9, COLOR_BLACK);
    ILI9488_DrawString(GRID_X, GRID_BOTTOM + 1, buf, COLOR_DARKGRAY, COLOR_BLACK, 1);
}

void UI_DrawStatusBar(void)
{
    const TimebaseEntry_t *tb = Osc_CurrentTimebase();
    const VoltdivEntry_t  *vd = Osc_CurrentVoltdiv();

    /* ---- Header row (top) ---- */
    ILI9488_FillRect(0, 0, SCREEN_W, GRID_Y - 1, COLOR_BLACK);

    uint16_t x = 4;
    ILI9488_DrawString(x, HEADER_Y, vd->label, COLOR_GREEN, COLOR_BLACK, 1);
    x += 48;
    ILI9488_DrawString(x, HEADER_Y, "V/div", COLOR_DARKGRAY, COLOR_BLACK, 1);
    x += 48;
    ILI9488_DrawString(x, HEADER_Y, tb->label, COLOR_GREEN, COLOR_BLACK, 1);
    x += 48;
    ILI9488_DrawString(x, HEADER_Y, "/div", COLOR_DARKGRAY, COLOR_BLACK, 1);
    x += 48;

    const char *mode =
        (osc.trig_mode == TRIG_AUTO)   ? "AUTO"  :
        (osc.trig_mode == TRIG_NORMAL) ? "NORM"  : "SING";
    ILI9488_DrawString(x, HEADER_Y, mode, COLOR_YELLOW, COLOR_BLACK, 1);
    x += 40;
    ILI9488_DrawString(x, HEADER_Y, (osc.trig_edge == TRIG_RISING) ? "R" : "F",
                       COLOR_YELLOW, COLOR_BLACK, 1);
    x += 24;

    const char *run = (osc.state == OSC_RUNNING) ? "RUN" : "STOP";
    uint16_t run_col = (osc.state == OSC_RUNNING) ? COLOR_GREEN : COLOR_RED;
    ILI9488_DrawString(x, HEADER_Y, run, run_col, COLOR_BLACK, 1);

    UI_DrawTimeLabel();
}

void UI_DrawMeasurements(const float *meas)
{
    if (meas == NULL) {
        return;
    }
    /* Clear the bottom status bar. */
    ILI9488_FillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, COLOR_BLACK);

    uint16_t y = STATUS_Y + 2;
    uint16_t x = 4;

    ILI9488_DrawString(x, y, "Vpp:", COLOR_DARKGRAY, COLOR_BLACK, 1);
    ILI9488_DrawFloat(x + 28, y, meas[0], 2, COLOR_WHITE, COLOR_BLACK, 1);

    x = 120;
    ILI9488_DrawString(x, y, "Freq:", COLOR_DARKGRAY, COLOR_BLACK, 1);
    ILI9488_DrawFloat(x + 34, y, meas[1], 1, COLOR_WHITE, COLOR_BLACK, 1);

    x = 250;
    ILI9488_DrawString(x, y, "DC:", COLOR_DARKGRAY, COLOR_BLACK, 1);
    ILI9488_DrawFloat(x + 22, y, meas[2], 2, COLOR_WHITE, COLOR_BLACK, 1);

    x = 360;
    ILI9488_DrawString(x, y, "Trig:", COLOR_DARKGRAY, COLOR_BLACK, 1);
    ILI9488_DrawFloat(x + 34, y, osc.trig_level, 2, COLOR_CYAN, COLOR_BLACK, 1);
}

void UI_Init(void)
{
    ILI9488_FillScreen(COLOR_BLACK);
    prev_valid = 0U;
    UI_DrawGrid();
    UI_DrawVoltLabel();
    UI_DrawStatusBar();
}
