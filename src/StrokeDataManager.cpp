#include "StrokeDataManager.h"
#include "ConfigManager.h"
#include "GNSSProcessor.h"
#include "IMUManager.h"
#include "SDCardManager.h"
#include "TrainingMode.h"
#include <cmath>

extern GNSSProcessor gnss;

static constexpr float kMinSpeedForSnapshot = 0.05f;

// 用于数据对齐的上一桨终点坐标
static double s_prevOutputLat = 0.0;
static double s_prevOutputLon = 0.0;
static bool s_hasPrevOutput = false;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Haversine 距离计算（与 IMUManager 一致）
static double haversineDistance(double lat1, double lon1, double lat2,
                                double lon2) {
  const double R = 6371000.0; // 地球半径（米）
  double dLat = (lat2 - lat1) * M_PI / 180.0;
  double dLon = (lon2 - lon1) * M_PI / 180.0;
  double lat1Rad = lat1 * M_PI / 180.0;
  double lat2Rad = lat2 * M_PI / 180.0;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1Rad) * cos(lat2Rad) * sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

// 从时间戳中减去指定毫秒数，用于反推波峰瞬间的RTC时间
static String subtractMillisFromTimestamp(const String &timestamp,
                                          unsigned long offsetMs) {
  if (timestamp.length() < 19 || offsetMs == 0)
    return timestamp;

  int year = timestamp.substring(0, 4).toInt();
  int month = timestamp.substring(5, 7).toInt();
  int day = timestamp.substring(8, 10).toInt();
  int hour = timestamp.substring(11, 13).toInt();
  int minute = timestamp.substring(14, 16).toInt();
  int second = timestamp.substring(17, 19).toInt();
  int ms = 0;
  if (timestamp.length() >= 23 && timestamp.charAt(19) == '.') {
    ms = timestamp.substring(20, 23).toInt();
  }

  // 总毫秒数减去偏移
  long totalMs = (long)ms - (long)offsetMs;
  long totalSec = (long)hour * 3600 + (long)minute * 60 + (long)second;

  // 处理毫秒借位
  while (totalMs < 0) {
    totalMs += 1000;
    totalSec--;
  }

  // 处理秒借位（跨天，极端情况下才会触发）
  if (totalSec < 0) {
    totalSec += 86400;
    day--;
    if (day < 1) {
      month--;
      if (month < 1) {
        month = 12;
        year--;
      }
      int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      day = daysInMonth[month - 1];
    }
  }

  hour = (int)(totalSec / 3600);
  minute = (int)((totalSec % 3600) / 60);
  second = (int)(totalSec % 60);

  char buf[28];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld", year, month,
           day, hour, minute, second, totalMs);
  return String(buf);
}

// 向时间戳中加上指定毫秒数，用于单调递增保护
static String addMillisToTimestamp(const String &timestamp,
                                   unsigned long addMs) {
  if (timestamp.length() < 19 || addMs == 0)
    return timestamp;

  int year = timestamp.substring(0, 4).toInt();
  int month = timestamp.substring(5, 7).toInt();
  int day = timestamp.substring(8, 10).toInt();
  int hour = timestamp.substring(11, 13).toInt();
  int minute = timestamp.substring(14, 16).toInt();
  int second = timestamp.substring(17, 19).toInt();
  int ms = 0;
  if (timestamp.length() >= 23 && timestamp.charAt(19) == '.') {
    ms = timestamp.substring(20, 23).toInt();
  }

  long totalMs = (long)ms + (long)addMs;
  long totalSec = (long)hour * 3600 + (long)minute * 60 + (long)second;

  // 处理毫秒进位
  totalSec += totalMs / 1000;
  totalMs = totalMs % 1000;

  // 处理秒进位（跨天，极端情况）
  if (totalSec >= 86400) {
    totalSec -= 86400;
    day++;
    // 简化处理，实际不会跨天
  }

  hour = (int)(totalSec / 3600);
  minute = (int)((totalSec % 3600) / 60);
  second = (int)(totalSec % 60);

  char buf[28];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld", year, month,
           day, hour, minute, second, totalMs);
  return String(buf);
}

