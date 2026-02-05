/**
 * @file lv_port_disp.h
 * LVGL Display Driver for SDL2
 */

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"
#include <stdbool.h>

/**
 * Initialize SDL display driver for LVGL
 */
void lv_port_disp_init(void);

/**
 * Cleanup SDL resources
 */
void lv_port_disp_deinit(void);

/**
 * Handle SDL events (window close, etc)
 * @return false if quit event received
 */
bool lv_port_disp_handle_events(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
