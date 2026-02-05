#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include "FS.h"
#include "SD_MMC.h"
#include <Arduino.h>

struct StrokeSnapshot;

struct TrainingSample {
  String timestamp;
  double lat;
  double lon;
  float speed;
  float pace;
  float strokeRate;
  int strokeCount;
  float strokeLength;
  float totalDistance;
  float elapsedSeconds = 0.0f;
};

class SDCardManager {
public:
  SDCardManager();
  bool begin();
  void logImuData(unsigned long timestamp, float ax, float ay, float az);
  bool isMounted() const { return cardMounted && !disabled; }
  bool isDisabled() const { return disabled; }
  void reset(); // 重置错误计数，重新启用SD卡

  // 训练文件管理
  void startNewTrainingFile(); // 兼容旧调用
  void startNewTrainingFile(const String &trainId,
                            const String &startTimestamp); // 开始新的训练文件
  void logTrainingSample(const TrainingSample &sample, const String &jsonLine);
  void logStrokeSnapshot(const StrokeSnapshot &snapshot);
  void logNmeaRaw(const String &nmea);
  void logDebug(const String &msg); // 记录调试日志

  // SD卡配置记录NMEA原始数据（CSV格式）
  void finalizeTrainingReport(unsigned long totalDurationMs);
  void closeCurrentFile(); // 兼容旧接口，等同于closeCurrentFiles
  void closeCurrentFiles();
  String getCurrentFileName() const { return currentLogFile; }
  String getCurrentSessionFolder() const { return currentSessionFolder; }

private:
  bool cardMounted;
  bool disabled;             // SD卡是否已被禁用（降级保护）
  uint8_t consecutiveErrors; // 连续错误计数
  static const uint8_t MAX_CONSECUTIVE_ERRORS = 20; // 连续失败20次后临时禁用
  unsigned long lastErrorTime;                      // 上次错误时间
  static const unsigned long ERROR_RECOVERY_INTERVAL =
      10000; // 10秒后自动恢复尝试

  File imuFile;          // 三轴加速度文件
  File trainingJsonFile; // JSON采样文件
  File strokeCsvFile;    // 划桨CSV数据
  File nmeaCsvFile;      // NMEA原始数据文件（CSV格式）
  File debugFile;        // 调试日志文件

  String currentLogFile; // 当前IMU日志文件名
  String trainingJsonPath;
  String strokeCsvPath;
  String nmeaCsvPath;          // NMEA原始数据文件路径（CSV格式）
  String debugPath;            // 调试日志文件路径
  String currentSessionFolder; // 当前训练目录
  String currentTrainId;       // 当前训练ID
  unsigned long lastFlushTime; // 上次flush时间
  static const unsigned long FLUSH_INTERVAL_MS = 500; // 每500ms刷新一次
  int trainingCounter;                                // 训练次数计数

  struct AggregatedStats {
    bool active = false;
    String startTimestamp;
    String endTimestamp;
    double startLat = 0.0;
    double startLon = 0.0;
    double endLat = 0.0;
    double endLon = 0.0;
    bool hasStartPosition = false;
    bool hasEndPosition = false;
    float lastDistance = 0.0f;
    uint32_t durationMs = 0;
    double sumSpeed = 0.0;
    double sumStrokeRate = 0.0;
    double sumStrokeLength = 0.0;
    uint32_t speedSamples = 0;
    uint32_t strokeRateSamples = 0;
    uint32_t strokeLengthSamples = 0;
    uint32_t totalStrokes = 0;
    bool reportGenerated = false;
  } stats;

  int getNextTrainingNumber(); // 获取下一个训练编号
  void ensureSessionFolder();
  void resetStats();
  String formatHhmmssTenths(uint32_t milliseconds) const;
  String formatSplit(float seconds) const;
  void appendPerStrokeCsv(const StrokeSnapshot &snapshot);
};

#endif
