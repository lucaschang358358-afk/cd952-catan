#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdint.h>
#include "hal.h"

uint32_t ui_blend(uint32_t a, uint32_t b, int pct_b);
void ui_fill_rrect(int x, int y, int w, int h, int r, uint32_t rgb);
void ui_draw_rrect(int x, int y, int w, int h, int r, uint32_t rgb);
void ui_card(int x, int y, int w, int h, int r, uint32_t bg, uint32_t border, uint32_t shadow, int selected, uint32_t accent);
void ui_draw_ascii(int x, int y, const char *str, int scale, uint32_t color);
int  ui_ascii_width(const char *str, int scale);
void ui_draw_badge(int x, int y, const char *tag, uint32_t bg, uint32_t fg);
void ui_draw_str(int x, int y, const char *utf8, int max_w, uint32_t rgb);
void ui_draw_thick_line(int x0, int y0, int x1, int y1, int thickness, uint32_t rgb);

/* 精緻海浪海洋與沙灘背景 */
void ui_draw_ocean_backdrop(int w, int h, int island_cx, int island_cy);

/* 精美繪製尖頂六角地形 (含 7 種獨立手繪風像素插圖: 森林/丘陵/牧場/農田/山脈/沙漠/黃金河) */
void ui_fill_hex_pointy(int cx, int cy, int r, uint32_t fill_rgb, uint32_t border_rgb);
void ui_draw_hex_outline(int cx, int cy, int r, int thickness, uint32_t rgb);
void ui_draw_hex_artwork(int cx, int cy, int r, int terrain_type, uint32_t seed);

/* 精緻港口碼頭 + 小帆船 + 建築與骰子 */
void ui_draw_harbor_ship(int cx, int cy, uint32_t badge_col, const char *label);
void ui_draw_die(int x, int y, int size, int val);
void ui_draw_settlement(int cx, int cy, uint32_t col, int highlight);
void ui_draw_city(int cx, int cy, uint32_t col, int highlight);
void ui_draw_robber(int cx, int cy);
void ui_draw_res_icon(int cx, int cy, int res_type);

#endif
