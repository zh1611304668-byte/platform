/*
 * File: SDCardManager.h
 * Purpose: Declares interfaces, types, and constants for the S D Card Manager module.
 */
#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include "FS.h"
#include "SD_MMC.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

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
  void reset(); // 閲嶇疆閿欒璁℃暟锛岄噸鏂板惎鐢⊿D鍗?

  // 璁粌鏂囦欢绠＄悊
  void startNewTrainingFile(); // 鍏煎鏃ц皟鐢?
  void startNewTrainingFile(const String &trainId,
                            const String &startTimestamp); // 寮€濮嬫柊鐨勮缁冩枃浠?
  void logTrainingSample(const TrainingSample &sample, const String &jsonLine);
  void logStrokeSnapshot(const StrokeSnapshot &snapshot);
  void logNmeaRaw(const String &nmea);
  void logDebug(const String &msg); // 璁板綍璋冭瘯鏃ュ織

  // SD鍗￠厤缃褰昇MEA鍘熷鏁版嵁锛圕SV鏍煎紡锛?
  void finalizeTrainingReport(unsigned long totalDurationMs);
  void closeCurrentFile(); // 鍏煎鏃ф帴鍙ｏ紝绛夊悓浜巆loseCurrentFiles
  void closeCurrentFiles();
  String getCurrentFileName() const { return currentLogFile; }
  String getCurrentSessionFolder() const { return currentSessionFolder; }

private:
  struct ImuSample {
    uint32_t ts_ms;
    float ax;
    float ay;
    float az;
  };

  bool cardMounted;
  bool disabled;                     // SD disable flag for degraded mode
  uint8_t consecutiveErrors;         // consecutive write errors
  static const uint8_t MAX_CONSECUTIVE_ERRORS = 20;
  unsigned long lastErrorTime;
  static const unsigned long ERROR_RECOVERY_INTERVAL =
      10000; // 10绉掑悗鑷姩鎭㈠灏濊瘯

  File imuFile;          // 涓夎酱鍔犻€熷害鏂囦欢
  File trainingJsonFile; // JSON閲囨牱鏂囦欢
  File strokeCsvFile;    // 鍒掓〃CSV鏁版嵁
  File legacyStrokeCsvFile;
  File nmeaCsvFile;      // NMEA鍘熷鏁版嵁鏂囦欢锛圕SV鏍煎紡锛?
  File debugFile;        // 璋冭瘯鏃ュ織鏂囦欢

  String currentLogFile; // 褰撳墠IMU鏃ュ織鏂囦欢鍚?
  String trainingJsonPath;
  String strokeCsvPath;
  String legacyStrokeCsvPath;
  String nmeaCsvPath;          // NMEA鍘熷鏁版嵁鏂囦欢璺緞锛圕SV鏍煎紡锛?
  String debugPath;            // 璋冭瘯鏃ュ織鏂囦欢璺緞
  String currentSessionFolder; // 褰撳墠璁粌鐩綍
  String currentTrainId;       // 褰撳墠璁粌ID
  unsigned long lastFlushTime; // 涓婃flush鏃堕棿
  static const unsigned long FLUSH_INTERVAL_MS = 500;
  int trainingCounter;
  long lastNmeaMs_;           // 鏈€杩戜竴娆″惈UTC鐨凬MEA鏃堕棿(ms, 褰撳ぉ)

  // IMU寮傛鍐欏叆
  QueueHandle_t imuQueue;
  TaskHandle_t imuTaskHandle;
  bool imuLoggingEnabled;
  uint32_t imuDropCount = 0;
  static const size_t IMU_QUEUE_LEN = 512; // 淇濆畧瀹归噺锛屼富寰幆蹇€熻繑鍥?
  static const int IMU_BATCH_SIZE = 50;    // 鍗曟鎵瑰啓鏉℃暟锛岄檷浣庡啓鏀惧ぇ

  static void imuLogTask(void *param);

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

  int getNextTrainingNumber(); // 鑾峰彇涓嬩竴涓缁冪紪鍙?
  void ensureSessionFolder();
  void resetStats();
  String formatHhmmssTenths(uint32_t milliseconds) const;
  String formatSplit(float seconds) const;
  void appendPerStrokeCsv(const StrokeSnapshot &snapshot);
  String lastStrokeCsvTimestamp_;
  unsigned long lastStrokeSysTimestamp_ = 0;
};

#endif



