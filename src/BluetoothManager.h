#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <lvgl.h>

#include "esp_gap_ble_api.h"

typedef enum {
    DEV_IDLE       = 0, // 未发现 / 初始状态
    DEV_SCANNING   = 1, // 扫描中（未使用，供将来扩展）
    DEV_CONNECTING = 2, // 正在连接
    DEV_CONNECTED  = 3, // 已连接
    DEV_BACKOFF    = 4  // 连接失败，等待退避重连
} dev_state_t;

// 设备结构体（扩展版）
typedef struct {
    const char *name;
    char address[18]; // MAC string like "AA:BB:CC:DD:EE:FF"
    uint8_t addrType;
    volatile bool found;
    volatile bool connected;   // 兼容外部读取，始终与 state 同步
    volatile bool uploaded;
    NimBLEClient* client;
    volatile int lastHeartRate;

    volatile bool connecting;  // 兼容外部读取，始终与 state 同步
    volatile dev_state_t state; // 主状态机（写入只允许 BLE 任务线程）
    unsigned long nextRetryAtMs; // 下次退避重连的绝对时间戳
    unsigned long lastDataTime;
    unsigned long lastPrintTime;
    int connectionAttempts;
    int dataCount;
    int batteryLevel;  // 电量百分比 (0-100)
} hr_device_t;

#define DEV_CONNECTED_FLAG(d) ((d).state == DEV_CONNECTED)

namespace BT {
constexpr int NUM_PRESETS = 10;

void begin();
void startTask();
void loop();
bool connectToPreset(int idx);
void requestConnect(int idx);
void setUploadSource(hr_device_t *dev);
hr_device_t *devices();
bool isScanning();
int activeIndex();
hr_device_t *activeDevice();
int getConnectedCount();
int getFoundDeviceCount();
bool hasConnectionCountChanged();
void clearConnectionCountChange();
void refreshScreen3UI(lv_obj_t *btns[], lv_obj_t *batteryObjs[],
                      int selectedIdx, int maxButtons, const int devIdxMap[]);
void triggerScan();
void setAutoScanEnabled(bool enabled);
void setContinuousScan(bool enabled);
void pushHREvent(int idx, int hr);
void pollUIEvents();
int getBatteryLevel(int idx);
void cleanupDisconnectedDevices();
void markConnectionCountDirty(); // 强制触发连接计数变化通知

bool isAddressInWhitelist(const String &address);
bool getDeviceInfoByAddress(const String &address, String &deviceCode,
                            String &rowerName);

void triggerAutoConnect();

String shortName(const char *raw);
} // namespace BT
