/* Built-in 5x7 bitmap font for debug text and placeholder UI. Ours, not
 * from the game; a proper openly licensed pixel font replaces it in the UI
 * later (FABLE_GUIDE Phase 4.4). */
#ifndef VS_TEXT_H
#define VS_TEXT_H

#include <stdint.h>

#include "core/vs_platform.h"

#define VS_TEXT_GLYPH_W 5
#define VS_TEXT_GLYPH_H 7

/* Creates the glyph texture (ASCII 32..126 in one row). */
int vs_text_init(void);
void vs_text_shutdown(void);
/* Draws at logical (x, y), top-left; scale 1 = 5x7 px per glyph, 6 px advance. */
void vs_text_draw(float x, float y, float scale, uint32_t tint, const char *s);
float vs_text_width(float scale, const char *s);

#endif
