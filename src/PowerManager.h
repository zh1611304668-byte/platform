#ifndef POWERMANAGER_H
#define POWERMANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <ui.h> 

// 定义AXP2101芯片类型
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

class PowerManager {
private:
    XPowersPMU power;
    bool initialized;
    bool shutdownRequested;             // 关机请求标志
    
    // 更新间隔常量
    static const unsigned long UPDATE_INTERVAL = 1000; // 1秒更新一次
    
    // 缓存的电池状态数据
    float batteryVoltage;
    float systemVoltage;
    float vbusVoltage;
    int batteryPercent;
    bool chargingStatus;        // 重命名避免与isCharging()函数冲突
    bool vbusConnectedStatus;   // 重命名避免与isVbusConnected()函数冲突
    unsigned long lastUpdate;
    
    // I2C引脚配置
    static const uint8_t PMU_SDA = 8;
    static const uint8_t PMU_SCL = 7;
    static const uint8_t PMU_IRQ = -1;
    
    // 配置电源轨道
    void configurePowerRails();
    // 配置充电参数
    void configureCharging();
    // 配置中断
    void configureInterrupts();
    // 配置电池校准
    void configureBatteryCalibration();
    
public:
    PowerManager();
    ~PowerManager();
    
    // 初始化电源管理系统
    bool begin();
    
    // 电池状态查询
    bool isBatteryConnected();
    float getBatteryVoltage();      // 返回电池电压(V)
    int getBatteryPercent();        // 返回电池电量百分比
    bool isCharging();
    bool isVbusConnected();
    
    // 充电状态
    String getChargeStatus();
    
    // 电源管理功能
    void update();                      // 更新电池状态
    void enablePowerRail(const char* rail, bool enable);  // 控制电源轨道
    void setPowerRailVoltage(const char* rail, uint16_t voltage_mv);  // 设置电源轨道电压
    void setChargingCurrent(uint16_t current_ma);  // 设置充电电流
    void setChargeVoltage(uint16_t voltage_mv);    // 设置充电电压
    void enableCharging(bool enable);   // 启用/禁用充电
    void wakeUp();                      // 从睡眠唤醒
    void setPowerKeyPressTime(uint8_t seconds);  // 设置电源键按压时间
    void printStatus();                 // 打印状态信息
    String getStatusString();           // 获取状态字符串
    void handleInterrupt();             // 处理中断
    bool checkPowerKeyPress();          // 检查电源键按压
    
    // 电源管理
    void wakeup();
    void shutdown();                    // 关机功能
    bool isShutdownRequested();         // 新增：检查是否请求关机
    void setShutdownFlag(bool flag);    // 新增：设置关机标志
    
    // 信息输出
    void printBatteryInfo();
    void printAllStatus();
    void printDetailedBatteryStatus();  // 新增：详细电池状态
    
    // 电池校准和学习
    void resetBatteryLearning();        // 重置电池学习
    void manualCalibrateBattery();      // 手动校准电池
    bool isBatteryLearning();           // 检查是否在学习状态
    
    // UI更新函数
    void updateBatteryUI();  // 更新电池图标显示
    void initBatteryUI();    // 初始化电池图标显示状态
    
    // 中断处理
    void handleInterrupts();
    
    // 看门狗管理
    void feedWatchdog();                    // 喂狗
    
    // 系统内存监控
    void printMemoryStatus();
};

#endif // POWERMANAGER_H
