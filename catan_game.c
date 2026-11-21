#include "catan_game.h"
#include "catan_map.h"
#include "ui_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * CD-952 卡坦島豪華完整版 v2 (Catan Deluxe Complete Edition)
 *  - 靜態精美地形快取 (海洋漸層+波浪+羅盤+7種手繪地形插圖+帆船港口，一次預繪零卡頓!)
 *  - 4 種地圖模式 (經典/黃金河擴充/富饒海港/雙沙漠)、2~4人、AI 3段難度、友善強盜、AI主動交易
 *  - 骰子2~12統計長條圖、16條完整歷史戰報、建築成本與銀行庫存圖鑑、點擊地形檢視器
 *  - 完整官方規則：蛇形開局、25張發展卡、真DFS敵村截斷最長道路、最大軍隊、雙向交易、自動存檔
 * ============================================================ */

#define MAX_PLAYERS   4
#define MAX_ROADS     15
#define MAX_SETTLES   5
#define MAX_CITIES    4
#define DEV_DECK_SIZE 25
#define MAX_HISTORY   16
#define SAVE_MAGIC    0x43415432u /* "CAT2" */

typedef enum {
    DEV_KNIGHT = 0,
    DEV_VP,
    DEV_ROAD_BUILD,
    DEV_PLENTY,
    DEV_MONOPOLY,
    NUM_DEV_TYPES
} DevType;

typedef enum {
    ST_TITLE = 0,
    ST_SETUP_MENU,
    ST_SETUP_SETTLE,
    ST_SETUP_ROAD,
    ST_PRE_ROLL,
    ST_MAIN_ACTION,
    ST_BUILD_ROAD,
    ST_BUILD_SETTLE,
    ST_BUILD_CITY,
    ST_DISCARD,
    ST_MOVE_ROBBER,
    ST_STEAL_TARGET,
    ST_TRADE_MODAL,
    ST_DEV_MODAL,
    ST_PLENTY_PICK,
    ST_MONOPOLY_PICK,
    ST_GOLD_PICK,     /* 黃金河產出：自選任意資源 */
    ST_AI_OFFER,      /* AI 主動向玩家提出交易報價! */
    ST_STATS_MODAL,   /* 骰子機率分佈長條圖 + 玩家數據 */
    ST_LOG_MODAL,     /* 16 條完整戰報紀錄 */
    ST_HELP_MODAL,    /* 建築配方與銀行/牌堆庫存圖鑑 */
    ST_PAUSE,
    ST_VICTORY
} GameState;

typedef struct {
    int is_ai;
    char name[16];
    uint32_t color;
    int res[5];
    int dev[NUM_DEV_TYPES];
    int dev_new[NUM_DEV_TYPES];
    int knights_played;
    int roads_left;
    int settles_left;
    int cities_left;
    int longest_road_len;
    int total_earned;
    int trades_done;
} Player;

typedef struct {
    uint32_t magic;
    CatanBoard board;
    Player players[MAX_PLAYERS];
    int player_count;
    int human_count;
    int win_vp;
    int map_mode;
    int ai_diff;         /* 0=新手, 1=戰術, 2=大師 */
    int friendly_robber; /* 1=友善強盜 */
    int fast_ai;         /* 1=極速 AI 節奏 */

    int bank[5];
    int dev_deck[DEV_DECK_SIZE];
    int dev_top;

    int turn_number;
    int cur_player;
    int setup_round;
    int setup_last_v;
    int die1, die2;
    int last_roll_sum;
    int dice_hist[13];   /* 2..12 擲出次數真實統計 */
    int dev_played_this_turn;
    int ai_offered_this_turn;
    int free_roads_left;
    int plenty_picks_left;

    /* 黃金河排隊自選資源 */
    int gold_queue_p[8];
    int gold_queue_cnt;
    int gold_queue_idx;

    /* AI 主動交易提案 */
    int offer_from_ai;
    int offer_to_human;
    int offer_ai_gives;
    int offer_ai_wants;

    int longest_road_owner;
    int largest_army_owner;
    int winner;

    int discard_queue[MAX_PLAYERS];
    int discard_q_idx;
    int discard_need;
    int discard_sel[5];

    GameState state;
    GameState return_state;
    GameState after_robber_state;

    char history[MAX_HISTORY][84];
    int  hist_count;
} CatanData;

static CatanData g;
static int g_redraw = 1;
static int g_ai_wait_ms = 0;
static int g_map_cached = 0;

/* 封面與設定選單狀態 */
static int g_title_sel = 0;
static int g_setup_row = 0;
static int g_cfg_map     = 0;
static int g_cfg_players = 4;
static int g_cfg_humans  = 1;
static int g_cfg_ai_diff = 1;
static int g_cfg_friendly = 0;
static int g_cfg_win_vp  = 10;
static int g_pause_sel = 0;

/* 地圖候選與地形檢視 */
static int g_cands[72];
static int g_cand_cnt = 0;
static int g_cand_idx = 0;
static int g_inspect_hex = -1;

/* 交易 / 彈窗狀態 */
static int g_trade_tab = 0;
static int g_trade_give = 0;
static int g_trade_get  = 1;
static int g_trade_give_amt = 1;
static int g_trade_target = 1;
static char g_trade_msg[80] = {0};

static int g_modal_sel = 0;
static int g_steal_cands[4];
static int g_steal_cnt = 0;

static uint32_t g_rng = 0x51A7C49Du;
static int rng_next(int m) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (int)((g_rng >> 16) % (uint32_t)m);
}

static const char *RES_NAMES[5] = { "木頭", "磚塊", "羊毛", "小麥", "礦石" };
static const char *RES_SHORT[5] = { "木", "磚", "羊", "麥", "礦" };
static const char *TERRAIN_NAMES[7] = { "森林(木)", "丘陵(磚)", "牧場(羊)", "農田(麥)", "山脈(礦)", "大沙漠", "黃金河(任選)" };
static const uint32_t RES_COLS[7] = {
    0x166534, /* 森林深綠 */
    0xC2410C, /* 丘陵赤陶 */
    0x65A30D, /* 牧場草綠 */
    0xCA8A04, /* 農田麥金 */
    0x475569, /* 山脈岩灰 */
    0xD6B483, /* 沙漠沙黃 */
    0xB45309  /* 黃金河金棕 */
};
static const uint32_t PLAYER_COLS[4] = { 0xEF4444, 0x3B82F6, 0xF59E0B, 0xE2E8F0 };
static const char *PLAYER_DEFAULTS[4] = { "紅方", "藍方", "橘方", "白方" };
static const char *MAP_MODE_NAMES[4] = { "經典卡坦島", "黃金河擴充 (含黃金格)", "富饒海港大亨", "雙沙漠硬核挑戰" };
static const char *AI_DIFF_NAMES[3]  = { "新手", "戰術 (主動交易)", "大師 (強攻+主動交易)" };
static const char *DEV_NAMES[NUM_DEV_TYPES] = {
    "騎士卡 (移強盜+偷牌)",
    "勝利點 (+1 VP)",
    "道路建設 (免費2條路)",
    "豐收之年 (任選2資源)",
    "壟斷卡 (搶走全場1種)"
};

static void push_log(const char *msg) {
    for (int i = MAX_HISTORY - 1; i > 0; i--) {
        snprintf(g.history[i], sizeof(g.history[i]), "%s", g.history[i - 1]);
    }
    snprintf(g.history[0], sizeof(g.history[0]), "%s", msg);
    if (g.hist_count < MAX_HISTORY) g.hist_count++;
}

static int has_save_file(void) {
    FILE *f = fopen(hal_get_save_path(), "rb");
    if (!f) return 0;
    uint32_t m = 0;
    int ok = (fread(&m, sizeof(m), 1, f) == 1 && m == SAVE_MAGIC);
    fclose(f);
    return ok;
}

static void save_game(void) {
    g.magic = SAVE_MAGIC;
    FILE *f = fopen(hal_get_save_path(), "wb");
    if (!f) return;
    fwrite(&g, sizeof(g), 1, f);
    fclose(f);
}

static int load_game(void) {
    FILE *f = fopen(hal_get_save_path(), "rb");
    if (!f) return 0;
    CatanData tmp;
    int ok = (fread(&tmp, sizeof(tmp), 1, f) == 1 && tmp.magic == SAVE_MAGIC);
    fclose(f);
    if (!ok) return 0;
    g = tmp;
    catan_board_rebuild_coords(&g.board, 256, 234, 42);
    g_map_cached = 0;
    hal_invalidate_map_cache();
    g_inspect_hex = -1;
    return 1;
}

/* ============================================================
 * 核心規則計算
 * ============================================================ */
static int player_total_res(int pi) {
    int sum = 0;
    for (int r = 0; r < 5; r++) sum += g.players[pi].res[r];
    return sum;
}

static int player_total_dev(int pi) {
    int sum = 0;
    for (int d = 0; d < NUM_DEV_TYPES; d++) sum += g.players[pi].dev[d] + g.players[pi].dev_new[d];
    return sum;
}

static int player_trade_rate(int pi, int res_type) {
    int rate = 4;
    PortType want = (PortType)(PORT_WOOD2 + res_type);
    for (int v = 0; v < NUM_VERTICES; v++) {
        if (g.board.verts[v].owner == pi) {
            if (g.board.verts[v].port == want) return 2;
            if (g.board.verts[v].port == PORT_ANY3 && rate > 3) rate = 3;
        }
    }
    return rate;
}

static int player_calc_vp(int pi, int include_hidden_dev) {
    int vp = 0;
    for (int v = 0; v < NUM_VERTICES; v++) if (g.board.verts[v].owner == pi) vp += g.board.verts[v].level;
    if (g.longest_road_owner == pi) vp += 2;
    if (g.largest_army_owner == pi) vp += 2;
    if (include_hidden_dev) vp += g.players[pi].dev[DEV_VP] + g.players[pi].dev_new[DEV_VP];
    return vp;
}

static int dfs_road(int pi, int v, uint8_t used_e[NUM_EDGES]) {
    int best = 0;
    const Vertex *vt = &g.board.verts[v];
    for (int i = 0; i < vt->deg; i++) {
        int ei = vt->adj_e[i], nv = vt->adj_v[i];
        if (g.board.edges[ei].owner != pi || used_e[ei]) continue;
        used_e[ei] = 1;
        int sub = 1;
        if (g.board.verts[nv].owner == -1 || g.board.verts[nv].owner == pi) sub += dfs_road(pi, nv, used_e);
        used_e[ei] = 0;
        if (sub > best) best = sub;
    }
    return best;
}

static int calc_longest_road(int pi) {
    int best = 0;
    uint8_t used_e[NUM_EDGES];
    memset(used_e, 0, sizeof(used_e));
    for (int v = 0; v < NUM_VERTICES; v++) {
        int has_own = 0;
        for (int i = 0; i < g.board.verts[v].deg; i++)
            if (g.board.edges[g.board.verts[v].adj_e[i]].owner == pi) { has_own = 1; break; }
        if (!has_own) continue;
        int len = dfs_road(pi, v, used_e);
        if (len > best) best = len;
    }
    return best;
}

static void update_awards_and_victory(void) {
    for (int p = 0; p < g.player_count; p++) g.players[p].longest_road_len = calc_longest_road(p);

    int cur_lr = g.longest_road_owner;
    if (cur_lr >= 0 && g.players[cur_lr].longest_road_len < 5) { g.longest_road_owner = -1; cur_lr = -1; }
    int cur_lr_len = (cur_lr >= 0) ? g.players[cur_lr].longest_road_len : 4;
    for (int p = 0; p < g.player_count; p++) {
        if (g.players[p].longest_road_len > cur_lr_len) {
            cur_lr_len = g.players[p].longest_road_len;
            g.longest_road_owner = p;
        }
    }

    int cur_la = g.largest_army_owner;
    int cur_la_cnt = (cur_la >= 0) ? g.players[cur_la].knights_played : 2;
    for (int p = 0; p < g.player_count; p++) {
        if (g.players[p].knights_played > cur_la_cnt) {
            cur_la_cnt = g.players[p].knights_played;
            g.largest_army_owner = p;
        }
    }

    if (g.state != ST_TITLE && g.state != ST_SETUP_MENU && g.state != ST_VICTORY) {
        if (player_calc_vp(g.cur_player, 1) >= g.win_vp) {
            g.winner = g.cur_player;
            g.state = ST_VICTORY;
            char msg[84];
            snprintf(msg, sizeof(msg), "恭喜 %s 達成 %d 分勝利點，榮登卡坦霸主！",
                     g.players[g.winner].name, player_calc_vp(g.winner, 1));
            push_log(msg);
            save_game();
        }
    }
}

static int can_place_settlement(int pi, int v, int is_setup) {
    if (v < 0 || v >= NUM_VERTICES || g.board.verts[v].owner != -1) return 0;
    for (int i = 0; i < g.board.verts[v].deg; i++)
        if (g.board.verts[g.board.verts[v].adj_v[i]].owner != -1) return 0;
    if (is_setup) return 1;
    if (g.players[pi].settles_left <= 0) return 0;
    for (int i = 0; i < g.board.verts[v].deg; i++)
        if (g.board.edges[g.board.verts[v].adj_e[i]].owner == pi) return 1;
    return 0;
}

