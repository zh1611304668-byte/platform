/**
 * @file lv_port_indev.h
 * LVGL Input Device Driver for SDL2 (Mouse)
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * Initialize input device (mouse) for LVGL
 */
void lv_port_indev_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
