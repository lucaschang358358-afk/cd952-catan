#include "catan_map.h"
#include <math.h>
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

/* 標準 19 格卡坦島軸向座標 (3-4-5-4-3 排列) */
static const int HEX_QR[NUM_HEXES][2] = {
    /* r = -2 (3 hexes) */
    { 0, -2 }, { 1, -2 }, { 2, -2 },
    /* r = -1 (4 hexes) */
    { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 },
    /* r =  0 (5 hexes) */
    { -2,  0 }, { -1,  0 }, { 0,  0 }, { 1,  0 }, { 2,  0 },
    /* r = +1 (4 hexes) */
    { -2,  1 }, { -1,  1 }, { 0,  1 }, { 1,  1 },
    /* r = +2 (3 hexes) */
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
    int dist = (abs(dq) + abs(dr) + abs(ds)) / 2;
    return (dist == 1);
}

void catan_board_rebuild_coords(CatanBoard *b, int center_x, int center_y, int hex_r) {
    b->hex_radius = hex_r;
    double sqrt3 = 1.7320508075688772;

    /* 1. 計算 19 格中心座標 */
    double hcx[NUM_HEXES], hcy[NUM_HEXES];
    for (int i = 0; i < NUM_HEXES; i++) {
        double q = (double)b->hexes[i].q;
        double r = (double)b->hexes[i].r;
        hcx[i] = (double)center_x + (double)hex_r * sqrt3 * (q + r * 0.5);
        hcy[i] = (double)center_y + (double)hex_r * 1.5 * r;
        b->hexes[i].cx = (int)llround(hcx[i]);
        b->hexes[i].cy = (int)llround(hcy[i]);
    }

    /* 2. 重新計算每個頂點的精準像素位置 */
    static const double COR_DX[6] = { 0.0, 0.8660254, 0.8660254, 0.0, -0.8660254, -0.8660254 };
    static const double COR_DY[6] = { -1.0, -0.5, 0.5, 1.0, 0.5, -0.5 };
    for (int h = 0; h < NUM_HEXES; h++) {
        for (int c = 0; c < 6; c++) {
            int vi = b->hexes[h].verts[c];
            if (vi >= 0 && vi < NUM_VERTICES) {
                b->verts[vi].x = (int)llround(hcx[h] + (double)hex_r * COR_DX[c]);
                b->verts[vi].y = (int)llround(hcy[h] + (double)hex_r * COR_DY[c]);
            }
        }
    }

    /* 3. 邊中點與 9 個外海港口位置 */
    for (int e = 0; e < NUM_EDGES; e++) {
        int v0 = b->edges[e].v0, v1 = b->edges[e].v1;
        b->edges[e].mx = (b->verts[v0].x + b->verts[v1].x) / 2;
        b->edges[e].my = (b->verts[v0].y + b->verts[v1].y) / 2;
    }
    for (int p = 0; p < NUM_PORTS; p++) {
        int e = b->ports[p].edge_idx;
        if (e >= 0 && e < NUM_EDGES) {
            int mx = b->edges[e].mx, my = b->edges[e].my;
            double dx = (double)(mx - center_x);
            double dy = (double)(my - center_y);
            double len = sqrt(dx * dx + dy * dy);
            if (len < 1.0) len = 1.0;
            b->ports[p].bx = (int)llround((double)mx + (dx / len) * 24.0);
            b->ports[p].by = (int)llround((double)my + (dy / len) * 24.0);
        }
    }
}

