#include "UIManager.h"
#include "BluetoothManager.h"
#include "ConfigManager.h"
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"
#include <Arduino_GFX_Library.h>
#include <cstring>

// ===================== 外部变量声明 =====================
extern int panel1ContentIndex;
extern int panel8ContentIndex;
extern volatile ScreenId current_screen;

// 屏幕切换相关的全局变量
extern lv_group_t* group_screen2;
extern lv_group_t* group_screen3;
extern lv_obj_t* screen2_focus_objs[2];
extern int screen2_focus_idx;
extern lv_obj_t* screen3_btns[9];
extern lv_obj_t* screen3_name_labels[9];
extern lv_obj_t* screen3_battery_labels[9];
extern lv_obj_t* screen3_bluetooth_icons[9];
extern int screen3_device_index_map[9];
extern int screen3_button_count;
extern unsigned long screen3_enter_time;
extern bool screen3_scan_triggered;
extern bool screen3_buttons_created;

// 显示系统相关变量
extern Arduino_GFX *gfx;
extern TouchDrvFT6X36 touch;
extern TCA9554 TCA;
extern uint32_t screenWidth;
extern uint32_t screenHeight;
extern ConfigManager configManager;

// ===================== Label 安全更新函数 =====================
void safeLabelUpdate(lv_obj_t* label, const char* text, const char* labelName) {
    // UI初始化保护
    extern bool ui_initialized;
    if (!ui_initialized) {
        return;
    }
    
    if (!label) {
        return;
    }
    
    if (!text) {
        return;
    }
    
    // 检查文本长度
    size_t textLen = strlen(text);
    if (textLen > 100) {
        return;
    }
    
    try {
        lv_label_set_text(label, text);
    } catch (...) {
    }
}

// ===================== Panel1 更新函数 =====================
void updatePanel1() {
  switch (panel1ContentIndex) {
    case 0: // 显示计时
      lv_obj_clear_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 1: // 显示划距
      lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      break;
    case 2: // 显示距离
      lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 3: // 显示桨数
      lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 4: // 显示速度
      lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 5: // 显示功率
      lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 6: // 显示心率
      lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
      break;
    default:
      lv_obj_clear_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
      break;
  }
}

// ===================== Panel8 更新函数 =====================
void updatePanel8() {
  switch (panel8ContentIndex) {
    case 0: // 显示心率
      lv_obj_clear_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 1: // 显示计时
      lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 2: // 显示划距
      lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      break;
    case 3: // 显示距离
      lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 4: // 显示桨数
      lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 5: // 显示速度
      lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      break;
    case 6: // 显示功率
      lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
      break;
    default:
      lv_obj_clear_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
      break;
  }
}

// ===================== 数据绑定函数 =====================
void updateBoundData() {
  // 计时数据绑定 (P1Timer Label7 <-> P8Timer Label13)
  const char* timerText = lv_label_get_text(ui_Label7);
  lv_label_set_text(ui_Label13, timerText);

  // 划距数据绑定 (P1Split Label12 <-> P8Split Label56)
  const char* splitText = lv_label_get_text(ui_Label12);
  lv_label_set_text(ui_Label56, splitText);

  // 距离数据绑定 (P1Dist Label23 <-> P8Dist Label60)
  const char* distText = lv_label_get_text(ui_Label23);
  lv_label_set_text(ui_Label60, distText);

  // 桨数数据绑定 (P1Stroke Label44 <-> P8Stroke Label64)
  const char* strokeText = lv_label_get_text(ui_Label44);
  lv_label_set_text(ui_Label64, strokeText);

  // 速度数据绑定 (P1Speed Label46 <-> P8Speed Label68)
  const char* speedText = lv_label_get_text(ui_Label46);
  lv_label_set_text(ui_Label68, speedText);

  // 功率数据绑定 (P1Power Label48 <-> P8Power Label72)
  const char* powerText = lv_label_get_text(ui_Label48);
  lv_label_set_text(ui_Label72, powerText);

}


