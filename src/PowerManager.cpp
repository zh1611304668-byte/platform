#include "PowerManager.h"

// 定义AXP2101芯片
#define XPOWERS_CHIP_AXP2101

PowerManager::PowerManager()
    : initialized(false), shutdownRequested(false), batteryVoltage(0),
      systemVoltage(0), vbusVoltage(0), batteryPercent(0),
      chargingStatus(false), vbusConnectedStatus(false), lastUpdate(0) {}

PowerManager::~PowerManager() {
  // 析构函数中可以添加清理代码
}

bool PowerManager::begin() {
  // 初始化I2C通信
  bool result = power.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL);

  if (!result) {
    Serial.println("[PowerManager] Failed to initialize AXP2101");
    return false;
  }

  // 只配置电源轨道，不配置充电
  configurePowerRails();

  // 恢复充电配置，不要移除configureCharging()调用
  configureCharging();

  // 简化中断配置，只保留电池检测
  configureInterrupts();

  power.disableTSPinMeasure();

  // 启用电池和VBUS测量功能（3.7V锂电池需要完整充放电检测）
  power.enableBattDetection();
  power.enableBattVoltageMeasure();
  power.enableSystemVoltageMeasure();
  power.enableVbusVoltageMeasure();

  // 设置充电LED模式（自动控制）
  power.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  initialized = true;

  return true;
}

void PowerManager::configurePowerRails() {
  // ===== 关键修复1：提高VBUS输入电流限制，避免瞬间大电流触发保护 =====
  power.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
  power.setVbusCurrentLimit(
      XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA); // 提高到2A，避免过流保护

  // ===== 关键修复2：进一步降低系统关机电压，适应电池供电 =====
  // 从2.8V降低到2.6V，针对电池供电时的电压跌落问题
  // 3.7V锂电池安全放电电压范围：3.0V-4.2V，2.6V有足够安全裕度
  power.setSysPowerDownVoltage(2600);

  // 验证设置是否生效
  uint16_t actualVoltage = power.getSysPowerDownVoltage();

  // 配置DC电源轨道（根据你的硬件需求调整）
  power.setDC1Voltage(3300); // 主电源 3.3V
  power.setDC2Voltage(1000); // 1.0V
  power.setDC3Voltage(3300); // 3.3V
  power.setDC4Voltage(1000); // 1.0V
  power.setDC5Voltage(3300); // 3.3V

  // 配置ALDO电源轨道
  power.setALDO1Voltage(3300);
  power.setALDO2Voltage(3300);
  power.setALDO3Voltage(3300);
  power.setALDO4Voltage(3300);

  // 配置BLDO电源轨道
  power.setBLDO1Voltage(1500);
  power.setBLDO2Voltage(2800);

  // 配置DLDO电源轨道
  power.setDLDO1Voltage(3300);
  power.setDLDO2Voltage(3300);

  // 配置CPUSLDO
  power.setCPUSLDOVoltage(1000);

  // ===== 关键安全功能：启用DCDC低压/高压自动关机保护 =====
  // 这些保护功能对硬件安全至关重要，防止电压异常损坏设备
  power.setDCHighVoltagePowerDown(
      true); // 启用高压保护（DCDC 120%/130% 高压自动关机）
  power.setDC1LowVoltagePowerDown(true); // 启用DC1低压保护（85%低压自动关机）
  power.setDC2LowVoltagePowerDown(true); // 启用DC2低压保护（85%低压自动关机）
  power.setDC3LowVoltagePowerDown(true); // 启用DC3低压保护（85%低压自动关机）
  power.setDC4LowVoltagePowerDown(true); // 启用DC4低压保护（85%低压自动关机）
  power.setDC5LowVoltagePowerDown(true); // 启用DC5低压保护（85%低压自动关机）

  // 启用必要的电源轨道（根据你的硬件需求调整）
  // power.enableDC1();  // 如果DC1连接到关键组件，取消注释
  power.enableDC2();
  power.enableDC3();
  power.enableDC4();
  power.enableDC5();
  power.enableALDO1();
  power.enableALDO2();
  power.enableALDO3();
  power.enableALDO4();
  power.enableBLDO1();
  power.enableBLDO2();
  power.enableCPUSLDO();
  power.enableDLDO1();
  power.enableDLDO2();
}