void normalizeStrokeSnapshot(StrokeSnapshot &snapshot) {
  if ((snapshot.lat == 0.0 && snapshot.lon == 0.0) && gnss.isValidFix()) {
    double latestLat = gnss.getLatitude();
    double latestLon = gnss.getLongitude();
    if (latestLat != 0.0 || latestLon != 0.0) {
      snapshot.lat = latestLat;
      snapshot.lon = latestLon;
    }
  }

  if (snapshot.speed <= kMinSpeedForSnapshot) {
    float liveSpeed = gnss.getSpeed();
    if (liveSpeed > snapshot.speed) {
      snapshot.speed = liveSpeed;
    }
  }

  // 移除 deriveStrokeLength 调用，直接使用测量值
}

// 外部变量声明
extern IMUManager imu;
extern TrainingMode training;
extern ConfigManager configManager;
extern SDCardManager sdCardManager;
extern float strokeRate;
extern float totalDistance;
extern float strokeLength;

// 全局实例
StrokeDataManager strokeDataMgr;

StrokeDataManager::StrokeDataManager()
    : queueMutex(nullptr), lastCapturedStroke(0), lastSentStroke(0),
      totalCaptured(0), totalSent(0), totalLost(0), queueFullCount(0) {}

StrokeDataManager::~StrokeDataManager() {
  if (queueMutex) {
    vSemaphoreDelete(queueMutex);
  }
}

bool StrokeDataManager::begin() {
  // 创建互斥锁
  queueMutex = xSemaphoreCreateMutex();
  if (!queueMutex) {
    Serial.println("[Stroke] 互斥锁创建失败");
    return false;
  }

  return true;
}

