#ifndef CATAN_MAP_H
#define CATAN_MAP_H

#include <stdint.h>

#define NUM_HEXES    19
#define NUM_VERTICES 54
#define NUM_EDGES    72
#define NUM_PORTS    9

typedef enum {
    RES_WOOD = 0,  /* 木頭 (森林) */
    RES_BRICK,     /* 磚塊 (丘陵) */
    RES_SHEEP,     /* 羊毛 (牧場) */
    RES_WHEAT,     /* 小麥 (農田) */
    RES_ORE,       /* 礦石 (山脈) */
    RES_DESERT,    /* 沙漠 (不產資源) */
    RES_GOLD       /* 黃金河擴充 (產出時可任選任意資源!) */
} TerrainType;

typedef enum {
    MAP_CLASSIC = 0,        /* 標準經典卡坦島 */
    MAP_GOLD_RIVER,         /* 黃金河擴充 (含 2 塊黃金河) */
    MAP_RICH_HARBORS,       /* 富饒海港大亨 (多 2:1 港口 + 豐產數字) */
    MAP_BARREN_CHALLENGE    /* 雙沙漠硬核荒漠挑戰 */
} MapMode;

typedef enum {
    PORT_NONE = 0,
    PORT_ANY3,
    PORT_WOOD2,
    PORT_BRICK2,
    PORT_SHEEP2,
    PORT_WHEAT2,
    PORT_ORE2
} PortType;

typedef struct {
    int q, r;
    int cx, cy;
    TerrainType terrain;
    int number;
    int verts[6];
} HexTile;

typedef struct {
    int x, y;
    int hexes[3];
    int hex_cnt;
    int adj_v[3];
    int adj_e[3];
    int deg;
    PortType port;
    int owner;
    int level;
} Vertex;

typedef struct {
    int v0, v1;
    int mx, my;
    int owner;
} Edge;

typedef struct {
    int edge_idx;
    int v0, v1;
    int bx, by;
    PortType type;
} PortInfo;

typedef struct {
    HexTile  hexes[NUM_HEXES];
    Vertex   verts[NUM_VERTICES];
    Edge     edges[NUM_EDGES];
    PortInfo ports[NUM_PORTS];
    int      robber_hex;
    int      hex_radius;
    int      map_mode;
} CatanBoard;

void catan_board_generate(CatanBoard *b, uint32_t seed, int map_mode, int center_x, int center_y, int hex_r);
void catan_board_rebuild_coords(CatanBoard *b, int center_x, int center_y, int hex_r);
int  catan_find_edge(const CatanBoard *b, int v0, int v1);
int  catan_hex_pips(int num);

#endif
