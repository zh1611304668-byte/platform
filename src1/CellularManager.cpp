/*
 * File: CellularManager.cpp
 * Purpose: Implements runtime logic for the Cellular Manager module.
 */
#include "CellularManager.h"

CellularManager::CellularManager(HardwareSerial& serial, uint8_t txPin, uint8_t rxPin, uint32_t baud) {
    // 构造函数现在为空，因为不再需要存储串口相关参数
}

void CellularManager::begin() {
    // 立即开始信号检查，优先显示信号强度
    
    // 立即开始检查信号
    lastSignalCheck = 0; // 设为0，立即触发第一次检查
    markSignalLost(); // 开机默认显示无信号，等待真实数据更新
}

void CellularManager::process() {
    extern bool hybridPppConnected;
    if (!hybridPppConnected) {
        markSignalLost();
        return; 
    }
    
    if (lastSignalUpdate != 0 && millis() - lastSignalUpdate > SIGNAL_STALE_TIMEOUT) {
        markSignalLost();
    }

    if (millis() - lastSignalCheck > SIGNAL_CHECK_INTERVAL) {
        lastSignalCheck = millis();
        updateSignalFromMQTT();
    }
}

void CellularManager::updateSignalFromMQTT() {
    // 从MQTTManager获取信号数据
    // ...
    // 更新信号强度UI
    updateSignalUI();
}

void CellularManager::setSignalStrength(int rssi, int ber) {
    this->rssi = rssi;
    this->ber = ber;
    lastSignalUpdate = millis();
    
    // 转换RSSI为dBm
    signaldBm = rssiTodBm(rssi);
    
    // 判断连接状态
    connected = (rssi != 99 && rssi > 0);
    
    // 更新UI显示
    updateSignalUI();
}

int CellularManager::rssiTodBm(int rssi) {
    if (rssi == 0) return -113;
    if (rssi == 1) return -111;
    if (rssi >= 2 && rssi <= 30) {
        return -109 + (rssi - 2) * 2;  // 每级2dBm
    }
    if (rssi == 31) return -51;
    return -120; // 99或其他无效值
}

String CellularManager::getSignalQuality() const {
    if (signaldBm > -85) return "极强";
    if (signaldBm > -95) return "强";
    if (signaldBm > -105) return "中等";
    if (signaldBm > -120) return "弱";
    return "无信号";
}

void CellularManager::updateSignalUI() {
    extern bool ui_initialized;
    if (!ui_initialized) {
        return;
    }
    // 根据信号强度控制UI显示
    if (signaldBm <= -120) {
        // 无信号 - 隐藏所有信号条
        lv_obj_add_flag(ui_sig1bar1on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar2on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar3on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar4on2, LV_OBJ_FLAG_HIDDEN);
    } else if (signaldBm <= -105) {
        // 弱信号 - 显示1格
        lv_obj_clear_flag(ui_sig1bar1on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar2on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar3on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar4on2, LV_OBJ_FLAG_HIDDEN);
    } else if (signaldBm <= -95) {
        // 中等信号 - 显示2格
        lv_obj_clear_flag(ui_sig1bar1on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_sig1bar2on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar3on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar4on2, LV_OBJ_FLAG_HIDDEN);
    } else if (signaldBm <= -85) {
        // 强信号 - 显示3格
        lv_obj_clear_flag(ui_sig1bar1on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_sig1bar2on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_sig1bar3on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_sig1bar4on2, LV_OBJ_FLAG_HIDDEN);
    } else {
        // 极强信号 - 显示4格
        lv_obj_clear_flag(ui_sig1bar1on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_sig1bar2on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_sig1bar3on2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_sig1bar4on2, LV_OBJ_FLAG_HIDDEN);
    }
}

void CellularManager::markSignalLost() {
    bool needUpdate = connected || rssi != 99 || signaldBm != -120;
    connected = false;
    rssi = 99;
    signaldBm = -120;
    ber = 99;
    lastSignalUpdate = 0;
    if (needUpdate) {
        updateSignalUI();
    }
}
