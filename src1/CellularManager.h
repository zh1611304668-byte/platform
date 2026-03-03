/*
 * File: CellularManager.h
 * Purpose: Declares interfaces, types, and constants for the Cellular Manager module.
 */
#ifndef CELLULAR_MANAGER_H
#define CELLULAR_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <lvgl.h>

// 外部4G连接状态
extern bool is4GConnected;

// 外部UI对象声明（保持与SquareLine自动生成的C链接一致）
#ifdef __cplusplus
extern "C" {
#endif
extern lv_obj_t * ui_sig1bar1on2;
extern lv_obj_t * ui_sig1bar2on2;
extern lv_obj_t * ui_sig1bar3on2;
extern lv_obj_t * ui_sig1bar4on2;
#ifdef __cplusplus
}
#endif

class CellularManager {
public:
    CellularManager(HardwareSerial& serial, uint8_t txPin, uint8_t rxPin, uint32_t baud = 115200);
    void begin();
    void process();
    
    // 信号强度相关
    int getRSSI() const { return rssi; }
    int getSignaldBm() const { return signaldBm; }
    bool isConnected() const { return connected; }
    String getSignalQuality() const;
    
    // 手动设置信号强度（从MQTTManager获取）
    void setSignalStrength(int rssi, int ber);
    
    // UI更新
    void updateSignalUI();

private:
    void updateSignalFromMQTT();
    int rssiTodBm(int rssi);
    
    // 信号状态
    int rssi = 99;           // 信号强度指示 (0-31, 99=未知)
    int signaldBm = -120;    // 转换后的dBm值
    int ber = 99;            // 信道误码率
    bool connected = false;  // 网络连接状态
    
    void markSignalLost();
    
    uint32_t lastSignalCheck = 0;
    uint32_t lastSignalUpdate = 0;
    static const uint32_t SIGNAL_CHECK_INTERVAL = 1000; // 改为2秒检查一次，优先显示信号
    static const uint32_t SIGNAL_STALE_TIMEOUT = 70000; // 超过70秒无更新才认为无信号（大于CSQ轮询间隔）
};

#endif
