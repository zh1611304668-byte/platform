/*
 * File: BluetoothManager.h
 * Purpose: Declares interfaces, types, and constants for the Bluetooth Manager module.
 */
#pragma once
#include <Arduino.h>   
#include <lvgl.h>     
#include <BLEDevice.h>
#include "esp_gap_ble_api.h"

// 设备结构体（扩展版）
typedef struct {
    const char *name;
    char address[18]; // MAC string like "AA:BB:CC:DD:EE:FF"
    esp_ble_addr_type_t addrType;
    volatile bool found;
    volatile bool connected;
    volatile bool uploaded;
    BLEClient* client;
    volatile int lastHeartRate;
    
    // 新增字段
    volatile bool connecting;
    unsigned long lastDataTime;
    unsigned long lastPrintTime;
    int connectionAttempts;
    int dataCount;
    int batteryLevel;  // 电量百分比 (0-100)
} hr_device_t;

// 对外 API
namespace BT {
    constexpr int NUM_PRESETS = 7;  // 扩展至7个设备

    void begin();
    void startTask();
    void loop();
    bool connectToPreset(int idx);
    void requestConnect(int idx);
    void setUploadSource(hr_device_t* dev);
    hr_device_t* devices();
    bool isScanning();
    int activeIndex();
    hr_device_t* activeDevice();
    int getConnectedCount();
    int getFoundDeviceCount();
    bool hasConnectionCountChanged();  // 检查连接数量是否变化（事件驱动）
    void clearConnectionCountChange(); // 清除变化标志
    void refreshScreen3UI(lv_obj_t* btns[], lv_obj_t* batteryObjs[], int selectedIdx, int maxButtons, const int devIdxMap[]);
    void triggerScan();
    void setAutoScanEnabled(bool enabled);
    void setContinuousScan(bool enabled);
    void pushHREvent(int idx, int hr);
    void pollUIEvents();
    int getBatteryLevel(int idx);
    void cleanupDisconnectedDevices();
    
    // API白名单相关函数
    bool isAddressInWhitelist(const String& address);
    bool getDeviceInfoByAddress(const String& address, String& deviceCode, String& rowerName);
    
    // 自动连接功能
    void triggerAutoConnect();  // 触发自动连接API设备
    
    // 辅助函数
    String shortName(const char* raw);  // 生成设备短名称
}  // namespace BT  