static int can_place_road(int pi, int e, int setup_v) {
    if (e < 0 || e >= NUM_EDGES || g.board.edges[e].owner != -1 || g.players[pi].roads_left <= 0) return 0;
    int v0 = g.board.edges[e].v0, v1 = g.board.edges[e].v1;
    if (setup_v >= 0) return (v0 == setup_v || v1 == setup_v);
    int vs[2] = { v0, v1 };
    for (int side = 0; side < 2; side++) {
        int v = vs[side];
        if (g.board.verts[v].owner == pi) return 1;
        if (g.board.verts[v].owner == -1) {
            for (int i = 0; i < g.board.verts[v].deg; i++)
                if (g.board.edges[g.board.verts[v].adj_e[i]].owner == pi) return 1;
        }
    }
    return 0;
}

static int can_upgrade_city(int pi, int v) {
    return (v >= 0 && v < NUM_VERTICES && g.players[pi].cities_left > 0 &&
            g.board.verts[v].owner == pi && g.board.verts[v].level == 1);
}

static int can_afford_road(int pi)   { return g.players[pi].res[RES_WOOD] >= 1 && g.players[pi].res[RES_BRICK] >= 1 && g.players[pi].roads_left > 0; }
static int can_afford_settle(int pi) { return g.players[pi].res[RES_WOOD] >= 1 && g.players[pi].res[RES_BRICK] >= 1 && g.players[pi].res[RES_SHEEP] >= 1 && g.players[pi].res[RES_WHEAT] >= 1 && g.players[pi].settles_left > 0; }
static int can_afford_city(int pi)   { return g.players[pi].res[RES_WHEAT] >= 2 && g.players[pi].res[RES_ORE] >= 3 && g.players[pi].cities_left > 0; }
static int can_afford_dev(int pi)    { return g.players[pi].res[RES_SHEEP] >= 1 && g.players[pi].res[RES_WHEAT] >= 1 && g.players[pi].res[RES_ORE] >= 1 && g.dev_top < DEV_DECK_SIZE; }

static void give_from_bank(int pi, int res, int amt) {
    if (res < 0 || res >= 5 || amt <= 0) return;
    int take = (amt <= g.bank[res]) ? amt : g.bank[res];
    g.bank[res] -= take;
    g.players[pi].res[res] += take;
    g.players[pi].total_earned += take;
}

static void pay_to_bank(int pi, int res, int amt) {
    if (res < 0 || res >= 5 || amt <= 0) return;
    if (g.players[pi].res[res] < amt) amt = g.players[pi].res[res];
    g.players[pi].res[res] -= amt;
    g.bank[res] += amt;
}

static int is_friendly_robber_blocked_hex(int h) {
    if (!g.friendly_robber) return 0;
    for (int c = 0; c < 6; c++) {
        int ow = g.board.verts[g.board.hexes[h].verts[c]].owner;
        if (ow >= 0 && ow != g.cur_player && player_calc_vp(ow, 0) <= 2) return 1;
    }
    return 0;
}

static void rebuild_candidates(void) {
    g_cand_cnt = 0; g_cand_idx = 0;
    int pi = g.cur_player;
    if (g.state == ST_SETUP_SETTLE) {
        for (int v = 0; v < NUM_VERTICES; v++) if (can_place_settlement(pi, v, 1)) g_cands[g_cand_cnt++] = v;
    } else if (g.state == ST_SETUP_ROAD) {
        for (int e = 0; e < NUM_EDGES; e++) if (can_place_road(pi, e, g.setup_last_v)) g_cands[g_cand_cnt++] = e;
    } else if (g.state == ST_BUILD_ROAD) {
        for (int e = 0; e < NUM_EDGES; e++) if (can_place_road(pi, e, -1)) g_cands[g_cand_cnt++] = e;
    } else if (g.state == ST_BUILD_SETTLE) {
        for (int v = 0; v < NUM_VERTICES; v++) if (can_place_settlement(pi, v, 0)) g_cands[g_cand_cnt++] = v;
    } else if (g.state == ST_BUILD_CITY) {
        for (int v = 0; v < NUM_VERTICES; v++) if (can_upgrade_city(pi, v)) g_cands[g_cand_cnt++] = v;
    } else if (g.state == ST_MOVE_ROBBER) {
        for (int h = 0; h < NUM_HEXES; h++) {
            if (h != g.board.robber_hex && !is_friendly_robber_blocked_hex(h)) g_cands[g_cand_cnt++] = h;
        }
        if (g_cand_cnt == 0) {
            for (int h = 0; h < NUM_HEXES; h++) if (h != g.board.robber_hex) g_cands[g_cand_cnt++] = h;
        }
    }
}

static int ai_delay(int base_ms) { return g.fast_ai ? (base_ms / 3) : base_ms; }

/* ============================================================
 * 開局與回合流程
 * ============================================================ */
static void start_new_game(void) {
    memset(&g, 0, sizeof(g));
    g.magic = SAVE_MAGIC;
    g.map_mode        = g_cfg_map;
    g.player_count    = g_cfg_players;
    g.human_count     = g_cfg_humans;
    g.ai_diff         = g_cfg_ai_diff;
    g.friendly_robber = g_cfg_friendly;
    g.win_vp          = g_cfg_win_vp;
    g.longest_road_owner = -1;
    g.largest_army_owner = -1;
    g.winner = -1;
    g_inspect_hex = -1;

    catan_board_generate(&g.board, hal_get_seed(), g.map_mode, 256, 234, 42);
    g_map_cached = 0;
    hal_invalidate_map_cache();

    for (int r = 0; r < 5; r++) g.bank[r] = 19;

    int k = 0;
    for (int i = 0; i < 14; i++) g.dev_deck[k++] = DEV_KNIGHT;
    for (int i = 0; i < 5;  i++) g.dev_deck[k++] = DEV_VP;
    for (int i = 0; i < 2;  i++) g.dev_deck[k++] = DEV_ROAD_BUILD;
    for (int i = 0; i < 2;  i++) g.dev_deck[k++] = DEV_PLENTY;
    for (int i = 0; i < 2;  i++) g.dev_deck[k++] = DEV_MONOPOLY;
    for (int i = DEV_DECK_SIZE - 1; i > 0; i--) {
        int j = rng_next(i + 1);
        int t = g.dev_deck[i]; g.dev_deck[i] = g.dev_deck[j]; g.dev_deck[j] = t;
    }
    g.dev_top = 0;

    for (int p = 0; p < g.player_count; p++) {
        g.players[p].is_ai = (p >= g.human_count) ? 1 : 0;
        snprintf(g.players[p].name, sizeof(g.players[p].name), "%s%s", PLAYER_DEFAULTS[p], g.players[p].is_ai ? "AI" : "");
        g.players[p].color = PLAYER_COLS[p];
        g.players[p].roads_left   = MAX_ROADS;
        g.players[p].settles_left = MAX_SETTLES;
        g.players[p].cities_left  = MAX_CITIES;
    }

    g.cur_player = 0;
    g.setup_round = 0;
    g.setup_last_v = -1;
    g.state = ST_SETUP_SETTLE;
    rebuild_candidates();
    push_log("開局 1/2：請點擊發光綠點建立第 1 座村莊！");
    g_ai_wait_ms = ai_delay(300);
    g_redraw = 1;
}

static void begin_turn(int pi) {
    g.cur_player = pi;
    if (pi == 0) g.turn_number++;
    g.dev_played_this_turn = 0;
    g.ai_offered_this_turn = 0;
    g.free_roads_left = 0;
    g.plenty_picks_left = 0;
    g.last_roll_sum = 0;
    for (int d = 0; d < NUM_DEV_TYPES; d++) {
        g.players[pi].dev[d] += g.players[pi].dev_new[d];
        g.players[pi].dev_new[d] = 0;
    }
    g.state = ST_PRE_ROLL;
    char msg[84];
    snprintf(msg, sizeof(msg), "第 %d 輪 · 輪到 %s：請按 [擲雙骰] (或先出騎士卡)", g.turn_number, g.players[pi].name);
    push_log(msg);
    g_ai_wait_ms = ai_delay(280);
    save_game();
}

static void finish_setup_road(int e) {
    int pi = g.cur_player;
    g.board.edges[e].owner = pi;
    g.players[pi].roads_left--;
    update_awards_and_victory();

    if (g.setup_round == 0) {
        if (g.cur_player + 1 < g.player_count) { g.cur_player++; g.state = ST_SETUP_SETTLE; }
        else { g.setup_round = 1; g.state = ST_SETUP_SETTLE; }
    } else {
        if (g.cur_player > 0) { g.cur_player--; g.state = ST_SETUP_SETTLE; }
        else { push_log("開局完成！正式展開卡坦島開拓競賽！"); begin_turn(0); return; }
    }
    rebuild_candidates();
    char msg[84];
    snprintf(msg, sizeof(msg), "開局 %d/2：%s 請建立村莊", g.setup_round + 1, g.players[g.cur_player].name);
    push_log(msg);
    g_ai_wait_ms = ai_delay(260);
}

static void place_setup_settlement(int v) {
    int pi = g.cur_player;
    g.board.verts[v].owner = pi;
    g.board.verts[v].level = 1;
    g.players[pi].settles_left--;
    g.setup_last_v = v;

    if (g.setup_round == 1) {
        for (int i = 0; i < g.board.verts[v].hex_cnt; i++) {
            int hi = g.board.verts[v].hexes[i];
            if (hi >= 0) {
                if (g.board.hexes[hi].terrain < RES_DESERT) give_from_bank(pi, (int)g.board.hexes[hi].terrain, 1);
                else if (g.board.hexes[hi].terrain == RES_GOLD) give_from_bank(pi, RES_WHEAT, 1);
            }
        }
    }
    update_awards_and_victory();
    g.state = ST_SETUP_ROAD;
    rebuild_candidates();
    char msg[84]; snprintf(msg, sizeof(msg), "%s 請在村莊旁點擊發光藍點修路", g.players[pi].name);
    push_log(msg);
    g_ai_wait_ms = ai_delay(220);
}

/* 黃金河自選資源佇列處理 */
static void advance_gold_queue(void) {
    while (g.gold_queue_idx < g.gold_queue_cnt) {
        int pi = g.gold_queue_p[g.gold_queue_idx];
        if (g.players[pi].is_ai) {
            /* AI 挑目前最少的一種資源 */
            int min_r = 0;
            for (int r = 1; r < 5; r++) if (g.players[pi].res[r] < g.players[pi].res[min_r]) min_r = r;
            give_from_bank(pi, min_r, 1);
            g.gold_queue_idx++;
        } else {
            g_modal_sel = 0;
            g.state = ST_GOLD_PICK;
            return;
        }
    }
    g.state = ST_MAIN_ACTION;
    g_ai_wait_ms = ai_delay(280);
}

static void advance_discard_queue(void) {
    while (g.discard_q_idx < g.player_count) {
        int pi = g.discard_queue[g.discard_q_idx];
        int tot = player_total_res(pi);
        if (tot > 7) {
            int need = tot / 2;
            if (g.players[pi].is_ai) {
                for (int k = 0; k < need; k++) {
                    int best_r = 0;
                    for (int r = 1; r < 5; r++) if (g.players[pi].res[r] > g.players[pi].res[best_r]) best_r = r;
                    pay_to_bank(pi, best_r, 1);
                }
                char msg[84]; snprintf(msg, sizeof(msg), "%s 手牌過多棄掉 %d 張", g.players[pi].name, need);
                push_log(msg);
                g.discard_q_idx++;
                continue;
            } else {
                g.discard_need = need;
                memset(g.discard_sel, 0, sizeof(g.discard_sel));
                g_modal_sel = 0;
                g.state = ST_DISCARD;
                return;
            }
        }
        g.discard_q_idx++;
    }
    g.state = ST_MOVE_ROBBER;
    rebuild_candidates();
    char msg[84]; snprintf(msg, sizeof(msg), "%s 請點選六角地形移動強盜！", g.players[g.cur_player].name);
    push_log(msg);
    g_ai_wait_ms = ai_delay(280);
}

static void execute_steal_from(int victim) {
    int pi = g.cur_player;
    int tot = player_total_res(victim);
    if (tot > 0) {
        int pick = rng_next(tot), acc = 0, stolen = 0;
        for (int r = 0; r < 5; r++) { acc += g.players[victim].res[r]; if (pick < acc) { stolen = r; break; } }
        g.players[victim].res[stolen]--;
        g.players[pi].res[stolen]++;
        char msg[84];
        if (!g.players[pi].is_ai)
            snprintf(msg, sizeof(msg), "從 %s 偷到了 1 張【%s】！", g.players[victim].name, RES_NAMES[stolen]);
        else
            snprintf(msg, sizeof(msg), "%s 從 %s 偷走 1 張資源", g.players[pi].name, g.players[victim].name);
        push_log(msg);
    }
    g.state = g.after_robber_state;
    g_ai_wait_ms = ai_delay(260);
}

