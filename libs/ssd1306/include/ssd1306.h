#pragma once
#include <stdint.h>

// Hardware config
#define SSD1306_I2C_ADDR         0x3C
#define SSD1306_I2C_SDA_PIN      20
#define SSD1306_I2C_SCL_PIN      21

// Display geometry
#define SSD1306_WIDTH            128
#define SSD1306_HEIGHT           32
#define SSD1306_PAGES            (SSD1306_HEIGHT / 8)
#define SSD1306_FB_SIZE          (SSD1306_WIDTH * SSD1306_PAGES)

// SSD1306 command bytes
#define SSD1306_SET_MEM_MODE     0x20
#define SSD1306_SET_COL_ADDR     0x21
#define SSD1306_SET_PAGE_ADDR    0x22
#define SSD1306_SET_SCROLL_OFF   0x2E
#define SSD1306_SET_CONTRAST     0x81
#define SSD1306_SET_CHARGE_PUMP  0x8D
#define SSD1306_SET_SEG_REMAP    0xA0
#define SSD1306_SET_ENTIRE_ON    0xA4
#define SSD1306_SET_NORM_DISP    0xA6
#define SSD1306_SET_MUX_RATIO    0xA8
#define SSD1306_SET_DISP         0xAE
#define SSD1306_SET_COM_OUT_DIR  0xC0
#define SSD1306_SET_DISP_OFFSET  0xD3
#define SSD1306_SET_CLK_DIV      0xD5
#define SSD1306_SET_PRECHARGE    0xD9
#define SSD1306_SET_COM_PIN_CFG  0xDA
#define SSD1306_SET_VCOM_DESEL   0xDB
#define SSD1306_START_LINE       0x40

// Initialise I2C0 on SDA=GP20 / SCL=GP21 at 400 kHz and send the SSD1306
// setup sequence for a 128×32 display at address 0x3C.
void ssd1306_init(void);

// Clear the framebuffer (does not update the display — call ssd1306_show).
void ssd1306_clear(void);

// Flush the framebuffer to the display over I2C.
void ssd1306_show(void);

// Render a NUL-terminated ASCII string at character position (col, row).
// col: 0-15 (16 columns × 8 px), row: 0-3 (4 rows × 8 px).
// Lowercase is mapped to uppercase. Unknown characters render as space.
// Clips silently at the display edge.
void ssd1306_puts(int col, int row, const char *s);
