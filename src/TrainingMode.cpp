#include "TrainingMode.h"
#include "ConfigManager.h"
#include "IMUManager.h"
#include "SDCardManager.h"
#include "StrokeDataManager.h"
#include "WifiTransferManager.h"

// 外部变量声明
extern bool rtcInitialized;
extern bool rtcTimeSynced;
extern String getRTCFullDateTime();
extern ConfigManager configManager;
extern SDCardManager sdCardManager;
extern StrokeDataManager strokeDataMgr;
extern WifiTransferManager wifiTransfer;

void TrainingMode::start() {
  // 立即显示UI反馈，让用户感知到操作已响应
  if (imageIndicator) {
    lv_obj_clear_flag(imageIndicator, LV_OBJ_FLAG_HIDDEN);
  }
  if (timeLabel) {
    lv_label_set_text(timeLabel, "00:00");
  }
  lv_timer_handler(); // 立即刷新一帧

  // 【关键】启动训练前关闭WiFi传输模式
  if (wifiTransfer.isActive()) {
    wifiTransfer.stop();
    Serial.println("[训练模式] WiFi传输模式已关闭");
  }

  // 检查配置是否就绪
  if (!configManager.isMQTTConfigReady() ||
      !configManager.isDeviceConfigReady()) {
    Serial.println("[训练模式] 配置未就绪，无法启动训练模式");
    Serial.printf("[训练模式] MQTT配置: %s, 设备配置: %s\n",
                  configManager.isMQTTConfigReady() ? "✓" : "✗",
                  configManager.isDeviceConfigReady() ? "✓" : "✗");
    return;
  }

  active = true;
  running = false; // 初始时不运行
  paused = false;
  startTime = 0;
  lastStrokeTime = 0;
  pauseStartTime = 0;
  totalPausedTime = 0;
  totalPausedSeconds = 0; // 初始化累计暂停秒数

  // 生成新的训练ID
  trainId = generateTrainId();
  Serial.printf("[训练模式] 生成训练ID: %s\n", trainId.c_str());

  // 训练开始时间戳将在第一桨时记录（见 onStrokeDetected）

  // 创建新的SD卡训练文件
  sdCardManager.startNewTrainingFile();

  // 训练开始时重置所有训练数据
  resetTrainingData();

  Serial.println("[训练模式] ✅ 配置就绪，训练模式已启动");
}

void TrainingMode::stop() {
  // 立即隐藏UI反馈，让用户第一时间感知到停止操作
  if (imageIndicator) {
    lv_obj_add_flag(imageIndicator, LV_OBJ_FLAG_HIDDEN);
  }
  if (timeLabel) {
    lv_label_set_text(timeLabel, "00:00");
  }

  // 清空屏幕训练数据
  clearScreenData();

  lv_timer_handler(); // 立即刷新一帧以使隐藏和清空生效

  active = false;
  running = false;

  // 关闭当前SD卡文件
  sdCardManager.closeCurrentFile();

  // 清空训练ID和时间戳
  Serial.printf("[训练模式] 训练结束，trainId: %s\n", trainId.c_str());
  trainId = "";
  trainingStartTimestamp = ""; // 清空训练开始时间戳
  totalPausedSeconds = 0;      // 重置累计暂停秒数

  // 重置全局训练数据变量
  resetTrainingData();

  // 【重要】清空划桨队列，避免退出训练后还发送历史数据
  strokeDataMgr.reset();
  Serial.println("[训练模式] 划桨队列已清空");

  // 【关键】训练结束后启动WiFi传输模式
  wifiTransfer.start();
  Serial.println("[训练模式] WiFi传输模式已启动，可以下载数据");
}

void TrainingMode::onStrokeDetected() {
  if (!active)
    return;

  unsigned long now = millis();
  lastStrokeTime = now;

  // 第一次检测到桨数变化，开始计时
  if (!running) {
    running = true;
    startTime = now;
    paused = false;
    pauseStartTime = 0;
    totalPausedTime = 0;

    // 【关键】在第一桨时记录训练开始的绝对时间戳
    extern String getValidTimestamp();
    trainingStartTimestamp = getValidTimestamp();
    Serial.printf("[训练模式] 第一桨检测，训练开始时间: %s\n",
                  trainingStartTimestamp.c_str());
  }

  // 如果是暂停状态，恢复计时
  if (paused) {
    paused = false;
    if (pauseStartTime > 0) {
      // 计算本次暂停的秒数并累加
      unsigned long pauseDurationMs = now - pauseStartTime;
      unsigned long pauseDurationSec = pauseDurationMs / 1000;
      totalPausedSeconds += pauseDurationSec;
      totalPausedTime += pauseDurationMs; // 保留毫秒版本用于兼容
      pauseStartTime = 0;

      Serial.printf("[训练模式] 恢复计时，本次暂停: %lu 秒，累计暂停: %lu 秒\n",
                    pauseDurationSec, totalPausedSeconds);
    }
  }
}