void catan_board_generate(CatanBoard *b, uint32_t seed, int center_x, int center_y, int hex_r) {
    memset(b, 0, sizeof(*b));
    g_mrng = seed ? seed : 0xCAFEBABEu;
    b->hex_radius = hex_r;

    /* 1. 標準 19 格地形分配: 4木 3磚 4羊 4麥 3礦 1沙漠 */
    int terrains[NUM_HEXES] = {
        RES_WOOD, RES_WOOD, RES_WOOD, RES_WOOD,
        RES_BRICK, RES_BRICK, RES_BRICK,
        RES_SHEEP, RES_SHEEP, RES_SHEEP, RES_SHEEP,
        RES_WHEAT, RES_WHEAT, RES_WHEAT, RES_WHEAT,
        RES_ORE, RES_ORE, RES_ORE,
        RES_DESERT
    };
    shuffle_int(terrains, NUM_HEXES);

    for (int i = 0; i < NUM_HEXES; i++) {
        b->hexes[i].q = HEX_QR[i][0];
        b->hexes[i].r = HEX_QR[i][1];
        b->hexes[i].terrain = (TerrainType)terrains[i];
        b->hexes[i].number = 0;
        if (terrains[i] == RES_DESERT) b->robber_hex = i;
    }

    /* 2. 標準 18 枚數字圓片 (確保 6 與 8 絕不相鄰!) */
    int tokens[18] = { 2, 3, 3, 4, 4, 5, 5, 6, 6, 8, 8, 9, 9, 10, 10, 11, 11, 12 };
    for (int attempt = 0; attempt < 200; attempt++) {
        shuffle_int(tokens, 18);
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

    /* 3. 自動拓撲生成 54 個唯一頂點與 72 條唯一道路邊 */
    double vx_f[NUM_VERTICES], vy_f[NUM_VERTICES];
    int vcnt = 0, ecnt = 0;
    double sqrt3 = 1.7320508075688772;
    static const double COR_DX[6] = { 0.0, 0.8660254, 0.8660254, 0.0, -0.8660254, -0.8660254 };
    static const double COR_DY[6] = { -1.0, -0.5, 0.5, 1.0, 0.5, -0.5 };

    for (int i = 0; i < NUM_VERTICES; i++) {
        b->verts[i].owner = -1;
        b->verts[i].hexes[0] = b->verts[i].hexes[1] = b->verts[i].hexes[2] = -1;
    }
    for (int i = 0; i < NUM_EDGES; i++) b->edges[i].owner = -1;

    for (int h = 0; h < NUM_HEXES; h++) {
        double hcx = sqrt3 * ((double)b->hexes[h].q + (double)b->hexes[h].r * 0.5);
        double hcy = 1.5 * (double)b->hexes[h].r;

        for (int c = 0; c < 6; c++) {
            double px = hcx + COR_DX[c];
            double py = hcy + COR_DY[c];
            int found = -1;
            for (int k = 0; k < vcnt; k++) {
                double dx = vx_f[k] - px, dy = vy_f[k] - py;
                if (dx * dx + dy * dy < 0.01) { found = k; break; }
            }
            if (found < 0 && vcnt < NUM_VERTICES) {
                found = vcnt++;
                vx_f[found] = px;
                vy_f[found] = py;
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
                b->edges[ei].v0 = v0;
                b->edges[ei].v1 = v1;
                b->edges[ei].owner = -1;
                if (b->verts[v0].deg < 3) {
                    b->verts[v0].adj_v[b->verts[v0].deg] = v1;
                    b->verts[v0].adj_e[b->verts[v0].deg] = ei;
                    b->verts[v0].deg++;
                }
                if (b->verts[v1].deg < 3) {
                    b->verts[v1].adj_v[b->verts[v1].deg] = v0;
                    b->verts[v1].adj_e[b->verts[v1].deg] = ei;
                    b->verts[v1].deg++;
                }
            }
        }
    }

    /* 4. 找出 30 條海岸線邊並依極角排序，均勻配置 9 個標準港口 (4x 3:1 + 5x 2:1) */
    int coast_edges[40];
    double coast_ang[40];
    int ccnt = 0;
    for (int e = 0; e < ecnt; e++) {
        int v0 = b->edges[e].v0, v1 = b->edges[e].v1;
        /* 若該邊的兩端點合計僅共享 1 塊陸地，即為海岸邊 */
        int shared = 0;
        for (int i = 0; i < b->verts[v0].hex_cnt; i++)
            for (int j = 0; j < b->verts[v1].hex_cnt; j++)
                if (b->verts[v0].hexes[i] == b->verts[v1].hexes[j]) shared++;
        if (shared == 1 && ccnt < 40) {
            coast_edges[ccnt] = e;
            double mx = 0.5 * (vx_f[v0] + vx_f[v1]);
            double my = 0.5 * (vy_f[v0] + vy_f[v1]);
            coast_ang[ccnt] = atan2(my, mx);
            ccnt++;
        }
    }
    for (int i = 0; i < ccnt; i++) {
        for (int j = i + 1; j < ccnt; j++) {
            if (coast_ang[j] < coast_ang[i]) {
                double ta = coast_ang[i]; coast_ang[i] = coast_ang[j]; coast_ang[j] = ta;
                int te = coast_edges[i]; coast_edges[i] = coast_edges[j]; coast_edges[j] = te;
            }
        }
    }

    int port_types[NUM_PORTS] = {
        PORT_ANY3, PORT_WOOD2, PORT_ANY3, PORT_BRICK2, PORT_ANY3,
        PORT_SHEEP2, PORT_WHEAT2, PORT_ANY3, PORT_ORE2
    };
    shuffle_int(port_types, NUM_PORTS);

    /* 在 30 條海岸邊上均勻挑選 9 條互不重疊頂點的港口邊 */
    static const int PORT_SLOTS[NUM_PORTS] = { 0, 3, 6, 10, 13, 16, 20, 23, 26 };
    for (int p = 0; p < NUM_PORTS; p++) {
        int ce = (PORT_SLOTS[p] < ccnt) ? coast_edges[PORT_SLOTS[p]] : 0;
        int v0 = b->edges[ce].v0, v1 = b->edges[ce].v1;
        b->ports[p].edge_idx = ce;
        b->ports[p].v0 = v0;
        b->ports[p].v1 = v1;
        b->ports[p].type = (PortType)port_types[p];
        b->verts[v0].port = (PortType)port_types[p];
        b->verts[v1].port = (PortType)port_types[p];
    }

    catan_board_rebuild_coords(b, center_x, center_y, hex_r);
}
