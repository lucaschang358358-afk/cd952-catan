#include "catan_game.h"
#include "catan_map.h"
#include "ui_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * CD-952 卡坦島完整版 (Catan Complete Edition)
 *  - 標準 19 格六角島、54 頂點、72 道路、9 港口 (3:1 / 2:1)
 *  - 完整蛇形開局 (Round 1 正向 / Round 2 反向並領取初始資源)
 *  - 2d6 擲骰產資源 (19張銀行庫存)、擲7超過7張棄半牌 + 移強盜 + 偷牌
 *  - 完整建設 (道路15/村莊5/城市4) + 距離規則 + 25張發展卡 (騎士/勝利點/道路建設/豐收/壟斷)
 *  - 最長道路 (真圖論DFS含敵村截斷) + 最大軍隊 (+2 VP)
 *  - 銀行/港口自動最優匯率交易 + 與 AI / 玩家雙向交易談判
 * ============================================================ */

#define MAX_PLAYERS   4
#define MAX_ROADS     15
#define MAX_SETTLES   5
#define MAX_CITIES    4
#define DEV_DECK_SIZE 25
#define SAVE_MAGIC    0x43415431u /* "CAT1" */

typedef enum {
    DEV_KNIGHT = 0,     /* 騎士卡 (14張) */
    DEV_VP,             /* 勝利點卡 (5張) */
    DEV_ROAD_BUILD,     /* 道路建設 (2張) */
    DEV_PLENTY,         /* 豐收之年 (2張) */
    DEV_MONOPOLY,       /* 壟斷卡 (2張) */
    NUM_DEV_TYPES
} DevType;

typedef enum {
    ST_TITLE = 0,
    ST_SETUP_MENU,
    ST_SETUP_SETTLE,    /* 開局放村莊 */
    ST_SETUP_ROAD,      /* 開局放道路 */
    ST_PRE_ROLL,        /* 回合開始：可先出騎士卡或擲骰 */
    ST_MAIN_ACTION,     /* 主行動：建設 / 交易 / 出發展卡 / 結束回合 */
    ST_BUILD_ROAD,      /* 選邊建道路 */
    ST_BUILD_SETTLE,    /* 選頂點建村莊 */
    ST_BUILD_CITY,      /* 選村莊升城市 */
    ST_DISCARD,         /* 擲 7 超過 7 張強制棄半牌 */
    ST_MOVE_ROBBER,     /* 移動強盜至另一六角格 */
    ST_STEAL_TARGET,    /* 選擇要偷取資源的相鄰對手 */
    ST_TRADE_MODAL,     /* 銀行/港口 & 玩家交易面板 */
    ST_DEV_MODAL,       /* 發展卡手牌面板 */
    ST_PLENTY_PICK,     /* 豐收之年：挑選 2 張任意資源 */
    ST_MONOPOLY_PICK,   /* 壟斷卡：挑選 1 種資源全部搶光 */
    ST_PAUSE,
    ST_VICTORY
} GameState;

typedef struct {
    int is_ai;
    char name[16];
    uint32_t color;
    int res[5];             /* 木 磚 羊 麥 礦 */
    int dev[NUM_DEV_TYPES]; /* 可使用的發展卡 */
    int dev_new[NUM_DEV_TYPES]; /* 本回合剛買(下回合才可出) */
    int knights_played;
    int roads_left;
    int settles_left;
    int cities_left;
    int longest_road_len;
} Player;

typedef struct {
    uint32_t magic;
    CatanBoard board;
    Player players[MAX_PLAYERS];
    int player_count;
    int human_count;
    int win_vp;

    int bank[5];            /* 官方每種資源 19 張 */
    int dev_deck[DEV_DECK_SIZE];
    int dev_top;

    int cur_player;
    int setup_round;        /* 0=正向, 1=反向 */
    int setup_last_v;       /* 剛放下的開局村莊頂點 */
    int die1, die2;
    int dev_played_this_turn;
    int free_roads_left;    /* 道路建設卡剩餘免費路 */
    int plenty_picks_left;  /* 豐收之年剩餘挑選數 */

    int longest_road_owner; /* -1 or 0..3 */
    int largest_army_owner; /* -1 or 0..3 */
    int winner;

    int discard_queue[MAX_PLAYERS];
    int discard_q_idx;
    int discard_need;
    int discard_sel[5];

    GameState state;
    GameState after_robber_state; /* 騎士卡在擲骰前使用完需回到 PRE_ROLL */
    char log1[96];
    char log2[96];
} CatanData;

static CatanData g;
static int g_redraw = 1;
static int g_ai_wait_ms = 0;

/* 介面游標狀態 */
static int g_title_sel = 0;
static int g_setup_row = 0;
static int g_cfg_players = 4;
static int g_cfg_humans  = 1;
static int g_cfg_win_vp  = 10;
static int g_pause_sel = 0;

/* 地圖目標候選清單 (方便方向鍵逐一切換 + 觸控直點) */
static int g_cands[72];
static int g_cand_cnt = 0;
static int g_cand_idx = 0;

/* 交易面板狀態 */
static int g_trade_tab = 0;      /* 0=銀行/港口, 1=玩家/AI談判 */
static int g_trade_give = 0;     /* 0..4 */
static int g_trade_get  = 1;     /* 0..4 */
static int g_trade_give_amt = 1; /* 玩家交易時出價張數 1..2 */
static int g_trade_target = 1;   /* 目標玩家索引 */
static char g_trade_msg[80] = {0};

/* 發展卡/偷牌/資源選擇游標 */
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
static const uint32_t RES_COLS[6] = {
    0x15803D, /* 木頭: 森林深綠 */
    0xC2410C, /* 磚塊: 丘陵赤陶 */
    0x65A30D, /* 羊毛: 牧場嫩綠 */
    0xCA8A04, /* 小麥: 農田金黃 */
    0x475569, /* 礦石: 山脈岩灰 */
    0xD6B483  /* 沙漠: 沙黃 */
};
static const uint32_t PLAYER_COLS[4] = { 0xEF4444, 0x3B82F6, 0xF59E0B, 0xE2E8F0 };
static const char *PLAYER_DEFAULTS[4] = { "紅方", "藍方", "橘方", "白方" };
static const char *DEV_NAMES[NUM_DEV_TYPES] = {
    "騎士卡 (移強盜+偷牌)",
    "勝利點 (+1 VP)",
    "道路建設 (免費2條路)",
    "豐收之年 (任選2資源)",
    "壟斷卡 (搶走全場1種)"
};

static void push_log(const char *msg) {
    snprintf(g.log2, sizeof(g.log2), "%s", g.log1);
    snprintf(g.log1, sizeof(g.log1), "%s", msg);
}

/* --- 存檔讀寫 --- */
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
    catan_board_rebuild_coords(&g.board, 256, 236, 42);
    return 1;
}

/* ============================================================
 * 核心規則計算：資源、匯率、勝利點、最長道路 DFS、最大軍隊
 * ============================================================ */
static int player_total_res(int pi) {
    int sum = 0;
    for (int r = 0; r < 5; r++) sum += g.players[pi].res[r];
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
    for (int v = 0; v < NUM_VERTICES; v++) {
        if (g.board.verts[v].owner == pi) vp += g.board.verts[v].level;
    }
    if (g.longest_road_owner == pi) vp += 2;
    if (g.largest_army_owner == pi) vp += 2;
    if (include_hidden_dev) vp += g.players[pi].dev[DEV_VP] + g.players[pi].dev_new[DEV_VP];
    return vp;
}

/* 真圖論 DFS 計算某玩家最長連續道路長度 (會被敵方村莊/城市截斷!) */
static int dfs_road(int pi, int v, uint8_t used_e[NUM_EDGES]) {
    int best = 0;
    const Vertex *vt = &g.board.verts[v];
    for (int i = 0; i < vt->deg; i++) {
        int ei = vt->adj_e[i];
        int nv = vt->adj_v[i];
        if (g.board.edges[ei].owner != pi || used_e[ei]) continue;
        used_e[ei] = 1;
        int sub = 1;
        /* 若下一個頂點被敵方佔領，道路至此截斷不能繼續穿過 */
        if (g.board.verts[nv].owner == -1 || g.board.verts[nv].owner == pi) {
            sub += dfs_road(pi, nv, used_e);
        }
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
        /* 只從連有自己道路的頂點出發 */
        int has_own = 0;
        for (int i = 0; i < g.board.verts[v].deg; i++) {
            if (g.board.edges[g.board.verts[v].adj_e[i]].owner == pi) { has_own = 1; break; }
        }
        if (!has_own) continue;
        int len = dfs_road(pi, v, used_e);
        if (len > best) best = len;
    }
    return best;
}

