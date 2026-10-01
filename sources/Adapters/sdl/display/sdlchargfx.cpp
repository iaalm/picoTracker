/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "sdlchargfx.h"
#include "Adapters/picoTracker/display/font.h"
#include <SDL3/SDL.h>
#include <string.h>

#define CELL_COUNT (TEXT_WIDTH * TEXT_HEIGHT)

// Character grid mirroring the device's: screen[] holds the glyph index
// (ascii - 32) and colors[] packs fg in the high nibble, bg in the low one.
static uint8_t screen_[CELL_COUNT];
static uint8_t colors_[CELL_COUNT];

static uint16_t palette_[16];
static uint8_t fg_color_ = 1;
static uint8_t bg_color_ = 0;
static uint8_t cursor_x_ = 0;
static uint8_t cursor_y_ = 0;
static uint8_t font_index_ = 0;

// Pixels the firmware painted directly via fill_rect rather than as text.
// Stored as a full-resolution overlay so DrawRect lands on exact pixel
// boundaries instead of being snapped to the character grid.
static uint32_t overlay_[SCREEN_WIDTH * SCREEN_HEIGHT];
static bool overlayUsed_[SCREEN_WIDTH * SCREEN_HEIGHT];

static SDL_Window *window_ = NULL;
static SDL_Renderer *renderer_ = NULL;
static SDL_Texture *texture_ = NULL;
static uint32_t framebuffer_[SCREEN_WIDTH * SCREEN_HEIGHT];

static uint32_t rgb565_to_argb8888(uint16_t c) {
  // Replicate the high bits into the low ones so full-scale 565 maps to
  // full-scale 888 (0x1f -> 0xff rather than 0xf8).
  uint32_t r = (c >> 11) & 0x1f;
  uint32_t g = (c >> 5) & 0x3f;
  uint32_t b = c & 0x1f;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  return 0xff000000u | (r << 16) | (g << 8) | b;
}

bool sdlchargfx_init(const char *title, int scale) {
  if (scale < 1) {
    scale = 1;
  }

  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_InitSubSystem(video) failed: %s", SDL_GetError());
    return false;
  }

  if (!SDL_CreateWindowAndRenderer(title, SCREEN_WIDTH * scale,
                                   SCREEN_HEIGHT * scale, 0, &window_,
                                   &renderer_)) {
    SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
    return false;
  }

  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                               SCREEN_HEIGHT);
  if (!texture_) {
    SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
    return false;
  }
  // The UI is a pixel-art character grid; keep it crisp when scaled up.
  SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
  SDL_SetRenderLogicalPresentation(renderer_, SCREEN_WIDTH, SCREEN_HEIGHT,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);

  memset(screen_, 0, sizeof(screen_));
  memset(colors_, 0, sizeof(colors_));
  memset(overlayUsed_, 0, sizeof(overlayUsed_));
  for (int i = 0; i < 16; i++) {
    palette_[i] = 0;
  }
  return true;
}

void sdlchargfx_shutdown() {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = NULL;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = NULL;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = NULL;
  }
}

void sdlchargfx_set_palette_color(uint8_t idx, uint16_t rgb565) {
  if (idx < 16) {
    palette_[idx] = rgb565;
  }
}

void sdlchargfx_set_font_index(uint8_t idx) {
  if (idx < (sizeof(fonts) / sizeof(fonts[0]))) {
    font_index_ = idx;
  }
}

void sdlchargfx_set_foreground(sdlchargfx_color_t color) {
  fg_color_ = color & 0xf;
}

void sdlchargfx_set_background(sdlchargfx_color_t color) {
  bg_color_ = color & 0xf;
}

void sdlchargfx_set_cursor(uint8_t x, uint8_t y) {
  cursor_x_ = x;
  cursor_y_ = y;
}

void sdlchargfx_clear(sdlchargfx_color_t color) {
  uint8_t c = color & 0xf;
  memset(screen_, 0, sizeof(screen_));
  memset(overlayUsed_, 0, sizeof(overlayUsed_));
  uint8_t packed = (uint8_t)((c << 4) | c);
  memset(colors_, packed, sizeof(colors_));
}