void TrainingMode::checkPauseConditions() {
  if (!active || !running)
    return;

  unsigned long now = millis();

  // 10秒无桨数变化暂停计时
  if (lastStrokeTime > 0 && (now - lastStrokeTime) > 10000) {
    if (!paused) {
      paused = true;
      pauseStartTime = now;
    }
  }

  // 暂停累计5分钟结束训练
  if (paused && (now - pauseStartTime) > 300000) { // 300000ms = 5分钟
    stop();
  }
}

void TrainingMode::update() {
  if (!active)
    return;

  checkPauseConditions(); // 检查暂停条件

  if (millis() - lastUpdate >= 1000) {
    updateDisplay();
    lastUpdate = millis();
  }
}

unsigned long TrainingMode::getElapsedSeconds() const {
  if (!active || !running)
    return 0;

  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - startTime - totalPausedTime;

  // 如果当前处于暂停状态，减去当前暂停的时间
  if (paused && pauseStartTime > 0) {
    elapsed -= (currentTime - pauseStartTime);
  }

  return elapsed / 1000;
}

unsigned long TrainingMode::getElapsedMillis() const {
  if (!active || !running)
    return 0;

  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - startTime - totalPausedTime;

  // 如果当前处于暂停状态，减去当前暂停的时间
  if (paused && pauseStartTime > 0) {
    elapsed -= (currentTime - pauseStartTime);
  }

  return elapsed;
}

void TrainingMode::updateDisplay() {
  if (!timeLabel)
    return;

  unsigned long elapsed = getElapsedSeconds();
  int minutes = elapsed / 60;
  int seconds = elapsed % 60;
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  lv_label_set_text(timeLabel, buffer);
}

void TrainingMode::clearScreenData() {
  // 外部UI标签声明
  extern lv_obj_t *ui_Label9;  // 桨频
  extern lv_obj_t *ui_Label44; // 桨数 (Panel1)
  extern lv_obj_t *ui_Label64; // 桨数 (Panel8)
  extern lv_obj_t *ui_Label12; // 划距 (Panel1)
  extern lv_obj_t *ui_Label56; // 划距 (Panel8)
  extern lv_obj_t *ui_Label23; // 总距离 (Panel1)
  extern lv_obj_t *ui_Label60; // 总距离 (Panel8)
  extern lv_obj_t *ui_Label7;  // 训练时间 (Panel1)
  extern lv_obj_t *ui_Label13; // 训练时间 (Panel8)

  // 清空训练数据显示（设为默认值）
  if (ui_Label9)
    lv_label_set_text(ui_Label9, "0.0"); // 桨频
  if (ui_Label44)
    lv_label_set_text(ui_Label44, "0"); // 桨数
  if (ui_Label64)
    lv_label_set_text(ui_Label64, "0"); // 桨数
  if (ui_Label12)
    lv_label_set_text(ui_Label12, "0.0"); // 划距
  if (ui_Label56)
    lv_label_set_text(ui_Label56, "0.0"); // 划距
  if (ui_Label23)
    lv_label_set_text(ui_Label23, "0.000"); // 总距离(公里)
  if (ui_Label60)
    lv_label_set_text(ui_Label60, "0.000"); // 总距离(公里)
  if (ui_Label7)
    lv_label_set_text(ui_Label7, "00:00"); // 训练时间
  if (ui_Label13)
    lv_label_set_text(ui_Label13, "00:00"); // 训练时间

  Serial.println("[训练模式] 屏幕数据已清空");
}

void TrainingMode::resetTrainingData() {
  // 外部全局变量声明
  extern float strokeRate;
  extern int strokeCount;
  extern float strokeLength;
  extern float totalDistance;
  extern IMUManager imu;

  // 重置全局训练数据变量
  strokeRate = 0.0f;
  strokeCount = 0;
  strokeLength = 0.0f;
  totalDistance = 0.0f;

  // 重置IMU管理器中的训练数据
  imu.resetStrokeCount();
  imu.resetTotalDistance();

  Serial.println("[训练模式] 训练数据已重置");
}

String TrainingMode::generateTrainId() {
  // 获取当前时间（优先RTC，备用4G时间）
  String currentTime;
  if (rtcInitialized && rtcTimeSynced) {
    currentTime = getRTCFullDateTime(); // 格式: "2024-10-16 14:30:25"
  } else {
    currentTime = configManager.getCurrentFormattedDateTime();
    if (currentTime.isEmpty() || currentTime == "null") {
      currentTime = "2024-01-01 00:00:00"; // 默认时间
    }
  }

  // 将时间转换为trainId格式：YYYYMMDDHHmmss
  // 从 "2024-10-16 14:30:25" 提取
  String year = currentTime.substring(0, 4);     // 2024
  String month = currentTime.substring(5, 7);    // 10
  String day = currentTime.substring(8, 10);     // 16
  String hour = currentTime.substring(11, 13);   // 14
  String minute = currentTime.substring(14, 16); // 30
  String second = currentTime.substring(17, 19); // 25

  // 生成4位随机数（1000-9999）
  int randomNum = random(1000, 10000);

  // 组合trainId：YYYYMMDDHHmmss + 4位随机数
  String trainIdStr =
      year + month + day + hour + minute + second + String(randomNum);

  return trainIdStr;
}
