#ifndef TRAINING_MODE_H
#define TRAINING_MODE_H

#include <Arduino.h>
#include <lvgl.h>

class TrainingMode {
public:
  void start();
  void stop();
  void update();

  bool isActive() const { return active; }
  bool isRunning() const { return running; }
  unsigned long getElapsedSeconds() const;
  unsigned long getElapsedMillis() const; // 获取经过的毫秒数（含小数精度）
  String getTrainId() const { return trainId; } // 获取训练ID
  String getTrainingStartTimestamp() const {
    return trainingStartTimestamp;
  } // 获取训练开始的绝对时间戳
  unsigned long getTotalPausedSeconds() const {
    return totalPausedSeconds;
  } // 获取累计暂停秒数

  // UI元素指针
  lv_obj_t *imageIndicator = nullptr;
  lv_obj_t *timeLabel = nullptr;

  void onStrokeDetected(); // 桨数变化时调用
  bool isPaused() const { return paused; }

private:
  void updateDisplay();
  void checkPauseConditions(); // 检查暂停条件
  void clearScreenData();      // 清空屏幕显示数据
  void resetTrainingData();    // 重置训练数据
  String generateTrainId();    // 生成训练ID

  bool active = false;         // 训练模式激活
  bool running = false;        // 计时器是否正在运行
  bool paused = false;         // 是否暂停
  unsigned long startTime = 0; // 实际开始时间（用于 UI 更新判断）
  unsigned long lastUpdate = 0;
  unsigned long lastStrokeTime = 0;   // 上一次桨数变化的时间
  unsigned long pauseStartTime = 0;   // 暂停开始时间
  unsigned long totalPausedTime = 0;  // 累计暂停时间（毫秒，保留用于兼容）
  String trainId = "";                // 训练ID（时间戳+4位随机数）
  String trainingStartTimestamp = ""; // 训练开始的绝对时间戳（RTC/4G时间）
  unsigned long totalPausedSeconds =
      0; // 累计暂停秒数（用于基于时间戳的训练时长计算）
};

#endif