void PowerManager::configureCharging() {
  // 设置预充电电流（深度放电时的保护性充电）
  power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_100MA);

  // 设置恒流充电电流 - 针对3000mAh电池优化
  // 1000mA = 0.33C充电率（标准快充，约3-4小时充满）
  // 对于3000mAh电池，1000mA是最佳平衡点
  power.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_1000MA);

  // 设置充电终止电流（充电完成判断阈值）
  // 对于3000mAh电池，建议使用更高的终止电流
  power.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_50MA);

  // 设置充电截止电压（4.2V - 标准3.7V锂电池满电电压）
  power.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

  // 启用纽扣电池充电
  power.enableButtonBatteryCharge();
  power.setButtonBatteryChargeVoltage(3300);
}

void PowerManager::configureInterrupts() {
  // 完全禁用中断功能（改用轮询方式，更稳定）
  power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

  // 清除所有中断标志
  power.clearIrqStatus();

  // 不启用任何中断 - 使用轮询方式检测充电状态
  // 注释：原本启用了电池、VBUS、充电、按键中断，但会导致中断风暴
  // 现在完全禁用中断，通过update()每1秒轮询充电状态，满足2秒内显示需求

  // 设置电源按键时间（保留按键功能）
  power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  power.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);

  // 关闭AXP2101硬件看门狗，避免误触发导致整机断电
  power.disableWatchdog();
}

void PowerManager::update() {
  if (!initialized)
    return;

  unsigned long now = millis();
  if (now - lastUpdate < UPDATE_INTERVAL)
    return;

  // 更新电源状态
  batteryVoltage = power.getBattVoltage();
  systemVoltage = power.getSystemVoltage();
  vbusVoltage = power.getVbusVoltage();
  vbusConnectedStatus = power.isVbusIn();

  // ===== 与官方示例02_axp2101_example.ino完全一致的充电状态检测 =====

  // 方法1：使用isCharging()直接检测（官方示例主要方法）
  bool isChargingNow = power.isCharging();

  // 方法2：使用getChargerStatus()获取详细状态（官方示例辅助方法）
  uint8_t chargeStatus = power.getChargerStatus();

  // 方法3：检测其他状态（官方示例中的完整检测）
  bool isDischarging = power.isDischarge();
  bool isStandby = power.isStandby();
  bool isVbusGood = power.isVbusGood();

  // 根据官方示例逻辑：优先使用isCharging()判断
  // 辅以getChargerStatus()进行详细状态确认
  if (isChargingNow) {
    chargingStatus = true; // 正在充电
  } else if (chargeStatus == XPOWERS_AXP2101_CHG_DONE_STATE) {
    chargingStatus = true; // 充电完成（保持显示充电图标）
  } else {
    chargingStatus = false; // 不在充电
  }

  // ===== 关键诊断：电压跌落监控（检测重启原因）=====
  // 监控系统电压，如果接近关机电压2.6V，说明电池电流不足
  static float minSystemVoltage = 5000.0f; // 记录最低系统电压
  if (systemVoltage < minSystemVoltage && systemVoltage > 0) {
    minSystemVoltage = systemVoltage;
  }

  // 如果系统电压低于3.0V（危险区域），立即报警
  if (systemVoltage < 3000 && systemVoltage > 0) {
    Serial.printf(
        "[PowerManager] ⚠️ 警告：系统电压过低 %.2fV，接近关机阈值2.6V！\n",
        systemVoltage / 1000.0f);
    Serial.printf("[PowerManager] 电池电压: %.2fV, 最低记录: %.2fV\n",
                  batteryVoltage / 1000.0f, minSystemVoltage / 1000.0f);
  }

  // 更新电池百分比（需要经过充放电周期校准）
  if (power.isBatteryConnect()) {
    int axpPercent = power.getBatteryPercent();
    float voltage = power.getBattVoltage(); // 修正：使用getBattVoltage()

    // 严格验证电量值的合理性，避免AXP2101偶尔返回的异常值
    if (axpPercent >= 1 && axpPercent <= 100) {
      // 1-100%直接接受（正常范围）
      batteryPercent = axpPercent;
    } else if (axpPercent == 0) {
      // 0%需要二次验证：只有电压真的很低时才接受
      // 3.7V锂电池：低于3.0V才可能是真的0%
      if (voltage > 0 && voltage < 3000) {
        batteryPercent = 0; // 电压确实很低，接受0%
      }
      // 否则保持缓存值 - AXP误报0%，实际电量并未耗尽
    }
    // 如果AXP返回-1或其他异常值，保持缓存的上一次有效值
  } else {
    batteryPercent = 0; // 电池未连接时清零
  }

  lastUpdate = now;
}