bool StrokeDataManager::captureStroke(int currentStrokeCount) {
  // 检测是否在训练模式
  if (!training.isActive()) {
    return false;
  }

  // 检查是否是新的一桨
  if (currentStrokeCount <= lastCapturedStroke) {
    return false;
  }

  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  String boatCode = deviceConfig.isValid ? deviceConfig.boatCode : "01";
  String trainId = training.getTrainId();
  extern String getValidTimestamp();

  // 直接从 IMU 获取统一的划桨数据
  const StrokeMetrics &metrics = imu.getLastStrokeMetrics();

  StrokeSnapshot snapshot;
  snapshot.timestamp = metrics.timestamp;
  snapshot.strokeNumber = metrics.strokeNumber;
  snapshot.trainId = trainId;
  snapshot.boatCode = boatCode;

  // 获取当前准确时间戳
  snapshot.captureTime = getValidTimestamp();

  // 单调递增保护：防止RTC秒/millis毫秒拼接导致时间戳倒退
  static String s_prevCaptureTime = "";
  if (!s_prevCaptureTime.isEmpty() && snapshot.captureTime.length() >= 23 &&
      s_prevCaptureTime.length() >= 23) {
    extern float calculateTimeDifference(const String &timestamp1,
                                         const String &timestamp2);
    float diff =
        calculateTimeDifference(s_prevCaptureTime, snapshot.captureTime);
    if (diff <= 0.0f) {
      // 时间倒退或相同，强制+1ms
      snapshot.captureTime = addMillisToTimestamp(s_prevCaptureTime, 1);
      Serial.printf("[Stroke] ⚠️ 时间戳倒退修正: diff=%.3f -> 强制+1ms\n", diff);
    }
  }

  // ========== 核心修改：基于时间戳计算训练时长（毫秒精度）==========
  String startTS = training.getTrainingStartTimestamp();
  if (!startTS.isEmpty() && snapshot.captureTime.length() >= 19) {
    // 声明外部函数：计算时间差
    extern float calculateTimeDifference(const String &timestamp1,
                                         const String &timestamp2);

    // 直接计算训练开始到当前的时间差（秒，含毫秒）
    float elapsedRaw = calculateTimeDifference(startTS, snapshot.captureTime);

    if (elapsedRaw > 0.0f) {
      // 扣除暂停时间
      float pausedSec = (float)training.getTotalPausedSeconds();

      if (elapsedRaw >= pausedSec) {
        snapshot.elapsedSeconds = elapsedRaw - pausedSec;
        Serial.printf(
            "[计算] elapsedRaw=%.3f, pausedSec=%.3f → elapsedSeconds=%.3f\n",
            elapsedRaw, pausedSec, snapshot.elapsedSeconds);
      } else {
        // 异常情况：暂停时间大于总时长，设为0
        snapshot.elapsedSeconds = 0.0f;
        Serial.printf("[Stroke] ⚠️ 暂停时间(%.1f秒) > 总时长(%.1f秒)\n",
                      pausedSec, elapsedRaw);
      }
    } else {
      // 时间差计算失败，回退到 millis() 方案
      snapshot.elapsedSeconds = training.getElapsedMillis() / 1000.0f;
      Serial.println("[Stroke] ⚠️ 时间差计算失败，使用millis()计算训练时长");
    }
  } else {
    // 训练开始时间戳不可用，回退到 millis() 方案
    snapshot.elapsedSeconds = training.getElapsedMillis() / 1000.0f;
  }

  // 坐标数据 - 直接使用 StrokeMetrics
  snapshot.startLat = metrics.startLat;
  snapshot.startLon = metrics.startLon;
  snapshot.endLat = metrics.endLat;
  snapshot.endLon = metrics.endLon;

  // 兼容旧字段
  snapshot.lat = metrics.endLat;
  snapshot.lon = metrics.endLon;

  // 运动数据
  snapshot.speed = gnss.getSpeed();
  snapshot.power = gnss.getPower();
  snapshot.pace = (snapshot.speed > 0.05f) ? (500.0f / snapshot.speed) : 0.0f;

  // IMU数据 - 桨频从 captureTime 差值计算，确保与 Timestamp/ElapsedTime
  // 完全对齐（复用上方的 s_prevCaptureTime）
  if (snapshot.strokeNumber == 1) {
    snapshot.strokeRate = 0.0f;
    s_prevCaptureTime = snapshot.captureTime;
  } else if (!s_prevCaptureTime.isEmpty() &&
             snapshot.captureTime.length() >= 19) {
    extern float calculateTimeDifference(const String &timestamp1,
                                         const String &timestamp2);
    float interval =
        calculateTimeDifference(s_prevCaptureTime, snapshot.captureTime);
    if (interval > 0.001f) {
      snapshot.strokeRate = 60.0f / interval;
    } else {
      snapshot.strokeRate = strokeRate; // 回退到IMU桨频
    }
    s_prevCaptureTime = snapshot.captureTime;
  } else {
    snapshot.strokeRate = strokeRate;
    s_prevCaptureTime = snapshot.captureTime;
  }

  // 先做一次归一化，确保后续划距计算使用最终要输出的坐标
  normalizeStrokeSnapshot(snapshot);

  bool coordsComplete = (snapshot.lat != 0.0 && snapshot.lon != 0.0);
  double currLat =
      coordsComplete ? round(snapshot.lat * 10000000.0) / 10000000.0 : 0.0;
  double currLon =
      coordsComplete ? round(snapshot.lon * 10000000.0) / 10000000.0 : 0.0;

  // 第一桨时重置变量，并用第一桨坐标初始化
  if (snapshot.strokeNumber == 1) {
    s_prevOutputLat = 0.0;
    s_prevOutputLon = 0.0;
    s_hasPrevOutput = false; // 标记为无前一桨
  }

  // 第一桨没有前一桨，划距为0；后续桨使用上一桨的输出坐标计算
  if (!s_hasPrevOutput || !coordsComplete) {
    snapshot.strokeLength = 0.0f;
  } else {
    double rawDist =
        haversineDistance(s_prevOutputLat, s_prevOutputLon, currLat, currLon);
    // 四舍五入到2位小数
    snapshot.strokeLength =
        roundf(static_cast<float>(rawDist) * 100.0f) / 100.0f;
  }

  // 更新上一桨输出坐标（用于下一桨的划距计算）
  if (coordsComplete) {
    s_prevOutputLat = currLat;
    s_prevOutputLon = currLon;
    s_hasPrevOutput = true;
  }

  // totalDistance 使用重新计算的划距累加（需要单独维护）
  static float s_totalOutputDistance = 0.0f;
  if (snapshot.strokeNumber == 1) {
    s_totalOutputDistance = 0.0f;
  }
  s_totalOutputDistance += snapshot.strokeLength;
  snapshot.totalDistance = s_totalOutputDistance;

  // 更新全局变量供UI显示（确保UI和MQTT数据一致）
  strokeLength = snapshot.strokeLength;
  totalDistance = snapshot.totalDistance;

  snapshot.isValid = true;
  snapshot.isSent = false;
  snapshot.retryCount = 0;

  // 将桨数即时写入SD卡（DISABLED: 会严重阻塞主循环，导致划桨检测失败）
  // TODO: 改为异步写入或在MQTT任务中批量写入
  // sdCardManager.logStrokeSnapshot(snapshot);

  // 更新最后捕获的桨数
  lastCapturedStroke = metrics.strokeNumber;

  // 推入队列
  bool pushed = pushToQueue(snapshot);
  if (pushed) {
    totalCaptured++;
  } else {
    Serial.printf("[Stroke] 队列满！丢弃 #%d\n", snapshot.strokeNumber);
    totalLost++;
    queueFullCount++;
  }

  return pushed;
}