static void move_robber_to(int hex_idx) {
    if (hex_idx < 0 || hex_idx >= NUM_HEXES || hex_idx == g.board.robber_hex) return;
    g.board.robber_hex = hex_idx;

    g_steal_cnt = 0;
    for (int c = 0; c < 6; c++) {
        int vi = g.board.hexes[hex_idx].verts[c];
        int ow = g.board.verts[vi].owner;
        if (ow >= 0 && ow != g.cur_player && player_total_res(ow) > 0) {
            if (g.friendly_robber && player_calc_vp(ow, 0) <= 2) continue;
            int dup = 0;
            for (int k = 0; k < g_steal_cnt; k++) if (g_steal_cands[k] == ow) dup = 1;
            if (!dup) g_steal_cands[g_steal_cnt++] = ow;
        }
    }

    if (g_steal_cnt == 0) {
        push_log("強盜已移動（周圍無可搶奪對手）");
        g.state = g.after_robber_state;
        g_ai_wait_ms = ai_delay(260);
    } else if (g_steal_cnt == 1) {
        execute_steal_from(g_steal_cands[0]);
    } else if (g.players[g.cur_player].is_ai) {
        int best = g_steal_cands[0];
        for (int k = 1; k < g_steal_cnt; k++)
            if (player_calc_vp(g_steal_cands[k], 0) > player_calc_vp(best, 0)) best = g_steal_cands[k];
        execute_steal_from(best);
    } else {
        g_modal_sel = 0;
        g.state = ST_STEAL_TARGET;
    }
}

static void do_roll_dice(void) {
    g.die1 = 1 + rng_next(6);
    g.die2 = 1 + rng_next(6);
    int sum = g.die1 + g.die2;
    g.last_roll_sum = sum;
    if (sum >= 2 && sum <= 12) g.dice_hist[sum]++;

    if (sum == 7) {
        char msg[84];
        snprintf(msg, sizeof(msg), "%s 擲出 7！強盜出沒！超過 7 張須棄半", g.players[g.cur_player].name);
        push_log(msg);
        g.after_robber_state = ST_MAIN_ACTION;
        for (int i = 0; i < g.player_count; i++) g.discard_queue[i] = (g.cur_player + i) % g.player_count;
        g.discard_q_idx = 0;
        advance_discard_queue();
        return;
    }

    int gained_any = 0;
    g.gold_queue_cnt = 0; g.gold_queue_idx = 0;
    for (int h = 0; h < NUM_HEXES; h++) {
        if (g.board.hexes[h].number != sum || h == g.board.robber_hex) continue;
        TerrainType t = g.board.hexes[h].terrain;
        if (t == RES_DESERT) continue;
        for (int c = 0; c < 6; c++) {
            int vi = g.board.hexes[h].verts[c];
            int ow = g.board.verts[vi].owner;
            if (ow >= 0) {
                gained_any = 1;
                if (t == RES_GOLD) {
                    for (int k = 0; k < g.board.verts[vi].level && g.gold_queue_cnt < 8; k++)
                        g.gold_queue_p[g.gold_queue_cnt++] = ow;
                } else {
                    give_from_bank(ow, (int)t, g.board.verts[vi].level);
                }
            }
        }
    }

    char msg[84];
    snprintf(msg, sizeof(msg), "%s 擲出 %d (%d+%d) · %s",
             g.players[g.cur_player].name, sum, g.die1, g.die2,
             gained_any ? "亮金框地形已產出資源！" : "本輪無人領取資源");
    push_log(msg);

    if (g.gold_queue_cnt > 0) advance_gold_queue();
    else { g.state = ST_MAIN_ACTION; g_ai_wait_ms = ai_delay(280); }
}

/* ============================================================
 * 建設與發展卡
 * ============================================================ */
static void execute_build_road(int e) {
    int pi = g.cur_player;
    if (!can_place_road(pi, e, -1)) return;
    if (g.free_roads_left > 0) g.free_roads_left--;
    else {
        if (!can_afford_road(pi)) return;
        pay_to_bank(pi, RES_WOOD, 1); pay_to_bank(pi, RES_BRICK, 1);
    }
    g.board.edges[e].owner = pi;
    g.players[pi].roads_left--;
    update_awards_and_victory();
    if (g.state == ST_VICTORY) return;

    if (g.free_roads_left > 0) {
        g.state = ST_BUILD_ROAD; rebuild_candidates();
        if (g_cand_cnt > 0) { push_log("道路建設卡：請修建第 2 條免費道路！"); return; }
        g.free_roads_left = 0;
    }
    g.state = ST_MAIN_ACTION;
    char msg[84]; snprintf(msg, sizeof(msg), "%s 修建了道路 (最長連續 %d)", g.players[pi].name, g.players[pi].longest_road_len);
    push_log(msg);
}

static void execute_build_settle(int v) {
    int pi = g.cur_player;
    if (!can_place_settlement(pi, v, 0) || !can_afford_settle(pi)) return;
    pay_to_bank(pi, RES_WOOD, 1); pay_to_bank(pi, RES_BRICK, 1);
    pay_to_bank(pi, RES_SHEEP, 1); pay_to_bank(pi, RES_WHEAT, 1);
    g.board.verts[v].owner = pi; g.board.verts[v].level = 1;
    g.players[pi].settles_left--;
    g.state = ST_MAIN_ACTION;
    char msg[84]; snprintf(msg, sizeof(msg), "%s 建立新村莊！(+1 VP)", g.players[pi].name);
    push_log(msg);
    update_awards_and_victory();
}

static void execute_build_city(int v) {
    int pi = g.cur_player;
    if (!can_upgrade_city(pi, v) || !can_afford_city(pi)) return;
    pay_to_bank(pi, RES_WHEAT, 2); pay_to_bank(pi, RES_ORE, 3);
    g.board.verts[v].level = 2;
    g.players[pi].cities_left--;
    g.players[pi].settles_left++;
    g.state = ST_MAIN_ACTION;
    char msg[84]; snprintf(msg, sizeof(msg), "%s 升級繁華城市！(雙倍產量)", g.players[pi].name);
    push_log(msg);
    update_awards_and_victory();
}

static void execute_buy_dev(void) {
    int pi = g.cur_player;
    if (!can_afford_dev(pi)) return;
    pay_to_bank(pi, RES_SHEEP, 1); pay_to_bank(pi, RES_WHEAT, 1); pay_to_bank(pi, RES_ORE, 1);
    int card = g.dev_deck[g.dev_top++];
    g.players[pi].dev_new[card]++;
    char msg[84];
    if (!g.players[pi].is_ai) snprintf(msg, sizeof(msg), "抽到發展卡：【%s】", DEV_NAMES[card]);
    else snprintf(msg, sizeof(msg), "%s 購買 1 張發展卡 (剩餘 %d)", g.players[pi].name, DEV_DECK_SIZE - g.dev_top);
    push_log(msg);
    update_awards_and_victory();
}

static void execute_play_dev(int dev_type, GameState return_state) {
    int pi = g.cur_player;
    if (dev_type == DEV_VP || g.dev_played_this_turn || g.players[pi].dev[dev_type] <= 0) return;
    g.players[pi].dev[dev_type]--;
    g.dev_played_this_turn = 1;

    if (dev_type == DEV_KNIGHT) {
        g.players[pi].knights_played++;
        update_awards_and_victory();
        if (g.state == ST_VICTORY) return;
        g.after_robber_state = return_state;
        g.state = ST_MOVE_ROBBER;
        rebuild_candidates();
        char msg[84]; snprintf(msg, sizeof(msg), "%s 出動騎士！請移動強盜並偷牌", g.players[pi].name);
        push_log(msg);
    } else if (dev_type == DEV_ROAD_BUILD) {
        g.free_roads_left = 2; g.state = ST_BUILD_ROAD; rebuild_candidates();
        if (g_cand_cnt == 0) { g.free_roads_left = 0; g.state = return_state; }
        else push_log("打出【道路建設】：可免費修 2 條路！");
    } else if (dev_type == DEV_PLENTY) {
        g.plenty_picks_left = 2; g_modal_sel = 0; g.state = ST_PLENTY_PICK;
        push_log("打出【豐收之年】：請任選 2 張銀行資源");
    } else if (dev_type == DEV_MONOPOLY) {
        g_modal_sel = 0; g.state = ST_MONOPOLY_PICK;
        push_log("打出【壟斷卡】：請選擇要搶光全場的資源");
    }
}

/* ============================================================
 * 智慧 AI (含主動向人類玩家提出交易提案!)
 * ============================================================ */
static int score_vertex_for_ai(int v) {
    int sc = 0, seen[5] = {0};
    for (int i = 0; i < g.board.verts[v].hex_cnt; i++) {
        int hi = g.board.verts[v].hexes[i];
        if (hi < 0 || g.board.hexes[hi].terrain == RES_DESERT) continue;
        int pips = catan_hex_pips(g.board.hexes[hi].number);
        int mult = (g.board.hexes[hi].terrain == RES_GOLD) ? 18 : 12;
        sc += pips * mult;
        if (g.board.hexes[hi].terrain < 5 && !seen[g.board.hexes[hi].terrain]) { seen[g.board.hexes[hi].terrain] = 1; sc += 9; }
    }
    if (g.board.verts[v].port != PORT_NONE) sc += 12;
    return sc + rng_next(g.ai_diff == 0 ? 30 : 4);
}

static int ai_try_propose_trade_to_human(int pi) {
    if (g.ai_diff == 0 || g.ai_offered_this_turn || g.human_count <= 0) return 0;
    g.ai_offered_this_turn = 1;
    if (rng_next(100) > 45) return 0; /* 適度頻率，不煩人 */

    int want = -1, give = -1;
    for (int r = 0; r < 5; r++) {
        if (g.players[pi].res[r] == 0) want = r;
        if (g.players[pi].res[r] >= 2) give = r;
    }
    if (want < 0 || give < 0 || want == give) return 0;

    /* 找手上真的有 want 資源的人類玩家 */
    for (int h = 0; h < g.player_count; h++) {
        if (!g.players[h].is_ai && g.players[h].res[want] >= 1) {
            g.offer_from_ai  = pi;
            g.offer_to_human = h;
            g.offer_ai_gives = give;
            g.offer_ai_wants = want;
            g_modal_sel = 0;
            g.state = ST_AI_OFFER;
            return 1;
        }
    }
    return 0;
}

static void ai_step(void) {
    int pi = g.cur_player;
    if (!g.players[pi].is_ai) return;

    if (g.state == ST_SETUP_SETTLE) {
        rebuild_candidates();
        int best_v = g_cands[0], best_sc = -999;
        for (int i = 0; i < g_cand_cnt; i++) {
            int sc = score_vertex_for_ai(g_cands[i]);
            if (sc > best_sc) { best_sc = sc; best_v = g_cands[i]; }
        }
        place_setup_settlement(best_v);
        return;
    }

    if (g.state == ST_SETUP_ROAD) {
        rebuild_candidates();
        if (g_cand_cnt > 0) finish_setup_road(g_cands[rng_next(g_cand_cnt)]);
        return;
    }

    if (g.state == ST_PRE_ROLL) {
        if (g.players[pi].dev[DEV_KNIGHT] > 0 && !g.dev_played_this_turn) {
            int blocked = 0;
            for (int c = 0; c < 6; c++)
                if (g.board.verts[g.board.hexes[g.board.robber_hex].verts[c]].owner == pi) blocked = 1;
            if (blocked || g.ai_diff >= 2) { execute_play_dev(DEV_KNIGHT, ST_PRE_ROLL); return; }
        }
        do_roll_dice();
        return;
    }

    if (g.state == ST_MOVE_ROBBER) {
        rebuild_candidates();
        int best_h = g_cands[0], best_sc = -9999;
        for (int i = 0; i < g_cand_cnt; i++) {
            int h = g_cands[i];
            if (g.board.hexes[h].terrain == RES_DESERT) continue;
            int sc = 0, pips = catan_hex_pips(g.board.hexes[h].number);
            for (int c = 0; c < 6; c++) {
                int ow = g.board.verts[g.board.hexes[h].verts[c]].owner;
                if (ow == pi) sc -= pips * 25;
                else if (ow >= 0) sc += pips * (10 + player_calc_vp(ow, 0) * 3);
            }
            if (sc > best_sc) { best_sc = sc; best_h = h; }
        }
        move_robber_to(best_h);
        return;
    }

    if (g.state == ST_MAIN_ACTION) {
        /* 1. 主動與玩家提議交易 */
        if (ai_try_propose_trade_to_human(pi)) return;

        /* 2. 銀行/港口交易 */
        int min_r = 0;
        for (int r = 1; r < 5; r++) if (g.players[pi].res[r] < g.players[pi].res[min_r]) min_r = r;
        for (int r = 0; r < 5; r++) {
            if (r == min_r) continue;
            int rate = player_trade_rate(pi, r);
            if (g.players[pi].res[r] >= rate + 1 && g.bank[min_r] > 0) {
                pay_to_bank(pi, r, rate); give_from_bank(pi, min_r, 1);
                g.players[pi].trades_done++;
                break;
            }
        }

        if (can_afford_city(pi)) {
            for (int v = 0; v < NUM_VERTICES; v++)
                if (can_upgrade_city(pi, v)) { execute_build_city(v); g_ai_wait_ms = ai_delay(260); return; }
        }
        if (can_afford_settle(pi)) {
            int best_v = -1, best_sc = -999;
            for (int v = 0; v < NUM_VERTICES; v++)
                if (can_place_settlement(pi, v, 0)) { int sc = score_vertex_for_ai(v); if (sc > best_sc) { best_sc = sc; best_v = v; } }
            if (best_v >= 0) { execute_build_settle(best_v); g_ai_wait_ms = ai_delay(260); return; }
        }
        if (can_afford_road(pi) && g.players[pi].roads_left > 2) {
            int cands[72], cnt = 0;
            for (int e = 0; e < NUM_EDGES; e++) if (can_place_road(pi, e, -1)) cands[cnt++] = e;
            if (cnt > 0 && (g.players[pi].res[RES_WOOD] >= 2 || g.players[pi].res[RES_BRICK] >= 2 || rng_next(100) < 55)) {
                execute_build_road(cands[rng_next(cnt)]); g_ai_wait_ms = ai_delay(240); return;
            }
        }
        if (can_afford_dev(pi)) { execute_buy_dev(); g_ai_wait_ms = ai_delay(240); return; }

        if (!g.dev_played_this_turn) {
            if (g.players[pi].dev[DEV_PLENTY] > 0) {
                g.players[pi].dev[DEV_PLENTY]--; g.dev_played_this_turn = 1;
                give_from_bank(pi, RES_ORE, 1); give_from_bank(pi, RES_WHEAT, 1);
                push_log("AI 打出【豐收之年】領取 2 資源"); g_ai_wait_ms = ai_delay(240); return;
            }
            if (g.players[pi].dev[DEV_MONOPOLY] > 0) {
                g.players[pi].dev[DEV_MONOPOLY]--; g.dev_played_this_turn = 1;
                int br = 0, bs = -1;
                for (int r = 0; r < 5; r++) {
                    int s = 0; for (int p = 0; p < g.player_count; p++) if (p != pi) s += g.players[p].res[r];
                    if (s > bs) { bs = s; br = r; }
                }
                int got = 0;
                for (int p = 0; p < g.player_count; p++)
                    if (p != pi) { got += g.players[p].res[br]; g.players[pi].res[br] += g.players[p].res[br]; g.players[p].res[br] = 0; }
                char msg[84]; snprintf(msg, sizeof(msg), "%s 打出壟斷卡搶走 %d 張%s！", g.players[pi].name, got, RES_NAMES[br]);
                push_log(msg); g_ai_wait_ms = ai_delay(280); return;
            }
        }

        begin_turn((pi + 1) % g.player_count);
    }
}

