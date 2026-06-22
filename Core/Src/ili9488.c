/**
 * @file    ili9488.c
 * @brief   ILI9488 480x320 TFT driver implementation (SPI1 / HAL).
 *
 * SPI runs at fPCLK/2 (PCLK2 = 100 MHz -> 50 MHz). The panel is configured for
 * 16-bit RGB565 pixels. Only the STM32 HAL is used; all primitives, the font
 * and text helpers are written from scratch.
 */
#include "ili9488.h"

/* HAL timeout used for blocking SPI transfers. */
#ifndef HAL_MAX_DELAY_MS
#define HAL_MAX_DELAY_MS  1000U
#endif

/* ----------------------------------------------------------------------- */
/* Embedded 5x7 font, ASCII 0x20..0x7E. Each glyph is 5 column bytes;       */
/* bit0 is the top row, bit6 the bottom row. One pixel of inter-char space  */
/* is added by the rendering code.                                          */
/* ----------------------------------------------------------------------- */
#define FONT_FIRST_CHAR  0x20
#define FONT_LAST_CHAR   0x7E
#define FONT_WIDTH       5      /**< Glyph width in source pixels.  */
#define FONT_HEIGHT      7      /**< Glyph height in source pixels. */

static const uint8_t font5x7[(FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)][FONT_WIDTH] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20 space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* backslash */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x0C,0x52,0x52,0x52,0x3E}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x04,0x08,0x10,0x08}, /* ~ */
};

/* ----------------------------------------------------------------------- */
/* Low-level SPI helpers                                                    */
/* ----------------------------------------------------------------------- */

/*
 * ILI9488 over 4-wire SPI only supports 18bpp (3 bytes/pixel, RGB666).
 * Colors are kept as RGB565 throughout the project and expanded to 3 bytes
 * here, at the lowest level.
 */
#define PIXEL_BYTES 3U

/** Chunk buffer for streaming pixels (PIXEL_CHUNK pixels x 3 bytes). */
#define PIXEL_CHUNK 64U
static uint8_t pixel_buf[PIXEL_CHUNK * PIXEL_BYTES];

/** @brief Expand an RGB565 color into 3 RGB666 bytes (top 6 bits used). */
static inline void rgb565_to_rgb666(uint16_t c, uint8_t *out)
{
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((c >> 5)  & 0x3F);
    uint8_t b5 = (uint8_t)( c        & 0x1F);
    out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
}

/** @brief Assert chip-select (active LOW). */
static inline void ili_cs_low(void)  { HAL_GPIO_WritePin(ILI9488_CS_PORT, ILI9488_CS_PIN, GPIO_PIN_RESET); }
/** @brief Release chip-select. */
static inline void ili_cs_high(void) { HAL_GPIO_WritePin(ILI9488_CS_PORT, ILI9488_CS_PIN, GPIO_PIN_SET); }
/** @brief Select command register (DC LOW). */
static inline void ili_dc_cmd(void)  { HAL_GPIO_WritePin(ILI9488_DC_PORT, ILI9488_DC_PIN, GPIO_PIN_RESET); }
/** @brief Select data register (DC HIGH). */
static inline void ili_dc_data(void) { HAL_GPIO_WritePin(ILI9488_DC_PORT, ILI9488_DC_PIN, GPIO_PIN_SET); }

/** @brief Send a single command byte (DC low). CS must already be asserted. */
static void ili_write_cmd(uint8_t cmd)
{
    ili_dc_cmd();
    HAL_SPI_Transmit(&hspi1, &cmd, 1U, HAL_MAX_DELAY_MS);
}

/** @brief Send @p len data bytes (DC high). CS must already be asserted. */
static void ili_write_data(const uint8_t *data, uint16_t len)
{
    ili_dc_data();
    HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, HAL_MAX_DELAY_MS);
}

/** @brief Send one data byte. */
static void ili_write_data8(uint8_t d)
{
    ili_dc_data();
    HAL_SPI_Transmit(&hspi1, &d, 1U, HAL_MAX_DELAY_MS);
}

/**
 * @brief Stream @p count copies of @p color into display RAM.
 * @note  Caller must have set the window and issued RAMWR; CS asserted, DC=data.
 */
static void ili_push_color(uint16_t color, uint32_t count)
{
    uint8_t px[PIXEL_BYTES];
    rgb565_to_rgb666(color, px);
    for (uint32_t i = 0U; i < PIXEL_CHUNK; i++) {
        pixel_buf[PIXEL_BYTES * i]      = px[0];
        pixel_buf[PIXEL_BYTES * i + 1U] = px[1];
        pixel_buf[PIXEL_BYTES * i + 2U] = px[2];
    }
    ili_dc_data();
    while (count > 0U) {
        uint32_t n = (count > PIXEL_CHUNK) ? PIXEL_CHUNK : count;
        HAL_SPI_Transmit(&hspi1, pixel_buf, (uint16_t)(n * PIXEL_BYTES), HAL_MAX_DELAY_MS);
        count -= n;
    }
}

