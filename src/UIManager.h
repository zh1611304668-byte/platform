#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <ui.h>

// ===================== 屏幕枚举 =====================
enum ScreenId { SCREEN1, SCREEN2, SCREEN3, SCREEN4 };

// ===================== 外部变量声明 =====================
extern int panel1ContentIndex;
extern int panel8ContentIndex;
extern volatile ScreenId current_screen;
extern int screen2_focus_idx;
extern int screen2_remembered_focus_idx;
extern int screen3_selected_idx;

// ===================== 函数声明 =====================

// UI状态安全访问函数
ScreenId safeGetCurrentScreen();
bool safeSetCurrentScreen(ScreenId newScreen);
int safeGetScreen3SelectedIdx();
bool safeSetScreen3SelectedIdx(int newIdx);

// 数据绑定函数
void updateBoundData();

// Panel 更新函数
void updatePanel1();
void updatePanel8();

// 屏幕切换函数
void switch_to_screen(ScreenId screen);

// Label 安全更新函数
void safeLabelUpdate(lv_obj_t* label, const char* text, const char* labelName);

// Screen2和Screen3聚焦状态同步函数
void syncScreen2LabelStates();
void syncScreen3FocusState();

// Screen3按钮状态更新函数
void updateScreen3ButtonStates();
void updateScreen3ButtonStatesSafe();

// Screen3触控事件回调函数
void screen3_button_event_cb(lv_event_t* e);

// 显示相关函数
void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p);
void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
void show_boot_animation(void);
void lcd_reset(void);

#endif // UI_MANAGER_H