static void update_awards_and_victory(void) {
    /* 1. 更新所有人的最長道路 (可能被敵村截斷!) */
    for (int p = 0; p < g.player_count; p++) {
        g.players[p].longest_road_len = calc_longest_road(p);
    }
    int cur_lr = g.longest_road_owner;
    if (cur_lr >= 0 && g.players[cur_lr].longest_road_len < 5) {
        g.longest_road_owner = -1;
        cur_lr = -1;
    }
    int cur_lr_len = (cur_lr >= 0) ? g.players[cur_lr].longest_road_len : 4;
    for (int p = 0; p < g.player_count; p++) {
        if (g.players[p].longest_road_len > cur_lr_len) {
            cur_lr_len = g.players[p].longest_road_len;
            g.longest_road_owner = p;
        }
    }

    /* 2. 更新最大軍隊 (>=3 張騎士卡且嚴格大於原持有者) */
    int cur_la = g.largest_army_owner;
    int cur_la_cnt = (cur_la >= 0) ? g.players[cur_la].knights_played : 2;
    for (int p = 0; p < g.player_count; p++) {
        if (g.players[p].knights_played > cur_la_cnt) {
            cur_la_cnt = g.players[p].knights_played;
            g.largest_army_owner = p;
        }
    }

    /* 3. 檢查當前行動玩家是否已達勝利分數 (含隱藏勝利點卡!) */
    if (g.state != ST_TITLE && g.state != ST_SETUP_MENU && g.state != ST_VICTORY) {
        if (player_calc_vp(g.cur_player, 1) >= g.win_vp) {
            g.winner = g.cur_player;
            g.state = ST_VICTORY;
            char msg[80];
            snprintf(msg, sizeof(msg), "恭喜 %s 達成 %d 分勝利點，稱霸卡坦島！",
                     g.players[g.winner].name, player_calc_vp(g.winner, 1));
            push_log(msg);
            save_game();
        }
    }
}

/* ============================================================
 * 合法建設位置檢查 (完整距離規則 + 道路連通規則)
 * ============================================================ */
static int can_place_settlement(int pi, int v, int is_setup) {
    if (v < 0 || v >= NUM_VERTICES || g.board.verts[v].owner != -1) return 0;
    /* 官方距離規則：相鄰頂點絕不可有任何村莊或城市 */
    for (int i = 0; i < g.board.verts[v].deg; i++) {
        if (g.board.verts[g.board.verts[v].adj_v[i]].owner != -1) return 0;
    }
    if (is_setup) return 1;
    if (g.players[pi].settles_left <= 0) return 0;
    /* 正式回合必須連接自己的道路 */
    for (int i = 0; i < g.board.verts[v].deg; i++) {
        if (g.board.edges[g.board.verts[v].adj_e[i]].owner == pi) return 1;
    }
    return 0;
}

