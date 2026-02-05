/**
 * @file lv_port_disp.c
 * LVGL Display Driver for SDL2
 */

#include "lv_port_disp.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

/* Display dimensions */
#define DISP_HOR_RES 480
#define DISP_VER_RES 320

/* SDL objects */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;

/* LVGL buffer */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[DISP_HOR_RES * 40];
static lv_color_t buf2[DISP_HOR_RES * 40];

/* Display driver */
static lv_disp_drv_t disp_drv;

/**
 * Flush callback - sends rendered pixels to SDL
 */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p) {
  int32_t x, y;

  /* Update texture with the rendered content */
  SDL_Rect rect;
  rect.x = area->x1;
  rect.y = area->y1;
  rect.w = lv_area_get_width(area);
  rect.h = lv_area_get_height(area);

  /* Lock texture and copy pixels */
  void *pixels;
  int pitch;

  if (SDL_LockTexture(texture, &rect, &pixels, &pitch) == 0) {
    for (y = 0; y < rect.h; y++) {
      memcpy((uint8_t *)pixels + y * pitch, color_p + y * rect.w,
             rect.w * sizeof(lv_color_t));
    }
    SDL_UnlockTexture(texture);
  }

  /* Render to screen */
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);

  /* Inform LVGL that flushing is done */
  lv_disp_flush_ready(drv);
}

/**
 * Initialize SDL display
 */
void lv_port_disp_init(void) {
  /* Initialize SDL */
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
    return;
  }

  /* Create window */
  window = SDL_CreateWindow("LVGL Rowing Simulator", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, DISP_HOR_RES, DISP_VER_RES,
                            SDL_WINDOW_SHOWN);

  if (!window) {
    fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
    SDL_Quit();
    return;
  }

  /* Create renderer */
  renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  /* Create texture for LVGL (RGB565 16-bit) */
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, DISP_HOR_RES,
                              DISP_VER_RES);

  if (!texture) {
    fprintf(stderr, "SDL_CreateTexture Error: %s\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  /* Clear to black */
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);

  /* Initialize LVGL draw buffer */
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISP_HOR_RES * 40);

  /* Initialize and register display driver */
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = DISP_HOR_RES;
  disp_drv.ver_res = DISP_VER_RES;
  disp_drv.flush_cb = disp_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  printf("LVGL display initialized: %dx%d\n", DISP_HOR_RES, DISP_VER_RES);
}

/**
 * Cleanup SDL resources
 */
void lv_port_disp_deinit(void) {
  if (texture)
    SDL_DestroyTexture(texture);
  if (renderer)
    SDL_DestroyRenderer(renderer);
  if (window)
    SDL_DestroyWindow(window);
  SDL_Quit();
}

/**
 * Handle SDL events (needed for window management)
 * Returns false if quit event received
 */
bool lv_port_disp_handle_events(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      return false;
    }
  }
  return true;
}