void PowerManager::enablePowerRail(const char *rail, bool enable) {
  if (!initialized)
    return;

  if (strcmp(rail, "DC1") == 0) {
    enable ? power.enableDC1() : power.disableDC1();
  } else if (strcmp(rail, "DC2") == 0) {
    enable ? power.enableDC2() : power.disableDC2();
  } else if (strcmp(rail, "DC3") == 0) {
    enable ? power.enableDC3() : power.disableDC3();
  } else if (strcmp(rail, "DC4") == 0) {
    enable ? power.enableDC4() : power.disableDC4();
  } else if (strcmp(rail, "DC5") == 0) {
    enable ? power.enableDC5() : power.disableDC5();
  }
  // 可以添加更多电源轨道控制
}

void PowerManager::setPowerRailVoltage(const char *rail, uint16_t voltage_mv) {
  if (!initialized)
    return;

  if (strcmp(rail, "DC1") == 0) {
    power.setDC1Voltage(voltage_mv);
  } else if (strcmp(rail, "DC2") == 0) {
    power.setDC2Voltage(voltage_mv);
  } else if (strcmp(rail, "DC3") == 0) {
    power.setDC3Voltage(voltage_mv);
  } else if (strcmp(rail, "DC4") == 0) {
    power.setDC4Voltage(voltage_mv);
  } else if (strcmp(rail, "DC5") == 0) {
    power.setDC5Voltage(voltage_mv);
  }
  // 可以添加更多电源轨道电压设置
}

void PowerManager::wakeUp() {
  if (!initialized)
    return;

  // 重新启用4G模块（拉高EN引脚）
  extern const int EN_4G; // 从MQTTManager.h引用

  digitalWrite(EN_4G, HIGH); // 拉高EN引脚使能4G模块
  Serial.println("[PowerManager] 4G模块已重新启用");
  delay(500); // 等待4G模块上电稳定

  // 重新启用电源轨道
  configurePowerRails();
}

void PowerManager::setPowerKeyPressTime(uint8_t seconds) {
  if (!initialized)
    return;

  switch (seconds) {
  case 4:
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    break;
  case 6:
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
    break;
  case 8:
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_8S);
    break;
  case 10:
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_10S);
    break;
  default:
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    break;
  }
}

void PowerManager::printStatus() {
  if (!initialized) {
    Serial.println("[PowerManager] Not initialized");
    return;
  }

  Serial.println("=== PowerManager Status ===");
  Serial.printf("Battery Voltage: %.2f mV\n", batteryVoltage);
  Serial.printf("System Voltage: %.2f mV\n", systemVoltage);
  Serial.printf("VBUS Voltage: %.2f mV\n", vbusVoltage);
  Serial.printf("Battery Percent: %d%%\n", batteryPercent);
  Serial.printf("Charging: %s\n", chargingStatus ? "YES" : "NO");
  Serial.printf("VBUS Connected: %s\n", vbusConnectedStatus ? "YES" : "NO");
  Serial.printf("Battery Connected: %s\n",
                power.isBatteryConnect() ? "YES" : "NO");

  Serial.println("===========================");
}

String PowerManager::getStatusString() {
  if (!initialized)
    return "PMU: Not initialized";

  String status = "BAT: ";
  status += String(batteryPercent) + "% ";
  status += String(batteryVoltage, 1) + "mV ";
  status += (chargingStatus ? "CHG" : "");
  status += (vbusConnectedStatus ? " USB" : "");

  return status;
}

