/*
 * 無敵 CD-952 (Besta RTOS / Project Muteki) 卡坦島完整版 雙緩衝觸控平台
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if __has_include(<muteki/common.h>)
#include <muteki/common.h>
#endif
#if __has_include(<muteki/threading.h>)
#include <muteki/threading.h>
#endif
#if __has_include(<muteki/utf16.h>)
#include <muteki/utf16.h>
#endif
#if __has_include(<muteki/draw/font.h>)
#include <muteki/draw/font.h>
#elif __has_include(<muteki/ui/font.h>)
#include <muteki/ui/font.h>
#elif __has_include(<muteki/font.h>)
#include <muteki/font.h>
#endif
#if __has_include(<muteki/draw/lcd.h>)
#include <muteki/draw/lcd.h>
#elif __has_include(<muteki/ui/lcd.h>)
#include <muteki/ui/lcd.h>
#elif __has_include(<muteki/lcd.h>)
#include <muteki/lcd.h>
#endif
#if __has_include(<muteki/draw/surface.h>)
#include <muteki/draw/surface.h>
#elif __has_include(<muteki/ui/surface.h>)
#include <muteki/ui/surface.h>
#elif __has_include(<muteki/surface.h>)
#include <muteki/surface.h>
#endif
#if __has_include(<muteki/ui/canvas.h>)
#include <muteki/ui/canvas.h>
#endif
#if __has_include(<muteki/ui/common.h>)
#include <muteki/ui/common.h>
#endif
#if __has_include(<muteki/ui.h>)
#include <muteki/ui.h>
#endif
#if __has_include(<muteki/ui/framework/event.h>)
#include <muteki/ui/framework/event.h>
#elif __has_include(<muteki/ui/event.h>)
#include <muteki/ui/event.h>
#elif __has_include(<muteki/event.h>)
#include <muteki/event.h>
#endif

#include "muteki_touch_adapt.h"
#include "hal.h"
#include "catan_game.h"

#define MAX_TEXT_QUEUE 160

typedef struct {
    int x, y, max_w;
    uint32_t fg, bg;
    char text[160];
} TextCmd;

static lcd_t *g_lcd = NULL;
static int g_w = 800, g_h = 480, g_is_xrgb = 1;
static void *g_backbuf = NULL;
static size_t g_fb_bytes = 0;

static TextCmd g_text_q[MAX_TEXT_QUEUE];
static int g_text_q_len = 0;
static uint32_t g_text_bg = 0x0F172A;
static int g_running = 1;

static inline uint16_t rgb888_to_rgb565(uint32_t rgb) {
    return (uint16_t)((((rgb >> 16) & 0xF8u) << 8) | (((rgb >> 8) & 0xFCu) << 3) | ((rgb & 0xF8u) >> 3));
}
static inline uint32_t dim75_rgb(uint32_t rgb) {
    return (((rgb & 0xFEFEFEu) >> 1) + ((rgb & 0xFCFCFCu) >> 2)) & 0xFFFFFFu;
}

static size_t utf8_to_utf16(const char *src, int src_len, UTF16 *dst, size_t max_dst) {
    size_t out = 0; int i = 0;
    while (i < src_len && out + 1 < max_dst) {
        unsigned char c = (unsigned char)src[i];
        uint32_t cp = 0;
        if ((c & 0x80) == 0) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < src_len) { cp = ((c & 0x1F) << 6) | (src[i + 1] & 0x3F); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < src_len) { cp = ((c & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F); i += 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < src_len) { cp = 0x3F; i += 4; }
        else { cp = 0x3F; i += 1; }
        dst[out++] = (UTF16)cp;
    }
    dst[out] = 0;
    return out;
}

int hal_get_width(void)  { return g_w; }
int hal_get_height(void) { return g_h; }

void hal_put_pixel(int x, int y, uint32_t rgb) {
    if (!g_backbuf || x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    if (g_is_xrgb) ((uint32_t *)g_backbuf)[y * g_w + x] = rgb;
    else ((uint16_t *)g_backbuf)[y * g_w + x] = rgb888_to_rgb565(rgb);
}

void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (!g_backbuf || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = (x + w > g_w) ? g_w : (x + w), y1 = (y + h > g_h) ? g_h : (y + h);
    int span = x1 - x0;
    if (span <= 0 || y0 >= y1) return;

    if (g_is_xrgb) {
        uint32_t *fb = (uint32_t *)g_backbuf;
        for (int py = y0; py < y1; py++) {
            uint32_t *row = fb + py * g_w + x0;
            int n = span;
            while (n >= 4) { row[0] = rgb; row[1] = rgb; row[2] = rgb; row[3] = rgb; row += 4; n -= 4; }
            while (n-- > 0) *row++ = rgb;
        }
    } else {
        uint16_t c16 = rgb888_to_rgb565(rgb);
        uint32_t c32 = ((uint32_t)c16 << 16) | (uint32_t)c16;
        uint16_t *fb = (uint16_t *)g_backbuf;
        for (int py = y0; py < y1; py++) {
            uint16_t *row = fb + py * g_w + x0;
            int n = span;
            if (((uintptr_t)row & 2u) && n > 0) { *row++ = c16; n--; }
            uint32_t *r32 = (uint32_t *)row;
            while (n >= 4) { r32[0] = c32; r32[1] = c32; r32 += 2; n -= 4; }
            row = (uint16_t *)r32;
            while (n-- > 0) *row++ = c16;
        }
    }
}

void hal_clear(uint32_t rgb) { g_text_q_len = 0; g_text_bg = rgb; hal_fill_rect(0, 0, g_w, g_h, rgb); }

void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    hal_fill_rect(x, y, w, 1, rgb); hal_fill_rect(x, y + h - 1, w, 1, rgb);
    hal_fill_rect(x, y, 1, h, rgb); hal_fill_rect(x + w - 1, y, 1, h, rgb);
}

void hal_fill_circle(int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy; if (py < 0 || py >= g_h) continue;
        int dx = 0; while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        hal_fill_rect(cx - dx, py, dx * 2 + 1, 1, rgb);
    }
}

void hal_draw_circle(int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        hal_put_pixel(cx + x, cy + y, rgb); hal_put_pixel(cx + y, cy + x, rgb);
        hal_put_pixel(cx - y, cy + x, rgb); hal_put_pixel(cx - x, cy + y, rgb);
        hal_put_pixel(cx - x, cy - y, rgb); hal_put_pixel(cx - y, cy - x, rgb);
        hal_put_pixel(cx + y, cy - x, rgb); hal_put_pixel(cx + x, cy - y, rgb);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x + 1); }
    }
}

void hal_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        hal_put_pixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void hal_set_text_bg(uint32_t rgb) { g_text_bg = rgb; }

void hal_draw_line_utf8(int x, int y, const char *utf8_text, int byte_len, int max_px_w, uint32_t rgb) {
    if (!utf8_text || byte_len <= 0 || g_text_q_len >= MAX_TEXT_QUEUE) return;
    if (y < -18 || y >= g_h || x >= g_w) return;
    TextCmd *tc = &g_text_q[g_text_q_len++];
    tc->x = x; tc->y = y;
    tc->max_w = (max_px_w > 0 && x + max_px_w <= g_w) ? max_px_w : (g_w - x);
    tc->fg = rgb; tc->bg = g_text_bg;
    size_t cp = (size_t)byte_len; if (cp >= sizeof(tc->text)) cp = sizeof(tc->text) - 1;
    memcpy(tc->text, utf8_text, cp); tc->text[cp] = '\0';
}

void hal_clear_text_in_rect(int rx, int ry, int rw, int rh) {
    int out = 0;
    for (int i = 0; i < g_text_q_len; i++) {
        TextCmd *tc = &g_text_q[i];
        int y_ov = (tc->y + 20 > ry) && (tc->y < ry + rh);
        if (!y_ov || tc->x >= rx + rw) { if (out != i) g_text_q[out] = *tc; out++; }
    }
    g_text_q_len = out;
}

void hal_dim_rect(int rx, int ry, int rw, int rh) {
    if (!g_backbuf || rw <= 0 || rh <= 0) return;
    int x0 = rx < 0 ? 0 : rx, y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw > g_w) ? g_w : (rx + rw), y1 = (ry + rh > g_h) ? g_h : (ry + rh);
    if (x0 >= x1 || y0 >= y1) return;

    if (g_is_xrgb) {
        uint32_t *fb = (uint32_t *)g_backbuf;
        for (int py = y0; py < y1; py++) {
            uint32_t *row = fb + py * g_w + x0;
            for (int n = x1 - x0; n > 0; n--) {
                uint32_t c = *row;
                *row++ = ((c & 0xFEFEFEu) >> 1) + ((c & 0xFCFCFCu) >> 2);
            }
        }
    } else {
        uint16_t *fb = (uint16_t *)g_backbuf;
        for (int py = y0; py < y1; py++) {
            uint16_t *row = fb + py * g_w + x0;
            for (int n = x1 - x0; n > 0; n--) {
                uint16_t c = *row;
                uint16_t r = (uint16_t)((((c >> 11) & 31) * 3) >> 2);
                uint16_t g = (uint16_t)((((c >> 5)  & 63) * 3) >> 2);
                uint16_t b = (uint16_t)(((c & 31) * 3) >> 2);
                *row++ = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
    }
    for (int i = 0; i < g_text_q_len; i++) {
        g_text_q[i].fg = dim75_rgb(g_text_q[i].fg);
        g_text_q[i].bg = dim75_rgb(g_text_q[i].bg);
    }
}

void hal_present(void) {
    if (!g_lcd || !g_lcd->surface || !g_lcd->surface->buffer || !g_backbuf) return;
    memcpy(g_lcd->surface->buffer, g_backbuf, g_fb_bytes);
    if (g_text_q_len > 0) {
        SetFontType(MONOSPACE_CJK);
        UTF16 u16[192];
        for (int i = 0; i < g_text_q_len; i++) {
            TextCmd *tc = &g_text_q[i];
            int dx = tc->x < 0 ? 0 : tc->x, mw = tc->max_w;
            if (dx + mw > g_w) mw = g_w - dx;
            if (mw <= 8) continue;
            utf8_to_utf16(tc->text, (int)strlen(tc->text), u16, 192);
            rgbSetColor(tc->fg);
            rgbSetBkColor(tc->bg);
            WriteAlignString(dx, tc->y, u16, mw, STR_ALIGN_LEFT, PRINT_NONE);
        }
        g_text_q_len = 0;
    }
}

uint32_t hal_get_seed(void) { return (uint32_t)time(NULL) ^ 0xCAFE55AAu; }
const char *hal_get_save_path(void) { return "C:\\cd952_catan.sav"; }
void hal_request_quit(void) { g_running = 0; }

static CatanKey map_key(short code) {
    switch (code) {
        case 1:  return CKEY_BACK;
        case 2:  return CKEY_LEFT;
        case 3:  return CKEY_UP;
        case 4:  return CKEY_RIGHT;
        case 5:  return CKEY_DOWN;
        case 10: case 13: return CKEY_OK;
        case 32: return CKEY_SPACE;
        case 49: case 82: case 114: return CKEY_ROAD;
        case 50: case 83: case 115: return CKEY_SETTLE;
        case 51: case 67: case 99:  return CKEY_CITY;
        case 52: case 68: case 100: return CKEY_BUYDEV;
        case 53: case 84: case 116: return CKEY_TRADE;
        case 54: case 80: case 112: return CKEY_PLAYDEV;
        case 69: case 101:          return CKEY_ENDTURN;
        default: return CKEY_NONE;
    }
}

int main(void) {
    g_lcd = GetActiveLCD();
    if (!g_lcd || !g_lcd->surface || !g_lcd->surface->buffer) return 1;

    g_w = (int)g_lcd->width;
    g_h = (int)g_lcd->height;
    g_is_xrgb = (g_lcd->surface->depth == LCD_SURFACE_PIXFMT_XRGB);
    g_fb_bytes = (size_t)g_w * (size_t)g_h * (g_is_xrgb ? 4u : 2u);
    g_backbuf = malloc(g_fb_bytes);
    if (!g_backbuf) return 1;

    catan_init();
    catan_render();

    ui_event_t ev = {0};
    int touch_cd = 0;
    while (g_running) {
        if (touch_cd > 0) touch_cd--;
        while ((TestKeyEvent(&ev) || MUTEKI_TEST_TOUCH(&ev)) && GetEvent(&ev)) {
            if (ev.event_type == UI_EVENT_TYPE_KEY) {
                CatanKey k = map_key(ev.key_code0);
                if (k != CKEY_NONE) catan_on_key(k);
            } else if (MUTEKI_HAS_TOUCH && MUTEKI_IS_TOUCH_DOWN(ev) && touch_cd == 0) {
                int tx = MUTEKI_TOUCH_X(ev), ty = MUTEKI_TOUCH_Y(ev);
                if (tx >= 0 && ty >= 0 && tx < g_w && ty < g_h) {
                    catan_on_touch(tx, ty);
                    touch_cd = 4;
                }
            }
            ClearEvent(&ev);
        }
        if (!g_running) break;

        catan_tick(30);
        if (catan_needs_redraw()) catan_render();
        OSSleep(30);
    }

    catan_shutdown();
    free(g_backbuf);
    return 0;
}