bool StrokeDataManager::getNextStroke(StrokeSnapshot &snapshot) {
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (!strokeQueue.empty()) {
      // 只取出，不删除（发送成功后才删除）
      snapshot = strokeQueue.front();
      xSemaphoreGive(queueMutex);
      return true;
    }
    xSemaphoreGive(queueMutex);
  }
  return false;
}

void StrokeDataManager::markStrokeSent(int strokeNumber, bool success) {
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (strokeQueue.empty()) {
      xSemaphoreGive(queueMutex);
      return;
    }

    StrokeSnapshot &front = strokeQueue.front();
    if (front.strokeNumber != strokeNumber) {
      Serial.printf("[Stroke] ⚠️ 序号不匹配！期望=%d, 实际=%d\n",
                    front.strokeNumber, strokeNumber);
      xSemaphoreGive(queueMutex);
      return;
    }

    if (success) {
      // 发送成功，从队列删除
      strokeQueue.pop();
      lastSentStroke = strokeNumber;
      totalSent++;
      xSemaphoreGive(queueMutex);
      // Serial.printf("[Stroke] ✓ 已发送 #%d\n", strokeNumber);
    } else {
      // 发送失败，记录重试次数，保留数据
      front.retryCount++;
      Serial.printf("[Stroke] ↻ 重试 #%d (第%d次)\n", strokeNumber,
                    front.retryCount);
      xSemaphoreGive(queueMutex);
    }
  }
}

void StrokeDataManager::getStatistics(size_t &pending, size_t &sent,
                                      size_t &lost, size_t &queueFull) {
  pending = getQueueSize();
  sent = totalSent;
  lost = totalLost;
  queueFull = queueFullCount;
}

size_t StrokeDataManager::getQueueSize() const { return strokeQueue.size(); }

void StrokeDataManager::resetStatistics() {
  totalCaptured = 0;
  totalSent = 0;
  totalLost = 0;
  queueFullCount = 0;

  Serial.println("[Stroke] 统计信息已重置");
}

void StrokeDataManager::reset() {
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // 清空队列
    while (!strokeQueue.empty()) {
      strokeQueue.pop();
    }

    // 重置计数器
    lastCapturedStroke = 0;
    lastSentStroke = 0;

    xSemaphoreGive(queueMutex);
  }

  resetStatistics();
  Serial.println("[Stroke] 管理器已重置");
}

bool StrokeDataManager::isQueueFull() const {
  return strokeQueue.size() >= MAX_QUEUE_SIZE;
}

bool StrokeDataManager::pushToQueue(const StrokeSnapshot &snapshot) {
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!isQueueFull()) {
      strokeQueue.push(snapshot);
      xSemaphoreGive(queueMutex);
      return true;
    }
    xSemaphoreGive(queueMutex);
  }
  return false;
}