void PowerManager::handleInterrupt() {
  if (!initialized)
    return;

  // 关键修复：先清除旧的中断状态，避免处理历史中断（参考官方示例loop前先清除）
  // 这样可以避免中断风暴 - 只处理清除后新产生的中断
  power.clearIrqStatus();

  // 获取中断状态寄存器
  uint32_t status = power.getIrqStatus();

  // 如果没有新中断发生，直接返回
  if (status == 0) {
    return;
  }

  // 只记录实际发生的中断（减少日志量）
  bool hasRealInterrupt = false;

  // VBUS（USB）相关中断 - 这些是真实的物理事件
  if (power.isVbusInsertIrq()) {
    Serial.println("[PowerManager] ⚡ VBUS插入");
    vbusConnectedStatus = true;
    delay(100);
    uint8_t chargeStatus = power.getChargerStatus();
    chargingStatus = (chargeStatus != XPOWERS_AXP2101_CHG_STOP_STATE);
    updateBatteryUI();
    hasRealInterrupt = true;
  }
  if (power.isVbusRemoveIrq()) {
    Serial.println("[PowerManager] ⚡ VBUS拔出");
    vbusConnectedStatus = false;
    chargingStatus = false;
    updateBatteryUI();
    hasRealInterrupt = true;
  }

  // 充电状态变化 - 真实的充电事件
  if (power.isBatChargeDoneIrq()) {
    Serial.println("[PowerManager] ⚡ 充电完成");
    uint8_t chargeStatus = power.getChargerStatus();
    chargingStatus = (chargeStatus == XPOWERS_AXP2101_CHG_DONE_STATE);
    updateBatteryUI();
    hasRealInterrupt = true;
  }
  if (power.isBatChargeStartIrq()) {
    Serial.println("[PowerManager] ⚡ 充电开始");
    chargingStatus = true;
    updateBatteryUI();
    hasRealInterrupt = true;
  }

  // 电池插拔 - 真实的物理事件
  if (power.isBatInsertIrq()) {
    Serial.println("[PowerManager] ⚡ 电池插入");
    hasRealInterrupt = true;
  }
  if (power.isBatRemoveIrq()) {
    Serial.println("[PowerManager] ⚡ 电池拔出");
    hasRealInterrupt = true;
  }

  // 电源按键
  if (power.isPekeyShortPressIrq()) {
    Serial.println("[PowerManager] ⚡ 电源键短按");
    hasRealInterrupt = true;
  }
  if (power.isPekeyLongPressIrq()) {
    Serial.println("[PowerManager] ⚡ 电源键长按");
    hasRealInterrupt = true;
  }

  // 看门狗
  if (power.isWdtExpireIrq()) {
    Serial.println("[PowerManager] ⚡ 看门狗超时");
    power.clrWatchdog();
    hasRealInterrupt = true;
  }

  // 最后再清除一次，确保处理完毕
  power.clearIrqStatus();
}

void PowerManager::feedWatchdog() {
  if (!initialized)
    return;
  // 看门狗已禁用，保留空实现以兼容调用处
}

void PowerManager::updateBatteryUI() {
  if (!initialized)
    return;

  // UI初始化保护
  extern bool ui_initialized;
  if (!ui_initialized) {
    return;
  }

  int percent = getBatteryPercent();

  // 控制电池图标显示逻辑
  // 0-25%: 只显示battery6on
  if (percent <= 25) {
    lv_obj_clear_flag(ui_battery6on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_battery7on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_battery8on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_battery9on, LV_OBJ_FLAG_HIDDEN);
  }
  // 25-50%: 显示battery6on, battery7on
  else if (percent <= 50) {
    lv_obj_clear_flag(ui_battery6on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_battery7on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_battery8on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_battery9on, LV_OBJ_FLAG_HIDDEN);
  }
  // 50-75%: 显示battery6on, battery7on, battery8on
  else if (percent <= 75) {
    lv_obj_clear_flag(ui_battery6on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_battery7on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_battery8on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_battery9on, LV_OBJ_FLAG_HIDDEN);
  }
  // 75-100%: 全部显示
  else {
    lv_obj_clear_flag(ui_battery6on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_battery7on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_battery8on, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_battery9on, LV_OBJ_FLAG_HIDDEN);
  }

  // 充电状态图标控制（ui_Image4 = 充电图标）
  if (isCharging()) {
    lv_obj_clear_flag(ui_Image4, LV_OBJ_FLAG_HIDDEN); // 充电中：显示
  } else {
    lv_obj_add_flag(ui_Image4, LV_OBJ_FLAG_HIDDEN); // 不充电：隐藏
  }
}

void PowerManager::initBatteryUI() {
  if (!initialized)
    return;

  // 初始化电池UI显示
  // ui_Image4 (充电图标) 会在 updateBatteryUI() 中根据充电状态自动设置
  updateBatteryUI();
}

int PowerManager::getBatteryPercent() {
  if (!initialized)
    return 0;

  // 返回缓存值，确保始终返回有效的电量百分比
  // 缓存在update()中更新，只保存0-100的有效值
  // 即使AXP2101临时返回-1（电量计未就绪），也能保持上一次有效值
  return batteryPercent;
}

bool PowerManager::isBatteryLearning() {
  if (!initialized)
    return false;
  // AXP2101的电池学习状态检测
  return power.getBatteryPercent() < 10 && power.isBatteryConnect();
}

bool PowerManager::isBatteryConnected() {
  if (!initialized)
    return false;
  return power.isBatteryConnect();
}

float PowerManager::getBatteryVoltage() {
  if (!initialized)
    return 0.0f;
  return power.getBattVoltage() / 1000.0f; // 转换为V
}

bool PowerManager::isCharging() {
  if (!initialized)
    return false;
  return chargingStatus;
}

bool PowerManager::isVbusConnected() {
  if (!initialized)
    return false;
  return vbusConnectedStatus;
}