/* ============================================================
 * 對外生命週期、輸入與彈窗邏輯
 * ============================================================ */
void catan_init(void) {
    g_rng = hal_get_seed();
    if (g_rng == 0) g_rng = 0x20261121u;
    memset(&g, 0, sizeof(g));
    g.state = ST_TITLE;
    g_title_sel = 0;
    g_map_cached = 0;
    g_redraw = 1;
}

void catan_shutdown(void) {
    if (g.state != ST_TITLE && g.state != ST_SETUP_MENU) save_game();
}

int catan_needs_redraw(void) { return g_redraw; }

void catan_tick(int dt_ms) {
    if (g.state == ST_TITLE || g.state == ST_SETUP_MENU || g.state == ST_PAUSE ||
        g.state == ST_VICTORY || g.state == ST_DISCARD || g.state == ST_GOLD_PICK ||
        g.state == ST_AI_OFFER || g.state == ST_STATS_MODAL || g.state == ST_LOG_MODAL ||
        g.state == ST_HELP_MODAL) return;

    if (g.players[g.cur_player].is_ai) {
        g_ai_wait_ms -= dt_ms;
        if (g_ai_wait_ms <= 0) { ai_step(); g_redraw = 1; }
    }
}

static void open_info_modal(GameState st) {
    g.return_state = g.state;
    g.state = st;
}

static void open_trade_modal(void) {
    g_trade_tab = 0; g_trade_give = 0; g_trade_get = 1;
    g_trade_give_amt = 1;
    g_trade_target = (g.cur_player + 1) % g.player_count;
    g_trade_msg[0] = '\0';
    g.state = ST_TRADE_MODAL;
}

static void execute_trade_confirm(void) {
    int pi = g.cur_player;
    if (g_trade_give == g_trade_get) { snprintf(g_trade_msg, sizeof(g_trade_msg), "換出與換入資源不能相同！"); return; }
    if (g_trade_tab == 0) {
        int rate = player_trade_rate(pi, g_trade_give);
        if (g.players[pi].res[g_trade_give] < rate) { snprintf(g_trade_msg, sizeof(g_trade_msg), "%s不足 %d 張！", RES_NAMES[g_trade_give], rate); return; }
        if (g.bank[g_trade_get] <= 0) { snprintf(g_trade_msg, sizeof(g_trade_msg), "銀行%s已空！", RES_NAMES[g_trade_get]); return; }
        pay_to_bank(pi, g_trade_give, rate); give_from_bank(pi, g_trade_get, 1);
        g.players[pi].trades_done++;
        snprintf(g_trade_msg, sizeof(g_trade_msg), "港口/銀行成交：%d%s -> 1%s", rate, RES_SHORT[g_trade_give], RES_SHORT[g_trade_get]);
    } else {
        int tgt = g_trade_target;
        if (tgt == pi) { snprintf(g_trade_msg, sizeof(g_trade_msg), "不能與自己交易"); return; }
        if (g.players[pi].res[g_trade_give] < g_trade_give_amt) { snprintf(g_trade_msg, sizeof(g_trade_msg), "你的%s不足！", RES_NAMES[g_trade_give]); return; }
        if (g.players[tgt].res[g_trade_get] < 1) { snprintf(g_trade_msg, sizeof(g_trade_msg), "%s沒有%s！", g.players[tgt].name, RES_NAMES[g_trade_get]); return; }
        int accept = 1;
        if (g.players[tgt].is_ai) {
            int needs = (g.players[tgt].res[g_trade_give] <= 1);
            int surplus = (g.players[tgt].res[g_trade_get] >= 2);
            accept = (g_trade_give_amt >= 2) || (needs && surplus) || (needs && rng_next(100) < 55);
        }
        if (!accept) { snprintf(g_trade_msg, sizeof(g_trade_msg), "%s拒絕交易 (試試出價 2:1)", g.players[tgt].name); return; }
        g.players[pi].res[g_trade_give] -= g_trade_give_amt;
        g.players[tgt].res[g_trade_give] += g_trade_give_amt;
        g.players[tgt].res[g_trade_get] -= 1;
        g.players[pi].res[g_trade_get] += 1;
        g.players[pi].trades_done++; g.players[tgt].trades_done++;
        snprintf(g_trade_msg, sizeof(g_trade_msg), "與%s成交：%d%s <-> 1%s！", g.players[tgt].name, g_trade_give_amt, RES_SHORT[g_trade_give], RES_SHORT[g_trade_get]);
    }
}

void catan_on_key(CatanKey key) {
    g_redraw = 1;

    if (g.state == ST_TITLE) {
        int has_sav = has_save_file(), n = has_sav ? 3 : 2;
        if (key == CKEY_UP) g_title_sel = (g_title_sel + n - 1) % n;
        else if (key == CKEY_DOWN) g_title_sel = (g_title_sel + 1) % n;
        else if (key == CKEY_BACK) hal_request_quit();
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            if (has_sav && g_title_sel == 0) { if (load_game()) return; }
            int off = has_sav ? 1 : 0;
            if (g_title_sel == off) { g_setup_row = 0; g.state = ST_SETUP_MENU; }
            else hal_request_quit();
        }
        return;
    }

    if (g.state == ST_SETUP_MENU) {
        if (key == CKEY_BACK) { g.state = ST_TITLE; return; }
        if (key == CKEY_UP) g_setup_row = (g_setup_row + 6) % 7;
        else if (key == CKEY_DOWN) g_setup_row = (g_setup_row + 1) % 7;
        else if (key == CKEY_LEFT || key == CKEY_RIGHT) {
            int d = (key == CKEY_RIGHT) ? 1 : -1;
            if (g_setup_row == 0) g_cfg_map = (g_cfg_map + 4 + d) % 4;
            else if (g_setup_row == 1) {
                g_cfg_players += d; if (g_cfg_players < 2) g_cfg_players = 4; if (g_cfg_players > 4) g_cfg_players = 2;
                if (g_cfg_humans > g_cfg_players) g_cfg_humans = g_cfg_players;
            } else if (g_setup_row == 2) {
                g_cfg_humans += d; if (g_cfg_humans < 1) g_cfg_humans = g_cfg_players; if (g_cfg_humans > g_cfg_players) g_cfg_humans = 1;
            } else if (g_setup_row == 3) g_cfg_ai_diff = (g_cfg_ai_diff + 3 + d) % 3;
            else if (g_setup_row == 4) g_cfg_friendly = !g_cfg_friendly;
            else if (g_setup_row == 5) { g_cfg_win_vp += d * 2; if (g_cfg_win_vp < 8) g_cfg_win_vp = 12; if (g_cfg_win_vp > 12) g_cfg_win_vp = 8; }
        } else if (key == CKEY_OK || key == CKEY_SPACE) start_new_game();
        return;
    }

    if (g.state == ST_STATS_MODAL || g.state == ST_LOG_MODAL || g.state == ST_HELP_MODAL) {
        if (key == CKEY_BACK || key == CKEY_OK || key == CKEY_SPACE) g.state = g.return_state;
        return;
    }

    if (g.state == ST_PAUSE) {
        if (key == CKEY_BACK) { g.state = g.return_state; return; }
        if (key == CKEY_UP) g_pause_sel = (g_pause_sel + 3) % 4;
        else if (key == CKEY_DOWN) g_pause_sel = (g_pause_sel + 1) % 4;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            if (g_pause_sel == 0) g.state = g.return_state;
            else if (g_pause_sel == 1) { g.fast_ai = !g.fast_ai; save_game(); }
            else if (g_pause_sel == 2) { save_game(); g.state = ST_TITLE; }
            else hal_request_quit();
        }
        return;
    }

    if (g.state == ST_VICTORY) {
        if (key == CKEY_OK || key == CKEY_SPACE || key == CKEY_BACK) g.state = ST_TITLE;
        return;
    }

    if (g.state == ST_AI_OFFER) {
        if (key == CKEY_LEFT || key == CKEY_RIGHT) g_modal_sel = 1 - g_modal_sel;
        else if (key == CKEY_BACK) {
            push_log("你拒絕了 AI 的交易提議");
            g.state = ST_MAIN_ACTION; g_ai_wait_ms = ai_delay(200);
        } else if (key == CKEY_OK || key == CKEY_SPACE) {
            if (g_modal_sel == 0) {
                int ai = g.offer_from_ai, hu = g.offer_to_human;
                g.players[ai].res[g.offer_ai_gives]--; g.players[hu].res[g.offer_ai_gives]++;
                g.players[hu].res[g.offer_ai_wants]--; g.players[ai].res[g.offer_ai_wants]++;
                g.players[ai].trades_done++; g.players[hu].trades_done++;
                char msg[84]; snprintf(msg, sizeof(msg), "與 %s 成交：獲得 %s、換出 %s！", g.players[ai].name, RES_NAMES[g.offer_ai_gives], RES_NAMES[g.offer_ai_wants]);
                push_log(msg);
            } else {
                push_log("你拒絕了 AI 的交易提議");
            }
            g.state = ST_MAIN_ACTION; g_ai_wait_ms = ai_delay(200);
        }
        return;
    }

    if (g.state == ST_GOLD_PICK) {
        if (key == CKEY_LEFT) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            int pi = g.gold_queue_p[g.gold_queue_idx];
            give_from_bank(pi, g_modal_sel, 1);
            char msg[84]; snprintf(msg, sizeof(msg), "黃金河豐收：%s 選取 1 張【%s】！", g.players[pi].name, RES_NAMES[g_modal_sel]);
            push_log(msg);
            g.gold_queue_idx++;
            advance_gold_queue();
        }
        return;
    }

    if (g.state == ST_DISCARD) {
        int pi = g.discard_queue[g.discard_q_idx];
        int chosen = 0; for (int r = 0; r < 5; r++) chosen += g.discard_sel[r];
        if (key == CKEY_LEFT) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_UP && chosen < g.discard_need && g.discard_sel[g_modal_sel] < g.players[pi].res[g_modal_sel]) g.discard_sel[g_modal_sel]++;
        else if (key == CKEY_DOWN && g.discard_sel[g_modal_sel] > 0) g.discard_sel[g_modal_sel]--;
        else if ((key == CKEY_OK || key == CKEY_SPACE) && chosen == g.discard_need) {
            for (int r = 0; r < 5; r++) pay_to_bank(pi, r, g.discard_sel[r]);
            g.discard_q_idx++; advance_discard_queue();
        }
        return;
    }

    if (g.state == ST_STEAL_TARGET) {
        if (key == CKEY_UP || key == CKEY_LEFT) g_modal_sel = (g_modal_sel + g_steal_cnt - 1) % g_steal_cnt;
        else if (key == CKEY_DOWN || key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % g_steal_cnt;
        else if (key == CKEY_OK || key == CKEY_SPACE) execute_steal_from(g_steal_cands[g_modal_sel]);
        return;
    }

    if (g.state == ST_TRADE_MODAL) {
        if (key == CKEY_BACK) { g.state = ST_MAIN_ACTION; return; }
        if (key == CKEY_LEFT) g_trade_give = (g_trade_give + 4) % 5;
        else if (key == CKEY_RIGHT) g_trade_give = (g_trade_give + 1) % 5;
        else if (key == CKEY_UP) g_trade_get = (g_trade_get + 4) % 5;
        else if (key == CKEY_DOWN) g_trade_get = (g_trade_get + 1) % 5;
        else if (key == CKEY_TRADE) g_trade_tab = 1 - g_trade_tab;
        else if (key == CKEY_OK || key == CKEY_SPACE) execute_trade_confirm();
        return;
    }

    if (g.state == ST_DEV_MODAL) {
        if (key == CKEY_BACK) { g.state = g.return_state; return; }
        if (key == CKEY_UP) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_DOWN) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            if (g.return_state == ST_PRE_ROLL && g_modal_sel != DEV_KNIGHT) { push_log("擲骰前僅可打出騎士卡！"); return; }
            execute_play_dev(g_modal_sel, g.return_state);
        }
        return;
    }

    if (g.state == ST_PLENTY_PICK || g.state == ST_MONOPOLY_PICK) {
        if (key == CKEY_LEFT) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            int pi = g.cur_player;
            if (g.state == ST_PLENTY_PICK) {
                give_from_bank(pi, g_modal_sel, 1); g.plenty_picks_left--;
                if (g.plenty_picks_left <= 0) { g.state = ST_MAIN_ACTION; push_log("已領取 2 張豐收資源！"); }
            } else {
                int got = 0;
                for (int p = 0; p < g.player_count; p++)
                    if (p != pi) { got += g.players[p].res[g_modal_sel]; g.players[pi].res[g_modal_sel] += g.players[p].res[g_modal_sel]; g.players[p].res[g_modal_sel] = 0; }
                char msg[84]; snprintf(msg, sizeof(msg), "壟斷【%s】成功！搶走全場 %d 張！", RES_NAMES[g_modal_sel], got);
                push_log(msg); g.state = ST_MAIN_ACTION;
            }
        }
        return;
    }

    if (g.players[g.cur_player].is_ai) return;

    /* 快捷查看統計/戰報/圖鑑 */
    if (g.state == ST_PRE_ROLL || g.state == ST_MAIN_ACTION) {
        if (key == CKEY_STATS) { open_info_modal(ST_STATS_MODAL); return; }
        if (key == CKEY_LOG)   { open_info_modal(ST_LOG_MODAL); return; }
        if (key == CKEY_HELP)  { open_info_modal(ST_HELP_MODAL); return; }
    }

    if (g.state == ST_SETUP_SETTLE || g.state == ST_SETUP_ROAD ||
        g.state == ST_BUILD_ROAD || g.state == ST_BUILD_SETTLE ||
        g.state == ST_BUILD_CITY || g.state == ST_MOVE_ROBBER) {
        if (key == CKEY_BACK && (g.state == ST_BUILD_ROAD || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) && g.free_roads_left == 0) {
            g.state = ST_MAIN_ACTION; return;
        }
        if (g_cand_cnt > 0) {
            if (key == CKEY_LEFT || key == CKEY_UP) g_cand_idx = (g_cand_idx + g_cand_cnt - 1) % g_cand_cnt;
            else if (key == CKEY_RIGHT || key == CKEY_DOWN) g_cand_idx = (g_cand_idx + 1) % g_cand_cnt;
            else if (key == CKEY_OK || key == CKEY_SPACE) {
                int c = g_cands[g_cand_idx];
                if (g.state == ST_SETUP_SETTLE) place_setup_settlement(c);
                else if (g.state == ST_SETUP_ROAD) finish_setup_road(c);
                else if (g.state == ST_BUILD_ROAD) execute_build_road(c);
                else if (g.state == ST_BUILD_SETTLE) execute_build_settle(c);
                else if (g.state == ST_BUILD_CITY) execute_build_city(c);
                else if (g.state == ST_MOVE_ROBBER) move_robber_to(c);
            }
        }
        return;
    }

    if (g.state == ST_PRE_ROLL) {
        if (key == CKEY_OK || key == CKEY_SPACE) do_roll_dice();
        else if (key == CKEY_PLAYDEV) { g_modal_sel = 0; g.return_state = ST_PRE_ROLL; g.state = ST_DEV_MODAL; }
        else if (key == CKEY_BACK) { g_pause_sel = 0; g.return_state = ST_PRE_ROLL; g.state = ST_PAUSE; }
        return;
    }

    if (g.state == ST_MAIN_ACTION) {
        if (key == CKEY_BACK) { g_pause_sel = 0; g.return_state = ST_MAIN_ACTION; g.state = ST_PAUSE; }
        else if (key == CKEY_ROAD && can_afford_road(g.cur_player)) { g.state = ST_BUILD_ROAD; rebuild_candidates(); if (g_cand_cnt == 0) g.state = ST_MAIN_ACTION; }
        else if (key == CKEY_SETTLE && can_afford_settle(g.cur_player)) { g.state = ST_BUILD_SETTLE; rebuild_candidates(); if (g_cand_cnt == 0) g.state = ST_MAIN_ACTION; }
        else if (key == CKEY_CITY && can_afford_city(g.cur_player)) { g.state = ST_BUILD_CITY; rebuild_candidates(); if (g_cand_cnt == 0) g.state = ST_MAIN_ACTION; }
        else if (key == CKEY_BUYDEV && can_afford_dev(g.cur_player)) execute_buy_dev();
        else if (key == CKEY_TRADE) open_trade_modal();
        else if (key == CKEY_PLAYDEV) { g_modal_sel = 0; g.return_state = ST_MAIN_ACTION; g.state = ST_DEV_MODAL; }
        else if (key == CKEY_ENDTURN || key == CKEY_SPACE) begin_turn((g.cur_player + 1) % g.player_count);
    }
}

