#ifndef _FONT_H_
#define _FONT_H_

#include "stdbool.h"
#include "stdint.h"

#define UI_FONT_WIDTH 20
#define UI_FONT_HEIGHT 18
#define UI_FONT_ADVANCE 12
#define UI_FONT_WIDE_ADVANCE 18
#define UI_FONT_PATH "/fonts/msyh.ttc"
#define UI_FONT_FALLBACK_PATH "/MSYH.TTC"

typedef void (*font_plot_fn)(uint16_t x, uint16_t y, uint32_t color);

void font_init(void);
bool font_ready(void);
uint32_t font_text_width(const char *text);
uint32_t font_utf8_next(const char **cursor);
uint32_t font_codepoint_advance(uint32_t codepoint);
void font_draw_codepoint(uint16_t x, uint16_t y, uint32_t codepoint, uint32_t color, font_plot_fn plot);

#endif
