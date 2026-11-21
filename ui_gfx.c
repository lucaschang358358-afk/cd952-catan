#include "ui_gfx.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t FONT5X7[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x00,0x36,0x36,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x08,0x08,0x3E,0x08,0x08}, {0x08,0x08,0x08,0x08,0x08}
};

static int aidx(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    if (ch == ':') return 36;
    if (ch == '/') return 37;
    if (ch == '+') return 38;
    if (ch == '-') return 39;
    return -1;
}

uint32_t ui_blend(uint32_t a, uint32_t b, int pct_b) {
    if (pct_b <= 0) return a;
    if (pct_b >= 100) return b;
    int pa = 100 - pct_b;
    int r  = ((int)((a >> 16) & 0xFF) * pa + (int)((b >> 16) & 0xFF) * pct_b) / 100;
    int g  = ((int)((a >> 8) & 0xFF)  * pa + (int)((b >> 8) & 0xFF)  * pct_b) / 100;
    int bl = ((int)(a & 0xFF)         * pa + (int)(b & 0xFF)         * pct_b) / 100;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void ui_fill_rrect(int x, int y, int w, int h, int r, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    if (r <= 1) { hal_fill_rect(x, y, w, h, rgb); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    hal_fill_rect(x, y + r, w, h - r * 2, rgb);
    int r2 = r * r;
    for (int dy = 0; dy < r; dy++) {
        int cy = r - 1 - dy, dx = 0;
        while ((dx + 1) * (dx + 1) + cy * cy <= r2) dx++;
        int inset = r - dx; if (inset < 0) inset = 0;
        hal_fill_rect(x + inset, y + dy, w - inset * 2, 1, rgb);
        hal_fill_rect(x + inset, y + h - 1 - dy, w - inset * 2, 1, rgb);
    }
}

void ui_draw_rrect(int x, int y, int w, int h, int r, uint32_t rgb) {
    if (w <= 2 || h <= 2) return;
    if (r <= 1) { hal_draw_rect(x, y, w, h, rgb); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    hal_fill_rect(x + r, y, w - r * 2, 1, rgb);
    hal_fill_rect(x + r, y + h - 1, w - r * 2, 1, rgb);
    hal_fill_rect(x, y + r, 1, h - r * 2, rgb);
    hal_fill_rect(x + w - 1, y + r, 1, h - r * 2, rgb);
    int r2 = r * r;
    for (int dy = 0; dy < r; dy++) {
        int cy = r - 1 - dy, dx = 0;
        while ((dx + 1) * (dx + 1) + cy * cy <= r2) dx++;
        int inset = r - dx;
        hal_put_pixel(x + inset, y + dy, rgb);
        hal_put_pixel(x + w - 1 - inset, y + dy, rgb);
        hal_put_pixel(x + inset, y + h - 1 - dy, rgb);
        hal_put_pixel(x + w - 1 - inset, y + h - 1 - dy, rgb);
    }
}

void ui_card(int x, int y, int w, int h, int r, uint32_t bg, uint32_t border, uint32_t shadow, int selected, uint32_t accent) {
    ui_fill_rrect(x + 2, y + 3, w, h, r, shadow);
    ui_fill_rrect(x, y, w, h, r, bg);
    if (selected) {
        ui_draw_rrect(x, y, w, h, r, accent);
        ui_draw_rrect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, accent);
    } else {
        ui_draw_rrect(x, y, w, h, r, border);
    }
}

void ui_draw_ascii(int x, int y, const char *str, int scale, uint32_t color) {
    if (!str || scale < 1) return;
    int cx = x;
    for (int i = 0; str[i]; i++) {
        int id = aidx(str[i]);
        if (id >= 0) {
            for (int c = 0; c < 5; c++) {
                uint8_t bits = FONT5X7[id][c];
                for (int r = 0; r < 7; r++)
                    if (bits & (1u << r)) hal_fill_rect(cx + c * scale, y + r * scale, scale, scale, color);
            }
        }
        cx += 6 * scale;
    }
}

int ui_ascii_width(const char *str, int scale) {
    if (!str) return 0;
    int n = (int)strlen(str);
    return n > 0 ? (n * 6 * scale - scale) : 0;
}

void ui_draw_badge(int x, int y, const char *tag, uint32_t bg, uint32_t fg) {
    int tw = ui_ascii_width(tag, 1);
    ui_fill_rrect(x, y, tw + 8, 13, 4, bg);
    ui_draw_ascii(x + 4, y + 3, tag, 1, fg);
}

void ui_draw_str(int x, int y, const char *utf8, int max_w, uint32_t rgb) {
    if (!utf8 || !utf8[0]) return;
    hal_draw_line_utf8(x, y, utf8, (int)strlen(utf8), max_w, rgb);
}

void ui_draw_thick_line(int x0, int y0, int x1, int y1, int thickness, uint32_t rgb) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int half = thickness / 2;
    while (1) {
        hal_fill_rect(x0 - half, y0 - half, thickness, thickness, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* 掃描線高速繪製尖頂六角形 (Pointy-top: 頂點在上下，寬度 = r*sqrt(3)) */
void ui_fill_hex_pointy(int cx, int cy, int r, uint32_t fill_rgb, uint32_t border_rgb) {
    int half_w = (r * 866) / 1000;
    int half_r = r / 2;

    for (int dy = -r; dy <= r; dy++) {
        int span;
        int ady = dy < 0 ? -dy : dy;
        if (ady <= half_r) {
            span = half_w;
        } else {
            span = (half_w * (r - ady)) / (r - half_r);
        }
        if (span >= 0) {
            hal_fill_rect(cx - span, cy + dy, span * 2 + 1, 1, fill_rgb);
            /* 繪製外框邊緣 */
            hal_put_pixel(cx - span, cy + dy, border_rgb);
            hal_put_pixel(cx + span, cy + dy, border_rgb);
            if (ady == r) hal_fill_rect(cx - span, cy + dy, span * 2 + 1, 1, border_rgb);
        }
    }
    /* 補粗六條斜邊外框 */
    int vx[6] = { cx, cx + half_w, cx + half_w, cx, cx - half_w, cx - half_w };
    int vy[6] = { cy - r, cy - half_r, cy + half_r, cy + r, cy + half_r, cy - half_r };
    for (int i = 0; i < 6; i++) {
        hal_draw_line(vx[i], vy[i], vx[(i + 1) % 6], vy[(i + 1) % 6], border_rgb);
    }
}

void ui_draw_die(int x, int y, int sz, int val) {
    ui_fill_rrect(x + 2, y + 2, sz, sz, 6, 0x050810);
    ui_fill_rrect(x, y, sz, sz, 6, 0xFFFDF8);
    ui_draw_rrect(x, y, sz, sz, 6, 0x94A3B8);
    if (val < 1 || val > 6) return;

    int q1 = sz / 4, q2 = sz / 2, q3 = (sz * 3) / 4;
    uint32_t col = (val == 1 || val == 4) ? 0xDC2626 : 0x111827;
    int pr = (sz >= 32) ? 3 : 2;

    if (val == 1 || val == 3 || val == 5) hal_fill_circle(x + q2, y + q2, pr, col);
    if (val >= 2) { hal_fill_circle(x + q1, y + q1, pr, col); hal_fill_circle(x + q3, y + q3, pr, col); }
    if (val >= 4) { hal_fill_circle(x + q3, y + q1, pr, col); hal_fill_circle(x + q1, y + q3, pr, col); }
    if (val == 6) { hal_fill_circle(x + q1, y + q2, pr, col); hal_fill_circle(x + q3, y + q2, pr, col); }
}

void ui_draw_settlement(int cx, int cy, uint32_t col, int highlight) {
    uint32_t dark = ui_blend(col, 0x000000, 35);
    uint32_t roof = ui_blend(col, 0xFFFFFF, 22);
    if (highlight) hal_fill_circle(cx, cy, 12, 0xFEF08A);
    /* 小房子：三角屋頂 + 矩形牆身 + 黑框 */
    hal_fill_rect(cx - 7, cy - 2, 15, 10, dark);
    hal_fill_rect(cx - 6, cy - 1, 13, 8, col);
    for (int dy = 0; dy <= 7; dy++) {
        hal_fill_rect(cx - dy - 1, cy - 9 + dy, (dy + 1) * 2 + 1, 1, dark);
        if (dy > 0) hal_fill_rect(cx - dy, cy - 9 + dy, dy * 2 + 1, 1, roof);
    }
}

void ui_draw_city(int cx, int cy, uint32_t col, int highlight) {
    uint32_t dark = ui_blend(col, 0x000000, 38);
    uint32_t hi   = ui_blend(col, 0xFFFFFF, 30);
    if (highlight) hal_fill_circle(cx, cy, 14, 0xFEF08A);
    /* 城堡高塔 + 城牆 */
    hal_fill_rect(cx - 10, cy - 3, 21, 12, dark);
    hal_fill_rect(cx - 9,  cy - 2, 19, 10, col);
    hal_fill_rect(cx - 10, cy - 11, 10, 10, dark);
    hal_fill_rect(cx - 9,  cy - 10, 8, 9, hi);
    /* 屋頂尖塔 */
    for (int dy = 0; dy <= 4; dy++) {
        hal_fill_rect(cx - 5 - dy, cy - 15 + dy, dy * 2 + 1, 1, hi);
    }
    /* 城垛與金色窗 */
    hal_fill_rect(cx + 2, cy - 5, 3, 3, hi);
    hal_fill_rect(cx + 6, cy - 5, 3, 3, hi);
    hal_fill_rect(cx - 6, cy - 7, 2, 3, 0xFEF08A);
    hal_fill_rect(cx + 2, cy + 1, 3, 3, 0xFEF08A);
}

void ui_draw_robber(int cx, int cy) {
    /* 經典黑色棋子造型強盜 + 紅眼帶 */
    hal_fill_circle(cx, cy + 7, 8, 0x0F172A);
    hal_fill_rect(cx - 6, cy - 3, 13, 10, 0x1E293B);
    hal_draw_rect(cx - 6, cy - 3, 13, 10, 0x94A3B8);
    hal_fill_circle(cx, cy - 8, 6, 0x1E293B);
    hal_draw_circle(cx, cy - 8, 6, 0xE2E8F0);
    hal_fill_rect(cx - 4, cy - 9, 9, 2, 0xEF4444);
}
