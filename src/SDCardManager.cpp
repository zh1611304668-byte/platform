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
      lastFlushTime(0), lastNmeaMs_(-1), imuQueue(nullptr),
      imuTaskHandle(nullptr), imuLoggingEnabled(false) {
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

  // 异步IMU写入支持
  if (!imuQueue) {
    imuQueue = xQueueCreate(IMU_QUEUE_LEN, sizeof(ImuSample));
    if (!imuQueue) {
      Serial.println("[SD] ⚠️ 创建IMU队列失败，保持同步写入模式");
    }
  }
  if (imuQueue && !imuTaskHandle) {
    BaseType_t res = xTaskCreatePinnedToCore(imuLogTask, "IMU_SD_Writer", 8192,
                                             this, 1, &imuTaskHandle, 1);
    if (res != pdPASS) {
      Serial.println("[SD] ⚠️ 创建IMU写入任务失败，保持同步写入模式");
      imuTaskHandle = nullptr;
    }
  }
  imuLoggingEnabled = (imuQueue && imuTaskHandle);

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
  if (!cardMounted || disabled || !imuFile) {
    return;
  }

  // 异步路径：快速入队，队列满时直接丢弃，绝不阻塞主循环
  if (imuLoggingEnabled && imuQueue) {
    ImuSample sample{(uint32_t)timestamp, ax, ay, az};
    if (xQueueSend(imuQueue, &sample, 0) != pdTRUE) {
      imuDropCount++;
    }
    return;
  }

  // 回退同步写入（不会尝试flush，尽量减少阻塞）
  imuFile.printf("%lu,%.4f,%.4f,%.4f\n", timestamp, ax, ay, az);
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
    imuFile.println("sys_ms,acc_x,acc_y,acc_z");
    imuFile.flush();
  }

  trainingJsonPath = "";
  trainingJsonFile = File();

  strokeCsvPath = currentSessionFolder + "/strokes.csv";
  strokeCsvFile = SD_MMC.open(strokeCsvPath.c_str(), FILE_WRITE);
  if (strokeCsvFile && strokeCsvFile.position() == 0) {
    // Session Information（模仿 SpeedCoach 格式）
    const DeviceConfig &devCfg = configManager.getDeviceConfig();
    String boatCode = devCfg.isValid ? devCfg.boatCode : "---";
    String boatName = devCfg.isValid ? devCfg.boatName : "---";
    String tenantCode = devCfg.isValid ? devCfg.tenantCode : "---";
    String imei = configManager.getDeviceIMEI();
    if (imei.isEmpty())
      imei = "---";

    strokeCsvFile.println("Session Information:,,,,Device Information:");
    strokeCsvFile.println();
    strokeCsvFile.printf("Name:,%s,,,Device Code:,%s\n", boatName.c_str(),
                         imei.c_str());
    strokeCsvFile.printf("Start Time:,%s,,,Boat Code:,%s\n",
                         startTimestamp.c_str(), boatCode.c_str());
    strokeCsvFile.printf("System of Units:,Meters/Speed,,,Tenant:,%s\n",
                         tenantCode.c_str());
    strokeCsvFile.println("Speed Input:,GPS,,,Firmware:,1.0.0");
    strokeCsvFile.println();
    strokeCsvFile.println();
    // Per-Stroke Data 标题和列头
    strokeCsvFile.println("Per-Stroke Data:");
    strokeCsvFile.println();
    strokeCsvFile.println(
        "BoatCode,StrokeLength(m),TotalDistance(m),ElapsedTime,Pace(/500m),"
        "Speed(m/s),StrokeRate(spm),Power(W),StrokeCount,Lat,Lon,Timestamp,"
        "ET_Diff(s),TS_Diff(s),60/StrokeRate(s)");
    strokeCsvFile.println(
        "(Code),(Meters),(Meters),(Seconds),(/500),(M/"
        "S),(SPM),(Watts),(Strokes),"
        "(Degrees),(Degrees),(DateTime),(Seconds),(Seconds),(Seconds)");
    strokeCsvFile.flush();
    Serial.println("[SD] ✅ Stroke CSV header written");
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
      nmeaCsvFile.println("sys_ms,nmea_ms,nmea");
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
  lastNmeaMs_ = -1; // 重置UTC缓存

  // 重新启用IMU异步写入
  imuLoggingEnabled = (imuQueue && imuTaskHandle);
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

// 从 Timestamp 字符串提取总秒数（分钟*60 + 秒.毫秒），用于计算 TS Diff
static float parseTimestampSeconds(const String &ts) {
  // 格式: "2026-03-04 16:42:36.856"
  int lastColon = ts.lastIndexOf(':');
  if (lastColon < 0)
    return 0.0f;
  int prevColon = ts.lastIndexOf(':', lastColon - 1);
  if (prevColon < 0)
    return 0.0f;
  int minute = ts.substring(prevColon + 1, lastColon).toInt();
  float sec = ts.substring(lastColon + 1).toFloat();
  return minute * 60.0f + sec;
}

void SDCardManager::appendPerStrokeCsv(const StrokeSnapshot &snapshot) {
  if (!strokeCsvFile) {
    return;
  }

  // 用 static 变量记住上一桨的数据，用于计算 Diff 和累加时间
  static float prevElapsed = 0.0f;
  static float prevTsSec = 0.0f;
  static float accumulatedElapsedFromTs = 0.0f;
  static bool hasPrev = false;

  // 第一桨时重置
  if (snapshot.strokeNumber <= 1) {
    hasPrev = false;
    accumulatedElapsedFromTs = 0.0f;
  }

  // Timestamp 保留完整3位小数（毫秒精度），与 ElapsedTime 精度一致
  String tsOut = snapshot.captureTime; // 格式已是 "2026-03-03 15:58:21.639"

  // 计算三列分析数据
  float curTsSec = parseTimestampSeconds(tsOut);
  String etDiffStr = "-";
  String tsDiffStr = "-";
  String srIntervalStr = "-";

  if (hasPrev) {
    float etDiffOrigin = snapshot.elapsedSeconds - prevElapsed;
    float tsDiff = curTsSec - prevTsSec;
    // 处理跨分钟边界（例如 59.9 → 0.1，差值变成负数）
    if (tsDiff < -30.0f)
      tsDiff += 3600.0f; // 跨小时
    if (tsDiff < 0.0f)
      tsDiff += 60.0f; // 跨分钟

    accumulatedElapsedFromTs += tsDiff;

    etDiffStr = String(etDiffOrigin, 3);
    tsDiffStr = String(tsDiff, 3);

    if (snapshot.strokeRate > 0.01f) {
      float csvStrokeRate = roundf(snapshot.strokeRate * 10.0f) / 10.0f;
      srIntervalStr = String(60.0f / csvStrokeRate, 3);
    }
  } else {
    // 第一桨时确保累计时间为0
    accumulatedElapsedFromTs = 0.0f;
  }

  // 训练时长：改为基于时间戳的差值累加结果
  String elapsed = String(accumulatedElapsedFromTs, 3);

  // 【调试】打印CSV输出的td值
  Serial.printf(
      "[CSV] 桨次%d，原始elapsedSeconds=%.3f → 基于时间戳累加CSV输出=%s\n",
      snapshot.strokeNumber, snapshot.elapsedSeconds, elapsed.c_str());

  // 四舍五入到2位小数，与CSV中 %.2f 格式保持一致，确保配速与速度字段对齐
  float roundedSpeed = roundf(snapshot.speed * 100.0f) / 100.0f;
  String split = (roundedSpeed > 0.05f) ? formatSplit(500.0f / roundedSpeed)
                                        : String("--");

  prevElapsed = snapshot.elapsedSeconds;
  prevTsSec = curTsSec;
  hasPrev = true;

  char line[640];
  snprintf(line, sizeof(line),
           "%s,%.2f,%.2f,%s,%s,%.2f,%.1f,%.1f,%d,%.7f,%.7f,%s,%s,%s,%s",
           snapshot.boatCode.c_str(), snapshot.strokeLength,
           snapshot.totalDistance, elapsed.c_str(), split.c_str(), roundedSpeed,
           snapshot.strokeRate, snapshot.power, snapshot.strokeNumber,
           snapshot.lat, snapshot.lon, tsOut.c_str(), etDiffStr.c_str(),
           tsDiffStr.c_str(), srIntervalStr.c_str());
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

  // 使用millis()作为系统时间戳；同时解析NMEA自带UTC（如果有），转为毫秒
  unsigned long sys_ms = millis();
  long nmea_ms = -1;
  // 仅对含 UTC 的句子(GGA/RMC/GLL)解析时间；VTG 不解析且不刷新 lastNmeaMs_
  bool hasTimeField = false;
  if (nmea.startsWith("$GNGGA") || nmea.startsWith("$GPGGA") ||
      nmea.startsWith("$GNRMC") || nmea.startsWith("$GPRMC") ||
      nmea.startsWith("$GNGLL") || nmea.startsWith("$GPGLL")) {
    int firstComma = nmea.indexOf(',');
    if (firstComma > 0 && nmea.length() >= firstComma + 7) {
      String timeField =
          nmea.substring(firstComma + 1, firstComma + 11); // hhmmss.ss
      if (timeField.length() >= 6 && isDigit(timeField[0])) {
        int hh = timeField.substring(0, 2).toInt();
        int mm = timeField.substring(2, 4).toInt();
        float sec = timeField.substring(4).toFloat();
        nmea_ms = (long)((hh * 3600 + mm * 60) * 1000 + sec * 1000);
        lastNmeaMs_ = nmea_ms;
        hasTimeField = true;
      }
    }
  }
  // 若本行无时间，尝试复用上一 UTC（不递增，避免虚假100Hz）
  if (!hasTimeField && lastNmeaMs_ >= 0) {
    nmea_ms = lastNmeaMs_;
  }

  String csvLine;
  csvLine.reserve(nmea.length() + 40);
  csvLine += sys_ms;
  csvLine += ",";
  csvLine += nmea_ms;
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
    // 训练结束时追加 Session Summary
    appendSessionSummary();
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

  imuLoggingEnabled = false;
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

// ===================== IMU 异步写入任务 =====================
void SDCardManager::imuLogTask(void *param) {
  SDCardManager *self = static_cast<SDCardManager *>(param);
  static char buffer[2048];
  int bufLen = 0;
  int batchCount = 0;
  TickType_t lastFlush = xTaskGetTickCount();
  TickType_t lastDropPrint = lastFlush;

  while (true) {
    ImuSample sample;
    bool got =
        xQueueReceive(self->imuQueue, &sample, pdMS_TO_TICKS(100)) == pdTRUE;

    // 如果被禁用或文件不可用，直接跳过
    if (self->disabled || !self->imuFile) {
      // nothing
    } else if (got) {
      int written = snprintf(
          buffer + bufLen, sizeof(buffer) - bufLen, "%lu,%.4f,%.4f,%.4f\n",
          (unsigned long)sample.ts_ms, sample.ax, sample.ay, sample.az);
      if (written > 0 && written < (int)(sizeof(buffer) - bufLen)) {
        bufLen += written;
        batchCount++;
      }
    }

    bool timeFlush =
        (xTaskGetTickCount() - lastFlush) >= pdMS_TO_TICKS(FLUSH_INTERVAL_MS);
    bool needFlush = (bufLen > 1800) || (batchCount >= IMU_BATCH_SIZE) ||
                     (timeFlush && bufLen > 0);

    if (needFlush && self->imuFile && !self->disabled && bufLen > 0) {
      size_t flushed = self->imuFile.write((uint8_t *)buffer, bufLen);
      if (flushed == (size_t)bufLen) {
        self->imuFile.flush();
        self->consecutiveErrors = 0;
      } else {
        self->consecutiveErrors++;
        self->lastErrorTime = millis();
        if (self->consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
          self->disabled = true;
          self->imuFile.close();
          Serial.println("[SD] IMU logging disabled due to errors");
        }
      }
      bufLen = 0;
      batchCount = 0;
      lastFlush = xTaskGetTickCount();
    }

    if ((xTaskGetTickCount() - lastDropPrint) >= pdMS_TO_TICKS(10000)) {
      if (self->imuDropCount > 0) {
        Serial.printf("[SD] IMU dropped samples: %lu (last 10s)\n",
                      (unsigned long)self->imuDropCount);
        self->imuDropCount = 0;
      }
      lastDropPrint = xTaskGetTickCount();
    }
  }
}

// 训练结束时追加 Session Summary（模仿 SpeedCoach 格式）
void SDCardManager::appendSessionSummary() {
  if (!strokeCsvFile || stats.totalStrokes == 0) {
    return;
  }

  // 计算平均值
  float avgSpeed = (stats.speedSamples > 0)
                       ? (float)(stats.sumSpeed / stats.speedSamples)
                       : 0.0f;
  float avgStrokeRate =
      (stats.strokeRateSamples > 0)
          ? (float)(stats.sumStrokeRate / stats.strokeRateSamples)
          : 0.0f;
  float avgStrokeLength =
      (stats.strokeLengthSamples > 0)
          ? (float)(stats.sumStrokeLength / stats.strokeLengthSamples)
          : 0.0f;

  // 格式化训练时长
  String elapsed = formatHhmmssTenths(stats.durationMs);

  // 格式化平均配速
  String avgPace =
      (avgSpeed > 0.05f) ? formatSplit(500.0f / avgSpeed) : String("--");

  // 写入 Session Summary
  strokeCsvFile.println();
  strokeCsvFile.println();
  strokeCsvFile.println("Session Summary:");
  strokeCsvFile.println();
  strokeCsvFile.println(
      "TotalDistance(m),TotalElapsedTime,AvgPace(/500m),AvgSpeed(m/s),"
      "AvgStrokeRate(spm),TotalStrokes,AvgStrokeLength(m),"
      "StartLat,StartLon,EndLat,EndLon");

  char line[256];
  snprintf(line, sizeof(line),
           "%.2f,%s,%s,%.2f,%.1f,%u,%.2f,%.7f,%.7f,%.7f,%.7f",
           stats.lastDistance, elapsed.c_str(), avgPace.c_str(), avgSpeed,
           avgStrokeRate, stats.totalStrokes, avgStrokeLength, stats.startLat,
           stats.startLon, stats.endLat, stats.endLon);
  strokeCsvFile.println(line);

  Serial.printf("[SD] ✅ Session Summary written: %u strokes, %.1fm, %s\n",
                stats.totalStrokes, stats.lastDistance, elapsed.c_str());
}