// ===================== 屏幕切换函数 =====================
void switch_to_screen(ScreenId screen)
{
  // Screen3退出时停止扫描并清理按钮，防止死锁
  if (safeGetCurrentScreen() == SCREEN3 && screen != SCREEN3) {
    BT::setContinuousScan(false); // 停止扫描

    // 清理Screen3按钮
    int cleanedCount = screen3_button_count;
    for (int i = 0; i < screen3_button_count; i++) {
      if (screen3_btns[i]) {
        if (group_screen3) {
          lv_group_remove_obj(screen3_btns[i]);
        }
        lv_obj_del(screen3_btns[i]); // Deletes the button and its children (labels)
        screen3_btns[i] = nullptr;
        screen3_name_labels[i] = nullptr;
        screen3_battery_labels[i] = nullptr;
        screen3_device_index_map[i] = -1;
      }
    }
    screen3_button_count = 0;
    screen3_buttons_created = false; // 重置标志
    Serial.printf("[SCREEN3] 退出时清理了 %d 个按钮\n", cleanedCount);
  }

  switch (screen) {
    case SCREEN1:
      lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
      lv_indev_set_group(lv_indev_get_next(NULL), nullptr);
      // 重新启用自动扫描，关闭持续扫描（离开Screen3时）
      BT::setAutoScanEnabled(true);
      BT::setContinuousScan(false);
      break;

    case SCREEN2:
      lv_scr_load_anim(ui_Screen2, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
      if (!group_screen2) {
        group_screen2 = lv_group_create();
        lv_group_add_obj(group_screen2, ui_Button3);
        lv_group_add_obj(group_screen2, ui_Button5);
      }
      lv_indev_set_group(lv_indev_get_next(NULL), group_screen2);
      screen2_focus_objs[0] = ui_Button3;
      screen2_focus_objs[1] = ui_Button5;
      
      // 使用记忆的聚焦状态，而不是每次重置为0
      screen2_focus_idx = screen2_remembered_focus_idx;
      
      // 不设置任何过渡动画，让颜色瞬间切换（避免白色闪烁）
      // SquareLine Studio中按钮没有边框，只有背景色变化（黑<->白）
      // 任何过渡动画都会导致中间状态的灰色/白色闪烁
      
      lv_group_focus_obj(screen2_focus_objs[screen2_focus_idx]);
      
      // 根据记忆的聚焦状态设置Label状态
      if (screen2_focus_idx == 0) {
        // 聚焦在Button3（蓝牙设备）
        lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
        lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
      } else {
        // 聚焦在Button5（设置）
        lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
        lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
      }
      
      // 重新启用自动扫描，关闭持续扫描（离开Screen3时）
      BT::setAutoScanEnabled(true);
      BT::setContinuousScan(false);
      break;

    case SCREEN3:
      lv_scr_load_anim(ui_Screen3, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
      if (!group_screen3) group_screen3 = lv_group_create();
      else                lv_group_remove_all_objs(group_screen3);

      // 记录进入时间并重置所有相关状态
      screen3_enter_time = millis();
      screen3_scan_triggered = false;
      screen3_buttons_created = false;  // 重置按钮创建标志，确保按键进入也能创建按钮
      
      // 启用Screen3持续扫描模式，禁用自动扫描
      BT::setAutoScanEnabled(false);
      BT::setContinuousScan(true);

      // 清空容器，布局由UI文件配置，由project.ino管理按钮
      lv_obj_clean(ui_uiBTListContainer);
      lv_obj_add_flag(ui_uiBTListContainer, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(ui_uiBTListContainer, LV_SCROLLBAR_MODE_AUTO);

      // 重置索引与映射
      safeSetScreen3SelectedIdx(0);
      screen3_button_count = 0;
      for (int i = 0; i < 9; ++i) screen3_device_index_map[i] = -1;

      // 设置输入组（暂时无对象，后续在project.ino动态加入）
      lv_indev_set_group(lv_indev_get_next(NULL), group_screen3);
      break;

    case SCREEN4:
      lv_scr_load_anim(ui_Screen4, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
      lv_indev_set_group(lv_indev_get_next(NULL), nullptr);
      // 重新启用自动扫描，关闭持续扫描（离开Screen3时）
      BT::setAutoScanEnabled(true);
      BT::setContinuousScan(false);
      break;
  }
  safeSetCurrentScreen(screen);
}

// ===================== Screen2标签状态同步函数 =====================
void syncScreen2LabelStates() {
  if (safeGetCurrentScreen() != SCREEN2) return;
  
  // 检查按钮的实际聚焦状态，同步标签状态
  bool button3_focused = lv_obj_has_state(ui_Button3, LV_STATE_FOCUSED);
  bool button5_focused = lv_obj_has_state(ui_Button5, LV_STATE_FOCUSED);
  
  if (button3_focused) {
    // Button3聚焦，对应screen2_focus_idx = 0
    if (screen2_focus_idx != 0) {
      screen2_focus_idx = 0;
      screen2_remembered_focus_idx = 0;  // 触控也要记住聚焦状态
    }
    lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
    lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
  } else if (button5_focused) {
    // Button5聚焦，对应screen2_focus_idx = 1
    if (screen2_focus_idx != 1) {
      screen2_focus_idx = 1;
      screen2_remembered_focus_idx = 1;  // 触控也要记住聚焦状态
    }
    lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
    lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
  } else {
    // 没有按钮聚焦，清除所有标签聚焦状态
    lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
    lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
  }
}

// ===================== Screen3聚焦状态同步函数 =====================
void syncScreen3FocusState() {
  if (safeGetCurrentScreen() != SCREEN3 || !screen3_buttons_created) return;
  
  // 遍历所有按钮，找出当前真正获得焦点的按钮
  for (int i = 0; i < screen3_button_count; i++) {
    if (screen3_btns[i] && lv_obj_has_state(screen3_btns[i], LV_STATE_FOCUSED)) {
      // 找到了当前聚焦的按钮，同步索引变量
      if (safeGetScreen3SelectedIdx() != i) {
        safeSetScreen3SelectedIdx(i);
        Serial.printf("[SCREEN3] 同步聚焦状态：触控聚焦到按钮 %d\n", i);
      }
      return;
    }
  }
  
  // 如果没有找到聚焦的按钮，确保索引有效
  if (safeGetScreen3SelectedIdx() >= screen3_button_count) {
    safeSetScreen3SelectedIdx(0);
  }
}

// ===================== Screen3按钮状态更新函数 =====================
void updateScreen3ButtonStates() {
  if (!screen3_buttons_created || screen3_button_count == 0) {
    return;
  }
  
  for (int i = 0; i < screen3_button_count; i++) {
    if (!screen3_btns[i] || !screen3_battery_labels[i]) {
      continue;
    }
    
    int mappedIdx = screen3_device_index_map[i];
    if (mappedIdx >= 1000) {
      // API设备
      int apiIdx = mappedIdx - 1000;
      const auto& rowerList = configManager.getRowerList();
      if (apiIdx < rowerList.size()) {
        const auto& rower = rowerList[apiIdx];
        
        // 查找对应的扫描设备
        int scannedDevIdx = -1;
        bool isConnected = false;
        for (int j = 0; j < BT::getFoundDeviceCount(); j++) {
            String scannedAddr = String(BT::devices()[j].address);
            if (scannedAddr.equalsIgnoreCase(rower.btAddr)) {
                scannedDevIdx = j;
                if (BT::devices()[j].connected) {
                  isConnected = true;
                }
                break;
            }
        }
        
        // 更新电量显示
        String batteryText = "";
        if (scannedDevIdx != -1 && isConnected && BT::devices()[scannedDevIdx].batteryLevel >= 0) {
          batteryText = String(BT::devices()[scannedDevIdx].batteryLevel) + "%";
        }
        
        // 安全更新标签
        // 使用静态缓冲区避免内存问题
        static char batteryBuffer[16];
        snprintf(batteryBuffer, sizeof(batteryBuffer), "%s", batteryText.c_str());
        safeLabelUpdate(screen3_battery_labels[i], batteryBuffer, "Screen3Battery");
        
        // 显示连接状态：已连接显示绿色圆点，未连接不显示任何标注
        if (screen3_bluetooth_icons[i] != nullptr) {
          if (isConnected) {
            lv_label_set_text(screen3_bluetooth_icons[i], "●"); // 实心圆点表示已连接
            lv_obj_set_style_text_color(screen3_bluetooth_icons[i], lv_color_make(0, 255, 0), 0); // 亮绿色
            lv_obj_clear_flag(screen3_bluetooth_icons[i], LV_OBJ_FLAG_HIDDEN); // 确保可见
          } else {
            lv_label_set_text(screen3_bluetooth_icons[i], ""); // 未连接不显示任何标注
            lv_obj_add_flag(screen3_bluetooth_icons[i], LV_OBJ_FLAG_HIDDEN); // 隐藏未连接的指示
          }
        }
      }
    }
  }
}

// ===================== 安全的Screen3按钮状态更新函数 =====================
void updateScreen3ButtonStatesSafe() {
  if (!screen3_buttons_created || screen3_button_count == 0) {
    return;
  }
  
  // 快速非阻塞检查，避免长时间持有锁
  for (int i = 0; i < screen3_button_count; i++) {
    if (!screen3_btns[i] || !screen3_battery_labels[i]) {
      continue;
    }
    
    int mappedIdx = screen3_device_index_map[i];
    if (mappedIdx >= 1000) {
      // API设备 - 检查连接状态和电量
      int apiIdx = mappedIdx - 1000;
      const auto& rowerList = configManager.getRowerList();
      if (apiIdx < rowerList.size()) {
        const auto& rower = rowerList[apiIdx];
        
        // 检查该设备是否已连接
        bool isConnected = false;
        int batteryLevel = 0;
        
        // 通过蓝牙地址查找连接状态
        for (int j = 0; j < BT::NUM_PRESETS; j++) {
          const auto& device = BT::devices()[j];
          if (device.address[0] != '\0' && String(device.address).equalsIgnoreCase(rower.btAddr)) {
            if (device.connected) {
              isConnected = true;
              batteryLevel = device.batteryLevel;
            }
            break;
          }
        }
        
        // 更新连接状态图标 - 修复版：已连接显示绿色圆点，未连接不显示任何标注
        if (screen3_bluetooth_icons[i]) {
          if (isConnected) {
            lv_label_set_text(screen3_bluetooth_icons[i], "●");  // 实心圆点表示已连接
            lv_obj_set_style_text_color(screen3_bluetooth_icons[i], lv_color_make(0, 255, 0), 0);  // 亮绿色
            lv_obj_clear_flag(screen3_bluetooth_icons[i], LV_OBJ_FLAG_HIDDEN);  // 确保可见
          } else {
            lv_label_set_text(screen3_bluetooth_icons[i], "");  // 未连接不显示任何标注
            lv_obj_add_flag(screen3_bluetooth_icons[i], LV_OBJ_FLAG_HIDDEN);  // 隐藏未连接的指示
          }
        }
        
        // 更新电量显示 - 修复版：也显示未连接设备的状态
        if (screen3_battery_labels[i]) {
          if (isConnected && batteryLevel > 0) {
            char batteryText[8];
            snprintf(batteryText, sizeof(batteryText), "%d%%", batteryLevel);
            lv_label_set_text(screen3_battery_labels[i], batteryText);
            lv_obj_set_style_text_color(screen3_battery_labels[i], lv_color_white(), 0);
            lv_obj_clear_flag(screen3_battery_labels[i], LV_OBJ_FLAG_HIDDEN);
          } else if (isConnected && batteryLevel <= 0) {
            // 已连接但电量未知
            lv_label_set_text(screen3_battery_labels[i], "--");
            lv_obj_set_style_text_color(screen3_battery_labels[i], lv_color_make(180, 180, 180), 0);
            lv_obj_clear_flag(screen3_battery_labels[i], LV_OBJ_FLAG_HIDDEN);
          } else {
            // 未连接
            lv_label_set_text(screen3_battery_labels[i], "");
            lv_obj_add_flag(screen3_battery_labels[i], LV_OBJ_FLAG_HIDDEN);
          }
        }
      }
    }
  }
}

// ===================== LVGL 刷新回调 =====================
void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif

  lv_disp_flush_ready(disp);
}

// ===================== LVGL 触摸读取回调 =====================
void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  int16_t x[1], y[1];
  uint8_t touched = touch.getPoint(x, y, 1);

  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    
    // 坐标变换以匹配显示旋转
    int16_t mapped_x = screenWidth - y[0];
    int16_t mapped_y = x[0];
    
    data->point.x = mapped_x;
    data->point.y = mapped_y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ===================== LCD 复位函数 =====================
void lcd_reset(void) {
  TCA.write1(1, 1);
  delay(10);
  TCA.write1(1, 0);
  delay(10);
  TCA.write1(1, 1);
  delay(200);
}

// ===================== 启动动画函数 =====================
void show_boot_animation(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t * logo_container = lv_obj_create(scr);
    lv_obj_set_size(logo_container, 230, 40);
    lv_obj_align(logo_container, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(logo_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_container, 0, 0);
    lv_obj_set_scrollbar_mode(logo_container, LV_SCROLLBAR_MODE_OFF);

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 3);
    lv_style_set_line_color(&style_line, lv_color_white());
    lv_style_set_line_rounded(&style_line, true);

    static const lv_point_t r_points[] = { {0,0}, {0,40}, {0,0}, {20,0}, {20,20}, {0,20}, {10,20}, {20,40} };
    lv_obj_t * line_r = lv_line_create(logo_container);
    lv_line_set_points(line_r, r_points, 8);
    lv_obj_add_style(line_r, &style_line, 0);
    lv_obj_align(line_r, LV_ALIGN_LEFT_MID, 0, 0);

    static const lv_point_t o_points[] = { {0,0}, {25,0}, {25,40}, {0,40}, {0,0} };
    lv_obj_t * line_o = lv_line_create(logo_container);
    lv_line_set_points(line_o, o_points, 5);
    lv_obj_add_style(line_o, &style_line, 0);
    lv_obj_align(line_o, LV_ALIGN_LEFT_MID, 30, 0);

    static const lv_point_t w_points[] = { {0,0}, {10,40}, {20,10}, {30,40}, {40,0} };
    lv_obj_t * line_w = lv_line_create(logo_container);
    lv_line_set_points(line_w, w_points, 5);
    lv_obj_add_style(line_w, &style_line, 0);
    lv_obj_align(line_w, LV_ALIGN_LEFT_MID, 65, 0);

    static const lv_point_t i_points[] = { {0,0}, {0,40} };
    lv_obj_t * line_i = lv_line_create(logo_container);
    lv_line_set_points(line_i, i_points, 2);
    lv_obj_add_style(line_i, &style_line, 0);
    lv_obj_align(line_i, LV_ALIGN_LEFT_MID, 115, 0);

    static const lv_point_t n_points[] = { {0,40}, {0,0}, {25,40}, {25,0} };
    lv_obj_t * line_n = lv_line_create(logo_container);
    lv_line_set_points(line_n, n_points, 4);
    lv_obj_add_style(line_n, &style_line, 0);
    lv_obj_align(line_n, LV_ALIGN_LEFT_MID, 125, 0);

    static const lv_point_t g_points[] = { {30,0}, {0,0}, {0,40}, {30,40}, {30,20}, {15,20} };
    lv_obj_t * line_g = lv_line_create(logo_container);
    lv_line_set_points(line_g, g_points, 6);
    lv_obj_add_style(line_g, &style_line, 0);
    lv_obj_align(line_g, LV_ALIGN_LEFT_MID, 160, 0);

    // 初始刷新
    lv_timer_handler();
}

// ===================== Screen3触控事件回调函数 =====================
void screen3_button_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* btn = lv_event_get_target(e);
  
  if (code == LV_EVENT_CLICKED) {
    // 找到被点击的按钮对应的索引
    int clicked_idx = -1;
    for (int i = 0; i < screen3_button_count; i++) {
      if (screen3_btns[i] == btn) {
        clicked_idx = i;
        break;
      }
    }
    
    if (clicked_idx >= 0 && clicked_idx < screen3_button_count) {
      // 更新选中索引
      safeSetScreen3SelectedIdx(clicked_idx);
      
      // 同步焦点到点击的按钮
      if (group_screen3) {
        lv_group_focus_obj(btn);
      }
      
      // 执行与K4按键相同的逻辑：切换上传源或连接设备
      int devIdx = screen3_device_index_map[clicked_idx];
      
      if (devIdx >= 1000) {
        // API设备 - 需要找到对应的扫描设备
        int apiIdx = devIdx - 1000;
        const auto& rowerList = configManager.getRowerList();
        if (apiIdx < rowerList.size()) {
          const auto& rower = rowerList[apiIdx];
          
          // 查找对应的扫描设备
          int scannedDevIdx = -1;
          for (int j = 0; j < BT::getFoundDeviceCount(); j++) {
            String scannedAddr = String(BT::devices()[j].address);
            if (scannedAddr.equalsIgnoreCase(rower.btAddr)) {
              scannedDevIdx = j;
              break;
            }
          }
          
          if (scannedDevIdx != -1) {
            auto &dev = BT::devices()[scannedDevIdx];
            
            if (dev.connected) {
              // 切换上传源
              BT::setUploadSource(&dev);
            } else if (dev.found) {
              BT::requestConnect(scannedDevIdx);
            }
          }
        }
      } else if (devIdx >= 0) {
        // 直接扫描设备
        auto &dev = BT::devices()[devIdx];
        
        if (dev.connected) {
          // 切换上传源
          BT::setUploadSource(&dev);
        } else if (dev.found) {
          BT::requestConnect(devIdx);
        }
      }
      
      // 刷新Screen3 UI
      BT::refreshScreen3UI(screen3_btns, screen3_battery_labels, safeGetScreen3SelectedIdx(), screen3_button_count, screen3_device_index_map);
    }
  }
}
