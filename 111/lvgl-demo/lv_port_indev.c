/**
 * @file lv_port_indev.c
 * LVGL Input Device Driver for SDL2 (Mouse)
 */

#include "lv_port_indev.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

static lv_indev_drv_t indev_drv;
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static bool mouse_pressed = false;

/**
 * Mouse read callback
 */
static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  /* Get current mouse state from SDL */
  int x, y;
  Uint32 buttons = SDL_GetMouseState(&x, &y);

  data->point.x = x;
  data->point.y = y;
  data->state = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT))
                    ? LV_INDEV_STATE_PRESSED
                    : LV_INDEV_STATE_RELEASED;
}

/**
 * Initialize mouse input device
 */
void lv_port_indev_init(void) {
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = mouse_read_cb;
  lv_indev_drv_register(&indev_drv);

  printf("LVGL input device initialized (mouse)\n");
}