void catan_on_touch(int tx, int ty) {
    int sw = hal_get_width(), sh = hal_get_height();
    g_redraw = 1;

    if (g.state == ST_TITLE) {
        int has_sav = has_save_file(), n = has_sav ? 3 : 2;
        int pw = 480, px = (sw - pw) / 2, py = 46;
        for (int i = 0; i < n; i++) {
            int ry = py + 158 + i * 54;
            if (tx >= px + 60 && tx <= px + pw - 60 && ty >= ry && ty <= ry + 42) { g_title_sel = i; catan_on_key(CKEY_OK); return; }
        }
        return;
    }

    if (g.state == ST_SETUP_MENU) {
        int pw = 540, px = (sw - pw) / 2, py = 20;
        for (int i = 0; i < 7; i++) {
            int ry = py + 52 + i * 52;
            if (tx >= px + 24 && tx <= px + pw - 24 && ty >= ry && ty <= ry + 42) {
                g_setup_row = i;
                if (i == 6) start_new_game();
                else catan_on_key(tx < sw / 2 ? CKEY_LEFT : CKEY_RIGHT);
                return;
            }
        }
        return;
    }

    if (g.state == ST_STATS_MODAL || g.state == ST_LOG_MODAL || g.state == ST_HELP_MODAL) {
        g.state = g.return_state;
        return;
    }

    if (g.state == ST_PAUSE) {
        int pw = 360, ph = 266, px = (sw - pw) / 2, py = (sh - ph) / 2;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { g.state = g.return_state; return; }
        int idx = (ty - (py + 56)) / 48;
        if (idx >= 0 && idx < 4) { g_pause_sel = idx; catan_on_key(CKEY_OK); }
        return;
    }

    if (g.state == ST_VICTORY) { g.state = ST_TITLE; return; }

    if (g.state == ST_AI_OFFER) {
        int mh = 200, my = (sh - mh) / 2;
        if (ty >= my + 132) {
            g_modal_sel = (tx < sw / 2) ? 0 : 1;
            catan_on_key(CKEY_OK);
        }
        return;
    }

    if (g.state == ST_GOLD_PICK || g.state == ST_PLENTY_PICK || g.state == ST_MONOPOLY_PICK) {
        int mw = 520, mh = 200, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        int r = (tx - (mx + 20)) / 96;
        if (r >= 0 && r < 5 && ty >= my + 74 && ty <= my + 148) { g_modal_sel = r; catan_on_key(CKEY_OK); }
        return;
    }

    if (g.state == ST_DISCARD) {
        int mw = 520, mh = 280, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        int pi = g.discard_queue[g.discard_q_idx];
        int chosen = 0; for (int r = 0; r < 5; r++) chosen += g.discard_sel[r];
        if (ty >= my + 74 && ty <= my + 190) {
            int r = (tx - (mx + 20)) / 96;
            if (r >= 0 && r < 5) {
                if (ty < my + 132 && chosen < g.discard_need && g.discard_sel[r] < g.players[pi].res[r]) g.discard_sel[r]++;
                else if (ty >= my + 132 && g.discard_sel[r] > 0) g.discard_sel[r]--;
            }
            return;
        }
        if (ty >= my + 214 && chosen == g.discard_need) catan_on_key(CKEY_OK);
        return;
    }

    if (g.state == ST_STEAL_TARGET) {
        int mh = 220, my = (sh - mh) / 2;
        int idx = (ty - (my + 60)) / 46;
        if (idx >= 0 && idx < g_steal_cnt) execute_steal_from(g_steal_cands[idx]);
        return;
    }

    if (g.state == ST_TRADE_MODAL) {
        int mw = 560, mh = 360, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        if (tx < mx || tx > mx + mw || ty < my || ty > my + mh) { g.state = ST_MAIN_ACTION; return; }
        if (ty >= my + 48 && ty <= my + 82) { g_trade_tab = (tx < mx + mw / 2) ? 0 : 1; g_trade_msg[0] = '\0'; return; }
        if (ty >= my + 110 && ty <= my + 158) { int r = (tx - (mx + 20)) / 104; if (r >= 0 && r < 5) g_trade_give = r; return; }
        if (ty >= my + 186 && ty <= my + 234) { int r = (tx - (mx + 20)) / 104; if (r >= 0 && r < 5) g_trade_get = r; return; }
        if (g_trade_tab == 1 && ty >= my + 246 && ty <= my + 282) {
            if (tx < mx + mw / 2) {
                g_trade_target = (g_trade_target + 1) % g.player_count;
                if (g_trade_target == g.cur_player) g_trade_target = (g_trade_target + 1) % g.player_count;
            } else g_trade_give_amt = (g_trade_give_amt == 1) ? 2 : 1;
            return;
        }
        if (ty >= my + 300) { if (tx < mx + mw / 2) execute_trade_confirm(); else g.state = ST_MAIN_ACTION; }
        return;
    }

    if (g.state == ST_DEV_MODAL) {
        int mw = 460, mh = 310, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        if (tx < mx || tx > mx + mw || ty < my || ty > my + mh) { g.state = g.return_state; return; }
        int idx = (ty - (my + 54)) / 44;
        if (idx >= 0 && idx < NUM_DEV_TYPES) { g_modal_sel = idx; catan_on_key(CKEY_OK); }
        return;
    }

    if (g.players[g.cur_player].is_ai) return;

    /* 地圖區域觸控 (x < 524) */
    if (tx < 524) {
        if (g.state == ST_SETUP_SETTLE || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) {
            int bv = -1, bd = 24 * 24;
            for (int i = 0; i < g_cand_cnt; i++) {
                int v = g_cands[i], dx = g.board.verts[v].x - tx, dy = g.board.verts[v].y - ty;
                if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; bv = v; }
            }
            if (bv >= 0) {
                if (g.state == ST_SETUP_SETTLE) place_setup_settlement(bv);
                else if (g.state == ST_BUILD_SETTLE) execute_build_settle(bv);
                else execute_build_city(bv);
            }
            return;
        }
        if (g.state == ST_SETUP_ROAD || g.state == ST_BUILD_ROAD) {
            int be = -1, bd = 24 * 24;
            for (int i = 0; i < g_cand_cnt; i++) {
                int e = g_cands[i], dx = g.board.edges[e].mx - tx, dy = g.board.edges[e].my - ty;
                if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; be = e; }
            }
            if (be >= 0) { if (g.state == ST_SETUP_ROAD) finish_setup_road(be); else execute_build_road(be); }
            return;
        }
        if (g.state == ST_MOVE_ROBBER) {
            int bh = -1, bd = 42 * 42;
            for (int i = 0; i < g_cand_cnt; i++) {
                int h = g_cands[i], dx = g.board.hexes[h].cx - tx, dy = g.board.hexes[h].cy - ty;
                if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; bh = h; }
            }
            if (bh >= 0) move_robber_to(bh);
            return;
        }
        /* 點擊地圖任何地形塊顯示地形檢視卡！ */
        int best_h = -1, bd = 40 * 40;
        for (int h = 0; h < NUM_HEXES; h++) {
            int dx = g.board.hexes[h].cx - tx, dy = g.board.hexes[h].cy - ty;
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best_h = h; }
        }
        g_inspect_hex = (g_inspect_hex == best_h) ? -1 : best_h;
        return;
    }

    /* 右側欄觸控按鈕 (x >= 526) */
    int rx = 526;
    /* 4 顆功能工具膠囊 (y = 264..290): [統計] [戰報] [圖鑑] [選單] */
    if ((g.state == ST_PRE_ROLL || g.state == ST_MAIN_ACTION) && ty >= 262 && ty <= 292) {
        int slot = (tx - (rx + 6)) / 66;
        if (slot == 0) open_info_modal(ST_STATS_MODAL);
        else if (slot == 1) open_info_modal(ST_LOG_MODAL);
        else if (slot == 2) open_info_modal(ST_HELP_MODAL);
        else if (slot == 3) { g_pause_sel = 0; g.return_state = g.state; g.state = ST_PAUSE; }
        return;
    }

    if (g.state == ST_BUILD_ROAD || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) {
        if (ty >= 424 && g.free_roads_left == 0) g.state = ST_MAIN_ACTION;
        return;
    }
    if (g.state == ST_PRE_ROLL) {
        if (ty >= 316 && ty <= 368) do_roll_dice();
        else if (ty >= 376 && ty <= 416) { g_modal_sel = 0; g.return_state = ST_PRE_ROLL; g.state = ST_DEV_MODAL; }
        else if (ty >= 424) { g_pause_sel = 0; g.return_state = ST_PRE_ROLL; g.state = ST_PAUSE; }
        return;
    }
    if (g.state == ST_MAIN_ACTION) {
        if (ty >= 298 && ty <= 334) { if (tx < rx + 136) catan_on_key(CKEY_ROAD); else catan_on_key(CKEY_SETTLE); }
        else if (ty >= 340 && ty <= 376) { if (tx < rx + 136) catan_on_key(CKEY_CITY); else catan_on_key(CKEY_BUYDEV); }
        else if (ty >= 382 && ty <= 418) { if (tx < rx + 136) open_trade_modal(); else { g_modal_sel = 0; g.return_state = ST_MAIN_ACTION; g.state = ST_DEV_MODAL; } }
        else if (ty >= 426) { begin_turn((g.cur_player + 1) % g.player_count); }
    }
}

