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
    RES_DESERT     /* 沙漠 (不產資源，強盜初始位置) */
} TerrainType;

typedef enum {
    PORT_NONE = 0,
    PORT_ANY3,     /* 3:1 通用港 */
    PORT_WOOD2,    /* 2:1 木頭港 */
    PORT_BRICK2,   /* 2:1 磚塊港 */
    PORT_SHEEP2,   /* 2:1 羊毛港 */
    PORT_WHEAT2,   /* 2:1 小麥港 */
    PORT_ORE2      /* 2:1 礦石港 */
} PortType;

typedef struct {
    int q, r;          /* Axial coordinates */
    int cx, cy;        /* Screen center pixels */
    TerrainType terrain;
    int number;        /* 2..12 (0 for desert) */
    int verts[6];      /* 6 corner vertex indices */
} HexTile;

typedef struct {
    int x, y;          /* Screen pixels */
    int hexes[3];      /* Adjacent hex indices (-1 if none) */
    int hex_cnt;
    int adj_v[3];      /* Adjacent vertex indices */
    int adj_e[3];      /* Incident edge indices */
    int deg;           /* 2 or 3 */
    PortType port;
    /* Runtime building state */
    int owner;         /* -1 = empty, 0..3 = player */
    int level;         /* 0 = none, 1 = settlement, 2 = city */
} Vertex;

typedef struct {
    int v0, v1;
    int mx, my;        /* Midpoint screen pixels */
    int owner;         /* -1 = empty, 0..3 = player */
} Edge;

typedef struct {
    int edge_idx;      /* Coastal edge */
    int v0, v1;
    int bx, by;        /* Badge center in ocean */
    PortType type;
} PortInfo;

typedef struct {
    HexTile  hexes[NUM_HEXES];
    Vertex   verts[NUM_VERTICES];
    Edge     edges[NUM_EDGES];
    PortInfo ports[NUM_PORTS];
    int      robber_hex;
    int      hex_radius;
} CatanBoard;

void catan_board_generate(CatanBoard *b, uint32_t seed, int center_x, int center_y, int hex_r);
void catan_board_rebuild_coords(CatanBoard *b, int center_x, int center_y, int hex_r);
int  catan_find_edge(const CatanBoard *b, int v0, int v1);
int  catan_hex_pips(int num);

#endif