static int can_place_road(int pi, int e, int setup_v) {
    if (e < 0 || e >= NUM_EDGES || g.board.edges[e].owner != -1) return 0;
    if (g.players[pi].roads_left <= 0) return 0;
    int v0 = g.board.edges[e].v0, v1 = g.board.edges[e].v1;
    if (setup_v >= 0) return (v0 == setup_v || v1 == setup_v);

    /* 必須連到自己建築，或連到「未被敵方建築擋住」的自己道路 */
    int vs[2] = { v0, v1 };
    for (int side = 0; side < 2; side++) {
        int v = vs[side];
        if (g.board.verts[v].owner == pi) return 1;
        if (g.board.verts[v].owner == -1) {
            for (int i = 0; i < g.board.verts[v].deg; i++) {
                if (g.board.edges[g.board.verts[v].adj_e[i]].owner == pi) return 1;
            }
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
}

static void pay_to_bank(int pi, int res, int amt) {
    if (res < 0 || res >= 5 || amt <= 0) return;
    if (g.players[pi].res[res] < amt) amt = g.players[pi].res[res];
    g.players[pi].res[res] -= amt;
    g.bank[res] += amt;
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
        for (int h = 0; h < NUM_HEXES; h++) if (h != g.board.robber_hex) g_cands[g_cand_cnt++] = h;
    }
}

/* ============================================================
 * 開局與回合流程控制
 * ============================================================ */
static void start_new_game(void) {
    memset(&g, 0, sizeof(g));
    g.magic = SAVE_MAGIC;
    g.player_count = g_cfg_players;
    g.human_count  = g_cfg_humans;
    g.win_vp       = g_cfg_win_vp;
    g.longest_road_owner = -1;
    g.largest_army_owner = -1;
    g.winner = -1;

    catan_board_generate(&g.board, hal_get_seed(), 256, 236, 42);
    for (int r = 0; r < 5; r++) g.bank[r] = 19;

    /* 標準 25 張發展卡牌堆：14騎士 / 5勝利點 / 2道路建設 / 2豐收 / 2壟斷 */
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
    push_log("開局階段 1/2：請點選發光頂點建立第 1 座村莊！");
    g_ai_wait_ms = 350;
    g_redraw = 1;
}

static void begin_turn(int pi) {
    g.cur_player = pi;
    g.dev_played_this_turn = 0;
    g.free_roads_left = 0;
    g.plenty_picks_left = 0;
    /* 將上回合新買的發展卡轉為本回合可用 */
    for (int d = 0; d < NUM_DEV_TYPES; d++) {
        g.players[pi].dev[d] += g.players[pi].dev_new[d];
        g.players[pi].dev_new[d] = 0;
    }
    g.state = ST_PRE_ROLL;
    char msg[80];
    snprintf(msg, sizeof(msg), "輪到 %s：請按 [擲骰子] (或先使用騎士卡)", g.players[pi].name);
    push_log(msg);
    g_ai_wait_ms = 320;
    save_game();
}

static void finish_setup_road(int e) {
    int pi = g.cur_player;
    g.board.edges[e].owner = pi;
    g.players[pi].roads_left--;
    update_awards_and_victory();

    if (g.setup_round == 0) {
        if (g.cur_player + 1 < g.player_count) {
            g.cur_player++;
            g.state = ST_SETUP_SETTLE;
        } else {
            g.setup_round = 1; /* 蛇形反向：最後一位玩家接著放第2座村莊！ */
            g.state = ST_SETUP_SETTLE;
        }
    } else {
        if (g.cur_player > 0) {
            g.cur_player--;
            g.state = ST_SETUP_SETTLE;
        } else {
            push_log("開局完成！正式進入卡坦島開拓競賽！");
            begin_turn(0);
            return;
        }
    }
    rebuild_candidates();
    char msg[80];
    snprintf(msg, sizeof(msg), "開局 %d/2：%s 請建立村莊", g.setup_round + 1, g.players[g.cur_player].name);
    push_log(msg);
    g_ai_wait_ms = 300;
}

static void place_setup_settlement(int v) {
    int pi = g.cur_player;
    g.board.verts[v].owner = pi;
    g.board.verts[v].level = 1;
    g.players[pi].settles_left--;
    g.setup_last_v = v;

    /* 第 2 輪蛇形開局：立刻獲得周圍所有非沙漠地形各 1 張起始資源！ */
    if (g.setup_round == 1) {
        for (int i = 0; i < g.board.verts[v].hex_cnt; i++) {
            int hi = g.board.verts[v].hexes[i];
            if (hi >= 0 && g.board.hexes[hi].terrain < RES_DESERT) {
                give_from_bank(pi, (int)g.board.hexes[hi].terrain, 1);
            }
        }
    }
    update_awards_and_victory();
    g.state = ST_SETUP_ROAD;
    rebuild_candidates();
    char msg[80];
    snprintf(msg, sizeof(msg), "%s 請在村莊旁修建起始道路", g.players[pi].name);
    push_log(msg);
    g_ai_wait_ms = 260;
}

/* 強盜棄牌流程 */
static void advance_discard_queue(void) {
    while (g.discard_q_idx < g.player_count) {
        int pi = g.discard_queue[g.discard_q_idx];
        int tot = player_total_res(pi);
        if (tot > 7) {
            int need = tot / 2;
            if (g.players[pi].is_ai) {
                /* AI 智慧棄牌：優先丟數量最多的資源 */
                for (int k = 0; k < need; k++) {
                    int best_r = 0;
                    for (int r = 1; r < 5; r++) if (g.players[pi].res[r] > g.players[pi].res[best_r]) best_r = r;
                    pay_to_bank(pi, best_r, 1);
                }
                char msg[80];
                snprintf(msg, sizeof(msg), "%s 手牌過多，已棄掉 %d 張資源", g.players[pi].name, need);
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
    /* 所有人棄牌完畢 -> 進入移動強盜 */
    g.state = ST_MOVE_ROBBER;
    rebuild_candidates();
    char msg[80];
    snprintf(msg, sizeof(msg), "%s 請點選一塊地形移動強盜！", g.players[g.cur_player].name);
    push_log(msg);
    g_ai_wait_ms = 320;
}

static void execute_steal_from(int victim) {
    int pi = g.cur_player;
    int tot = player_total_res(victim);
    if (tot > 0) {
        int pick = rng_next(tot);
        int acc = 0, stolen = 0;
        for (int r = 0; r < 5; r++) {
            acc += g.players[victim].res[r];
            if (pick < acc) { stolen = r; break; }
        }
        g.players[victim].res[stolen]--;
        g.players[pi].res[stolen]++;
        char msg[80];
        if (!g.players[pi].is_ai)
            snprintf(msg, sizeof(msg), "從 %s 偷到了 1 張【%s】！", g.players[victim].name, RES_NAMES[stolen]);
        else
            snprintf(msg, sizeof(msg), "%s 從 %s 偷走 1 張資源卡", g.players[pi].name, g.players[victim].name);
        push_log(msg);
    }
    g.state = g.after_robber_state;
    g_ai_wait_ms = 300;
}

static void move_robber_to(int hex_idx) {
    if (hex_idx < 0 || hex_idx >= NUM_HEXES || hex_idx == g.board.robber_hex) return;
    g.board.robber_hex = hex_idx;

    /* 找出所有在該地形有建築且手牌 >= 1 的敵對玩家 */
    g_steal_cnt = 0;
    for (int c = 0; c < 6; c++) {
        int vi = g.board.hexes[hex_idx].verts[c];
        int ow = g.board.verts[vi].owner;
        if (ow >= 0 && ow != g.cur_player && player_total_res(ow) > 0) {
            int dup = 0;
            for (int k = 0; k < g_steal_cnt; k++) if (g_steal_cands[k] == ow) dup = 1;
            if (!dup) g_steal_cands[g_steal_cnt++] = ow;
        }
    }

    if (g_steal_cnt == 0) {
        push_log("強盜已移動（周圍無可偷取的對手）");
        g.state = g.after_robber_state;
        g_ai_wait_ms = 300;
    } else if (g_steal_cnt == 1) {
        execute_steal_from(g_steal_cands[0]);
    } else if (g.players[g.cur_player].is_ai) {
        /* AI 優先偷最高分玩家 */
        int best = g_steal_cands[0];
        for (int k = 1; k < g_steal_cnt; k++) {
            if (player_calc_vp(g_steal_cands[k], 0) > player_calc_vp(best, 0)) best = g_steal_cands[k];
        }
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

    if (sum == 7) {
        char msg[80];
        snprintf(msg, sizeof(msg), "%s 擲出 7！強盜出沒！手牌超過 7 張須棄半", g.players[g.cur_player].name);
        push_log(msg);
        g.after_robber_state = ST_MAIN_ACTION;
        for (int i = 0; i < g.player_count; i++) g.discard_queue[i] = (g.cur_player + i) % g.player_count;
        g.discard_q_idx = 0;
        advance_discard_queue();
        return;
    }

    int gained_any = 0;
    for (int h = 0; h < NUM_HEXES; h++) {
        if (g.board.hexes[h].number != sum || h == g.board.robber_hex) continue;
        int res = (int)g.board.hexes[h].terrain;
        if (res >= RES_DESERT) continue;
        for (int c = 0; c < 6; c++) {
            int vi = g.board.hexes[h].verts[c];
            int ow = g.board.verts[vi].owner;
            if (ow >= 0) {
                give_from_bank(ow, res, g.board.verts[vi].level);
                gained_any = 1;
            }
        }
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "%s 擲出 %d (%d+%d) · %s",
             g.players[g.cur_player].name, sum, g.die1, g.die2,
             gained_any ? "各玩家已領取資源！" : "本回合無人產出資源");
    push_log(msg);
    g.state = ST_MAIN_ACTION;
    g_ai_wait_ms = 320;
}

/* ============================================================
 * 建設與發展卡動作執行
 * ============================================================ */
static void execute_build_road(int e) {
    int pi = g.cur_player;
    if (!can_place_road(pi, e, -1)) return;
    if (g.free_roads_left > 0) {
        g.free_roads_left--;
    } else {
        if (!can_afford_road(pi)) return;
        pay_to_bank(pi, RES_WOOD, 1);
        pay_to_bank(pi, RES_BRICK, 1);
    }
    g.board.edges[e].owner = pi;
    g.players[pi].roads_left--;
    update_awards_and_victory();
    if (g.state == ST_VICTORY) return;

    if (g.free_roads_left > 0) {
        g.state = ST_BUILD_ROAD;
        rebuild_candidates();
        if (g_cand_cnt > 0) {
            push_log("道路建設卡：請再建第 2 條免費道路！");
            return;
        }
        g.free_roads_left = 0;
    }
    g.state = ST_MAIN_ACTION;
    char msg[80]; snprintf(msg, sizeof(msg), "%s 修建了一條道路！", g.players[pi].name);
    push_log(msg);
}

static void execute_build_settle(int v) {
    int pi = g.cur_player;
    if (!can_place_settlement(pi, v, 0) || !can_afford_settle(pi)) return;
    pay_to_bank(pi, RES_WOOD, 1); pay_to_bank(pi, RES_BRICK, 1);
    pay_to_bank(pi, RES_SHEEP, 1); pay_to_bank(pi, RES_WHEAT, 1);
    g.board.verts[v].owner = pi;
    g.board.verts[v].level = 1;
    g.players[pi].settles_left--;
    g.state = ST_MAIN_ACTION;
    char msg[80]; snprintf(msg, sizeof(msg), "%s 建立了新村莊！(+1 VP)", g.players[pi].name);
    push_log(msg);
    update_awards_and_victory();
}

static void execute_build_city(int v) {
    int pi = g.cur_player;
    if (!can_upgrade_city(pi, v) || !can_afford_city(pi)) return;
    pay_to_bank(pi, RES_WHEAT, 2); pay_to_bank(pi, RES_ORE, 3);
    g.board.verts[v].level = 2;
    g.players[pi].cities_left--;
    g.players[pi].settles_left++; /* 官方規則：升城市後村莊歸還庫存！ */
    g.state = ST_MAIN_ACTION;
    char msg[80]; snprintf(msg, sizeof(msg), "%s 將村莊升級為繁華城市！(產量x2)", g.players[pi].name);
    push_log(msg);
    update_awards_and_victory();
}

static void execute_buy_dev(void) {
    int pi = g.cur_player;
    if (!can_afford_dev(pi)) return;
    pay_to_bank(pi, RES_SHEEP, 1); pay_to_bank(pi, RES_WHEAT, 1); pay_to_bank(pi, RES_ORE, 1);
    int card = g.dev_deck[g.dev_top++];
    g.players[pi].dev_new[card]++;
    char msg[96];
    if (!g.players[pi].is_ai)
        snprintf(msg, sizeof(msg), "抽到發展卡：【%s】", DEV_NAMES[card]);
    else
        snprintf(msg, sizeof(msg), "%s 購買了 1 張發展卡", g.players[pi].name);
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
        char msg[80]; snprintf(msg, sizeof(msg), "%s 打出騎士卡！請移動強盜並偷牌", g.players[pi].name);
        push_log(msg);
    } else if (dev_type == DEV_ROAD_BUILD) {
        g.free_roads_left = 2;
        g.state = ST_BUILD_ROAD;
        rebuild_candidates();
        if (g_cand_cnt == 0) { g.free_roads_left = 0; g.state = return_state; }
        else push_log("打出【道路建設】：可免費修建 2 條道路！");
    } else if (dev_type == DEV_PLENTY) {
        g.plenty_picks_left = 2;
        g_modal_sel = 0;
        g.state = ST_PLENTY_PICK;
        push_log("打出【豐收之年】：請從銀行挑選 2 張任意資源");
    } else if (dev_type == DEV_MONOPOLY) {
        g_modal_sel = 0;
        g.state = ST_MONOPOLY_PICK;
        push_log("打出【壟斷卡】：請選擇要搶光全場的資源種類");
    }
}

/* ============================================================
 * 智慧 AI 決策引擎 (開局選址、強盜干擾、銀行/港口交易、建設規劃)
 * ============================================================ */
static int score_vertex_for_ai(int v) {
    int sc = 0;
    int seen[5] = {0};
    for (int i = 0; i < g.board.verts[v].hex_cnt; i++) {
        int hi = g.board.verts[v].hexes[i];
        if (hi < 0 || g.board.hexes[hi].terrain >= RES_DESERT) continue;
        int pips = catan_hex_pips(g.board.hexes[hi].number);
        sc += pips * 12;
        if (!seen[g.board.hexes[hi].terrain]) { seen[g.board.hexes[hi].terrain] = 1; sc += 8; }
    }
    if (g.board.verts[v].port != PORT_NONE) sc += 10;
    return sc + rng_next(5);
}

static void ai_do_bank_trade_if_helpful(int pi) {
    /* 若某資源 >= 匯率且有缺乏的資源，自動跟銀行/港口換取 */
    int min_r = 0;
    for (int r = 1; r < 5; r++) if (g.players[pi].res[r] < g.players[pi].res[min_r]) min_r = r;
    for (int r = 0; r < 5; r++) {
        if (r == min_r) continue;
        int rate = player_trade_rate(pi, r);
        if (g.players[pi].res[r] >= rate + 1 && g.bank[min_r] > 0) {
            pay_to_bank(pi, r, rate);
            give_from_bank(pi, min_r, 1);
            return;
        }
    }
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
        /* 若強盜擋在自己格上且手上有騎士卡，優先出騎士！ */
        if (g.players[pi].dev[DEV_KNIGHT] > 0 && !g.dev_played_this_turn) {
            int blocked = 0;
            for (int c = 0; c < 6; c++)
                if (g.board.verts[g.board.hexes[g.board.robber_hex].verts[c]].owner == pi) blocked = 1;
            if (blocked) { execute_play_dev(DEV_KNIGHT, ST_PRE_ROLL); return; }
        }
        do_roll_dice();
        return;
    }

    if (g.state == ST_MOVE_ROBBER) {
        /* AI 把強盜放到最高分敵人產出最高的地形上 */
        int best_h = -1, best_sc = -999;
        for (int h = 0; h < NUM_HEXES; h++) {
            if (h == g.board.robber_hex || g.board.hexes[h].terrain == RES_DESERT) continue;
            int sc = 0;
            int pips = catan_hex_pips(g.board.hexes[h].number);
            for (int c = 0; c < 6; c++) {
                int ow = g.board.verts[g.board.hexes[h].verts[c]].owner;
                if (ow == pi) sc -= pips * 20;
                else if (ow >= 0) sc += pips * (8 + player_calc_vp(ow, 0) * 2);
            }
            if (sc > best_sc) { best_sc = sc; best_h = h; }
        }
        if (best_h < 0) best_h = (g.board.robber_hex + 1) % NUM_HEXES;
        move_robber_to(best_h);
        return;
    }

    if (g.state == ST_MAIN_ACTION) {
        ai_do_bank_trade_if_helpful(pi);

        /* 1. 優先升級城市 */
        if (can_afford_city(pi)) {
            for (int v = 0; v < NUM_VERTICES; v++) {
                if (can_upgrade_city(pi, v)) { execute_build_city(v); g_ai_wait_ms = 280; return; }
            }
        }
        /* 2. 建立村莊 */
        if (can_afford_settle(pi)) {
            int best_v = -1, best_sc = -999;
            for (int v = 0; v < NUM_VERTICES; v++) {
                if (can_place_settlement(pi, v, 0)) {
                    int sc = score_vertex_for_ai(v);
                    if (sc > best_sc) { best_sc = sc; best_v = v; }
                }
            }
            if (best_v >= 0) { execute_build_settle(best_v); g_ai_wait_ms = 280; return; }
        }
        /* 3. 修建道路擴張 */
        if (can_afford_road(pi) && g.players[pi].roads_left > 2) {
            int cands[72], cnt = 0;
            for (int e = 0; e < NUM_EDGES; e++) if (can_place_road(pi, e, -1)) cands[cnt++] = e;
            if (cnt > 0 && (g.players[pi].res[RES_WOOD] >= 2 || g.players[pi].res[RES_BRICK] >= 2 || rng_next(100) < 55)) {
                execute_build_road(cands[rng_next(cnt)]);
                g_ai_wait_ms = 260;
                return;
            }
        }
        /* 4. 購買發展卡 */
        if (can_afford_dev(pi)) { execute_buy_dev(); g_ai_wait_ms = 260; return; }

        /* 5. 使用豐收/壟斷/騎士卡 */
        if (!g.dev_played_this_turn) {
            if (g.players[pi].dev[DEV_PLENTY] > 0) {
                g.players[pi].dev[DEV_PLENTY]--; g.dev_played_this_turn = 1;
                give_from_bank(pi, RES_ORE, 1); give_from_bank(pi, RES_WHEAT, 1);
                push_log("AI 打出【豐收之年】領取 2 張資源");
                g_ai_wait_ms = 260; return;
            }
            if (g.players[pi].dev[DEV_MONOPOLY] > 0) {
                g.players[pi].dev[DEV_MONOPOLY]--; g.dev_played_this_turn = 1;
                int best_r = 0, best_sum = -1;
                for (int r = 0; r < 5; r++) {
                    int s = 0; for (int p = 0; p < g.player_count; p++) if (p != pi) s += g.players[p].res[r];
                    if (s > best_sum) { best_sum = s; best_r = r; }
                }
                int got = 0;
                for (int p = 0; p < g.player_count; p++) {
                    if (p != pi) { got += g.players[p].res[best_r]; g.players[pi].res[best_r] += g.players[p].res[best_r]; g.players[p].res[best_r] = 0; }
                }
                char msg[80]; snprintf(msg, sizeof(msg), "%s 打出壟斷卡搶走全場 %d 張%s！", g.players[pi].name, got, RES_NAMES[best_r]);
                push_log(msg); g_ai_wait_ms = 300; return;
            }
        }

        /* 結束 AI 回合 */
        begin_turn((pi + 1) % g.player_count);
    }
}

/* ============================================================
 * 對外生命週期、按鍵與觸控處理
 * ============================================================ */
void catan_init(void) {
    g_rng = hal_get_seed();
    if (g_rng == 0) g_rng = 0x20261121u;
    memset(&g, 0, sizeof(g));
    g.state = ST_TITLE;
    g_title_sel = 0;
    g_redraw = 1;
}

void catan_shutdown(void) {
    if (g.state != ST_TITLE && g.state != ST_SETUP_MENU) save_game();
}

int catan_needs_redraw(void) { return g_redraw; }

void catan_tick(int dt_ms) {
    if (g.state == ST_TITLE || g.state == ST_SETUP_MENU || g.state == ST_PAUSE || g.state == ST_VICTORY) return;
    if (g.state == ST_DISCARD) return; /* 等待人類棄牌 */

    if (g.players[g.cur_player].is_ai) {
        g_ai_wait_ms -= dt_ms;
        if (g_ai_wait_ms <= 0) {
            ai_step();
            g_redraw = 1;
        }
    }
}

static void open_trade_modal(void) {
    g_trade_tab = 0;
    g_trade_give = 0; g_trade_get = 1;
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
        if (g.players[pi].res[g_trade_give] < rate) {
            snprintf(g_trade_msg, sizeof(g_trade_msg), "%s 不足 %d 張！", RES_NAMES[g_trade_give], rate);
            return;
        }
        if (g.bank[g_trade_get] <= 0) {
            snprintf(g_trade_msg, sizeof(g_trade_msg), "銀行 %s 庫存已空！", RES_NAMES[g_trade_get]);
            return;
        }
        pay_to_bank(pi, g_trade_give, rate);
        give_from_bank(pi, g_trade_get, 1);
        snprintf(g_trade_msg, sizeof(g_trade_msg), "交易成功：%d %s -> 1 %s", rate, RES_NAMES[g_trade_give], RES_NAMES[g_trade_get]);
    } else {
        int tgt = g_trade_target;
        if (tgt == pi) { snprintf(g_trade_msg, sizeof(g_trade_msg), "不能跟自己交易！"); return; }
        if (g.players[pi].res[g_trade_give] < g_trade_give_amt) {
            snprintf(g_trade_msg, sizeof(g_trade_msg), "你的 %s 不足 %d 張！", RES_NAMES[g_trade_give], g_trade_give_amt);
            return;
        }
        if (g.players[tgt].res[g_trade_get] < 1) {
            snprintf(g_trade_msg, sizeof(g_trade_msg), "%s 沒有 %s 可交換！", g.players[tgt].name, RES_NAMES[g_trade_get]);
            return;
        }
        /* AI 評估是否願意成交 (出 2 換 1 高機率答應；1 換 1 視 AI 是否缺貨而定) */
        int accept = 1;
        if (g.players[tgt].is_ai) {
            int tgt_needs = (g.players[tgt].res[g_trade_give] <= 1);
            int tgt_surplus = (g.players[tgt].res[g_trade_get] >= 2);
            accept = (g_trade_give_amt >= 2) || (tgt_needs && tgt_surplus) || (tgt_needs && rng_next(100) < 55);
        }
        if (!accept) {
            snprintf(g_trade_msg, sizeof(g_trade_msg), "%s 拒絕了這筆提議（可試加碼 2:1）", g.players[tgt].name);
            return;
        }
        g.players[pi].res[g_trade_give] -= g_trade_give_amt;
        g.players[tgt].res[g_trade_give] += g_trade_give_amt;
        g.players[tgt].res[g_trade_get] -= 1;
        g.players[pi].res[g_trade_get] += 1;
        snprintf(g_trade_msg, sizeof(g_trade_msg), "與 %s 成交：%d %s <-> 1 %s！",
                 g.players[tgt].name, g_trade_give_amt, RES_NAMES[g_trade_give], RES_NAMES[g_trade_get]);
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
        if (key == CKEY_UP) g_setup_row = (g_setup_row + 3) % 4;
        else if (key == CKEY_DOWN) g_setup_row = (g_setup_row + 1) % 4;
        else if (key == CKEY_LEFT || key == CKEY_RIGHT) {
            int d = (key == CKEY_RIGHT) ? 1 : -1;
            if (g_setup_row == 0) { g_cfg_players = (g_cfg_players == 4) ? 3 : 4; if (g_cfg_humans > g_cfg_players) g_cfg_humans = g_cfg_players; }
            else if (g_setup_row == 1) { g_cfg_humans += d; if (g_cfg_humans < 1) g_cfg_humans = 1; if (g_cfg_humans > g_cfg_players) g_cfg_humans = g_cfg_players; }
            else if (g_setup_row == 2) { g_cfg_win_vp += d * 2; if (g_cfg_win_vp < 8) g_cfg_win_vp = 8; if (g_cfg_win_vp > 12) g_cfg_win_vp = 12; }
        } else if (key == CKEY_OK || key == CKEY_SPACE) start_new_game();
        return;
    }

    if (g.state == ST_PAUSE) {
        if (key == CKEY_BACK) { g.state = ST_MAIN_ACTION; return; }
        if (key == CKEY_UP) g_pause_sel = (g_pause_sel + 2) % 3;
        else if (key == CKEY_DOWN) g_pause_sel = (g_pause_sel + 1) % 3;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            if (g_pause_sel == 0) g.state = ST_MAIN_ACTION;
            else if (g_pause_sel == 1) { save_game(); g.state = ST_TITLE; }
            else hal_request_quit();
        }
        return;
    }

    if (g.state == ST_VICTORY) {
        if (key == CKEY_OK || key == CKEY_SPACE || key == CKEY_BACK) g.state = ST_TITLE;
        return;
    }

    /* 棄牌彈窗 */
    if (g.state == ST_DISCARD) {
        int pi = g.discard_queue[g.discard_q_idx];
        int chosen = 0; for (int r = 0; r < 5; r++) chosen += g.discard_sel[r];
        if (key == CKEY_LEFT) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_UP && chosen < g.discard_need && g.discard_sel[g_modal_sel] < g.players[pi].res[g_modal_sel]) g.discard_sel[g_modal_sel]++;
        else if (key == CKEY_DOWN && g.discard_sel[g_modal_sel] > 0) g.discard_sel[g_modal_sel]--;
        else if ((key == CKEY_OK || key == CKEY_SPACE) && chosen == g.discard_need) {
            for (int r = 0; r < 5; r++) pay_to_bank(pi, r, g.discard_sel[r]);
            g.discard_q_idx++;
            advance_discard_queue();
        }
        return;
    }

    /* 選偷牌目標 */
    if (g.state == ST_STEAL_TARGET) {
        if (key == CKEY_UP || key == CKEY_LEFT) g_modal_sel = (g_modal_sel + g_steal_cnt - 1) % g_steal_cnt;
        else if (key == CKEY_DOWN || key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % g_steal_cnt;
        else if (key == CKEY_OK || key == CKEY_SPACE) execute_steal_from(g_steal_cands[g_modal_sel]);
        return;
    }

    /* 交易面板 */
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

    /* 發展卡面板 */
    if (g.state == ST_DEV_MODAL) {
        if (key == CKEY_BACK) { g.state = (g.die1 == 0) ? ST_PRE_ROLL : ST_MAIN_ACTION; return; }
        if (key == CKEY_UP) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_DOWN) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            GameState ret = (g.die1 == 0) ? ST_PRE_ROLL : ST_MAIN_ACTION;
            if (ret == ST_PRE_ROLL && g_modal_sel != DEV_KNIGHT) { push_log("擲骰前僅可打出騎士卡！"); return; }
            execute_play_dev(g_modal_sel, ret);
        }
        return;
    }

    /* 豐收之年 / 壟斷卡 資源選擇 */
    if (g.state == ST_PLENTY_PICK || g.state == ST_MONOPOLY_PICK) {
        if (key == CKEY_LEFT) g_modal_sel = (g_modal_sel + 4) % 5;
        else if (key == CKEY_RIGHT) g_modal_sel = (g_modal_sel + 1) % 5;
        else if (key == CKEY_OK || key == CKEY_SPACE) {
            int pi = g.cur_player;
            if (g.state == ST_PLENTY_PICK) {
                give_from_bank(pi, g_modal_sel, 1);
                g.plenty_picks_left--;
                if (g.plenty_picks_left <= 0) { g.state = ST_MAIN_ACTION; push_log("已領取 2 張豐收資源！"); }
            } else {
                int got = 0;
                for (int p = 0; p < g.player_count; p++) {
                    if (p != pi) { got += g.players[p].res[g_modal_sel]; g.players[pi].res[g_modal_sel] += g.players[p].res[g_modal_sel]; g.players[p].res[g_modal_sel] = 0; }
                }
                char msg[80]; snprintf(msg, sizeof(msg), "壟斷【%s】成功！共搶得 %d 張！", RES_NAMES[g_modal_sel], got);
                push_log(msg);
                g.state = ST_MAIN_ACTION;
            }
        }
        return;
    }

    if (g.players[g.cur_player].is_ai) return;

    /* 地圖點選模式 (鍵盤切換候選點) */
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
        else if (key == CKEY_PLAYDEV) { g_modal_sel = 0; g.die1 = 0; g.state = ST_DEV_MODAL; }
        else if (key == CKEY_BACK) { g_pause_sel = 0; g.state = ST_PAUSE; }
        return;
    }

    if (g.state == ST_MAIN_ACTION) {
        if (key == CKEY_BACK) { g_pause_sel = 0; g.state = ST_PAUSE; }
        else if (key == CKEY_ROAD && can_afford_road(g.cur_player)) { g.state = ST_BUILD_ROAD; rebuild_candidates(); if (g_cand_cnt == 0) g.state = ST_MAIN_ACTION; }
        else if (key == CKEY_SETTLE && can_afford_settle(g.cur_player)) { g.state = ST_BUILD_SETTLE; rebuild_candidates(); if (g_cand_cnt == 0) g.state = ST_MAIN_ACTION; }
        else if (key == CKEY_CITY && can_afford_city(g.cur_player)) { g.state = ST_BUILD_CITY; rebuild_candidates(); if (g_cand_cnt == 0) g.state = ST_MAIN_ACTION; }
        else if (key == CKEY_BUYDEV && can_afford_dev(g.cur_player)) execute_buy_dev();
        else if (key == CKEY_TRADE) open_trade_modal();
        else if (key == CKEY_PLAYDEV) { g_modal_sel = 0; g.state = ST_DEV_MODAL; }
        else if (key == CKEY_ENDTURN || key == CKEY_SPACE) begin_turn((g.cur_player + 1) % g.player_count);
    }
}