/* ============================================================
 * 零卡頓精美地圖渲染 (一次預渲染到快取 + 每幀毫秒級動態疊加!)
 * ============================================================ */
static void build_static_map_cache(void) {
    /* 1. 漸層海景 + 淺海光暈 + 波浪 + 航海羅盤 */
    ui_draw_ocean_backdrop(524, 480, 256, 234);

    /* 2. 溫暖金色沙灘底襯 (比 19 格六角形外擴 5px，讓島嶼輪廓極度立體!) */
    for (int h = 0; h < NUM_HEXES; h++) {
        ui_fill_hex_pointy(g.board.hexes[h].cx, g.board.hexes[h].cy, g.board.hex_radius + 5, 0xE7C98F, 0xB8925A);
    }

    /* 3. 繪製 9 座木造港口碼頭與小帆船 */
    const char *port_lbl[7] = { "", "3:1", "2:1W", "2:1B", "2:1S", "2:1G", "2:1O" };
    const uint32_t port_col[7] = { 0, 0x475569, 0x166534, 0xC2410C, 0x65A30D, 0xCA8A04, 0x475569 };
    for (int p = 0; p < NUM_PORTS; p++) {
        const PortInfo *pi = &g.board.ports[p];
        ui_draw_thick_line(pi->bx, pi->by, g.board.verts[pi->v0].x, g.board.verts[pi->v0].y, 3, 0x92400E);
        ui_draw_thick_line(pi->bx, pi->by, g.board.verts[pi->v1].x, g.board.verts[pi->v1].y, 3, 0x92400E);
        ui_draw_harbor_ship(pi->bx, pi->by, port_col[pi->type], port_lbl[pi->type]);
    }

    /* 4. 繪製 19 塊手繪插圖風六角地形 (森林/丘陵/牧場/農田/山脈/沙漠/黃金河!) */
    for (int h = 0; h < NUM_HEXES; h++) {
        const HexTile *ht = &g.board.hexes[h];
        ui_fill_hex_pointy(ht->cx, ht->cy, g.board.hex_radius, RES_COLS[ht->terrain], 0xFEF3C7);
        ui_draw_hex_artwork(ht->cx, ht->cy, g.board.hex_radius, (int)ht->terrain, (uint32_t)h * 37u);
    }

    g_map_cached = hal_save_map_cache(524, 480);
}

static void render_board_map(void) {
    if (!g_map_cached || !hal_restore_map_cache()) {
        build_static_map_cache();
    }
    hal_fill_rect(524, 0, 2, 480, 0x1E293B);

    /* 1. 動態層：骰子產出金框高光 + 檢視高光 + 數字圓片 + 強盜 */
    for (int h = 0; h < NUM_HEXES; h++) {
        const HexTile *ht = &g.board.hexes[h];
        int produced = (g.last_roll_sum > 0 && g.last_roll_sum != 7 && ht->number == g.last_roll_sum && h != g.board.robber_hex);
        int is_rob_sel = (g.state == ST_MOVE_ROBBER && g_cand_cnt > 0 && g_cands[g_cand_idx] == h);
        if (produced) ui_draw_hex_outline(ht->cx, ht->cy, g.board.hex_radius - 2, 3, 0xFEF08A);
        else if (is_rob_sel || h == g_inspect_hex) ui_draw_hex_outline(ht->cx, ht->cy, g.board.hex_radius - 2, 3, 0x38BDF8);

        if (ht->number > 0) {
            int is_red = (ht->number == 6 || ht->number == 8);
            uint32_t bg = produced ? 0xFEF08A : 0xFFFBEB;
            hal_fill_circle(ht->cx + 1, ht->cy + 2, 14, 0x1C1917);
            hal_fill_circle(ht->cx, ht->cy, 14, bg);
            hal_draw_circle(ht->cx, ht->cy, 14, is_red ? 0xDC2626 : 0x78350F);
            char nb[8]; snprintf(nb, sizeof(nb), "%d", ht->number);
            int nw = ui_ascii_width(nb, 2);
            ui_draw_ascii(ht->cx - nw / 2, ht->cy - 9, nb, 2, is_red ? 0xDC2626 : 0x1E293B);
            int pips = catan_hex_pips(ht->number);
            int px0 = ht->cx - (pips * 4) / 2 + 2;
            for (int d = 0; d < pips; d++) hal_fill_rect(px0 + d * 4, ht->cy + 8, 2, 2, is_red ? 0xDC2626 : 0x475569);
        }

        if (h == g.board.robber_hex) ui_draw_robber(ht->cx + 15, ht->cy - 7);
    }

    /* 2. 道路層 */
    for (int e = 0; e < NUM_EDGES; e++) {
        const Edge *ed = &g.board.edges[e];
        if (ed->owner >= 0) {
            int x0 = g.board.verts[ed->v0].x, y0 = g.board.verts[ed->v0].y;
            int x1 = g.board.verts[ed->v1].x, y1 = g.board.verts[ed->v1].y;
            ui_draw_thick_line(x0, y0, x1, y1, 7, 0x090D16);
            ui_draw_thick_line(x0, y0, x1, y1, 5, g.players[ed->owner].color);
        }
    }
    if (g.state == ST_SETUP_ROAD || g.state == ST_BUILD_ROAD) {
        for (int i = 0; i < g_cand_cnt; i++) {
            const Edge *ed = &g.board.edges[g_cands[i]];
            int sel = (i == g_cand_idx);
            hal_fill_circle(ed->mx, ed->my, sel ? 8 : 6, sel ? 0xFEF08A : 0x38BDF8);
            hal_draw_circle(ed->mx, ed->my, sel ? 8 : 6, 0x0F172A);
        }
    }

    /* 3. 村莊與城市層 */
    for (int v = 0; v < NUM_VERTICES; v++) {
        const Vertex *vt = &g.board.verts[v];
        if (vt->owner >= 0) {
            int hi = (g.state == ST_BUILD_CITY && g_cand_cnt > 0 && g_cands[g_cand_idx] == v);
            if (vt->level == 2) ui_draw_city(vt->x, vt->y, g.players[vt->owner].color, hi);
            else ui_draw_settlement(vt->x, vt->y, g.players[vt->owner].color, hi);
        }
    }
    if (g.state == ST_SETUP_SETTLE || g.state == ST_BUILD_SETTLE) {
        for (int i = 0; i < g_cand_cnt; i++) {
            const Vertex *vt = &g.board.verts[g_cands[i]];
            int sel = (i == g_cand_idx);
            hal_fill_circle(vt->x, vt->y, sel ? 9 : 6, sel ? 0xFEF08A : 0x4ADE80);
            hal_draw_circle(vt->x, vt->y, sel ? 9 : 6, 0x0F172A);
        }
    }

    /* 4. 左上角雙骰 + 輪次徽章 */
    if (g.die1 > 0) {
        ui_draw_die(10, 10, 32, g.die1);
        ui_draw_die(48, 10, 32, g.die2);
    }
    char tbadge[24]; snprintf(tbadge, sizeof(tbadge), "T%d", g.turn_number);
    ui_draw_badge(474, 10, tbadge, 0x0F172A, 0x94A3B8);

    /* 5. 點擊地形檢視浮動小卡 */
    if (g_inspect_hex >= 0 && g_inspect_hex < NUM_HEXES) {
        const HexTile *ih = &g.board.hexes[g_inspect_hex];
        ui_card(8, 398, 254, 30, 8, 0x131C31, 0x38BDF8, 0x050810, 0, 0);
        hal_set_text_bg(0x131C31);
        char info[80];
        if (ih->number > 0)
            snprintf(info, sizeof(info), "%s · 點數%d (%d/36)%s", TERRAIN_NAMES[ih->terrain], ih->number, catan_hex_pips(ih->number),
                     (g_inspect_hex == g.board.robber_hex) ? " [強盜]" : "");
        else
            snprintf(info, sizeof(info), "%s · 不產出資源", TERRAIN_NAMES[ih->terrain]);
        ui_draw_str(16, 404, info, 240, 0x38BDF8);
    }

    /* 6. 底部即時戰報條 */
    ui_fill_rrect(8, 434, 508, 40, 8, 0x0B1524);
    ui_draw_rrect(8, 434, 508, 40, 8, 0x334155);
    hal_set_text_bg(0x0B1524);
    ui_draw_str(16, 444, g.history[0], 492, 0xFEF08A);
}

