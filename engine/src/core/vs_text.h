/* UI text: Liberation Mono at Courier metrics (vs_font_data.h, SIL OFL
 * 1.1), standing in for the browser font the original's menus use. The
 * layout metrics below are those of the old 5x7 font: 6 px advance and
 * 7 px cap height at scale 1. */
#ifndef VS_TEXT_H
#define VS_TEXT_H

#include <stdint.h>

#include "core/vs_platform.h"

#define VS_TEXT_GLYPH_W 5
#define VS_TEXT_GLYPH_H 7

/* Creates the glyph texture (ASCII 32..126 at four sizes). */
int vs_text_init(void);
void vs_text_shutdown(void);
/* Draws at logical (x, y): x left edge, y top of the capitals; 6 px advance
 * and 7 px capitals at scale 1, descenders below. */
void vs_text_draw(float x, float y, float scale, uint32_t tint, const char *s);
float vs_text_width(float scale, const char *s);

#endif
