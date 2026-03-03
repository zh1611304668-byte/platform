/*
 * File: StrokeDataManager.h
 * Purpose: Declares interfaces, types, and constants for the Stroke Data Manager module.
 */
#ifndef STROKE_DATA_MANAGER_H
#define STROKE_DATA_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include <queue>

// 划桨事件快照（完整的状态记录）
struct StrokeSnapshot {
  // 基础信息
  unsigned long timestamp; // 捕获时间戳（毫秒，用于排序和调试）
  String captureTime;      // 捕获时的格式化时间字符串（用于JSON上报）
  int strokeNumber;        // 桨序号（1,2,3...）
  String trainId;          // 训练ID
  String boatCode;         // 船只编号

  // GNSS数据快照
  // 状态标记
  bool isValid;   // 数据是否有效
  bool isSent;    // 是否已发送
  int retryCount; // 重试次数

  // 缺失的字段定义
  double lat;
  double lon;
  double startLat;
  double startLon;
  double endLat;
  double endLon;
  float speed;
  float power;
  float pace;
  float strokeRate;
  float strokeLength;
  float totalDistance;
  float elapsedSeconds;
  float strokeIntervalSec;

  // 构造函数
  StrokeSnapshot()
      : timestamp(0), strokeNumber(0), lat(0.0), lon(0.0), startLat(0.0),
        startLon(0.0), endLat(0.0), endLon(0.0), speed(0.0f), power(0.0f),
        pace(0.0f), strokeRate(0.0f), strokeLength(0.0f), totalDistance(0.0f),
        elapsedSeconds(0.0f), strokeIntervalSec(0.0f), isValid(false),
        isSent(false), retryCount(0) {}
};

// 划桨数据管理器
class StrokeDataManager {
private:
  std::queue<StrokeSnapshot> strokeQueue; // 独立的划桨队列
  SemaphoreHandle_t queueMutex;           // 队列互斥锁

  int lastCapturedStroke; // 最后捕获的桨数
  int lastSentStroke;     // 最后发送成功的桨数

  static const size_t MAX_QUEUE_SIZE = 1000; // 独立的大容量队列

  // 统计信息
  unsigned long totalCaptured;  // 总捕获数
  unsigned long totalSent;      // 总发送数
  unsigned long totalLost;      // 总丢失数
  unsigned long queueFullCount; // 队列满次数

public:
  StrokeDataManager();
  ~StrokeDataManager();

  // 初始化
  bool begin();

  // 捕获划桨快照（由主循环调用）
  // currentStrokeCount: IMU检测到的当前桨数
  // 返回: 成功捕获返回true，队列满返回false
  bool captureStroke(int currentStrokeCount);

  // 获取待发送的划桨（由mqttTask调用）
  // snapshot: 输出参数，返回待发送的快照
  // 返回: 有待发送数据返回true，队列空返回false
  bool getNextStroke(StrokeSnapshot &snapshot);

  // 标记划桨已发送（由mqttTask调用）
  // strokeNumber: 桨序号
  // success: 是否发送成功
  void markStrokeSent(int strokeNumber, bool success);

  // 获取统计信息
  void getStatistics(size_t &pending, size_t &sent, size_t &lost,
                     size_t &queueFull);

  // 获取队列状态
  size_t getQueueSize() const;
  size_t getMaxQueueSize() const { return MAX_QUEUE_SIZE; }

  // 重置统计信息（训练开始时调用）
  void resetStatistics();

  // 重置管理器（训练结束时调用）
  void reset();

private:
  // 内部辅助函数
  bool isQueueFull() const;
  bool pushToQueue(const StrokeSnapshot &snapshot);
};

// 全局实例
extern StrokeDataManager strokeDataMgr;

void normalizeStrokeSnapshot(StrokeSnapshot &snapshot);

#endif // STROKE_DATA_MANAGER_H
