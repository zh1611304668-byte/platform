#include "SDCardManager.h"
#include "ConfigManager.h"
#include "StrokeDataManager.h"
#include "TrainingMode.h"
#include <Arduino.h>

extern TrainingMode training;
extern bool rtcInitialized;
extern bool rtcTimeSynced;
extern String getRTCFullDateTime();
extern ConfigManager configManager;

// SD_MMC Pin definitions for ESP32-S3
#define SD_MMC_CLK_PIN 11
#define SD_MMC_CMD_PIN 10
#define SD_MMC_D0_PIN 9

SDCardManager::SDCardManager()
    : cardMounted(false), disabled(false), consecutiveErrors(0),
      trainingCounter(0), currentLogFile(""), trainingJsonPath(""),
      strokeCsvPath(""), nmeaCsvPath(""), debugPath(""),
      currentSessionFolder(""), currentTrainId(""), lastErrorTime(0),
      lastFlushTime(0) {
  resetStats();
}

bool SDCardManager::begin() {
  Serial.println("Initializing SD card...");

  if (!SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)) {
    Serial.println("[SD] Pin change failed!");
    cardMounted = false;
    return false;
  }

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("[SD] Card Mount Failed");
    cardMounted = false;
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] No SD card attached");
    cardMounted = false;
    return false;
  }

  cardMounted = true;
  disabled = false;
  consecutiveErrors = 0;
  resetStats();
  return true;
}

void SDCardManager::resetStats() { stats = AggregatedStats(); }

void SDCardManager::ensureSessionFolder() {
  if (currentSessionFolder.isEmpty()) {
    currentSessionFolder = "/";
  }
  if (!SD_MMC.exists(currentSessionFolder.c_str())) {
    SD_MMC.mkdir(currentSessionFolder.c_str());
  }
}

void SDCardManager::logImuData(unsigned long timestamp, float ax, float ay,
                               float az) {
  if (!cardMounted) {
    return;
  }
  if (disabled) {
    unsigned long now = millis();
    if (now - lastErrorTime > ERROR_RECOVERY_INTERVAL) {
      disabled = false;
      consecutiveErrors = 0;
    } else {
      return;
    }
  }

  if (!imuFile) {
    return;
  }

  // 内部缓冲区 - 使用static在调用间保持数据
  static char buffer[2048]; // 约15-20条样本数据
  static int bufferLen = 0;
  static int sampleCount = 0;

  // 格式化数据到缓冲区
  int written = snprintf(buffer + bufferLen, sizeof(buffer) - bufferLen,
                         "%lu,%.4f,%.4f,%.4f\n", timestamp, ax, ay, az);

  // 缓冲区溢出保护
  if (written < 0 || written >= (int)(sizeof(buffer) - bufferLen)) {
    // 立即刷新现有数据
    if (bufferLen > 0) {
      imuFile.print(buffer);
      imuFile.flush();
    }
    bufferLen = 0;
    sampleCount = 0;
    return;
  }

  bufferLen += written;
  sampleCount++;

  // 刷新条件：缓冲区接近满(>1800字节) 或 累积20条样本
  if (bufferLen > 1800 || sampleCount >= 20) {
    size_t flushed = imuFile.print(buffer);
    if (flushed > 0) {
      imuFile.flush();
      consecutiveErrors = 0;
      lastFlushTime = millis();
    } else {
      consecutiveErrors++;
      lastErrorTime = millis();

      if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
        disabled = true;
        imuFile.close();
      }
    }
    bufferLen = 0;
    sampleCount = 0;
  }
}

void SDCardManager::startNewTrainingFile() {
  String trainId = training.getTrainId();
  if (trainId.isEmpty()) {
    trainId = String(millis());
  }

  String sessionStartTime;
  if (rtcInitialized && rtcTimeSynced) {
    sessionStartTime = getRTCFullDateTime();
  } else {
    sessionStartTime = configManager.getCurrentFormattedDateTime();
    if (sessionStartTime.isEmpty() || sessionStartTime == "null") {
      sessionStartTime = "1970-01-01 00:00:00";
    }
  }

  startNewTrainingFile(trainId, sessionStartTime);
}