/* ----------------------------------------------------------------------- */
/* Public primitives                                                        */
/* ----------------------------------------------------------------------- */

void ILI9488_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];

    ili_write_cmd(ILI9488_CMD_CASET);
    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)(x1 & 0xFF);
    ili_write_data(buf, 4U);

    ili_write_cmd(ILI9488_CMD_PASET);
    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)(y1 & 0xFF);
    ili_write_data(buf, 4U);

    ili_write_cmd(ILI9488_CMD_RAMWR);
}

void ILI9488_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= ILI9488_WIDTH || y >= ILI9488_HEIGHT || w == 0U || h == 0U) {
        return;
    }
    if ((uint32_t)x + w > ILI9488_WIDTH)  { w = (uint16_t)(ILI9488_WIDTH - x); }
    if ((uint32_t)y + h > ILI9488_HEIGHT) { h = (uint16_t)(ILI9488_HEIGHT - y); }

    ili_cs_low();
    ILI9488_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    ili_push_color(color, (uint32_t)w * h);
    ili_cs_high();
}

void ILI9488_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ILI9488_WIDTH || y >= ILI9488_HEIGHT) {
        return;
    }
    uint8_t buf[PIXEL_BYTES];
    rgb565_to_rgb666(color, buf);
    ili_cs_low();
    ILI9488_SetWindow(x, y, x, y);
    ili_write_data(buf, PIXEL_BYTES);
    ili_cs_high();
}

void ILI9488_DrawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    ILI9488_FillRect(x, y, 1U, h, color);
}

void ILI9488_DrawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    ILI9488_FillRect(x, y, w, 1U, color);
}

void ILI9488_FillScreen(uint16_t color)
{
    ILI9488_FillRect(0U, 0U, ILI9488_WIDTH, ILI9488_HEIGHT, color);
}

void ILI9488_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    /* Bresenham's line algorithm. */
    int16_t dx = (int16_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    int16_t dy = (int16_t)((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);

    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < (int16_t)ILI9488_WIDTH && y0 < (int16_t)ILI9488_HEIGHT) {
            ILI9488_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int16_t e2 = (int16_t)(2 * err);
        if (e2 > -dy) { err = (int16_t)(err - dy); x0 = (int16_t)(x0 + sx); }
        if (e2 <  dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
}

void ILI9488_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t size)
{
    if ((uint8_t)c < FONT_FIRST_CHAR || (uint8_t)c > FONT_LAST_CHAR) {
        c = (char)FONT_FIRST_CHAR; /* render unsupported glyphs as space */
    }
    if (size == 0U) {
        size = 1U;
    }
    const uint8_t *glyph = font5x7[(uint8_t)c - FONT_FIRST_CHAR];

    for (uint8_t col = 0U; col < FONT_WIDTH; col++) {
        uint8_t bits = glyph[col];
        for (uint8_t row = 0U; row < FONT_HEIGHT; row++) {
            uint16_t color = (bits & (1U << row)) ? fg : bg;
            if (size == 1U) {
                ILI9488_DrawPixel((uint16_t)(x + col), (uint16_t)(y + row), color);
            } else {
                ILI9488_FillRect((uint16_t)(x + col * size), (uint16_t)(y + row * size),
                                 size, size, color);
            }
        }
    }
}

void ILI9488_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t size)
{
    if (size == 0U) {
        size = 1U;
    }
    uint16_t cursor = x;
    const uint16_t advance = (uint16_t)((FONT_WIDTH + 1U) * size); /* +1 px spacing */
    while (*str != '\0') {
        ILI9488_DrawChar(cursor, y, *str, fg, bg, size);
        cursor = (uint16_t)(cursor + advance);
        str++;
    }
}

void ILI9488_DrawNumber(uint16_t x, uint16_t y, int32_t num, uint16_t fg, uint16_t bg, uint8_t size)
{
    char tmp[12];
    int i = 0;
    uint32_t mag;
    int neg = 0;

    if (num < 0) {
        neg = 1;
        mag = (uint32_t)(-(num + 1)) + 1U; /* safe negation incl. INT32_MIN */
    } else {
        mag = (uint32_t)num;
    }
    do {
        tmp[i++] = (char)('0' + (mag % 10U));
        mag /= 10U;
    } while (mag > 0U && i < (int)sizeof(tmp) - 1);
    if (neg) {
        tmp[i++] = '-';
    }
    /* tmp currently holds the digits reversed -> emit in correct order. */
    char out[13];
    int j = 0;
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
    ILI9488_DrawString(x, y, out, fg, bg, size);
}

