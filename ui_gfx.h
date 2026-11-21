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

/* 實心與外框尖頂六角形 (Pointy-top Hexagon, 專為卡坦島設計) */
void ui_fill_hex_pointy(int cx, int cy, int r, uint32_t fill_rgb, uint32_t border_rgb);

/* 繪製實體骰子與卡坦島建築 (村莊、城市、強盜) */
void ui_draw_die(int x, int y, int size, int val);
void ui_draw_settlement(int cx, int cy, uint32_t col, int highlight);
void ui_draw_city(int cx, int cy, uint32_t col, int highlight);
void ui_draw_robber(int cx, int cy);

#endif