String PowerManager::getChargeStatus() {
  if (!initialized)
    return "未初始化";

  if (!power.isBatteryConnect())
    return "无电池";
  return chargingStatus ? "充电中" : "未充电";
}

// getBatteryDisplayText() 函数已删除，因为不再需要电量数字显示

void PowerManager::configureBatteryCalibration() {
  // - setBatteryCapacity()
  // - resetCoulombCounter()
  // - setBatteryVoltageLimit()
}

void PowerManager::resetBatteryLearning() {
  if (!initialized)
    return;

  // - resetCoulombCounter()
  // - disableBatteryDetection() / enableBatteryDetection()

  // 可以执行的操作：重新启用电池检测
  power.enableBattDetection();
  delay(100);
  power.disableBattDetection();
  delay(100);
  power.enableBattDetection();
}

void PowerManager::manualCalibrateBattery() {
  if (!initialized)
    return;

  // 步骤1：重置所有电池学习数据
  resetBatteryLearning();

  // 步骤2：根据当前电压设置初始电量
  float voltage = getBatteryVoltage();
  int estimatedPercent = 0;

  // 步骤3：如果正在充电，等待充电完成进行满电校准
  // Serial.println("检测到充电中，建议充满电后进行满电校准");
  // Serial.println("充满电后设备会自动进行电量校准");
}

void PowerManager::wakeup() {
  // 调用wakeUp()函数
  wakeUp();
}

void PowerManager::printBatteryInfo() { printDetailedBatteryStatus(); }

void PowerManager::printAllStatus() {
  printStatus();
  printDetailedBatteryStatus();
}

void PowerManager::handleInterrupts() {
  if (!initialized)
    return;

  // 调用内部的handleInterrupt函数处理中断
  handleInterrupt();
}

bool PowerManager::checkPowerKeyPress() {
  if (!initialized)
    return false;

  bool shortPress = power.isPekeyShortPressIrq();
  bool longPress = power.isPekeyLongPressIrq();

  if (shortPress || longPress) {
    power.clearIrqStatus();
    return true;
  }

  return false;
}

void PowerManager::printDetailedBatteryStatus() {
  if (!initialized) {
    return;
  }
}

// 真正的关机功能（与02_axp2101_example.ino的enterPmuSleep()一致）
void PowerManager::shutdown() {
  if (!initialized)
    return;

  Serial.println(
      "[PowerManager] System entering sleep mode, press PWRKEY to wake up");
  delay(100); // 确保串口输出完成

  // 使用XPowersLib提供的标准关机函数
  // 这会关闭所有电源输出 (除了VRTC)，并允许通过PEK(电源键)唤醒
  power.shutdown();
}

// 检查是否请求关机
bool PowerManager::isShutdownRequested() { return shutdownRequested; }

// 设置关机标志
void PowerManager::setShutdownFlag(bool flag) {
  shutdownRequested = flag;
  if (flag) {
    Serial.println("[PowerManager] Shutdown requested");
  }
}

// ===================== 系统内存监控函数 =====================
void PowerManager::printMemoryStatus() {
  static uint32_t lastMemoryCheck = 0;
  uint32_t now = millis();

  // 每10秒检查一次内存状态
  if (now - lastMemoryCheck > 10000) {
    lastMemoryCheck = now;

    // 获取堆内存信息
    size_t freeHeap = ESP.getFreeHeap();
    size_t totalHeap = ESP.getHeapSize();
    size_t usedHeap = totalHeap - freeHeap;

    // 获取最大可分配内存块
    size_t maxAlloc = ESP.getMaxAllocHeap();

    // 获取栈信息
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);

    Serial.printf("[MEMORY] 堆内存: 已用=%zu, 空闲=%zu, 总计=%zu, 最大块=%zu\n",
                  usedHeap, freeHeap, totalHeap, maxAlloc);
    Serial.printf("[MEMORY] 栈剩余: %u words (%u bytes)\n", stackHighWaterMark,
                  stackHighWaterMark * 4);

    // 内存使用率警告
    float heapUsagePercent = (float)usedHeap / totalHeap * 100.0f;
    if (heapUsagePercent > 80.0f) {
      Serial.printf("[WARNING] 堆内存使用率过高: %.1f%%\n", heapUsagePercent);
    }

    // 栈空间警告
    if (stackHighWaterMark < 512) { // 少于2KB栈空间
      Serial.printf("[WARNING] 栈空间不足: %u words\n", stackHighWaterMark);
    }

    // 最大分配块警告
    if (maxAlloc < 10240) { // 少于10KB连续空间
      Serial.printf("[WARNING] 最大连续内存块过小: %zu bytes\n", maxAlloc);
    }
  }
}
