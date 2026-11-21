#include "catan_map.h"
#include <stdlib.h>
#include <string.h>

static uint32_t g_mrng = 1u;
static int mrng(int m) {
    g_mrng = g_mrng * 1664525u + 1013904223u;
    return (int)((g_mrng >> 16) % (uint32_t)m);
}

static void shuffle_int(int *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = mrng(i + 1);
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* 純整數平方根 (免連結 -lm，ARM9 超高速且 100% 跨平台相容!) */
static int isqrt_u32(uint32_t n) {
    uint32_t x = n, y = (x + 1u) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return (int)x;
}

/* 純整數極角排序輔助 (0..1023) */
static int iangle_1024(int dy, int dx) {
    if (dx == 0 && dy == 0) return 0;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int oct = (ax >= ay) ? ((ay * 128) / (ax + 1)) : (256 - (ax * 128) / (ay + 1));
    if (dx >= 0 && dy >= 0) return oct;
    if (dx <  0 && dy >= 0) return 512 - oct;
    if (dx <  0 && dy <  0) return 512 + oct;
    return (1024 - oct) & 1023;
}

static const int HEX_QR[NUM_HEXES][2] = {
    { 0, -2 }, { 1, -2 }, { 2, -2 },
    { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 },
    { -2,  0 }, { -1,  0 }, { 0,  0 }, { 1,  0 }, { 2,  0 },
    { -2,  1 }, { -1,  1 }, { 0,  1 }, { 1,  1 },
    { -2,  2 }, { -1,  2 }, { 0,  2 }
};

int catan_hex_pips(int num) {
    if (num < 2 || num > 12 || num == 7) return 0;
    return 6 - abs(7 - num);
}

int catan_find_edge(const CatanBoard *b, int v0, int v1) {
    for (int i = 0; i < NUM_EDGES; i++) {
        if ((b->edges[i].v0 == v0 && b->edges[i].v1 == v1) ||
            (b->edges[i].v0 == v1 && b->edges[i].v1 == v0)) return i;
    }
    return -1;
}

static int hexes_are_adjacent(const CatanBoard *b, int h1, int h2) {
    int dq = b->hexes[h1].q - b->hexes[h2].q;
    int dr = b->hexes[h1].r - b->hexes[h2].r;
    int ds = -(dq + dr);
    return ((abs(dq) + abs(dr) + abs(ds)) / 2 == 1);
}

void catan_board_rebuild_coords(CatanBoard *b, int center_x, int center_y, int hex_r) {
    b->hex_radius = hex_r;
    /* 1000x 定點 sqrt(3) = 1732, sqrt(3)/2 = 866 */
    static const int COR_DX[6] = { 0, 866, 866, 0, -866, -866 };
    static const int COR_DY[6] = { -1000, -500, 500, 1000, 500, -500 };

    int hcx[NUM_HEXES], hcy[NUM_HEXES];
    for (int i = 0; i < NUM_HEXES; i++) {
        int q2 = b->hexes[i].q * 2 + b->hexes[i].r; /* 2*(q + r/2) */
        hcx[i] = center_x + (hex_r * 866 * q2) / 1000;
        hcy[i] = center_y + (hex_r * 3 * b->hexes[i].r) / 2;
        b->hexes[i].cx = hcx[i];
        b->hexes[i].cy = hcy[i];
    }

    for (int h = 0; h < NUM_HEXES; h++) {
        for (int c = 0; c < 6; c++) {
            int vi = b->hexes[h].verts[c];
            if (vi >= 0 && vi < NUM_VERTICES) {
                b->verts[vi].x = hcx[h] + (hex_r * COR_DX[c]) / 1000;
                b->verts[vi].y = hcy[h] + (hex_r * COR_DY[c]) / 1000;
            }
        }
    }

    for (int e = 0; e < NUM_EDGES; e++) {
        int v0 = b->edges[e].v0, v1 = b->edges[e].v1;
        b->edges[e].mx = (b->verts[v0].x + b->verts[v1].x) / 2;
        b->edges[e].my = (b->verts[v0].y + b->verts[v1].y) / 2;
    }

    for (int p = 0; p < NUM_PORTS; p++) {
        int e = b->ports[p].edge_idx;
        if (e >= 0 && e < NUM_EDGES) {
            int mx = b->edges[e].mx, my = b->edges[e].my;
            int dx = mx - center_x, dy = my - center_y;
            int len = isqrt_u32((uint32_t)(dx * dx + dy * dy));
            if (len < 1) len = 1;
            b->ports[p].bx = mx + (dx * 27) / len;
            b->ports[p].by = my + (dy * 27) / len;
        }
    }
}

void catan_board_generate(CatanBoard *b, uint32_t seed, int map_mode, int center_x, int center_y, int hex_r) {
    memset(b, 0, sizeof(*b));
    g_mrng = seed ? seed : 0xCAFEBABEu;
    b->hex_radius = hex_r;
    b->map_mode = map_mode;

    int terrains[NUM_HEXES] = {
        RES_WOOD, RES_WOOD, RES_WOOD, RES_WOOD,
        RES_BRICK, RES_BRICK, RES_BRICK,
        RES_SHEEP, RES_SHEEP, RES_SHEEP, RES_SHEEP,
        RES_WHEAT, RES_WHEAT, RES_WHEAT, RES_WHEAT,
        RES_ORE, RES_ORE, RES_ORE,
        RES_DESERT
    };

    if (map_mode == MAP_GOLD_RIVER) {
        /* 黃金河擴充：1張木頭與1張羊毛升級為珍貴黃金河！ */
        terrains[0] = RES_GOLD;
        terrains[7] = RES_GOLD;
    } else if (map_mode == MAP_BARREN_CHALLENGE) {
        /* 雙沙漠荒漠挑戰 */
        terrains[14] = RES_DESERT;
    }

    shuffle_int(terrains, NUM_HEXES);

    int desert_cnt = 0;
    for (int i = 0; i < NUM_HEXES; i++) {
        b->hexes[i].q = HEX_QR[i][0];
        b->hexes[i].r = HEX_QR[i][1];
        b->hexes[i].terrain = (TerrainType)terrains[i];
        b->hexes[i].number = 0;
        if (terrains[i] == RES_DESERT) { b->robber_hex = i; desert_cnt++; }
    }

    int tokens[18] = { 2, 3, 3, 4, 4, 5, 5, 6, 6, 8, 8, 9, 9, 10, 10, 11, 11, 12 };
    if (map_mode == MAP_RICH_HARBORS) {
        /* 富饒海港大亨：將極難擲出的 2 與 12 改為 5 與 9，資源超爽爆噴！ */
        tokens[0] = 5; tokens[17] = 9;
    }

    int need_tokens = NUM_HEXES - desert_cnt;
    for (int attempt = 0; attempt < 220; attempt++) {
        shuffle_int(tokens, need_tokens);
        int ti = 0;
        for (int i = 0; i < NUM_HEXES; i++) {
            b->hexes[i].number = (b->hexes[i].terrain == RES_DESERT) ? 0 : tokens[ti++];
        }
        int bad = 0;
        for (int i = 0; i < NUM_HEXES && !bad; i++) {
            if (b->hexes[i].number != 6 && b->hexes[i].number != 8) continue;
            for (int j = i + 1; j < NUM_HEXES; j++) {
                if ((b->hexes[j].number == 6 || b->hexes[j].number == 8) && hexes_are_adjacent(b, i, j)) {
                    bad = 1; break;
                }
            }
        }
        if (!bad) break;
    }

    /* 以純整數千分座標建立 54 個唯一頂點與 72 條道路邊 */
    int vx_k[NUM_VERTICES], vy_k[NUM_VERTICES];
    int vcnt = 0, ecnt = 0;
    static const int COR_DX[6] = { 0, 866, 866, 0, -866, -866 };
    static const int COR_DY[6] = { -1000, -500, 500, 1000, 500, -500 };

    for (int i = 0; i < NUM_VERTICES; i++) {
        b->verts[i].owner = -1;
        b->verts[i].hexes[0] = b->verts[i].hexes[1] = b->verts[i].hexes[2] = -1;
    }
    for (int i = 0; i < NUM_EDGES; i++) b->edges[i].owner = -1;

    for (int h = 0; h < NUM_HEXES; h++) {
        int q2 = b->hexes[h].q * 2 + b->hexes[h].r;
        int hcx = 866 * q2;
        int hcy = 1500 * b->hexes[h].r;

        for (int c = 0; c < 6; c++) {
            int px = hcx + COR_DX[c];
            int py = hcy + COR_DY[c];
            int found = -1;
            for (int k = 0; k < vcnt; k++) {
                int dx = vx_k[k] - px, dy = vy_k[k] - py;
                if (dx * dx + dy * dy < 4000) { found = k; break; }
            }
            if (found < 0 && vcnt < NUM_VERTICES) {
                found = vcnt++;
                vx_k[found] = px; vy_k[found] = py;
            }
            b->hexes[h].verts[c] = found;
            if (found >= 0 && b->verts[found].hex_cnt < 3) {
                b->verts[found].hexes[b->verts[found].hex_cnt++] = h;
            }
        }
    }

    for (int h = 0; h < NUM_HEXES; h++) {
        for (int c = 0; c < 6; c++) {
            int v0 = b->hexes[h].verts[c];
            int v1 = b->hexes[h].verts[(c + 1) % 6];
            int ei = -1;
            for (int e = 0; e < ecnt; e++) {
                if ((b->edges[e].v0 == v0 && b->edges[e].v1 == v1) ||
                    (b->edges[e].v0 == v1 && b->edges[e].v1 == v0)) { ei = e; break; }
            }
            if (ei < 0 && ecnt < NUM_EDGES) {
                ei = ecnt++;
                b->edges[ei].v0 = v0; b->edges[ei].v1 = v1; b->edges[ei].owner = -1;
                if (b->verts[v0].deg < 3) { b->verts[v0].adj_v[b->verts[v0].deg] = v1; b->verts[v0].adj_e[b->verts[v0].deg] = ei; b->verts[v0].deg++; }
                if (b->verts[v1].deg < 3) { b->verts[v1].adj_v[b->verts[v1].deg] = v0; b->verts[v1].adj_e[b->verts[v1].deg] = ei; b->verts[v1].deg++; }
            }
        }
    }

    /* 依極角排序 30 條海岸邊並配置 9 座港口 */
    int coast_edges[40], coast_ang[40], ccnt = 0;
    for (int e = 0; e < ecnt; e++) {
        int v0 = b->edges[e].v0, v1 = b->edges[e].v1, shared = 0;
        for (int i = 0; i < b->verts[v0].hex_cnt; i++)
            for (int j = 0; j < b->verts[v1].hex_cnt; j++)
                if (b->verts[v0].hexes[i] == b->verts[v1].hexes[j]) shared++;
        if (shared == 1 && ccnt < 40) {
            coast_edges[ccnt] = e;
            coast_ang[ccnt] = iangle_1024((vy_k[v0] + vy_k[v1]) / 2, (vx_k[v0] + vx_k[v1]) / 2);
            ccnt++;
        }
    }
    for (int i = 0; i < ccnt; i++) {
        for (int j = i + 1; j < ccnt; j++) {
            if (coast_ang[j] < coast_ang[i]) {
                int ta = coast_ang[i]; coast_ang[i] = coast_ang[j]; coast_ang[j] = ta;
                int te = coast_edges[i]; coast_edges[i] = coast_edges[j]; coast_edges[j] = te;
            }
        }
    }

    int port_types[NUM_PORTS] = {
        PORT_ANY3, PORT_WOOD2, PORT_ANY3, PORT_BRICK2, PORT_ANY3,
        PORT_SHEEP2, PORT_WHEAT2, PORT_ANY3, PORT_ORE2
    };
    if (map_mode == MAP_RICH_HARBORS) {
        port_types[0] = PORT_WHEAT2; port_types[2] = PORT_ORE2;
    }
    shuffle_int(port_types, NUM_PORTS);

    static const int PORT_SLOTS[NUM_PORTS] = { 0, 3, 6, 10, 13, 16, 20, 23, 26 };
    for (int p = 0; p < NUM_PORTS; p++) {
        int ce = (PORT_SLOTS[p] < ccnt) ? coast_edges[PORT_SLOTS[p]] : 0;
        int v0 = b->edges[ce].v0, v1 = b->edges[ce].v1;
        b->ports[p].edge_idx = ce; b->ports[p].v0 = v0; b->ports[p].v1 = v1;
        b->ports[p].type = (PortType)port_types[p];
        b->verts[v0].port = (PortType)port_types[p];
        b->verts[v1].port = (PortType)port_types[p];
    }

    catan_board_rebuild_coords(b, center_x, center_y, hex_r);
}