void catan_on_touch(int tx, int ty) {
    int sw = hal_get_width(), sh = hal_get_height();
    g_redraw = 1;

    if (g.state == ST_TITLE) {
        int has_sav = has_save_file(), n = has_sav ? 3 : 2;
        int pw = 460, px = (sw - pw) / 2, py = 54;
        for (int i = 0; i < n; i++) {
            int ry = py + 150 + i * 54;
            if (tx >= px + 60 && tx <= px + pw - 60 && ty >= ry && ty <= ry + 42) {
                g_title_sel = i; catan_on_key(CKEY_OK); return;
            }
        }
        return;
    }

    if (g.state == ST_SETUP_MENU) {
        int pw = 500, px = (sw - pw) / 2, py = 46;
        for (int i = 0; i < 4; i++) {
            int ry = py + 68 + i * 58;
            if (tx >= px + 28 && tx <= px + pw - 28 && ty >= ry && ty <= ry + 44) {
                g_setup_row = i;
                if (i == 3) start_new_game();
                else catan_on_key(tx < sw / 2 ? CKEY_LEFT : CKEY_RIGHT);
                return;
            }
        }
        return;
    }

    if (g.state == ST_PAUSE) {
        int pw = 340, ph = 220, px = (sw - pw) / 2, py = (sh - ph) / 2;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { g.state = ST_MAIN_ACTION; return; }
        int idx = (ty - (py + 56)) / 48;
        if (idx >= 0 && idx < 3) { g_pause_sel = idx; catan_on_key(CKEY_OK); }
        return;
    }

    if (g.state == ST_VICTORY) { g.state = ST_TITLE; return; }

    /* 棄牌觸控：點資源卡上方[+]、下方[-]、底部確認 */
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
        if (ty >= my + 216 && chosen == g.discard_need) { catan_on_key(CKEY_OK); }
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
        /* 分頁切換：銀行/港口 vs 玩家/AI */
        if (ty >= my + 48 && ty <= my + 82) {
            g_trade_tab = (tx < mx + mw / 2) ? 0 : 1;
            g_trade_msg[0] = '\0';
            return;
        }
        /* 點擊選擇換出資源 (5格) */
        if (ty >= my + 110 && ty <= my + 158) {
            int r = (tx - (mx + 20)) / 104;
            if (r >= 0 && r < 5) g_trade_give = r;
            return;
        }
        /* 點擊選擇換入資源 (5格) */
        if (ty >= my + 186 && ty <= my + 234) {
            int r = (tx - (mx + 20)) / 104;
            if (r >= 0 && r < 5) g_trade_get = r;
            return;
        }
        /* 玩家交易對象與出價張數 */
        if (g_trade_tab == 1 && ty >= my + 246 && ty <= my + 282) {
            if (tx < mx + mw / 2) {
                g_trade_target = (g_trade_target + 1) % g.player_count;
                if (g_trade_target == g.cur_player) g_trade_target = (g_trade_target + 1) % g.player_count;
            } else {
                g_trade_give_amt = (g_trade_give_amt == 1) ? 2 : 1;
            }
            return;
        }
        if (ty >= my + 300) {
            if (tx < mx + mw / 2) execute_trade_confirm();
            else g.state = ST_MAIN_ACTION;
        }
        return;
    }

    if (g.state == ST_DEV_MODAL) {
        int mw = 460, mh = 310, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        if (tx < mx || tx > mx + mw || ty < my || ty > my + mh) { g.state = (g.die1 == 0) ? ST_PRE_ROLL : ST_MAIN_ACTION; return; }
        int idx = (ty - (my + 54)) / 44;
        if (idx >= 0 && idx < NUM_DEV_TYPES) { g_modal_sel = idx; catan_on_key(CKEY_OK); }
        return;
    }

    if (g.state == ST_PLENTY_PICK || g.state == ST_MONOPOLY_PICK) {
        int mw = 520, mh = 200, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        int r = (tx - (mx + 20)) / 96;
        if (r >= 0 && r < 5 && ty >= my + 74 && ty <= my + 146) { g_modal_sel = r; catan_on_key(CKEY_OK); }
        return;
    }

    if (g.players[g.cur_player].is_ai) return;

    /* 直接點擊六角地圖上的發光候選頂點 / 邊 / 地形！ */
    if (tx < 520) {
        if (g.state == ST_SETUP_SETTLE || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) {
            int best_v = -1, best_d2 = 24 * 24;
            for (int i = 0; i < g_cand_cnt; i++) {
                int v = g_cands[i];
                int dx = g.board.verts[v].x - tx, dy = g.board.verts[v].y - ty;
                if (dx * dx + dy * dy < best_d2) { best_d2 = dx * dx + dy * dy; best_v = v; }
            }
            if (best_v >= 0) {
                if (g.state == ST_SETUP_SETTLE) place_setup_settlement(best_v);
                else if (g.state == ST_BUILD_SETTLE) execute_build_settle(best_v);
                else execute_build_city(best_v);
            }
            return;
        }
        if (g.state == ST_SETUP_ROAD || g.state == ST_BUILD_ROAD) {
            int best_e = -1, best_d2 = 24 * 24;
            for (int i = 0; i < g_cand_cnt; i++) {
                int e = g_cands[i];
                int dx = g.board.edges[e].mx - tx, dy = g.board.edges[e].my - ty;
                if (dx * dx + dy * dy < best_d2) { best_d2 = dx * dx + dy * dy; best_e = e; }
            }
            if (best_e >= 0) {
                if (g.state == ST_SETUP_ROAD) finish_setup_road(best_e);
                else execute_build_road(best_e);
            }
            return;
        }
        if (g.state == ST_MOVE_ROBBER) {
            int best_h = -1, best_d2 = 42 * 42;
            for (int i = 0; i < g_cand_cnt; i++) {
                int h = g_cands[i];
                int dx = g.board.hexes[h].cx - tx, dy = g.board.hexes[h].cy - ty;
                if (dx * dx + dy * dy < best_d2) { best_d2 = dx * dx + dy * dy; best_h = h; }
            }
            if (best_h >= 0) move_robber_to(best_h);
            return;
        }
        return;
    }

    /* 右側控制欄觸控按鈕 (x >= 526) */
    int rx = 532;
    if (g.state == ST_BUILD_ROAD || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) {
        if (ty >= 424 && g.free_roads_left == 0) g.state = ST_MAIN_ACTION;
        return;
    }
    if (g.state == ST_PRE_ROLL) {
        if (ty >= 324 && ty <= 372) do_roll_dice();
        else if (ty >= 380 && ty <= 418) { g_modal_sel = 0; g.die1 = 0; g.state = ST_DEV_MODAL; }
        else if (ty >= 426) { g_pause_sel = 0; g.state = ST_PAUSE; }
        return;
    }
    if (g.state == ST_MAIN_ACTION) {
        if (ty >= 292 && ty <= 330) {
            if (tx < rx + 128) catan_on_key(CKEY_ROAD);
            else catan_on_key(CKEY_SETTLE);
        } else if (ty >= 336 && ty <= 374) {
            if (tx < rx + 128) catan_on_key(CKEY_CITY);
            else catan_on_key(CKEY_BUYDEV);
        } else if (ty >= 380 && ty <= 418) {
            if (tx < rx + 128) open_trade_modal();
            else { g_modal_sel = 0; g.state = ST_DEV_MODAL; }
        } else if (ty >= 426) {
            if (tx < rx + 178) begin_turn((g.cur_player + 1) % g.player_count);
            else { g_pause_sel = 0; g.state = ST_PAUSE; }
        }
    }
}

