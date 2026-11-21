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
    {0x08,0x08,0x3E,0x08,0x08}, {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}
};

static int aidx(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    if (ch == ':') return 36;
    if (ch == '/') return 37;
    if (ch == '+') return 38;
    if (ch == '-') return 39;
    if (ch == '.') return 40;
    if (ch == '!') return 41;
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

/* 多層次海景漸層 + 淺海珊瑚藍光暈 + 精緻波浪紋理 (只渲染一次進快取，遊戲中 0 負擔!) */
void ui_draw_ocean_backdrop(int w, int h, int island_cx, int island_cy) {
    /* 1. 深海垂直漸層 */
    for (int y = 0; y < h; y++) {
        int pct = (y * 100) / (h > 1 ? h : 1);
        uint32_t col = ui_blend(0x0B2E4E, 0x0E4A73, pct);
        hal_fill_rect(0, y, w, 1, col);
    }

    /* 2. 島嶼周圍由深到淺的熱帶淺海光暈圈 */
    hal_fill_circle(island_cx, island_cy, 232, 0x0F5582);
    hal_fill_circle(island_cx, island_cy, 206, 0x13699B);
    hal_fill_circle(island_cx, island_cy, 184, 0x1D82B5);
    hal_fill_circle(island_cx, island_cy, 166, 0x38A3C7);

    /* 3. 散佈精緻白色海浪波紋 */
    for (int wy = 18; wy < h - 18; wy += 34) {
        for (int wx = 18 + ((wy / 34) & 1) * 32; wx < w - 24; wx += 64) {
            int dx = wx - island_cx, dy = wy - island_cy;
            if (dx * dx + dy * dy < 168 * 168) continue;
            uint32_t wc = (dx * dx + dy * dy < 220 * 220) ? 0x67C5E0 : 0x246894;
            hal_fill_rect(wx, wy, 7, 1, wc);
            hal_fill_rect(wx + 6, wy + 1, 5, 1, wc);
            hal_fill_rect(wx + 10, wy, 7, 1, wc);
        }
    }

    /* 4. 左下角小型航海羅盤裝飾 */
    int cx = 42, cy = h - 78;
    hal_draw_circle(cx, cy, 18, 0x3882B0);
    hal_draw_circle(cx, cy, 14, 0x1E577D);
    hal_draw_line(cx, cy - 16, cx, cy + 16, 0x7DD3FC);
    hal_draw_line(cx - 16, cy, cx + 16, cy, 0x7DD3FC);
    hal_fill_circle(cx, cy, 3, 0xFEF08A);
    ui_draw_ascii(cx - 2, cy - 26, "N", 1, 0xBAE6FD);
}

void ui_fill_hex_pointy(int cx, int cy, int r, uint32_t fill_rgb, uint32_t border_rgb) {
    int half_w = (r * 866) / 1000;
    int half_r = r / 2;
    for (int dy = -r; dy <= r; dy++) {
        int ady = dy < 0 ? -dy : dy;
        int span = (ady <= half_r) ? half_w : (half_w * (r - ady)) / (r - half_r);
        if (span >= 0) {
            /* 微妙立體垂直光照漸層：上方略亮、下方略沉 */
            int gpct = ((dy + r) * 18) / (r * 2 + 1);
            uint32_t row_col = ui_blend(fill_rgb, 0x000000, gpct);
            hal_fill_rect(cx - span, cy + dy, span * 2 + 1, 1, row_col);
        }
    }
    ui_draw_hex_outline(cx, cy, r, 1, border_rgb);
}

void ui_draw_hex_outline(int cx, int cy, int r, int thickness, uint32_t rgb) {
    int half_w = (r * 866) / 1000;
    int half_r = r / 2;
    int vx[6] = { cx, cx + half_w, cx + half_w, cx, cx - half_w, cx - half_w };
    int vy[6] = { cy - r, cy - half_r, cy + half_r, cy + r, cy + half_r, cy - half_r };
    for (int i = 0; i < 6; i++) {
        if (thickness <= 1) hal_draw_line(vx[i], vy[i], vx[(i + 1) % 6], vy[(i + 1) % 6], rgb);
        else ui_draw_thick_line(vx[i], vy[i], vx[(i + 1) % 6], vy[(i + 1) % 6], thickness, rgb);
    }
}

/* 輔助：畫一棵精緻立體松樹 */
static void draw_pine_tree(int x, int y, int sz) {
    hal_fill_rect(x - 1, y + sz / 2, 3, 5, 0x5C3A21);
    for (int dy = 0; dy < sz; dy++) {
        int hw = (dy * 2) / 3 + 1;
        hal_fill_rect(x - hw, y - sz / 2 + dy, hw * 2 + 1, 1, (dy < sz / 2) ? 0x16A34A : 0x14532D);
    }
    hal_put_pixel(x, y - sz / 2, 0x4ADE80);
}

/* 輔助：畫一隻可愛白色綿羊 */
static void draw_mini_sheep(int x, int y) {
    hal_fill_rect(x - 4, y + 3, 2, 3, 0x1E293B);
    hal_fill_rect(x + 2, y + 3, 2, 3, 0x1E293B);
    hal_fill_circle(x - 2, y, 4, 0xF8FAFC);
    hal_fill_circle(x + 2, y, 4, 0xFFFFFF);
    hal_fill_circle(x, y - 2, 4, 0xFFFFFF);
    hal_fill_circle(x + 6, y - 1, 2, 0x334155);
}

/* 輔助：畫一座雪頂山峰 */
static void draw_snow_peak(int x, int y, int h, int hw) {
    for (int dy = 0; dy < h; dy++) {
        int w = (hw * dy) / (h > 1 ? h : 1);
        int is_snow = (dy < h / 3);
        uint32_t left_c  = is_snow ? 0xF8FAFC : 0x64748B;
        uint32_t right_c = is_snow ? 0xCBD5E1 : 0x334155;
        hal_fill_rect(x - w, y - h / 2 + dy, w + 1, 1, left_c);
        hal_fill_rect(x,     y - h / 2 + dy, w + 1, 1, right_c);
    }
}

/* 7 種卡坦島手繪質感地形插圖 (全部畫於六角形內，避開中央數字圓片區 r=15) */
void ui_draw_hex_artwork(int cx, int cy, int r, int terrain_type, uint32_t seed) {
    (void)seed;
    /* 外圈溫暖海岸沙邊內襯 */
    ui_draw_hex_outline(cx, cy, r - 1, 1, 0xFDE68A);

    switch (terrain_type) {
        case 0: { /* 森林 (木頭): 環繞多層深淺松樹林 */
            static const int TX[7] = { -18,  18, -22,  22,   0, -14,  14 };
            static const int TY[7] = { -16, -15,   6,   7, -26,  22,  22 };
            for (int i = 0; i < 7; i++) draw_pine_tree(cx + TX[i], cy + TY[i], 11);
            break;
        }
        case 1: { /* 丘陵 (磚塊): 陶土山丘 + 紅磚疊塊 */
            hal_fill_circle(cx - 14, cy - 14, 11, 0xEA580C);
            hal_fill_circle(cx + 15, cy - 12, 10, 0x9A3412);
            hal_fill_circle(cx - 12, cy + 20, 10, 0xB45309);
            /* 紅磚塊圖案 */
            hal_fill_rect(cx + 10, cy + 16, 12, 5, 0xDC2626);
            hal_draw_rect(cx + 10, cy + 16, 12, 5, 0xFED7AA);
            hal_fill_rect(cx + 13, cy + 12, 12, 5, 0xB91C1C);
            hal_draw_rect(cx + 13, cy + 12, 12, 5, 0xFED7AA);
            hal_fill_rect(cx - 23, cy - 4, 10, 4, 0xDC2626);
            hal_draw_rect(cx - 23, cy - 4, 10, 4, 0xFED7AA);
            break;
        }
        case 2: { /* 牧場 (羊毛): 嫩綠草坡 + 木圍欄 + 可愛白綿羊 */
            hal_fill_circle(cx - 14, cy - 14, 12, 0x84CC16);
            hal_fill_circle(cx + 14, cy + 16, 12, 0x4D7C0F);
            /* 小木圍欄 */
            hal_fill_rect(cx - 24, cy - 22, 18, 2, 0x92400E);
            hal_fill_rect(cx - 22, cy - 25, 2, 7, 0x78350F);
            hal_fill_rect(cx - 10, cy - 25, 2, 7, 0x78350F);
            draw_mini_sheep(cx + 15, cy - 17);
            draw_mini_sheep(cx - 16, cy + 18);
            draw_mini_sheep(cx + 16, cy + 17);
            break;
        }
        case 3: { /* 農田 (小麥): 金黃麥浪條紋 + 麥穗束 */
            for (int dy = -26; dy <= 26; dy += 6) {
                int hw = (dy < -16 || dy > 16) ? 16 : 26;
                hal_fill_rect(cx - hw, cy + dy, hw * 2, 2, (dy & 4) ? 0xFACC15 : 0xA16207);
            }
            /* 金色麥穗點綴 */
            static const int WX[6] = { -18, 18, -22, 22, -12, 14 };
            static const int WY[6] = { -16, -16, 6, 8, 22, 22 };
            for (int i = 0; i < 6; i++) {
                hal_fill_rect(cx + WX[i], cy + WY[i] - 4, 2, 9, 0xFEF08A);
                hal_fill_rect(cx + WX[i] - 2, cy + WY[i] - 2, 6, 2, 0xFDE047);
            }
            break;
        }
        case 4: { /* 山脈 (礦石): 壯麗雪頂群峰 + 銀藍礦脈閃光 */
            draw_snow_peak(cx - 14, cy - 12, 22, 14);
            draw_snow_peak(cx + 15, cy - 10, 20, 13);
            draw_snow_peak(cx - 12, cy + 18, 18, 13);
            draw_snow_peak(cx + 14, cy + 19, 16, 12);
            /* 礦石結晶高光 */
            hal_fill_rect(cx - 18, cy + 16, 4, 4, 0x38BDF8);
            hal_fill_rect(cx + 18, cy + 15, 4, 4, 0xBAE6FD);
            hal_fill_rect(cx + 12, cy - 10, 3, 3, 0x7DD3FC);
            break;
        }
        case 5: { /* 沙漠: 沙丘波紋 + 綠色仙人掌 */
            for (int dy = -22; dy <= 22; dy += 9) {
                hal_fill_rect(cx - 18, cy + dy, 16, 2, 0xB8935F);
                hal_fill_rect(cx + 4,  cy + dy + 3, 15, 2, 0xE7C999);
            }
            /* 小仙人掌 */
            int kx = cx - 15, ky = cy + 12;
            hal_fill_rect(kx, ky - 8, 4, 14, 0x15803D);
            hal_fill_rect(kx - 4, ky - 4, 4, 3, 0x16A34A);
            hal_fill_rect(kx - 4, ky - 7, 2, 4, 0x16A34A);
            hal_fill_rect(kx + 4, ky - 2, 4, 3, 0x16A34A);
            hal_fill_rect(kx + 6, ky - 6, 2, 5, 0x16A34A);
            break;
        }
        case 6: { /* 黃金河擴充地形: 閃耀藍河 + 燦爛金塊! */
            ui_draw_thick_line(cx - 24, cy - 16, cx + 24, cy + 16, 9, 0x0EA5E9);
            ui_draw_thick_line(cx - 24, cy - 16, cx + 24, cy + 16, 4, 0x7DD3FC);
            static const int GX[6] = { -16, 18, -20, 16, -6, 10 };
            static const int GY[6] = { 12, -14, -4, 4, -22, 22 };
            for (int i = 0; i < 6; i++) {
                hal_fill_circle(cx + GX[i], cy + GY[i], 4, 0xFACC15);
                hal_put_pixel(cx + GX[i] - 1, cy + GY[i] - 1, 0xFFFFFF);
            }
            break;
        }
        default: break;
    }
}

void ui_draw_harbor_ship(int cx, int cy, uint32_t badge_col, const char *label) {
    /* 可愛帆船剪影 (白帆 + 棕船身) */
    for (int dy = 0; dy < 7; dy++) {
        hal_fill_rect(cx - 1, cy - 16 + dy, dy + 2, 1, 0xF8FAFC);
    }
    hal_fill_rect(cx - 2, cy - 16, 1, 9, 0x92400E);
    hal_fill_rect(cx - 7, cy - 8, 15, 3, 0x78350F);

    /* 清楚港口匯率徽章 */
    ui_fill_rrect(cx - 17, cy - 4, 34, 15, 5, badge_col);
    ui_draw_rrect(cx - 17, cy - 4, 34, 15, 5, 0xFEF08A);
    int tw = ui_ascii_width(label, 1);
    ui_draw_ascii(cx - tw / 2, cy, label, 1, 0xFFFFFF);
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
    uint32_t dark = ui_blend(col, 0x000000, 38);
    uint32_t roof = ui_blend(col, 0xFFFFFF, 25);
    if (highlight) hal_fill_circle(cx, cy, 13, 0xFEF08A);
    hal_fill_circle(cx + 2, cy + 4, 8, 0x09131F);
    hal_fill_rect(cx - 7, cy - 2, 15, 10, dark);
    hal_fill_rect(cx - 6, cy - 1, 13, 8, col);
    hal_fill_rect(cx - 2, cy + 2, 4, 5, 0xFEF08A); /* 溫暖小門窗 */
    for (int dy = 0; dy <= 7; dy++) {
        hal_fill_rect(cx - dy - 1, cy - 9 + dy, (dy + 1) * 2 + 1, 1, dark);
        if (dy > 0) hal_fill_rect(cx - dy, cy - 9 + dy, dy * 2 + 1, 1, roof);
    }
}

void ui_draw_city(int cx, int cy, uint32_t col, int highlight) {
    uint32_t dark = ui_blend(col, 0x000000, 40);
    uint32_t hi   = ui_blend(col, 0xFFFFFF, 32);
    if (highlight) hal_fill_circle(cx, cy, 15, 0xFEF08A);
    hal_fill_circle(cx + 2, cy + 5, 10, 0x09131F);
    hal_fill_rect(cx - 10, cy - 3, 21, 12, dark);
    hal_fill_rect(cx - 9,  cy - 2, 19, 10, col);
    hal_fill_rect(cx - 10, cy - 12, 10, 11, dark);
    hal_fill_rect(cx - 9,  cy - 11, 8, 10, hi);
    for (int dy = 0; dy <= 4; dy++) hal_fill_rect(cx - 5 - dy, cy - 16 + dy, dy * 2 + 1, 1, hi);
    hal_fill_rect(cx + 2, cy - 6, 3, 4, hi);
    hal_fill_rect(cx + 6, cy - 6, 3, 4, hi);
    hal_fill_rect(cx - 6, cy - 8, 2, 3, 0xFEF08A);
    hal_fill_rect(cx + 2, cy + 1, 3, 3, 0xFEF08A);
}

void ui_draw_robber(int cx, int cy) {
    hal_fill_circle(cx + 2, cy + 9, 8, 0x090D16);
    hal_fill_circle(cx, cy + 7, 8, 0x0F172A);
    hal_fill_rect(cx - 6, cy - 3, 13, 10, 0x1E293B);
    hal_draw_rect(cx - 6, cy - 3, 13, 10, 0xCBD5E1);
    hal_fill_circle(cx, cy - 8, 6, 0x1E293B);
    hal_draw_circle(cx, cy - 8, 6, 0xF8FAFC);
    hal_fill_rect(cx - 4, cy - 9, 9, 2, 0xEF4444);
}

void ui_draw_res_icon(int cx, int cy, int res_type) {
    switch (res_type) {
        case 0: draw_pine_tree(cx, cy, 10); break;
        case 1:
            hal_fill_rect(cx - 6, cy - 2, 12, 5, 0xDC2626);
            hal_draw_rect(cx - 6, cy - 2, 12, 5, 0xFED7AA);
            hal_fill_rect(cx - 4, cy - 6, 10, 5, 0x991B1B);
            hal_draw_rect(cx - 4, cy - 6, 10, 5, 0xFED7AA);
            break;
        case 2: draw_mini_sheep(cx, cy); break;
        case 3:
            hal_fill_rect(cx - 1, cy - 6, 2, 12, 0xFEF08A);
            hal_fill_rect(cx - 4, cy - 4, 8, 2, 0xFDE047);
            hal_fill_rect(cx - 4, cy,     8, 2, 0xFDE047);
            break;
        default: draw_snow_peak(cx, cy, 12, 8); break;
    }
}