void SDCardManager::startNewTrainingFile(const String &trainId,
                                         const String &startTimestamp) {
  if (!cardMounted || disabled) {
    Serial.println("[SD] Cannot start new file - SD card not ready");
    return;
  }

  closeCurrentFiles();
  resetStats();
  stats.active = true;
  stats.startTimestamp = startTimestamp;
  stats.endTimestamp = startTimestamp;

  String safeTrainId = trainId;
  if (safeTrainId.isEmpty()) {
    safeTrainId = String(millis());
  }

  int nextNumber = getNextTrainingNumber();
  char baseName[16];
  snprintf(baseName, sizeof(baseName), "imu_log_%03d", nextNumber);
  String base(baseName);

  currentSessionFolder = "/" + base + "_" + safeTrainId;
  if (currentSessionFolder.length() > 60) {
    currentSessionFolder = currentSessionFolder.substring(0, 60);
  }

  if (SD_MMC.exists(currentSessionFolder.c_str())) {
    currentSessionFolder += "_" + String((uint32_t)millis(), HEX);
  }
  ensureSessionFolder();

  // 将IMU文件也放入训练文件夹中
  char imuFileName[32];
  snprintf(imuFileName, sizeof(imuFileName), "imu_log_%03d.csv", nextNumber);
  currentLogFile = currentSessionFolder + "/" + String(imuFileName);

  imuFile = SD_MMC.open(currentLogFile.c_str(), FILE_WRITE);
  if (!imuFile) {
    Serial.printf("[SD] ❌ Failed to open IMU log file: %s\n",
                  currentLogFile.c_str());
    currentLogFile = "";
    return;
  }
  Serial.printf("[SD] ✅ Opened IMU log file: %s\n", currentLogFile.c_str());

  if (imuFile.size() == 0 || imuFile.position() == 0) {
    imuFile.println("timestamp,acc_x,acc_y,acc_z");
    imuFile.flush();
  }

  trainingJsonPath = "";
  trainingJsonFile = File();

  strokeCsvPath = currentSessionFolder + "/strokes.csv";
  strokeCsvFile = SD_MMC.open(strokeCsvPath.c_str(), FILE_WRITE);
  if (strokeCsvFile && strokeCsvFile.size() == 0) {
    strokeCsvFile.println(
        "BoatCode,StrokeLength(m),TotalDistance(m),ElapsedTime,Pace(/500m),"
        "Speed(m/s),StrokeRate(spm),StrokeCount,Lat,Lon,Timestamp");
    strokeCsvFile.flush();
  }

  // 创建 NMEA 原始数据文件（CSV格式）
  char gnssFileName[32];
  snprintf(gnssFileName, sizeof(gnssFileName), "gnss_%03d.csv", nextNumber);
  nmeaCsvPath = currentSessionFolder + "/" + String(gnssFileName);
  nmeaCsvFile = SD_MMC.open(nmeaCsvPath.c_str(), FILE_WRITE);

  if (nmeaCsvFile) {
    Serial.printf("[SD] ✅ Opened GNSS file: %s\n", nmeaCsvPath.c_str());
    // 使用position()而不是size()，更可靠地检测新文件
    if (nmeaCsvFile.position() == 0) {
      nmeaCsvFile.println("timestamp,nmea");
      nmeaCsvFile.flush();
      Serial.println("[SD] ✅ GNSS CSV header written");
    } else {
      Serial.printf("[SD] ℹ️ GNSS file exists, position=%d\n",
                    nmeaCsvFile.position());
    }
  } else {
    Serial.printf("[SD] ❌ Failed to open GNSS file: %s\n",
                  nmeaCsvPath.c_str());
  }

  currentTrainId = safeTrainId;
  lastFlushTime = millis();
  consecutiveErrors = 0;
}

void SDCardManager::logTrainingSample(const TrainingSample &sample,
                                      const String &jsonLine) {
  if (!cardMounted || disabled || !stats.active) {
    return;
  }

  (void)jsonLine;

  unsigned long now = millis();
  if (now - lastFlushTime > FLUSH_INTERVAL_MS) {
    if (strokeCsvFile)
      strokeCsvFile.flush();
    lastFlushTime = now;
  }

  if (stats.startTimestamp.isEmpty() && !sample.timestamp.isEmpty()) {
    stats.startTimestamp = sample.timestamp;
  }
  if (!stats.hasStartPosition && (sample.lat != 0.0 || sample.lon != 0.0)) {
    stats.startLat = sample.lat;
    stats.startLon = sample.lon;
    stats.hasStartPosition = true;
  }
  if (!sample.timestamp.isEmpty()) {
    stats.endTimestamp = sample.timestamp;
  }
  if (sample.lat != 0.0 || sample.lon != 0.0) {
    stats.endLat = sample.lat;
    stats.endLon = sample.lon;
    stats.hasEndPosition = true;
  }

  stats.sumSpeed += sample.speed;
  stats.speedSamples++;
  stats.sumStrokeRate += sample.strokeRate;
  stats.strokeRateSamples++;
  stats.lastDistance = sample.totalDistance;
  if (sample.strokeCount > stats.totalStrokes) {
    stats.totalStrokes = sample.strokeCount;
  }
  if (sample.elapsedSeconds > 0.0f) {
    stats.durationMs = static_cast<uint32_t>(sample.elapsedSeconds * 1000.0f);
  }
}