void ILI9488_DrawFloat(uint16_t x, uint16_t y, float val, uint8_t decimals, uint16_t fg, uint16_t bg, uint8_t size)
{
    char buf[24];
    int idx = 0;
    int neg = (val < 0.0f);
    if (neg) {
        val = -val;
        buf[idx++] = '-';
    }

    /* Rounding factor 10^decimals. */
    uint32_t factor = 1U;
    for (uint8_t d = 0U; d < decimals; d++) {
        factor *= 10U;
    }
    /* Scale, round and split into integer / fractional parts. */
    uint32_t scaled = (uint32_t)(val * (float)factor + 0.5f);
    uint32_t ipart  = scaled / factor;
    uint32_t fpart  = scaled % factor;

    /* Integer part. */
    char ibuf[12];
    int ii = 0;
    do {
        ibuf[ii++] = (char)('0' + (ipart % 10U));
        ipart /= 10U;
    } while (ipart > 0U && ii < (int)sizeof(ibuf));
    while (ii > 0) {
        buf[idx++] = ibuf[--ii];
    }

    if (decimals > 0U) {
        buf[idx++] = '.';
        /* Emit fractional digits with leading zeros. */
        for (int p = (int)decimals - 1; p >= 0; p--) {
            uint32_t div = 1U;
            for (int k = 0; k < p; k++) {
                div *= 10U;
            }
            buf[idx++] = (char)('0' + ((fpart / div) % 10U));
        }
    }
    buf[idx] = '\0';
    ILI9488_DrawString(x, y, buf, fg, bg, size);
}

/* ----------------------------------------------------------------------- */
/* Initialization                                                           */
/* ----------------------------------------------------------------------- */

/** @brief Hardware reset pulse on LCD_RST. */
static void ili_hw_reset(void)
{
    HAL_GPIO_WritePin(ILI9488_RST_PORT, ILI9488_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(5U);
    HAL_GPIO_WritePin(ILI9488_RST_PORT, ILI9488_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(ILI9488_RST_PORT, ILI9488_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(150U);
}

void ILI9488_Init(void)
{
    ili_hw_reset();

    ili_cs_low();

    ili_write_cmd(ILI9488_CMD_SWRESET);
    ili_cs_high();
    HAL_Delay(120U);
    ili_cs_low();

    /* Positive gamma. */
    ili_write_cmd(ILI9488_CMD_PGAMCTRL);
    { const uint8_t g[15] = {0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,
                             0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F};
      ili_write_data(g, 15U); }
    /* Negative gamma. */
    ili_write_cmd(ILI9488_CMD_NGAMCTRL);
    { const uint8_t g[15] = {0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,
                             0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F};
      ili_write_data(g, 15U); }

    /* Power control. */
    ili_write_cmd(ILI9488_CMD_PWCTR1); ili_write_data8(0x17); ili_write_data8(0x15);
    ili_write_cmd(ILI9488_CMD_PWCTR2); ili_write_data8(0x41);
    /* VCOM control. */
    ili_write_cmd(ILI9488_CMD_VMCTR1);
    ili_write_data8(0x00); ili_write_data8(0x12); ili_write_data8(0x80);

    /* Memory access control: landscape orientation. */
    ili_write_cmd(ILI9488_CMD_MADCTL);
    ili_write_data8(ILI9488_MADCTL_LANDSCAPE);

    /* Interface pixel format: 18 bits/pixel (RGB666), value 0x66.
     * ILI9488 over 4-wire SPI does not support 16bpp; pixels are written as
     * 3 bytes/pixel (see rgb565_to_rgb666 / ili_push_color). */
    ili_write_cmd(ILI9488_CMD_PIXFMT);
    ili_write_data8(0x66);

    /* Interface mode control. */
    ili_write_cmd(ILI9488_CMD_IFMODE);  ili_write_data8(0x00);
    /* Frame rate. */
    ili_write_cmd(ILI9488_CMD_FRMCTR1); ili_write_data8(0xA0);
    /* Display inversion off. */
    ili_write_cmd(ILI9488_CMD_INVOFF);
    /* Set image function. */
    ili_write_cmd(ILI9488_CMD_IMGFUNC);  ili_write_data8(0x00);
    /* Adjust control 3. */
    ili_write_cmd(ILI9488_CMD_ADJUST);
    { const uint8_t a[4] = {0xA9,0x51,0x2C,0x82}; ili_write_data(a, 4U); }

    ili_write_cmd(ILI9488_CMD_SLPOUT);
    ili_cs_high();
    HAL_Delay(120U);
    ili_cs_low();

    ili_write_cmd(ILI9488_CMD_NORON);
    ili_write_cmd(ILI9488_CMD_DISPON);
    ili_cs_high();
    HAL_Delay(20U);

    ILI9488_FillScreen(COLOR_BLACK);
}
