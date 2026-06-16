/**
 * @file    ili9488.h
 * @brief   Driver for the ILI9488 3.5" 480x320 TFT over SPI1 (HAL based).
 *
 * The panel is driven in landscape orientation (480 wide x 320 tall) using a
 * 16-bit RGB565 pixel format. All drawing primitives, an embedded 5x7 ASCII
 * font and text/number helpers are implemented here from scratch.
 *
 * Wiring (see project pin map, do not change):
 *   PA5 -> SPI1_SCK, PA6 -> SPI1_MISO, PA7 -> SPI1_MOSI
 *   PB0 -> LCD_DC, PB1 -> LCD_CS (active LOW), PB2 -> LCD_RST (active LOW)
 */
#ifndef ILI9488_H
#define ILI9488_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Panel geometry --------------------------------------------------- */
#define ILI9488_WIDTH   480U   /**< Active width in landscape mode.  */
#define ILI9488_HEIGHT  320U   /**< Active height in landscape mode. */

/* ---- Control pins (PORTB) --------------------------------------------- */
#define ILI9488_DC_PORT    GPIOB
#define ILI9488_DC_PIN     GPIO_PIN_0
#define ILI9488_CS_PORT    GPIOB
#define ILI9488_CS_PIN     GPIO_PIN_1
#define ILI9488_RST_PORT   GPIOB
#define ILI9488_RST_PIN    GPIO_PIN_2

/* ---- 16-bit RGB565 colors --------------------------------------------- */
#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_GREEN    0x07E0
#define COLOR_YELLOW   0xFFE0
#define COLOR_CYAN     0x07FF
#define COLOR_RED      0xF800
#define COLOR_BLUE     0x001F
#define COLOR_ORANGE   0xFD20
#define COLOR_DARKGRAY 0x4208
#define COLOR_GRID     0x2104   /**< Dark gray used for the graticule. */

/* ---- ILI9488 command set (subset used here) --------------------------- */
#define ILI9488_CMD_SWRESET    0x01
#define ILI9488_CMD_SLPOUT     0x11   /**< Sleep out.                    */
#define ILI9488_CMD_NORON      0x13   /**< Normal display mode on.       */
#define ILI9488_CMD_INVOFF     0x20   /**< Display inversion off.        */
#define ILI9488_CMD_DISPON     0x29   /**< Display on.                   */
#define ILI9488_CMD_CASET      0x2A   /**< Column address set.           */
#define ILI9488_CMD_PASET      0x2B   /**< Page (row) address set.       */
#define ILI9488_CMD_RAMWR      0x2C   /**< Memory write.                 */
#define ILI9488_CMD_MADCTL     0x36   /**< Memory access control.        */
#define ILI9488_CMD_PIXFMT     0x3A   /**< Interface pixel format.       */
#define ILI9488_CMD_IFMODE     0xB0   /**< Interface mode control.       */
#define ILI9488_CMD_FRMCTR1    0xB1   /**< Frame rate control.           */
#define ILI9488_CMD_PWCTR1     0xC0   /**< Power control 1.              */
#define ILI9488_CMD_PWCTR2     0xC1   /**< Power control 2.              */
#define ILI9488_CMD_VMCTR1     0xC5   /**< VCOM control.                 */
#define ILI9488_CMD_PGAMCTRL   0xE0   /**< Positive gamma.               */
#define ILI9488_CMD_NGAMCTRL   0xE1   /**< Negative gamma.               */
#define ILI9488_CMD_IMGFUNC    0xE9   /**< Set image function.           */
#define ILI9488_CMD_ADJUST     0xF7   /**< Adjust control 3.             */

/**
 * @brief MADCTL value for 480x320 landscape, BGR order.
 * @note  Bits: MY=0 MX=1 MV=1 ML=0 BGR=1 -> 0x28 | 0x08 = 0x28 (MV|MX) + BGR.
 */
#define ILI9488_MADCTL_LANDSCAPE  0x28

/** SPI handle defined in main.c. */
extern SPI_HandleTypeDef hspi1;

/* ---- Public API ------------------------------------------------------- */

/** @brief Run the full ILI9488 power-up / configuration sequence. */
void ILI9488_Init(void);

/**
 * @brief  Set the active drawing window (column / row address ranges).
 * @param  x0,y0 top-left corner (inclusive).
 * @param  x1,y1 bottom-right corner (inclusive).
 */
void ILI9488_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/** @brief Fill a rectangle with a solid color. */
void ILI9488_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/** @brief Draw a single pixel. */
void ILI9488_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/** @brief Draw an arbitrary line (Bresenham). */
void ILI9488_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/** @brief Fast vertical line of height @p h starting at (x,y). */
void ILI9488_DrawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

/** @brief Fast horizontal line of width @p w starting at (x,y). */
void ILI9488_DrawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

/** @brief Fill the whole screen with a solid color. */
void ILI9488_FillScreen(uint16_t color);

/**
 * @brief  Draw a single 5x7 character scaled by @p size.
 * @param  fg foreground color, bg background color.
 * @param  size integer scale factor (1 = 5x7 px, 2 = 10x14 px, ...).
 */
void ILI9488_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t size);

/** @brief Draw a NUL-terminated string using the 5x7 font. */
void ILI9488_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t size);

/** @brief Draw a signed integer in base 10. */
void ILI9488_DrawNumber(uint16_t x, uint16_t y, int32_t num, uint16_t fg, uint16_t bg, uint8_t size);

/** @brief Draw a float with @p decimals fractional digits. */
void ILI9488_DrawFloat(uint16_t x, uint16_t y, float val, uint8_t decimals, uint16_t fg, uint16_t bg, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif /* ILI9488_H */