void sdlchargfx_putc(char c, bool invert) {
  if (cursor_x_ >= TEXT_WIDTH || cursor_y_ >= TEXT_HEIGHT) {
    return;
  }
  if ((unsigned char)c < 32) {
    return;
  }
  int idx = cursor_y_ * TEXT_WIDTH + cursor_x_;
  screen_[idx] = (uint8_t)c - 32;
  colors_[idx] = invert ? (uint8_t)((bg_color_ << 4) | fg_color_)
                        : (uint8_t)((fg_color_ << 4) | bg_color_);

  // Text wins over any rect previously painted on these pixels.
  for (int py = 0; py < CHAR_HEIGHT; py++) {
    int y = cursor_y_ * CHAR_HEIGHT + py;
    bool *row = &overlayUsed_[y * SCREEN_WIDTH + cursor_x_ * CHAR_WIDTH];
    memset(row, 0, CHAR_WIDTH * sizeof(bool));
  }
}

void sdlchargfx_fill_rect(sdlchargfx_color_t color, uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height) {
  uint32_t argb = rgb565_to_argb8888(palette_[color & 0xf]);
  for (int py = y; py < y + height; py++) {
    if (py < 0 || py >= SCREEN_HEIGHT) {
      continue;
    }
    for (int px = x; px < x + width; px++) {
      if (px < 0 || px >= SCREEN_WIDTH) {
        continue;
      }
      overlay_[py * SCREEN_WIDTH + px] = argb;
      overlayUsed_[py * SCREEN_WIDTH + px] = true;
    }
  }
}

static void render_to_framebuffer() {
  const font_t *font = fonts[font_index_];

  for (int cy = 0; cy < TEXT_HEIGHT; cy++) {
    for (int cx = 0; cx < TEXT_WIDTH; cx++) {
      int idx = cy * TEXT_WIDTH + cx;
      uint8_t character = screen_[idx];
      uint32_t fg = rgb565_to_argb8888(palette_[colors_[idx] >> 4]);
      uint32_t bg = rgb565_to_argb8888(palette_[colors_[idx] & 0xf]);

      const uint16_t *pixel_data =
          (character < 96) ? (*font)[character]
                           : FONT_SPECIAL_CHARACTERS_BITMAP[character - 96];

      for (int row = 0; row < CHAR_HEIGHT; row++) {
        uint16_t bits = pixel_data[row];
        int y = cy * CHAR_HEIGHT + row;
        for (int col = 0; col < CHAR_WIDTH; col++) {
          // The font is stored left-to-right, so bit 0 is the leftmost pixel.
          // (The device's renderer walks columns in reverse and so indexes
          // these rows MSB-first; reproducing that here would mirror the
          // glyphs.)
          uint16_t mask = 1 << col;
          int x = cx * CHAR_WIDTH + col;
          int fbIdx = y * SCREEN_WIDTH + x;
          framebuffer_[fbIdx] = overlayUsed_[fbIdx] ? overlay_[fbIdx]
                                : (bits & mask)     ? fg
                                                    : bg;
        }
      }
    }
  }
}

void sdlchargfx_present() {
  if (!renderer_ || !texture_) {
    return;
  }
  render_to_framebuffer();
  SDL_UpdateTexture(texture_, NULL, framebuffer_,
                    SCREEN_WIDTH * sizeof(uint32_t));
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, NULL, NULL);
  SDL_RenderPresent(renderer_);
}

bool sdlchargfx_screenshot(const char *path) {
  render_to_framebuffer();
  SDL_Surface *surface = SDL_CreateSurfaceFrom(
      SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_ARGB8888, framebuffer_,
      SCREEN_WIDTH * sizeof(uint32_t));
  if (!surface) {
    SDL_Log("SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
    return false;
  }
  bool ok = SDL_SaveBMP(surface, path);
  if (!ok) {
    SDL_Log("SDL_SaveBMP failed: %s", SDL_GetError());
  }
  SDL_DestroySurface(surface);
  return ok;
}