/* ============================================================
 * 精美卡坦島視覺渲染
 * ============================================================ */
static void render_board_map(void) {
    /* 深海背景 */
    hal_fill_rect(0, 0, 524, 480, 0x0C3B5E);
    hal_fill_rect(524, 0, 2, 480, 0x1E293B);

    /* 1. 繪製 9 個海岸港口與碼頭連線 */
    const char *port_lbl[7] = { "", "3:1", "2:1W", "2:1B", "2:1S", "2:1G", "2:1O" };
    const uint32_t port_col[7] = { 0, 0x475569, 0x15803D, 0xC2410C, 0x65A30D, 0xCA8A04, 0x64748B };
    for (int p = 0; p < NUM_PORTS; p++) {
        const PortInfo *pi = &g.board.ports[p];
        hal_draw_line(pi->bx, pi->by, g.board.verts[pi->v0].x, g.board.verts[pi->v0].y, 0xD6B483);
        hal_draw_line(pi->bx, pi->by, g.board.verts[pi->v1].x, g.board.verts[pi->v1].y, 0xD6B483);
        ui_fill_rrect(pi->bx - 16, pi->by - 8, 32, 16, 6, port_col[pi->type]);
        ui_draw_rrect(pi->bx - 16, pi->by - 8, 32, 16, 6, 0xFEF08A);
        int tw = ui_ascii_width(port_lbl[pi->type], 1);
        ui_draw_ascii(pi->bx - tw / 2, pi->by - 3, port_lbl[pi->type], 1, 0xFFFFFF);
    }

    /* 2. 繪製 19 塊彩色六角形地形 + 數字圓片 + 機率點 + 強盜 */
    for (int h = 0; h < NUM_HEXES; h++) {
        const HexTile *ht = &g.board.hexes[h];
        uint32_t fill = RES_COLS[ht->terrain];
        int is_rob_cand = (g.state == ST_MOVE_ROBBER && h != g.board.robber_hex);
        int is_rob_sel  = (is_rob_cand && g_cand_cnt > 0 && g_cands[g_cand_idx] == h);
        ui_fill_hex_pointy(ht->cx, ht->cy, g.board.hex_radius, fill, is_rob_sel ? 0xFEF08A : 0xFEF3C7);

        if (ht->number > 0) {
            int is_red = (ht->number == 6 || ht->number == 8);
            hal_fill_circle(ht->cx, ht->cy, 15, 0xFFFBEB);
            hal_draw_circle(ht->cx, ht->cy, 15, is_red ? 0xDC2626 : 0x78350F);
            char nb[8]; snprintf(nb, sizeof(nb), "%d", ht->number);
            int nw = ui_ascii_width(nb, 2);
            ui_draw_ascii(ht->cx - nw / 2, ht->cy - 9, nb, 2, is_red ? 0xDC2626 : 0x1E293B);
            /* 機率點 (pips) */
            int pips = catan_hex_pips(ht->number);
            int px0 = ht->cx - (pips * 4) / 2 + 2;
            for (int d = 0; d < pips; d++) hal_fill_rect(px0 + d * 4, ht->cy + 8, 2, 2, is_red ? 0xDC2626 : 0x475569);
        }

        if (h == g.board.robber_hex) {
            ui_draw_robber(ht->cx + 16, ht->cy - 8);
        }
    }

    /* 3. 繪製 72 條道路 (已建道路粗線 + 建路候選發光點) */
    for (int e = 0; e < NUM_EDGES; e++) {
        const Edge *ed = &g.board.edges[e];
        if (ed->owner >= 0) {
            int x0 = g.board.verts[ed->v0].x, y0 = g.board.verts[ed->v0].y;
            int x1 = g.board.verts[ed->v1].x, y1 = g.board.verts[ed->v1].y;
            ui_draw_thick_line(x0, y0, x1, y1, 7, 0x0F172A);
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

    /* 4. 繪製 54 個頂點 (已建村莊/城市 + 建村/升城候選發光點) */
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

    /* 底部事件訊息條 */
    ui_fill_rrect(8, 434, 508, 40, 8, 0x0B1524);
    ui_draw_rrect(8, 434, 508, 40, 8, 0x334155);
    hal_set_text_bg(0x0B1524);
    ui_draw_str(16, 444, g.log1, 492, 0xFEF08A);

    /* 頂部即時雙骰顯示 */
    if (g.die1 > 0) {
        ui_draw_die(12, 10, 32, g.die1);
        ui_draw_die(50, 10, 32, g.die2);
    }
}

static void render_right_hud(void) {
    int rx = 526, rw = 274;
    hal_fill_rect(rx, 0, rw, 480, 0x0F172A);

    /* 1. 玩家計分板 (3~4人) */
    for (int p = 0; p < g.player_count; p++) {
        int py = 8 + p * 42;
        int cur = (p == g.cur_player);
        ui_card(rx + 6, py, rw - 12, 36, 8, cur ? 0x1E293B : 0x131C31, cur ? 0xFEF08A : 0x23314d, 0x050810, cur, 0xFEF08A);
        hal_fill_circle(rx + 20, py + 18, 7, g.players[p].color);
        hal_set_text_bg(cur ? 0x1E293B : 0x131C31);
        int show_vp = player_calc_vp(p, !g.players[p].is_ai);
        char ln[64];
        snprintf(ln, sizeof(ln), "%s %d分 卡:%d", g.players[p].name, show_vp, player_total_res(p));
        ui_draw_str(rx + 32, py + 9, ln, 174, 0xF8FAFC);

        if (g.longest_road_owner == p) ui_draw_badge(rx + rw - 66, py + 4, "ROAD", 0xD97706, 0xFFFFFF);
        if (g.largest_army_owner == p) ui_draw_badge(rx + rw - 66, py + 19, "ARMY", 0xDC2626, 0xFFFFFF);
    }

    /* 2. 當前人類玩家資源手牌 (5張彩色卡) */
    int view_p = g.players[g.cur_player].is_ai ? 0 : g.cur_player;
    int cy = 186;
    hal_set_text_bg(0x0F172A);
    char rhdr[64];
    snprintf(rhdr, sizeof(rhdr), "%s 資源 (剩路%d 村%d 城%d)",
             g.players[view_p].name, g.players[view_p].roads_left,
             g.players[view_p].settles_left, g.players[view_p].cities_left);
    ui_draw_str(rx + 8, cy, rhdr, rw - 16, 0x94A3B8);

    for (int r = 0; r < 5; r++) {
        int bx = rx + 6 + r * 53;
        ui_card(bx, cy + 22, 48, 66, 8, RES_COLS[r], 0xF8FAFC, 0x050810, 0, 0);
        hal_set_text_bg(RES_COLS[r]);
        ui_draw_str(bx + 15, cy + 30, RES_SHORT[r], 24, 0xFFFFFF);
        char num[8]; snprintf(num, sizeof(num), "%d", g.players[view_p].res[r]);
        int nw = ui_ascii_width(num, 2);
        ui_draw_ascii(bx + (48 - nw) / 2, cy + 62, num, 2, 0xFFFFFF);
    }

    /* 3. 底部動態觸控行動按鈕區 */
    int pi = g.cur_player;
    if (g.players[pi].is_ai) {
        ui_card(rx + 6, 336, rw - 12, 56, 10, 0x1E293B, 0x38BDF8, 0x050810, 0, 0);
        hal_set_text_bg(0x1E293B);
        ui_draw_str(rx + 46, 356, "電腦 AI 思考行動中...", 200, 0x38BDF8);
        return;
    }

    if (g.state == ST_SETUP_SETTLE || g.state == ST_SETUP_ROAD || g.state == ST_MOVE_ROBBER) {
        ui_card(rx + 6, 326, rw - 12, 72, 10, 0x1E293B, 0xFEF08A, 0x050810, 1, 0xFEF08A);
        hal_set_text_bg(0x1E293B);
        const char *tip = (g.state == ST_MOVE_ROBBER) ? "請直接點擊地形移動強盜" :
                          (g.state == ST_SETUP_SETTLE) ? "請點擊發光綠點建村莊" : "請點擊發光藍點修道路";
        ui_draw_str(rx + 26, 342, tip, rw - 36, 0xFEF08A);
        ui_draw_str(rx + 26, 368, "(也可用方向鍵 + Enter)", rw - 36, 0x94A3B8);
        return;
    }

    if (g.state == ST_BUILD_ROAD || g.state == ST_BUILD_SETTLE || g.state == ST_BUILD_CITY) {
        ui_card(rx + 6, 326, rw - 12, 72, 10, 0x1E293B, 0x4ADE80, 0x050810, 1, 0x4ADE80);
        hal_set_text_bg(0x1E293B);
        ui_draw_str(rx + 26, 344, "請點擊地圖發光位置建造", rw - 36, 0x4ADE80);
        ui_draw_str(rx + 26, 370, "或按方向鍵切換 + Enter", rw - 36, 0x94A3B8);
        if (g.free_roads_left == 0) {
            ui_card(rx + 6, 424, rw - 12, 42, 10, 0x334155, 0x94A3B8, 0x050810, 0, 0);
            hal_set_text_bg(0x334155); ui_draw_str(rx + 84, 436, "取消建造 [ESC]", 120, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_PRE_ROLL) {
        ui_card(rx + 6, 324, rw - 12, 48, 12, 0xEA580C, 0xFEF08A, 0x050810, 1, 0xFEF08A);
        hal_set_text_bg(0xEA580C); ui_draw_str(rx + 74, 339, "擲 雙 骰 [空白]", 150, 0xFFFFFF);

        ui_card(rx + 6, 380, rw - 12, 38, 10, 0x7C3AED, 0xA78BFA, 0x050810, 0, 0);
        hal_set_text_bg(0x7C3AED); ui_draw_str(rx + 56, 390, "先出騎士卡 [P]", 160, 0xFFFFFF);

        ui_card(rx + 6, 426, rw - 12, 38, 10, 0x334155, 0x475569, 0x050810, 0, 0);
        hal_set_text_bg(0x334155); ui_draw_str(rx + 80, 436, "暫停選單 [ESC]", 130, 0xE2E8F0);
        return;
    }

    /* 主行動 8 顆功能按鈕 */
    uint32_t c_rd = can_afford_road(pi)   ? 0x2563EB : 0x1E293B;
    uint32_t c_st = can_afford_settle(pi) ? 0x059669 : 0x1E293B;
    uint32_t c_ct = can_afford_city(pi)   ? 0xD97706 : 0x1E293B;
    uint32_t c_dv = can_afford_dev(pi)    ? 0x7C3AED : 0x1E293B;

    ui_card(rx + 6,   292, 126, 38, 8, c_rd, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_rd); ui_draw_str(rx + 18, 302, "修路(木磚)", 108, 0xFFFFFF);

    ui_card(rx + 138, 292, 128, 38, 8, c_st, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_st); ui_draw_str(rx + 150, 302, "蓋村(木磚羊麥)", 114, 0xFFFFFF);

    ui_card(rx + 6,   336, 126, 38, 8, c_ct, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_ct); ui_draw_str(rx + 18, 346, "升城(2麥3礦)", 112, 0xFFFFFF);

    ui_card(rx + 138, 336, 128, 38, 8, c_dv, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(c_dv); ui_draw_str(rx + 150, 346, "買卡(羊麥礦)", 112, 0xFFFFFF);

    ui_card(rx + 6,   380, 126, 38, 8, 0x0284C7, 0x38BDF8, 0x050810, 0, 0);
    hal_set_text_bg(0x0284C7); ui_draw_str(rx + 22, 390, "交易 / 港口", 104, 0xFFFFFF);

    int dev_cnt = 0; for (int d = 0; d < NUM_DEV_TYPES; d++) dev_cnt += g.players[pi].dev[d] + g.players[pi].dev_new[d];
    ui_card(rx + 138, 380, 128, 38, 8, 0x6D28D9, 0xA78BFA, 0x050810, 0, 0);
    hal_set_text_bg(0x6D28D9);
    char dstr[32]; snprintf(dstr, sizeof(dstr), "發展卡 (%d)", dev_cnt);
    ui_draw_str(rx + 154, 390, dstr, 108, 0xFFFFFF);

    ui_card(rx + 6,   426, 174, 42, 10, 0x10B981, 0xFEF08A, 0x050810, 0, 0);
    hal_set_text_bg(0x10B981); ui_draw_str(rx + 36, 438, "結束回合 [空白]", 130, 0xFFFFFF);

    ui_card(rx + 186, 426, 80, 42, 10, 0x334155, 0x475569, 0x050810, 0, 0);
    hal_set_text_bg(0x334155); ui_draw_str(rx + 206, 438, "選單", 50, 0xFFFFFF);
}

static void render_modals(int sw, int sh) {
    if (g.state == ST_DISCARD) {
        int mw = 520, mh = 280, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xEF4444, 0x050810, 1, 0xEF4444);
        int pi = g.discard_queue[g.discard_q_idx];
        int chosen = 0; for (int r = 0; r < 5; r++) chosen += g.discard_sel[r];
        hal_set_text_bg(0x131C31);
        char hdr[96]; snprintf(hdr, sizeof(hdr), "強盜來襲！%s 須棄掉 %d 張 (已選 %d/%d)", g.players[pi].name, g.discard_need, chosen, g.discard_need);
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

        /* 分頁 */
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

        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 20, my + 166, "我換取 (1 張)：", 200, 0x94A3B8);
        for (int r = 0; r < 5; r++) {
            int bx = mx + 20 + r * 104;
            ui_card(bx, my + 188, 94, 46, 8, RES_COLS[r], (g_trade_get == r) ? 0xFEF08A : 0x334155, 0x050810, (g_trade_get == r), 0xFEF08A);
            hal_set_text_bg(RES_COLS[r]);
            ui_draw_str(bx + 26, my + 202, RES_NAMES[r], 56, 0xFFFFFF);
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

    if (g.state == ST_PLENTY_PICK || g.state == ST_MONOPOLY_PICK) {
        int mw = 520, mh = 200, mx = (sw - mw) / 2, my = (sh - mh) / 2;
        hal_clear_text_in_rect(mx - 8, my - 8, mw + 16, mh + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(mx, my, mw, mh, 14, 0x131C31, 0xFEF08A, 0x050810, 1, 0xFEF08A);
        hal_set_text_bg(0x131C31);
        ui_draw_str(mx + 24, my + 24, (g.state == ST_PLENTY_PICK) ? "豐收之年：點選 1 張免費領取的資源" : "壟斷卡：點選要搶光全場的資源種類", mw - 48, 0xFEF08A);
        for (int r = 0; r < 5; r++) {
            int bx = mx + 20 + r * 96;
            ui_card(bx, my + 74, 86, 72, 10, RES_COLS[r], (g_modal_sel == r) ? 0xFEF08A : 0xFFFFFF, 0x050810, (g_modal_sel == r), 0xFEF08A);
            hal_set_text_bg(RES_COLS[r]); ui_draw_str(bx + 24, my + 100, RES_NAMES[r], 50, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_PAUSE) {
        int pw = 340, ph = 220, px = (sw - pw) / 2, py = (sh - ph) / 2;
        hal_clear_text_in_rect(px - 8, py - 8, pw + 16, ph + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(px, py, pw, ph, 14, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31); ui_draw_str(px + 116, py + 16, "遊戲暫停選單", 140, 0xFFFFFF);
        const char *po[3] = { "繼續遊戲 (ESC)", "存檔並回主畫面", "離開卡坦島" };
        for (int i = 0; i < 3; i++) {
            uint32_t bg = (i == g_pause_sel) ? 0x2563EB : 0x1E293B;
            ui_fill_rrect(px + 28, py + 56 + i * 48, pw - 56, 38, 8, bg);
            hal_set_text_bg(bg); ui_draw_str(px + 74, py + 66 + i * 48, po[i], 200, 0xFFFFFF);
        }
        return;
    }

    if (g.state == ST_VICTORY) {
        int pw = 460, ph = 200, px = (sw - pw) / 2, py = (sh - ph) / 2;
        hal_clear_text_in_rect(px - 8, py - 8, pw + 16, ph + 16); hal_dim_rect(0, 0, sw, sh);
        ui_card(px, py, pw, ph, 14, 0x131C31, 0xFACC15, 0x050810, 1, 0xFACC15);
        hal_set_text_bg(0x131C31);
        ui_draw_str(px + 116, py + 24, "【卡坦島霸主誕生！】", 240, 0xFACC15);
        ui_draw_str(px + 32, py + 72, g.log1, pw - 64, 0xFFFFFF);
        ui_fill_rrect(px + 130, py + 132, 200, 42, 10, 0x10B981);
        hal_set_text_bg(0x10B981); ui_draw_str(px + 178, py + 144, "返回主畫面", 120, 0xFFFFFF);
    }
}

void catan_render(void) {
    int sw = hal_get_width(), sh = hal_get_height();
    g_redraw = 0;

    if (g.state == ST_TITLE) {
        hal_clear(0x0C3B5E);
        ui_fill_hex_pointy(120, 140, 56, RES_COLS[RES_WOOD], 0xFEF3C7);
        ui_fill_hex_pointy(680, 140, 56, RES_COLS[RES_WHEAT], 0xFEF3C7);
        ui_fill_hex_pointy(120, 340, 56, RES_COLS[RES_BRICK], 0xFEF3C7);
        ui_fill_hex_pointy(680, 340, 56, RES_COLS[RES_ORE], 0xFEF3C7);

        int pw = 460, ph = 370, px = (sw - pw) / 2, py = 54;
        ui_card(px, py, pw, ph, 16, 0x131C31, 0xFACC15, 0x050810, 1, 0xFACC15);
        hal_set_text_bg(0x131C31);
        ui_draw_str(px + 104, py + 28, "卡 坦 島  CATAN  完整版", 280, 0xFACC15);
        ui_draw_str(px + 54,  py + 66, "19格六角島 · 9港口 · 25張發展卡 · 強盜偷牌 · AI交易", 380, 0x94A3B8);
        ui_draw_str(px + 84,  py + 96, "蛇形開局 · 最長道路 · 最大軍隊 · 觸控/鍵盤雙支援", 340, 0x38BDF8);

        int has_sav = has_save_file(), n = has_sav ? 3 : 2;
        const char *opts[3];
        if (has_sav) { opts[0] = "繼續上次存檔"; opts[1] = "開始新的一局"; opts[2] = "離開遊戲"; }
        else { opts[0] = "開始新的一局"; opts[1] = "離開遊戲"; }

        for (int i = 0; i < n; i++) {
            uint32_t bg = (i == g_title_sel) ? ((i == 0) ? 0x059669 : 0x2563EB) : 0x1E293B;
            ui_fill_rrect(px + 60, py + 150 + i * 54, pw - 120, 42, 10, bg);
            hal_set_text_bg(bg); ui_draw_str(px + 156, py + 162 + i * 54, opts[i], 180, 0xFFFFFF);
        }
        hal_present();
        return;
    }

    if (g.state == ST_SETUP_MENU) {
        hal_clear(0x0C3B5E);
        int pw = 500, ph = 360, px = (sw - pw) / 2, py = 46;
        ui_card(px, py, pw, ph, 16, 0x131C31, 0x38BDF8, 0x050810, 1, 0x38BDF8);
        hal_set_text_bg(0x131C31); ui_draw_str(px + 164, py + 22, "卡坦島開局設定", 200, 0x38BDF8);

        char rows[4][80];
        snprintf(rows[0], sizeof(rows[0]), "總玩家人數：< %d 人對戰 >", g_cfg_players);
        snprintf(rows[1], sizeof(rows[1]), "人類玩家數：< %d 人類 vs %d AI >", g_cfg_humans, g_cfg_players - g_cfg_humans);
        snprintf(rows[2], sizeof(rows[2]), "勝利目標分：< %d 勝利點 (官方10分) >", g_cfg_win_vp);
        snprintf(rows[3], sizeof(rows[3]), ">>>   隨機生成卡坦島，出發！   <<<");

        for (int i = 0; i < 4; i++) {
            uint32_t bg = (i == g_setup_row) ? ((i == 3) ? 0xEA580C : 0x2563EB) : 0x1E293B;
            ui_fill_rrect(px + 28, py + 68 + i * 58, pw - 56, 44, 10, bg);
            hal_set_text_bg(bg); ui_draw_str(px + 64, py + 80 + i * 58, rows[i], pw - 100, 0xFFFFFF);
        }
        hal_present();
        return;
    }

    render_board_map();
    render_right_hud();
    render_modals(sw, sh);
    hal_present();
}
