#ifndef CATAN_GAME_H
#define CATAN_GAME_H

#include "hal.h"

void catan_init(void);
void catan_shutdown(void);
void catan_on_key(CatanKey key);
void catan_on_touch(int tx, int ty);
void catan_tick(int dt_ms);
int  catan_needs_redraw(void);
void catan_render(void);

#endif
