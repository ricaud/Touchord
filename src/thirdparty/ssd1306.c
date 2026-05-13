/*

MIT License

Copyright (c) 2021 David Schramm

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <hardware/spi.h>
#include <stdlib.h>
#include <string.h>

#include "Defines.h"
#include "ssd1306.h"
#include "font.h"

#define TC_LCD_MEMORY_ACCESS_VERTICAL 0x70u
#define TC_LCD_PANEL_Y_OFFSET         0x22u
#define TC_LCD_COLOR_BLACK            0x0000u
#define TC_LCD_COLOR_WHITE            0xFFFFu

static bool tc_lcd_initialized = false;
// Touchord's UI is still authored for a 128x64 monochrome surface.
// We keep that logical framebuffer and scale it onto the larger color panel.
static uint16_t tc_x_map[LCD_PHYSICAL_WIDTH];
static int16_t tc_y_map[LCD_PHYSICAL_HEIGHT];
static uint8_t tc_spi_line_buffer[LCD_PHYSICAL_WIDTH * 2u];

static inline void tc_write_command(uint8_t value)
{
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI_PORT, &value, 1);
    gpio_put(LCD_PIN_CS, 1);
}

static inline void tc_write_data(const uint8_t *data, size_t len)
{
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI_PORT, data, len);
    gpio_put(LCD_PIN_CS, 1);
}

static inline void tc_write_data8(uint8_t value)
{
    tc_write_data(&value, 1);
}

static void tc_lcd_reset(void)
{
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(100);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(100);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(100);
}

static void tc_lcd_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    const uint16_t panel_y_start = (uint16_t)(y_start + TC_LCD_PANEL_Y_OFFSET);
    const uint16_t panel_y_end = (uint16_t)(y_end - 1u + TC_LCD_PANEL_Y_OFFSET);
    const uint8_t col_data[4] = {
        (uint8_t)(x_start >> 8),
        (uint8_t)x_start,
        (uint8_t)((x_end - 1u) >> 8),
        (uint8_t)(x_end - 1u)
    };
    const uint8_t row_data[4] = {
        (uint8_t)(panel_y_start >> 8),
        (uint8_t)panel_y_start,
        (uint8_t)(panel_y_end >> 8),
        (uint8_t)panel_y_end
    };

    tc_write_command(0x2A);
    tc_write_data(col_data, sizeof col_data);

    tc_write_command(0x2B);
    tc_write_data(row_data, sizeof row_data);

    tc_write_command(0x2C);
}

static void tc_lcd_init_panel(void)
{
    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_init(LCD_PIN_CS);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_init(LCD_PIN_BL);
    gpio_set_dir(LCD_PIN_BL, GPIO_OUT);

    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_BL, 1);

    spi_init(LCD_SPI_PORT, LCD_SPI_BAUDRATE);
    gpio_set_function(LCD_PIN_CLK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    tc_lcd_reset();

    tc_write_command(0x11);
    sleep_ms(120);

    tc_write_command(0x36);
    tc_write_data8(TC_LCD_MEMORY_ACCESS_VERTICAL);

    tc_write_command(0x3A);
    tc_write_data8(0x05);

    tc_write_command(0xB2);
    tc_write_data8(0x0C);
    tc_write_data8(0x0C);
    tc_write_data8(0x00);
    tc_write_data8(0x33);
    tc_write_data8(0x33);

    tc_write_command(0xB7);
    tc_write_data8(0x35);

    tc_write_command(0xBB);
    tc_write_data8(0x35);

    tc_write_command(0xC0);
    tc_write_data8(0x2C);

    tc_write_command(0xC2);
    tc_write_data8(0x01);

    tc_write_command(0xC3);
    tc_write_data8(0x13);

    tc_write_command(0xC4);
    tc_write_data8(0x20);

    tc_write_command(0xC6);
    tc_write_data8(0x0F);

    tc_write_command(0xD0);
    tc_write_data8(0xA4);
    tc_write_data8(0xA1);

    tc_write_command(0xD6);
    tc_write_data8(0xA1);

    tc_write_command(0xE0);
    tc_write_data8(0xF0);
    tc_write_data8(0x00);
    tc_write_data8(0x04);
    tc_write_data8(0x04);
    tc_write_data8(0x04);
    tc_write_data8(0x05);
    tc_write_data8(0x29);
    tc_write_data8(0x33);
    tc_write_data8(0x3E);
    tc_write_data8(0x38);
    tc_write_data8(0x12);
    tc_write_data8(0x12);
    tc_write_data8(0x28);
    tc_write_data8(0x30);

    tc_write_command(0xE1);
    tc_write_data8(0xF0);
    tc_write_data8(0x07);
    tc_write_data8(0x0A);
    tc_write_data8(0x0D);
    tc_write_data8(0x0B);
    tc_write_data8(0x07);
    tc_write_data8(0x28);
    tc_write_data8(0x33);
    tc_write_data8(0x3E);
    tc_write_data8(0x36);
    tc_write_data8(0x14);
    tc_write_data8(0x14);
    tc_write_data8(0x29);
    tc_write_data8(0x32);

    tc_write_command(0x21);
    tc_write_command(0x29);
}

static void tc_prepare_scaling(const ssd1306_t *p)
{
    const uint16_t active_x_end = (uint16_t)(p->scaled_x_offset + p->scaled_width);
    const uint16_t active_y_end = (uint16_t)(p->scaled_y_offset + p->scaled_height);

    for (uint16_t x = 0; x < p->physical_width; ++x) {
        if (x < p->scaled_x_offset || x >= active_x_end) {
            tc_x_map[x] = 0;
            continue;
        }

        tc_x_map[x] = (uint16_t)(((uint32_t)(x - p->scaled_x_offset) * p->width) / p->scaled_width);
        if (tc_x_map[x] >= p->width) {
            tc_x_map[x] = (uint16_t)(p->width - 1u);
        }
    }

    for (uint16_t y = 0; y < p->physical_height; ++y) {
        if (y < p->scaled_y_offset || y >= active_y_end) {
            tc_y_map[y] = -1;
            continue;
        }

        tc_y_map[y] = (int16_t)(((uint32_t)(y - p->scaled_y_offset) * p->height) / p->scaled_height);
        if ((uint16_t)tc_y_map[y] >= p->height) {
            tc_y_map[y] = (int16_t)(p->height - 1u);
        }
    }
}

static inline bool tc_get_pixel(const ssd1306_t *p, uint16_t x, uint16_t y)
{
    return (p->buffer[x + p->width * (y >> 3u)] & (uint8_t)(1u << (y & 0x07u))) != 0u;
}

static inline void tc_fill_line(uint16_t color)
{
    for (uint16_t x = 0; x < LCD_PHYSICAL_WIDTH; ++x) {
        tc_spi_line_buffer[x * 2u] = (uint8_t)(color >> 8);
        tc_spi_line_buffer[x * 2u + 1u] = (uint8_t)color;
    }
}

bool ssd1306_init(ssd1306_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance)
{
    (void)address;
    (void)i2c_instance;

    if (!tc_lcd_initialized) {
        tc_lcd_init_panel();
        tc_lcd_initialized = true;
    }

    p->width = width;
    p->height = height;
    p->pages = (uint8_t)((height + 7u) / 8u);
    p->address = address;
    p->i2c_i = i2c_instance;
    p->physical_width = LCD_PHYSICAL_WIDTH;
    p->physical_height = LCD_PHYSICAL_HEIGHT;
    p->scaled_width = p->physical_width;
    p->scaled_height = (uint16_t)(((uint32_t)p->height * p->scaled_width) / p->width);
    if (p->scaled_height > p->physical_height) {
        p->scaled_height = p->physical_height;
        p->scaled_width = (uint16_t)(((uint32_t)p->width * p->scaled_height) / p->height);
    }
    p->scaled_x_offset = (uint16_t)((p->physical_width - p->scaled_width) / 2u);
    p->scaled_y_offset = (uint16_t)((p->physical_height - p->scaled_height) / 2u);
    p->fg_color = TC_LCD_COLOR_WHITE;
    p->bg_color = TC_LCD_COLOR_BLACK;

    p->bufsize = (size_t)p->pages * p->width;
    p->buffer = malloc(p->bufsize);
    if (p->buffer == NULL) {
        p->bufsize = 0;
        return false;
    }

    memset(p->buffer, 0, p->bufsize);
    tc_prepare_scaling(p);
    tc_fill_line(p->bg_color);
    ssd1306_show(p);
    return true;
}

inline void ssd1306_deinit(ssd1306_t *p)
{
    free(p->buffer);
    p->buffer = NULL;
    p->bufsize = 0;
}

inline void ssd1306_poweroff(ssd1306_t *p)
{
    (void)p;
    gpio_put(LCD_PIN_BL, 0);
}

inline void ssd1306_poweron(ssd1306_t *p)
{
    (void)p;
    gpio_put(LCD_PIN_BL, 1);
}

inline void ssd1306_contrast(ssd1306_t *p, uint8_t val)
{
    (void)p;
    gpio_put(LCD_PIN_BL, val != 0u);
}

inline void ssd1306_invert(ssd1306_t *p, uint8_t inv)
{
    if (inv) {
        p->fg_color = TC_LCD_COLOR_BLACK;
        p->bg_color = TC_LCD_COLOR_WHITE;
    } else {
        p->fg_color = TC_LCD_COLOR_WHITE;
        p->bg_color = TC_LCD_COLOR_BLACK;
    }
}

inline void ssd1306_clear(ssd1306_t *p)
{
    memset(p->buffer, 0, p->bufsize);
}

void ssd1306_clear_pixel(ssd1306_t *p, uint32_t x, uint32_t y)
{
    if (x >= p->width || y >= p->height) {
        return;
    }

    p->buffer[x + p->width * (y >> 3u)] &= (uint8_t)~(1u << (y & 0x07u));
}

void ssd1306_draw_pixel(ssd1306_t *p, uint32_t x, uint32_t y)
{
    if (x >= p->width || y >= p->height) {
        return;
    }

    p->buffer[x + p->width * (y >> 3u)] |= (uint8_t)(1u << (y & 0x07u));
}

void ssd1306_draw_line(ssd1306_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t dx = abs(x2 - x1);
    int32_t sx = x1 < x2 ? 1 : -1;
    int32_t dy = -abs(y2 - y1);
    int32_t sy = y1 < y2 ? 1 : -1;
    int32_t err = dx + dy;

    while (true) {
        ssd1306_draw_pixel(p, (uint32_t)x1, (uint32_t)y1);
        if (x1 == x2 && y1 == y2) {
            break;
        }

        int32_t e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void ssd1306_clear_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < width; ++i) {
        for (uint32_t j = 0; j < height; ++j) {
            ssd1306_clear_pixel(p, x + i, y + j);
        }
    }
}

void ssd1306_draw_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < width; ++i) {
        for (uint32_t j = 0; j < height; ++j) {
            ssd1306_draw_pixel(p, x + i, y + j);
        }
    }
}

void ssd1306_draw_empty_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    ssd1306_draw_line(p, (int32_t)x, (int32_t)y, (int32_t)(x + width), (int32_t)y);
    ssd1306_draw_line(p, (int32_t)x, (int32_t)(y + height), (int32_t)(x + width), (int32_t)(y + height));
    ssd1306_draw_line(p, (int32_t)x, (int32_t)y, (int32_t)x, (int32_t)(y + height));
    ssd1306_draw_line(p, (int32_t)(x + width), (int32_t)y, (int32_t)(x + width), (int32_t)(y + height));
}

void ssd1306_draw_char_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, char c)
{
    if (c < font[3] || c > font[4]) {
        return;
    }

    uint32_t parts_per_line = (font[0] >> 3) + ((font[0] & 7u) > 0u);
    for (uint8_t w = 0; w < font[1]; ++w) {
        uint32_t pp = (uint32_t)(c - font[3]) * font[1] * parts_per_line + w * parts_per_line + 5u;
        for (uint32_t lp = 0; lp < parts_per_line; ++lp) {
            uint8_t line = font[pp];

            for (int8_t j = 0; j < 8; ++j, line >>= 1) {
                if (line & 1u) {
                    ssd1306_draw_square(p, x + w * scale, y + ((lp << 3u) + (uint32_t)j) * scale, scale, scale);
                }
            }

            ++pp;
        }
    }
}

void ssd1306_draw_string_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, const char *s)
{
    for (int32_t x_n = (int32_t)x; *s; x_n += (int32_t)((font[1] + font[2]) * scale)) {
        ssd1306_draw_char_with_font(p, (uint32_t)x_n, y, scale, font, *(s++));
    }
}

void ssd1306_draw_char(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, char c)
{
    ssd1306_draw_char_with_font(p, x, y, scale, font_8x5, c);
}

void ssd1306_draw_string(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s)
{
    ssd1306_draw_string_with_font(p, x, y, scale, font_8x5, s);
}

static inline uint32_t ssd1306_bmp_get_val(const uint8_t *data, size_t offset, uint8_t size)
{
    switch (size) {
    case 1:
        return data[offset];
    case 2:
        return (uint32_t)data[offset] | ((uint32_t)data[offset + 1u] << 8u);
    case 4:
        return (uint32_t)data[offset] |
               ((uint32_t)data[offset + 1u] << 8u) |
               ((uint32_t)data[offset + 2u] << 16u) |
               ((uint32_t)data[offset + 3u] << 24u);
    default:
        __builtin_unreachable();
    }
}

void ssd1306_bmp_show_image_with_offset(ssd1306_t *p, const uint8_t *data, const long size, uint32_t x_offset, uint32_t y_offset)
{
    if (size < 54) {
        return;
    }

    const uint32_t bf_off_bits = ssd1306_bmp_get_val(data, 10, 4);
    const uint32_t bi_size = ssd1306_bmp_get_val(data, 14, 4);
    const uint32_t bi_width = ssd1306_bmp_get_val(data, 18, 4);
    const int32_t bi_height = (int32_t)ssd1306_bmp_get_val(data, 22, 4);
    const uint16_t bi_bit_count = (uint16_t)ssd1306_bmp_get_val(data, 28, 2);
    const uint32_t bi_compression = ssd1306_bmp_get_val(data, 30, 4);

    if (bi_bit_count != 1u || bi_compression != 0u) {
        return;
    }

    const uint32_t table_start = 14u + bi_size;
    uint8_t color_val = 0;

    for (uint8_t i = 0; i < 2u; ++i) {
        if (!((data[table_start + i * 4u] << 16u) |
              (data[table_start + i * 4u + 1u] << 8u) |
              data[table_start + i * 4u + 2u])) {
            color_val = i;
            break;
        }
    }

    uint32_t bytes_per_line = (bi_width / 8u) + ((bi_width & 7u) ? 1u : 0u);
    if (bytes_per_line & 3u) {
        bytes_per_line = (bytes_per_line & ~3u) + 4u;
    }

    const uint8_t *img_data = data + bf_off_bits;
    int32_t step = bi_height > 0 ? -1 : 1;
    int32_t border = bi_height > 0 ? -1 : -bi_height;

    for (uint32_t y = bi_height > 0 ? (uint32_t)bi_height - 1u : 0u; y != (uint32_t)border; y = (uint32_t)((int32_t)y + step)) {
        for (uint32_t x = 0; x < bi_width; ++x) {
            if (((img_data[x >> 3u] >> (7u - (x & 7u))) & 1u) == color_val) {
                ssd1306_draw_pixel(p, x_offset + x, y_offset + y);
            }
        }
        img_data += bytes_per_line;
    }
}

inline void ssd1306_bmp_show_image(ssd1306_t *p, const uint8_t *data, const long size)
{
    ssd1306_bmp_show_image_with_offset(p, data, size, 0, 0);
}

void ssd1306_show(ssd1306_t *p)
{
    tc_lcd_set_window(0, 0, p->physical_width, p->physical_height);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);

    for (uint16_t y = 0; y < p->physical_height; ++y) {
        const int16_t src_y = tc_y_map[y];

        if (src_y < 0) {
            tc_fill_line(p->bg_color);
        } else {
            const uint16_t active_x_end = (uint16_t)(p->scaled_x_offset + p->scaled_width);
            uint8_t *dst = tc_spi_line_buffer;

            for (uint16_t x = 0; x < p->physical_width; ++x) {
                uint16_t color = p->bg_color;

                if (x >= p->scaled_x_offset && x < active_x_end) {
                    color = tc_get_pixel(p, tc_x_map[x], (uint16_t)src_y) ? p->fg_color : p->bg_color;
                }

                *(dst++) = (uint8_t)(color >> 8);
                *(dst++) = (uint8_t)color;
            }
        }

        spi_write_blocking(LCD_SPI_PORT, tc_spi_line_buffer, sizeof tc_spi_line_buffer);
    }

    gpio_put(LCD_PIN_CS, 1);
}