String SDCardManager::formatHhmmssTenths(uint32_t milliseconds) const {
  uint32_t totalSeconds = milliseconds / 1000;
  uint32_t tenths = (milliseconds % 1000) / 100;
  uint32_t hours = totalSeconds / 3600;
  uint32_t minutes = (totalSeconds % 3600) / 60;
  uint32_t seconds = totalSeconds % 60;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%01u", hours, minutes,
           seconds, tenths);
  return String(buffer);
}

String SDCardManager::formatSplit(float seconds) const {
  if (seconds <= 0.0f) {
    return String("--");
  }
  uint32_t totalSeconds = static_cast<uint32_t>(seconds);
  uint32_t minutes = totalSeconds / 60;
  uint32_t secs = totalSeconds % 60;
  uint32_t tenths =
      static_cast<uint32_t>((seconds - totalSeconds) * 10.0f + 0.5f);
  if (tenths >= 10) {
    tenths = 0;
    secs++;
    if (secs >= 60) {
      secs = 0;
      minutes++;
    }
  }
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02u:%02u.%01u", minutes, secs, tenths);
  return String(buffer);
}

void SDCardManager::appendPerStrokeCsv(const StrokeSnapshot &snapshot) {
  if (!strokeCsvFile) {
    return;
  }

  // 训练时长直接输出为秒数格式（保留1位小数），例如: 29.1
  String elapsed = String(snapshot.elapsedSeconds, 1);

  // 【调试】打印CSV输出的td值
  Serial.printf("[CSV] 桨次%d，elapsedSeconds=%.3f → CSV输出=%s\n",
                snapshot.strokeNumber, snapshot.elapsedSeconds,
                elapsed.c_str());

  String split = (snapshot.speed > 0.05f) ? formatSplit(500.0f / snapshot.speed)
                                          : String("--");

  char line[512];
  snprintf(line, sizeof(line), "%s,%.2f,%.2f,%s,%s,%.2f,%.1f,%d,%.7f,%.7f,%s",
           snapshot.boatCode.c_str(), snapshot.strokeLength,
           snapshot.totalDistance, elapsed.c_str(), split.c_str(),
           snapshot.speed, snapshot.strokeRate, snapshot.strokeNumber,
           snapshot.lat, snapshot.lon, snapshot.captureTime.c_str());
  strokeCsvFile.println(line);
}

void SDCardManager::logNmeaRaw(const String &nmea) {
  static unsigned long nmeaCount = 0;
  static unsigned long lastDebugPrint = 0;

  nmeaCount++;

  // 每10秒打印一次统计信息
  if (millis() - lastDebugPrint > 10000) {
    Serial.printf("[SD] NMEA stats: Total=%lu, Mounted=%d, Disabled=%d, "
                  "Active=%d, FileOpen=%d\n",
                  nmeaCount, cardMounted, disabled, stats.active,
                  (bool)nmeaCsvFile);
    lastDebugPrint = millis();
  }

  if (!cardMounted || disabled || !stats.active) {
    static unsigned long lastWarning = 0;
    if (millis() - lastWarning > 10000) {
      Serial.printf(
          "[SD] NMEA not logged: mounted=%d, disabled=%d, active=%d\n",
          cardMounted, disabled, stats.active);
      lastWarning = millis();
    }
    return;
  }
  if (!nmeaCsvFile) {
    static unsigned long lastFileWarning = 0;
    if (millis() - lastFileWarning > 10000) {
      Serial.println("[SD] NMEA file not open!");
      lastFileWarning = millis();
    }
    return;
  }

  // 使用millis()作为时间戳，与IMU数据格式一致，便于对齐
  unsigned long timestamp = millis();

  String csvLine;
  csvLine.reserve(nmea.length() + 20);
  csvLine += timestamp;
  csvLine += ",";
  csvLine += nmea;

  nmeaCsvFile.println(csvLine);

  // 定期 flush，与 stroke 文件一起
  unsigned long now = millis();
  if (now - lastFlushTime > FLUSH_INTERVAL_MS) {
    if (nmeaCsvFile)
      nmeaCsvFile.flush();
    lastFlushTime = now; // 更新 flush 时间，避免频繁落盘
  }
}