static void render_right_hud(void) {
    int rx = 526, rw = 274;
    hal_fill_rect(rx, 0, rw, 480, 0x0F172A);

    /* 1. 精緻玩家計分板 (含 VP / 手牌 / 發展卡 / 騎士數 / 路長 / 雙勳章) */
    for (int p = 0; p < g.player_count; p++) {
        int py = 6 + p * 38;
        int cur = (p == g.cur_player);
        ui_card(rx + 6, py, rw - 12, 33, 7, cur ? 0x1E293B : 0x131C31, cur ? 0xFEF08A : 0x23314d, 0x050810, cur, 0xFEF08A);
        hal_fill_circle(rx + 18, py + 16, 6, g.players[p].color);
        hal_set_text_bg(cur ? 0x1E293B : 0x131C31);
        int vp = player_calc_vp(p, !g.players[p].is_ai);
        char ln[64];
        snprintf(ln, sizeof(ln), "%s %d分 牌%d 路%d 騎%d",
                 g.players[p].name, vp, player_total_res(p), g.players[p].longest_road_len, g.players[p].knights_played);
        ui_draw_str(rx + 28, py + 8, ln, 198, 0xF8FAFC);
        if (g.longest_road_owner == p) ui_draw_badge(rx + rw - 44, py + 2, "RD", 0xD97706, 0xFFFFFF);
        if (g.largest_army_owner == p) ui_draw_badge(rx + rw - 44, py + 17, "AR", 0xDC2626, 0xFFFFFF);
    }

    /* 2. 5 張帶微型插圖與港口匯率的資源手牌 */
    int view_p = g.players[g.cur_player].is_ai ? 0 : g.cur_player;
    int cy = 164;
    hal_set_text_bg(0x0F172A);
    char rhdr[64];
    snprintf(rhdr, sizeof(rhdr), "%s手牌 (剩路%d 村%d 城%d)",
             g.players[view_p].name, g.players[view_p].roads_left,
             g.players[view_p].settles_left, g.players[view_p].cities_left);
    ui_draw_str(rx + 8, cy, rhdr, rw - 16, 0x94A3B8);

    for (int r = 0; r < 5; r++) {
        int bx = rx + 6 + r * 53;
        ui_card(bx, cy + 20, 48, 72, 8, RES_COLS[r], 0xF8FAFC, 0x050810, 0, 0);
        ui_draw_res_icon(bx + 24, cy + 34, r);
        char num[8]; snprintf(num, sizeof(num), "%d", g.players[view_p].res[r]);
        int nw = ui_ascii_width(num, 2);
        ui_draw_ascii(bx + (48 - nw) / 2, cy + 52, num, 2, 0xFFFFFF);
        /* 顯示該資源目前港口匯率 */
        int rate = player_trade_rate(view_p, r);
        char rt[8]; snprintf(rt, sizeof(rt), "%d:1", rate);
        ui_draw_ascii(bx + 15, cy + 78, rt, 1, (rate < 4) ? 0xFEF08A : 0xCBD5E1);
    }

    /* 3. 中央 4 顆工具膠囊：[統計] [戰報] [圖鑑] [選單] */
    const char *tools[4] = { "統計", "戰報", "圖鑑", "選單" };
    for (int i = 0; i < 4; i++) {
        int tx = rx + 6 + i * 66;
        ui_fill_rrect(tx, 264, 60, 26, 7, 0x1E293B);
        ui_draw_rrect(tx, 264, 60, 26, 7, 0x334155);
        hal_set_text_bg(0x1E293B);
        ui_draw_str(tx + 12, 269, tools[i], 44, 0xCBD5E1);
    }

    /* 4. 底部動態觸控行動區 */
    int pi = g.cur_player;
    if (g.players[pi].is_ai) {
        ui_card(rx + 6, 336, rw - 12, 56, 10, 0x1E293B, 0x38BDF8, 0x050810, 0, 0);
        hal_set_text_bg(0x1E293B);
        ui_draw_str(rx + 48, 356, "電腦 AI 思考行動中...", 200, 0x38BDF8);
        return;
    }

    if (g.state == ST_SETUP_SETTLE || g.state == ST_SETUP_ROAD || g.state == ST_MOVE_ROBBER) {
        ui_card(rx + 6, 316, rw - 12, 74, 10, 0x1E293B, 0xFEF08A, 0x050810, 1, 0xFEF08A);
        hal_set_text_bg(0x1E293B);
        const char *tip = (g.state == ST_MOVE_ROBBER) ? "點擊地圖任意地形放強盜" :
                          (g.state == ST_SETUP_SETTLE) ? "點擊發光綠點建村莊" : "點擊發光藍點修起始道路";
        ui_draw_str(rx + 24, 332, tip, rw - 36, 0xFEF08A);
        ui_draw_str(rx + 24, 358, "(或方向鍵切換 + Enter)", rw - 36, 0x94A3B8);
        return;
    }

    if (g.state == ST_BUILD_ROAD || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) {
        ui_card(rx + 6, 316, rw - 12, 74, 10, 0x1E293B, 0x4ADE80, 0x050810, 1, 0x4ADE80);
        hal_set_text_bg(0x1E293B);
        ui_draw_str(rx + 24, 334, "請點擊地圖發光位置建造", rw - 36, 0x4ADE80);
        ui_draw_str(rx + 24, 360, "或按方向鍵切換 + Enter", rw - 36, 0x94A3B8);
        if (g.free_roads_left == 0) {
            ui_card(rx + 6, 424, rw - 12, 42, 10, 0x334155, 0x94A3B8, 0x050810, 0, 0);
            hal_set_text_bg(0x334155); ui_draw_str(rx + 84, 436, "取消建造 [ESC]", 120, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_PRE_ROLL) {
        ui_card(rx + 6, 316, rw - 12, 52, 12, 0xEA580C, 0xFEF08A, 0x050810, 1, 0xFEF08A);
        hal_set_text_bg(0xEA580C); ui_draw_str(rx + 74, 333, "擲 雙 骰 [空白]", 150, 0xFFFFFF);

        ui_card(rx + 6, 376, rw - 12, 40, 10, 0x7C3AED, 0xA78BFA, 0x050810, 0, 0);
        hal_set_text_bg(0x7C3AED); ui_draw_str(rx + 56, 387, "先出騎士卡 [P]", 160, 0xFFFFFF);

        ui_card(rx + 6, 424, rw - 12, 40, 10, 0x334155, 0x475569, 0x050810, 0, 0);
        hal_set_text_bg(0x334155); ui_draw_str(rx + 80, 435, "暫停選單 [ESC]", 130, 0xE2E8F0);
        return;
    }

    uint32_t c_rd = can_afford_road(pi)   ? 0x2563EB : 0x1E293B;
    uint32_t c_st = can_afford_settle(pi) ? 0x059669 : 0x1E293B;
    uint32_t c_ct = can_afford_city(pi)   ? 0xD97706 : 0x1E293B;
    uint32_t c_dv = can_afford_dev(pi)    ? 0x7C3AED : 0x1E293B;

    ui_card(rx + 6,   298, 126, 36, 8, c_rd, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_rd); ui_draw_str(rx + 18, 307, "修路(木磚)", 108, 0xFFFFFF);

    ui_card(rx + 138, 298, 128, 36, 8, c_st, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_st); ui_draw_str(rx + 148, 307, "蓋村(木磚羊麥)", 116, 0xFFFFFF);

    ui_card(rx + 6,   340, 126, 36, 8, c_ct, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_ct); ui_draw_str(rx + 18, 349, "升城(2麥3礦)", 112, 0xFFFFFF);

    ui_card(rx + 138, 340, 128, 36, 8, c_dv, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_dv); ui_draw_str(rx + 148, 349, "買卡(羊麥礦)", 112, 0xFFFFFF);

    ui_card(rx + 6,   382, 126, 36, 8, 0x0284C7, 0x38BDF8, 0x050810, 0, 0);
    hal_set_text_bg(0x0284C7); ui_draw_str(rx + 22, 391, "交易 / 港口", 104, 0xFFFFFF);

    ui_card(rx + 138, 382, 128, 36, 8, 0x6D28D9, 0xA78BFA, 0x050810, 0, 0);
    hal_set_text_bg(0x6D28D9);
    char dstr[32]; snprintf(dstr, sizeof(dstr), "發展卡 (%d)", player_total_dev(pi));
    ui_draw_str(rx + 154, 391, dstr, 108, 0xFFFFFF);

    ui_card(rx + 6,   426, rw - 12, 42, 10, 0x10B981, 0xFEF08A, 0x050810, 0, 0);
    hal_set_text_bg(0x10B981); ui_draw_str(rx + 74, 438, "結束本回合 [空白]", 150, 0xFFFFFF);
}

static void render_modals(int sw, int sh) {
    if (g.state == ST_STATS_MODAL) {
        int mw = 600, mh = 390, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 20, my + 16, "本局骰子機率長條圖 (2~12點) 與玩家數據統計", mw - 40, 0x38BDF8);

        int max_c = 1;
        for (int s = 2; s <= 12; s++) if (g.dice_hist[s] > max_c) max_c = g.dice_hist[s];

        int chart_x = mx + 34, chart_y = my + 170, bar_w = 34, gap = 14;
        hal_fill_rect(chart_x - 8, chart_y, 11 * (bar_w + gap) + 8, 2, 0x334155);
        for (int s = 2; s <= 12; s++) {
            int bx = chart_x + (s - 2) * (bar_w + gap);
            int bh = (g.dice_hist[s] * 105) / max_c;
            uint32_t col = (s == 7) ? 0xEF4444 : ((s == 6 || s == 8) ? 0xF59E0B : 0x3B82F6);
            if (bh > 0) ui_fill_rrect(bx, chart_y - bh, bar_w, bh, 4, col);
            char lb[8], cnt[8];
            snprintf(lb, sizeof(lb), "%d", s);
            snprintf(cnt, sizeof(cnt), "%d", g.dice_hist[s]);
            ui_draw_ascii(bx + (bar_w - ui_ascii_width(lb, 1)) / 2, chart_y + 6, lb, 1, 0xE2E8F0);
            if (g.dice_hist[s] > 0) ui_draw_ascii(bx + (bar_w - ui_ascii_width(cnt, 1)) / 2, chart_y - bh - 11, cnt, 1, 0xFEF08A);
        }

        for (int p = 0; p < g.player_count; p++) {
            int ry = my + 206 + p * 36;
            ui_fill_rrect(mx + 20, ry, mw - 40, 30, 8, 0x1E293B);
            hal_fill_circle(mx + 34, ry + 15, 6, g.players[p].color);
            hal_set_text_bg(0x1E293B);
            char row[112];
            snprintf(row, sizeof(row), "%s  總分:%d  累計產資源:%d  交易:%d次  最長路:%d  騎士:%d",
                     g.players[p].name, player_calc_vp(p, !g.players[p].is_ai),
                     g.players[p].total_earned, g.players[p].trades_done,
                     g.players[p].longest_road_len, g.players[p].knights_played);
            ui_draw_str(mx + 48, ry + 6, row, mw - 72, 0xF8FAFC);
        }
        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 200, my + mh - 28, "點擊任意處或按 [ESC] 關閉", 240, 0x94A3B8);
        return;
    }

    if (g.state == ST_LOG_MODAL) {
        int mw = 580, mh = 390, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xFACC15, 0x050810, 1, 0xFACC15);
        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 20, my + 16, "完整戰報歷史紀錄 (由新到舊)", mw - 40, 0xFACC15);
        int show = g.hist_count < 13 ? g.hist_count : 13;
        for (int i = 0; i < show; i++) {
            ui_draw_str(mx + 22, my + 48 + i * 24, g.history[i], mw - 44, (i == 0) ? 0xFEF08A : 0xCBD5E1);
        }
        ui_draw_str(mx + 190, my + mh - 28, "點擊任意處或按 [ESC] 關閉", 240, 0x94A3B8);
        return;
    }

    if (g.state == ST_HELP_MODAL) {
        int mw = 560, mh = 370, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0x10B981, 0x050810, 1, 0x10B981);
        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 20, my + 16, "卡坦島建設成本速查 & 銀行庫存圖鑑", mw - 40, 0x34D399);

        ui_draw_str(mx + 24, my + 52,  "• 道路 (上限15)：1 木頭 + 1 磚塊  (連續5條獲最長道路 +2 VP)", mw - 48, 0xF8FAFC);
        ui_draw_str(mx + 24, my + 82,  "• 村莊 (上限5) ：1 木 + 1 磚 + 1 羊 + 1 麥  (+1 VP，須隔1格)", mw - 48, 0xF8FAFC);
        ui_draw_str(mx + 24, my + 112, "• 城市 (上限4) ：2 小麥 + 3 礦石  (+2 VP，產量雙倍)", mw - 48, 0xF8FAFC);
        ui_draw_str(mx + 24, my + 142, "• 發展卡       ：1 羊 + 1 麥 + 1 礦  (3張騎士獲最大軍隊 +2 VP)", mw - 48, 0xF8FAFC);
        ui_draw_str(mx + 24, my + 178, "• 港口標記說明 ：W=木2:1  B=磚2:1  S=羊2:1  G=麥2:1  O=礦2:1", mw - 48, 0x38BDF8);

        char binfo[128];
        snprintf(binfo, sizeof(binfo), "銀行剩餘庫存：木%d  磚%d  羊%d  麥%d  礦%d  ·  剩餘發展卡:%d張",
                 g.bank[0], g.bank[1], g.bank[2], g.bank[3], g.bank[4], DEV_DECK_SIZE - g.dev_top);
        ui_fill_rrect(mx + 20, my + 220, mw - 40, 44, 10, 0x1E293B);
        hal_set_text_bg(0x1E293B); ui_draw_str(mx + 32, my + 234, binfo, mw - 64, 0xFEF08A);

        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 24, my + 284, "提示：主行動時點擊任何六角地形，可查看該格詳細點數與機率！", mw - 48, 0x94A3B8);
        ui_draw_str(mx + 180, my + mh - 30, "點擊任意處或按 [ESC] 關閉", 240, 0x94A3B8);
        return;
    }

    if (g.state == ST_AI_OFFER) {
        int mw = 460, mh = 200, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31);
        char h1[80], h2[80];
        snprintf(h1, sizeof(h1), "【%s 主動向你提出交易！】", g.players[g.offer_from_ai].name);
        snprintf(h2, sizeof(h2), "對方給你 1 張【%s】  <-->  換你 1 張【%s】", RES_NAMES[g.offer_ai_gives], RES_NAMES[g.offer_ai_wants]);
        ui_draw_str(mx + 86, my + 24, h1, 320, 0x38BDF8);
        ui_draw_str(mx + 36, my + 70, h2, mw - 72, 0xFEF08A);

        uint32_t b0 = (g_modal_sel == 0) ? 0x10B981 : 0x1E293B;
        uint32_t b1 = (g_modal_sel == 1) ? 0xEF4444 : 0x1E293B;
        ui_fill_rrect(mx + 28, my + 132, 190, 44, 10, b0);
        hal_set_text_bg(b0); ui_draw_str(mx + 84, my + 145, "同意成交", 100, 0xFFFFFF);
        ui_fill_rrect(mx + 242, my + 132, 190, 44, 10, b1);
        hal_set_text_bg(b1); ui_draw_str(mx + 308, my + 145, "拒絕", 80, 0xFFFFFF);
        return;
    }

    if (g.state == ST_GOLD_PICK || g.state == ST_PLENTY_PICK || g.state == ST_MONOPOLY_PICK) {
        int mw = 520, mh = 200, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xFACC15, 0x050810, 1, 0xFACC15);
        hal_set_text_bg(0x131C31);
        const char *title = (g.state == ST_GOLD_PICK) ? "黃金河產出！請任選 1 張想要的資源" :
                            (g.state == ST_PLENTY_PICK) ? "豐收之年：請任選 1 張免費資源" : "壟斷卡：請選擇要搶光全場的資源";
        ui_draw_str(mx + 24, my + 24, title, mw - 48, 0xFEF08A);
        for (int r = 0; r < 5; r++) {
            int bx = mx + 20 + r * 96;
            ui_card(bx, my + 74, 86, 74, 10, RES_COLS[r], (g_modal_sel == r) ? 0xFEF08A : 0xFFFFFF, 0x050810, (g_modal_sel == r), 0xFEF08A);
            ui_draw_res_icon(bx + 43, my + 96, r);
            hal_set_text_bg(RES_COLS[r]); ui_draw_str(bx + 24, my + 120, RES_NAMES[r], 50, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_DISCARD) {
        int mw = 520, mh = 280, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xEF4444, 0x050810, 1, 0xEF4444);
        int pi = g.discard_queue[g.discard_q_idx];
        int chosen = 0; for (int r = 0; r < 5; r++) chosen += g.discard_sel[r];
        hal_set_text_bg(0x131C31);
        char hdr[96]; snprintf(hdr, sizeof(hdr), "強盜來襲！%s 須棄 %d 張 (已選 %d/%d)", g.players[pi].name, g.discard_need, chosen, g.discard_need);
        ui_draw_str(mx + 24, my + 20, hdr, mw - 48, 0xFEF08A);
        ui_draw_str(mx + 24, my + 48, "點卡片上半部 +1、下半部 -1 (或方向鍵調整)", mw - 48, 0x94A3B8);
        for (int r = 0; r < 5; r++) {
            int bx = mx + 20 + r * 96;
            ui_card(bx, my + 78, 86, 110, 10, RES_COLS[r], (g_modal_sel == r) ? 0xFEF08A : 0xFFFFFF, 0x050810, (g_modal_sel == r), 0xFEF08A);
            hal_set_text_bg(RES_COLS[r]);
            ui_draw_str(bx + 24, my + 88, RES_NAMES[r], 50, 0xFFFFFF);
            char hold[24], sel[24];
            snprintf(hold, sizeof(hold), "有:%d", g.players[pi].res[r]);
            snprintf(sel, sizeof(sel), "棄:%d", g.discard_sel[r]);
            ui_draw_str(bx + 22, my + 120, hold, 56, 0xE2E8F0);
            ui_draw_str(bx + 22, my + 150, sel, 56, 0xFEF08A);
        }
        uint32_t bg = (chosen == g.discard_need) ? 0x10B981 : 0x334155;
        ui_fill_rrect(mx + 150, my + 214, 220, 42, 10, bg);
        hal_set_text_bg(bg); ui_draw_str(mx + 206, my + 226, "確認棄牌", 120, 0xFFFFFF);
        return;
    }

    if (g.state == ST_STEAL_TARGET) {
        int mw = 360, mh = 220, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xF59E0B, 0x050810, 1, 0xF59E0B);
        hal_set_text_bg(0x131C31); ui_draw_str(mx + 24, my + 20, "選擇要偷取 1 張資源的對手：", mw - 48, 0xFEF08A);
        for (int i = 0; i < g_steal_cnt; i++) {
            int v = g_steal_cands[i];
            uint32_t bg = (i == g_modal_sel) ? 0x2563EB : 0x1E293B;
            ui_fill_rrect(mx + 24, my + 60 + i * 46, mw - 48, 38, 8, bg);
            hal_set_text_bg(bg);
            char ln[64]; snprintf(ln, sizeof(ln), "%s (手牌 %d 張)", g.players[v].name, player_total_res(v));
            ui_draw_str(mx + 44, my + 70 + i * 46, ln, mw - 80, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_TRADE_MODAL) {
        int mw = 560, mh = 360, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31); ui_draw_str(mx + 20, my + 14, "卡坦島貿易中心 (銀行港口自動套用 3:1 / 2:1 優惠)", mw - 40, 0xF8FAFC);

        uint32_t t0 = (g_trade_tab == 0) ? 0x2563EB : 0x1E293B;
        uint32_t t1 = (g_trade_tab == 1) ? 0x2563EB : 0x1E293B;
        ui_fill_rrect(mx + 20, my + 48, 254, 34, 8, t0);
        hal_set_text_bg(t0); ui_draw_str(mx + 82, my + 56, "銀行 / 港口交易", 150, 0xFFFFFF);
        ui_fill_rrect(mx + 286, my + 48, 254, 34, 8, t1);
        hal_set_text_bg(t1); ui_draw_str(mx + 344, my + 56, "與玩家 / AI 交易", 160, 0xFFFFFF);

        int pi = g.cur_player;
        int rate = (g_trade_tab == 0) ? player_trade_rate(pi, g_trade_give) : g_trade_give_amt;
        hal_set_text_bg(0x131C31);
        char l1[64]; snprintf(l1, sizeof(l1), "我付出 (%d 張)：", rate);
        ui_draw_str(mx + 20, my + 90, l1, 200, 0x94A3B8);
        for (int r = 0; r < 5; r++) {
            int bx = mx + 20 + r * 104;
            ui_card(bx, my + 112, 94, 46, 8, RES_COLS[r], (g_trade_give == r) ? 0xFEF08A : 0x334155, 0x050810, (g_trade_give == r), 0xFEF08A);
            hal_set_text_bg(RES_COLS[r]);
            char txt[32]; snprintf(txt, sizeof(txt), "%s(%d)", RES_SHORT[r], g.players[pi].res[r]);
            ui_draw_str(bx + 18, my + 126, txt, 70, 0xFFFFFF);
        }

        hal_set_text_bg(0x131C31); ui_draw_str(mx + 20, my + 166, "我換取 (1 張)：", 200, 0x94A3B8);
        for (int r = 0; r < 5; r++) {
            int bx = mx + 20 + r * 104;
            ui_card(bx, my + 188, 94, 46, 8, RES_COLS[r], (g_trade_get == r) ? 0xFEF08A : 0x334155, 0x050810, (g_trade_get == r), 0xFEF08A);
            hal_set_text_bg(RES_COLS[r]); ui_draw_str(bx + 26, my + 202, RES_NAMES[r], 56, 0xFFFFFF);
        }

        if (g_trade_tab == 1) {
            ui_fill_rrect(mx + 20, my + 246, 254, 34, 8, 0x1E293B);
            hal_set_text_bg(0x1E293B);
            char tg[48]; snprintf(tg, sizeof(tg), "對象：%s (點切換)", g.players[g_trade_target].name);
            ui_draw_str(mx + 42, my + 254, tg, 220, 0x38BDF8);
            ui_fill_rrect(mx + 286, my + 246, 254, 34, 8, 0x1E293B);
            char am[48]; snprintf(am, sizeof(am), "出價：%d 換 1 (點切換)", g_trade_give_amt);
            ui_draw_str(mx + 314, my + 254, am, 220, 0xFEF08A);
        } else if (g_trade_msg[0]) {
            hal_set_text_bg(0x131C31); ui_draw_str(mx + 20, my + 254, g_trade_msg, mw - 40, 0xFEF08A);
        }
        if (g_trade_tab == 1 && g_trade_msg[0]) {
            hal_set_text_bg(0x131C31); ui_draw_str(mx + 20, my + 282, g_trade_msg, mw - 40, 0xFEF08A);
        }

        ui_fill_rrect(mx + 20, my + 306, 254, 38, 10, 0x10B981);
        hal_set_text_bg(0x10B981); ui_draw_str(mx + 104, my + 316, "確認交易", 100, 0xFFFFFF);
        ui_fill_rrect(mx + 286, my + 306, 254, 38, 10, 0x334155);
        hal_set_text_bg(0x334155); ui_draw_str(mx + 382, my + 316, "關閉", 60, 0xFFFFFF);
        return;
    }

    if (g.state == ST_DEV_MODAL) {
        int mw = 460, mh = 310, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xA78BFA, 0x050810, 1, 0xA78BFA);
        hal_set_text_bg(0x131C31); ui_draw_str(mx + 20, my + 16, "發展卡手牌 (點擊打出 · 剛買的卡下回合可用)", mw - 40, 0xF8FAFC);
        int pi = g.cur_player;
        for (int d = 0; d < NUM_DEV_TYPES; d++) {
            int ry = my + 54 + d * 44;
            uint32_t bg = (d == g_modal_sel) ? 0x6D28D9 : 0x1E293B;
            ui_fill_rrect(mx + 20, ry, mw - 40, 36, 8, bg);
            hal_set_text_bg(bg);
            char ln[96];
            snprintf(ln, sizeof(ln), "%s  [可用:%d 新:%d]", DEV_NAMES[d], g.players[pi].dev[d], g.players[pi].dev_new[d]);
            ui_draw_str(mx + 32, ry + 9, ln, mw - 64, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_PAUSE) {
        int pw = 360, ph = 266, px = (sw - pw) / 2, py = (sh - ph) / 2;
        hal_clear_text_in_rect(px - 8, py - 8, pw + 16, ph + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(px, py, pw, ph, 14, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31); ui_draw_str(px + 122, py + 16, "遊戲暫停選單", 140, 0xFFFFFF);
        char po[4][48];
        snprintf(po[0], sizeof(po[0]), "繼續遊戲 (ESC)");
        snprintf(po[1], sizeof(po[1]), "AI 速度：%s", g.fast_ai ? "極速快節奏" : "標準節奏");
        snprintf(po[2], sizeof(po[2]), "存檔並回主畫面");
        snprintf(po[3], sizeof(po[3]), "離開卡坦島");
        for (int i = 0; i < 4; i++) {
            uint32_t bg = (i == g_pause_sel) ? 0x2563EB : 0x1E293B;
            ui_fill_rrect(px + 28, py + 56 + i * 48, pw - 56, 38, 8, bg);
            hal_set_text_bg(bg); ui_draw_str(px + 74, py + 66 + i * 48, po[i], 220, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_VICTORY) {
        int pw = 460, ph = 200, px = (sw - pw) / 2, py = (sh - ph) / 2;
        hal_clear_text_in_rect(px - 8, py - 8, pw + 16, ph + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(px, py, pw, ph, 14, 0x131C31, 0xFACC15, 0x050810, 1, 0xFACC15);
        hal_set_text_bg(0x131C31);
        ui_draw_str(px + 116, py + 24, "【卡坦島霸主誕生！】", 240, 0xFACC15);
        ui_draw_str(px + 32, py + 72, g.history[0], pw - 64, 0xFFFFFF);
        ui_fill_rrect(px + 130, py + 132, 200, 42, 10, 0x10B981);
        hal_set_text_bg(0x10B981); ui_draw_str(px + 178, py + 144, "返回主畫面", 120, 0xFFFFFF);
    }
}

void catan_render(void) {
    int sw = hal_get_width(), sh = hal_get_height();
    g_redraw = 0;

    if (g.state == ST_TITLE) {
        ui_draw_ocean_backdrop(sw, sh, sw / 2, sh / 2);
        /* 四角展示精繪地形六角形 */
        int hx[4] = { 96, sw - 96, 96, sw - 96 };
        int hy[4] = { 130, 130, sh - 130, sh - 130 };
        int ht[4] = { RES_WOOD, RES_WHEAT, RES_ORE, RES_GOLD };
        for (int i = 0; i < 4; i++) {
            ui_fill_hex_pointy(hx[i], hy[i], 52, RES_COLS[ht[i]], 0xFEF3C7);
            ui_draw_hex_artwork(hx[i], hy[i], 52, ht[i], (uint32_t)i);
        }

        int pw = 480, ph = 384, px = (sw - pw) / 2, py = 46;
        ui_card(px, py, pw, ph, 16, 0x131C31, 0xFACC15, 0x050810, 1, 0xFACC15);
        hal_set_text_bg(0x131C31);
        ui_draw_str(px + 86, py + 24, "卡 坦 島  CATAN  豪華完整版 v2", 340, 0xFACC15);
        ui_draw_str(px + 42, py + 62, "手繪風格六角島 · 黃金河擴充 · 友善強盜 · AI主動談判", 400, 0x38BDF8);
        ui_draw_str(px + 54, py + 92, "骰子機率統計圖 · 16條戰報 · 港口帆船 · 靜態快取零卡頓", 390, 0x94A3B8);

        int has_sav = has_save_file(), n = has_sav ? 3 : 2;
        const char *opts[3];
        if (has_sav) { opts[0] = "繼續上次存檔"; opts[1] = "自訂規則開新局"; opts[2] = "離開遊戲"; }
        else { opts[0] = "自訂規則開新局"; opts[1] = "離開遊戲"; }

        for (int i = 0; i < n; i++) {
            uint32_t bg = (i == g_title_sel) ? ((i == 0) ? 0x059669 : 0x2563EB) : 0x1E293B;
            ui_fill_rrect(px + 60, py + 158 + i * 54, pw - 120, 42, 10, bg);
            hal_set_text_bg(bg); ui_draw_str(px + 160, py + 170 + i * 54, opts[i], 180, 0xFFFFFF);
        }
        hal_present();
        return;
    }

    if (g.state == ST_SETUP_MENU) {
        ui_draw_ocean_backdrop(sw, sh, sw / 2, sh / 2);
        int pw = 540, ph = 436, px = (sw - pw) / 2, py = 20;
        ui_card(px, py, pw, ph, 16, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31); ui_draw_str(px + 174, py + 16, "卡坦島豪華開局設定", 220, 0x38BDF8);

        char rows[7][88];
        snprintf(rows[0], sizeof(rows[0]), "地圖擴充：< %s >", MAP_MODE_NAMES[g_cfg_map]);
        snprintf(rows[1], sizeof(rows[1]), "總人數  ：< %d 人對戰 (2~4人) >", g_cfg_players);
        snprintf(rows[2], sizeof(rows[2]), "人類玩家：< %d 位人類 vs %d 位 AI >", g_cfg_humans, g_cfg_players - g_cfg_humans);
        snprintf(rows[3], sizeof(rows[3]), "AI 難度 ：< %s >", AI_DIFF_NAMES[g_cfg_ai_diff]);
        snprintf(rows[4], sizeof(rows[4]), "強盜規則：< %s >", g_cfg_friendly ? "友善強盜 (<=2分免疫)" : "標準官方強盜");
        snprintf(rows[5], sizeof(rows[5]), "勝利目標：< %d 勝利點 >", g_cfg_win_vp);
        snprintf(rows[6], sizeof(rows[6]), ">>>   生成豪華海島，立即開局！   <<<");

        for (int i = 0; i < 7; i++) {
            uint32_t bg = (i == g_setup_row) ? ((i == 6) ? 0xEA580C : 0x2563EB) : 0x1E293B;
            ui_fill_rrect(px + 24, py + 52 + i * 52, pw - 48, 42, 10, bg);
            hal_set_text_bg(bg); ui_draw_str(px + 44, py + 64 + i * 52, rows[i], pw - 80, 0xFFFFFF);
        }
        hal_present();
        return;
    }

    render_board_map();
    render_right_hud();
    render_modals(sw, sh);
    hal_present();
}
