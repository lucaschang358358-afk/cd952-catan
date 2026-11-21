#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    CKEY_NONE = 0,
    CKEY_UP,
    CKEY_DOWN,
    CKEY_LEFT,
    CKEY_RIGHT,
    CKEY_OK,       /* Enter */
    CKEY_SPACE,    /* Space: 擲骰 / 結束回合 / 確認 */
    CKEY_BACK,     /* ESC: 取消 / 暫停選單 */
    CKEY_ROAD,     /* 1 / R: 建道路 */
    CKEY_SETTLE,   /* 2 / S: 建村莊 */
    CKEY_CITY,     /* 3 / C: 升級城市 */
    CKEY_BUYDEV,   /* 4 / D: 買發展卡 */
    CKEY_TRADE,    /* 5 / T: 交易 */
    CKEY_PLAYDEV,  /* 6 / P: 使用發展卡 */
    CKEY_STATS,    /* 7 / G: 骰子統計與資產圖表 */
    CKEY_LOG,      /* 8 / L: 完整戰報紀錄 */
    CKEY_HELP,     /* 9 / H: 建築成本與圖鑑 */
    CKEY_ENDTURN   /* E: 結束回合 */
} CatanKey;

int  hal_get_width(void);
int  hal_get_height(void);

void hal_clear(uint32_t rgb);
void hal_put_pixel(int x, int y, uint32_t rgb);
void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_fill_circle(int cx, int cy, int r, uint32_t rgb);
void hal_draw_circle(int cx, int cy, int r, uint32_t rgb);
void hal_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb);

/* 靜態精緻地圖快取 (一次預渲染精美地形，每幀 memcpy 零卡頓!) */
int  hal_save_map_cache(int w, int h);
int  hal_restore_map_cache(void);
void hal_invalidate_map_cache(void);

void hal_set_text_bg(uint32_t rgb);
void hal_draw_line_utf8(int x, int y, const char *utf8_text, int byte_len, int max_px_w, uint32_t rgb);
void hal_clear_text_in_rect(int rx, int ry, int rw, int rh);
void hal_dim_rect(int rx, int ry, int rw, int rh);
void hal_present(void);

uint32_t hal_get_seed(void);
const char *hal_get_save_path(void);
void hal_request_quit(void);

#endif
