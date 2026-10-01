/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _SDL_CHARGFX_H_
#define _SDL_CHARGFX_H_

#include <stdbool.h>
#include <stdint.h>

// Same character grid geometry as the device: 32x24 cells of 10x10 pixels,
// giving the firmware's native 320x240 screen.
#define TEXT_WIDTH 32
#define TEXT_HEIGHT 24
#define CHAR_WIDTH 10
#define CHAR_HEIGHT 10

#define SCREEN_WIDTH (TEXT_WIDTH * CHAR_WIDTH)
#define SCREEN_HEIGHT (TEXT_HEIGHT * CHAR_HEIGHT)

// Mirrors chargfx_color_t on the device: an index into a 16 entry palette.
typedef uint8_t sdlchargfx_color_t;

bool sdlchargfx_init(const char *title, int scale);
void sdlchargfx_shutdown();

void sdlchargfx_clear(sdlchargfx_color_t color);
void sdlchargfx_set_foreground(sdlchargfx_color_t color);
void sdlchargfx_set_background(sdlchargfx_color_t color);
void sdlchargfx_set_palette_color(uint8_t idx, uint16_t rgb565);
void sdlchargfx_set_font_index(uint8_t idx);
void sdlchargfx_set_cursor(uint8_t x, uint8_t y);
void sdlchargfx_putc(char c, bool invert);
void sdlchargfx_fill_rect(sdlchargfx_color_t color, uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height);

// Pushes the character grid to the SDL window.
void sdlchargfx_present();

// Writes the current window contents to a PNG-less raw BMP. Used by the
// --screenshot option so the emulator can be checked in headless CI.
bool sdlchargfx_screenshot(const char *path);

#endif