void SDCardManager::logStrokeSnapshot(const StrokeSnapshot &snapshot) {
  if (!cardMounted || disabled || !stats.active) {
    return;
  }

  StrokeSnapshot normalized = snapshot;
  normalizeStrokeSnapshot(normalized);

  appendPerStrokeCsv(normalized);

  stats.sumStrokeLength += normalized.strokeLength;
  stats.strokeLengthSamples++;
  if (normalized.strokeNumber > stats.totalStrokes) {
    stats.totalStrokes = normalized.strokeNumber;
  }
  stats.lastDistance = normalized.totalDistance;

  if (stats.startTimestamp.isEmpty() && !normalized.captureTime.isEmpty()) {
    stats.startTimestamp = normalized.captureTime;
  }

  if (!stats.hasStartPosition &&
      (normalized.lat != 0.0 || normalized.lon != 0.0)) {
    stats.startLat = normalized.lat;
    stats.startLon = normalized.lon;
    stats.hasStartPosition = true;
  }
  if (normalized.lat != 0.0 || normalized.lon != 0.0) {
    stats.endLat = normalized.lat;
    stats.endLon = normalized.lon;
    stats.hasEndPosition = true;
  }
  if (!normalized.captureTime.isEmpty()) {
    stats.endTimestamp = normalized.captureTime;
  }

  unsigned long now = millis();
  if (now - lastFlushTime > FLUSH_INTERVAL_MS) {
    if (strokeCsvFile)
      strokeCsvFile.flush();
    lastFlushTime = now;
  }
}

void SDCardManager::closeCurrentFiles() {
  if (imuFile) {
    imuFile.flush();
    imuFile.close();
  }
  if (strokeCsvFile) {
    strokeCsvFile.flush();
    strokeCsvFile.close();
    strokeCsvFile = File();
  }
  if (nmeaCsvFile) {
    nmeaCsvFile.flush();
    nmeaCsvFile.close();
  }
  nmeaCsvFile = File();

  // 关闭调试日志文件
  if (debugFile) {
    debugFile.println("=== Debug Log Ended ===");
    debugFile.close();
  }
  debugFile = File();

  currentLogFile = "";
  trainingJsonPath = "";
  trainingJsonFile = File();
  strokeCsvPath = "";
  nmeaCsvPath = "";
  currentSessionFolder = "";
  currentTrainId = "";
  stats.active = false;
}

void SDCardManager::closeCurrentFile() { closeCurrentFiles(); }

int SDCardManager::getNextTrainingNumber() {
  if (!cardMounted) {
    return 1;
  }

  int maxNumber = 0;
  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    return 1;
  }

  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    // 查找文件夹而不是CSV文件，因为现在IMU文件在文件夹里
    if (file.isDirectory() && name.startsWith("imu_log_")) {
      // 提取编号：imu_log_026_trainId -> 026
      int startIdx = name.indexOf("_") + 1;       // 跳过 "imu"
      startIdx = name.indexOf("_", startIdx) + 1; // 跳过 "log"
      int endIdx = name.indexOf("_", startIdx);   // 找到训练ID前的下划线

      if (endIdx < 0) {
        endIdx = name.length(); // 如果没有下划线（不应该发生），用整个字符串
      }

      if (startIdx > 0 && endIdx > startIdx) {
        String numStr = name.substring(startIdx, endIdx);
        int num = numStr.toInt();
        if (num > maxNumber) {
          maxNumber = num;
        }
      }
    }
    file = root.openNextFile();
  }
  root.close();

  int nextNumber = maxNumber + 1;
  Serial.printf("[SD] Next training number: %d (max found: %d)\n", nextNumber,
                maxNumber);
  return nextNumber;
}

void SDCardManager::reset() {
  consecutiveErrors = 0;
  disabled = false;
}

void SDCardManager::logDebug(const String &msg) {
  // 调试日志已禁用
  (void)msg;
  return;
}
