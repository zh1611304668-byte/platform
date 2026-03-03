/*
 * File: SDCardManager.cpp
 * Purpose: Implements runtime logic for the S D Card Manager module.
 */
#include "SDCardManager.h"
#include "ConfigManager.h"
#include "FirmwareVersion.h"
#include "StrokeDataManager.h"
#include "TrainingMode.h"
#include <Arduino.h>

extern TrainingMode training;
extern bool rtcInitialized;
extern bool rtcTimeSynced;
extern String getRTCFullDateTime();
extern ConfigManager configManager;
extern float calculateTimeDifference(const String &timestamp1,
                                     const String &timestamp2);

// SD_MMC Pin definitions for ESP32-S3
#define SD_MMC_CLK_PIN 11
#define SD_MMC_CMD_PIN 10
#define SD_MMC_D0_PIN 9

namespace {

String sanitizeFileToken(const String &src) {
  String out;
  out.reserve(src.length());
  for (size_t i = 0; i < src.length(); ++i) {
    char c = src.charAt(i);
    bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '_' || c == '-';
    out += ok ? c : '_';
  }
  while (out.length() > 0 && out.charAt(out.length() - 1) == '_') {
    out.remove(out.length() - 1);
  }
  return out.isEmpty() ? String("UNKNOWN") : out;
}

String deviceIdForExport() {
  const DeviceConfig &cfg = configManager.getDeviceConfig();
  String id = cfg.stdId;
  if (id.isEmpty()) {
    id = cfg.boatCode;
  }
  return sanitizeFileToken(id);
}

String tsDateToken(const String &ts) {
  if (ts.length() >= 10) {
    return ts.substring(0, 4) + ts.substring(5, 7) + ts.substring(8, 10);
  }
  return "19700101";
}

String tsTimeTokenAmPm(const String &ts) {
  if (ts.length() < 16) {
    return "1200AM";
  }
  int hour24 = ts.substring(11, 13).toInt();
  int minute = ts.substring(14, 16).toInt();
  bool isPm = hour24 >= 12;
  int hour12 = hour24 % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d%02d%s", hour12, minute, isPm ? "PM" : "AM");
  return String(buf);
}

String tsDisplayForSpeedCoach(const String &ts) {
  if (ts.length() < 19) {
    return ts;
  }
  String out = ts.substring(0, 19);
  out.setCharAt(4, '/');
  out.setCharAt(7, '/');
  return out;
}

void writeStrokeCsvPreamble(File &f, const String &deviceId,
                            const String &sessionStartTs) {
  const String sessionStartDisplay = tsDisplayForSpeedCoach(sessionStartTs);
  const String sessionName = String("JustGo-") + deviceId;

  f.println("Session Information:");
  f.printf("Name:,%s\n", sessionName.c_str());
  f.printf("Start Time:,%s\n", sessionStartDisplay.c_str());
  f.println("Type:,Just Go");
  f.println("System of Units:,Meters/Speed");
  f.println("Speed Input:,RTK");
  f.println();

  f.println("Device Information:");
  f.printf("Name:,TMcoach %s\n", deviceId.c_str());
  f.println("Model:,ESP32 TMcoach-Compatible Export");
  f.printf("Serial:,%s\n", deviceId.c_str());
  f.printf("Firmware Version:,%s\n", FIRMWARE_VERSION);
  f.printf("Build:,%s\n", FIRMWARE_BUILD_STAMP);
  f.println();

  f.println("Per-Stroke Data:");
  f.println(
      "Interval (Interval),Distance (RTK) (Meters),Distance (IMP) (Meters),"
      "Elapsed Time (HH:MM:SS.tenths),Split (RTK) (/500),Speed (RTK) (M/S),"
      "Split (IMP) (/500),Speed (IMP) (M/S),Stroke Rate (SPM),"
      "Total Strokes (Strokes),Distance/Stroke (RTK) (Meters),"
      "Distance/Stroke (IMP) (Meters),Heart Rate (BPM),Power (Watts),"
      "Catch (Degrees),Slip (Degrees),Finish (Degrees),Wash (Degrees),"
      "Force Avg (Newtons),Work (Joules),Force Max (Newtons),"
      "Max Force Angle (Degrees),RTK Lat. (Degrees),RTK Lon. (Degrees),"
      "Timestamp,DeltaTime (Seconds)");
}

} // namespace

SDCardManager::SDCardManager()
    : cardMounted(false), disabled(false), consecutiveErrors(0),
      trainingCounter(0), currentLogFile(""), trainingJsonPath(""),
      strokeCsvPath(""), legacyStrokeCsvPath(""), nmeaCsvPath(""), debugPath(""),
      currentSessionFolder(""), currentTrainId(""), lastStrokeCsvTimestamp_(""),
      lastStrokeSysTimestamp_(0),
      lastErrorTime(0),
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

  // 寮傛IMU鍐欏叆鏀寔
  if (!imuQueue) {
    imuQueue = xQueueCreate(IMU_QUEUE_LEN, sizeof(ImuSample));
    if (!imuQueue) {
      Serial.println("[SD] Failed to create IMU queue; fallback to sync writes");
    }
  }
  if (imuQueue && !imuTaskHandle) {
    BaseType_t res = xTaskCreatePinnedToCore(imuLogTask, "IMU_SD_Writer",
                                             8192, this, 1, &imuTaskHandle, 1);
    if (res != pdPASS) {
      Serial.println("[SD] Failed to create IMU writer task; fallback to sync writes");
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

  // 寮傛璺緞锛氬揩閫熷叆闃燂紝闃熷垪婊℃椂鐩存帴涓㈠純锛岀粷涓嶉樆濉炰富寰幆
  if (imuLoggingEnabled && imuQueue) {
    ImuSample sample{(uint32_t)timestamp, ax, ay, az};
    if (xQueueSend(imuQueue, &sample, 0) != pdTRUE) {
      imuDropCount++;
    }
    return;
  }

  // 鍥為€€鍚屾鍐欏叆锛堜笉浼氬皾璇昮lush锛屽敖閲忓噺灏戦樆濉烇級
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
  lastStrokeCsvTimestamp_ = "";
  lastStrokeSysTimestamp_ = 0;

  String safeTrainId = trainId;
  if (safeTrainId.isEmpty()) {
    safeTrainId = String(millis());
  }

  int nextNumber = getNextTrainingNumber();
  String exportDeviceId = deviceIdForExport();
  String exportDate = tsDateToken(startTimestamp);
  String exportTime = tsTimeTokenAmPm(startTimestamp);
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

  // IMU file name includes deviceId + date + time
  String imuFileName = "imu_log_" + String(nextNumber) + "_" + exportDeviceId +
                       "_" + exportDate + "_" + exportTime + ".csv";
  currentLogFile = currentSessionFolder + "/" + imuFileName;

  imuFile = SD_MMC.open(currentLogFile.c_str(), FILE_WRITE);
  if (!imuFile) {
    Serial.printf("[SD] 鉂?Failed to open IMU log file: %s\n",
                  currentLogFile.c_str());
    currentLogFile = "";
    return;
  }
  Serial.printf("[SD] 鉁?Opened IMU log file: %s\n", currentLogFile.c_str());

  if (imuFile.size() == 0 || imuFile.position() == 0) {
    imuFile.println("sys_ms,acc_x,acc_y,acc_z");
    imuFile.flush();
  }

  trainingJsonPath = "";
  trainingJsonFile = File();

  String strokeFileName =
      "TMcoach " + exportDeviceId + " " + exportDate + " " + exportTime + ".csv";
  strokeCsvPath = currentSessionFolder + "/" + strokeFileName;
  if (SD_MMC.exists(strokeCsvPath.c_str())) {
    SD_MMC.remove(strokeCsvPath.c_str());
  }
  strokeCsvFile = SD_MMC.open(strokeCsvPath.c_str(), FILE_WRITE);
  if (strokeCsvFile) {
    writeStrokeCsvPreamble(strokeCsvFile, exportDeviceId, startTimestamp);
    strokeCsvFile.flush();
  }

  legacyStrokeCsvPath = currentSessionFolder + "/strokes.csv";
  if (SD_MMC.exists(legacyStrokeCsvPath.c_str())) {
    SD_MMC.remove(legacyStrokeCsvPath.c_str());
  }
  legacyStrokeCsvFile = SD_MMC.open(legacyStrokeCsvPath.c_str(), FILE_WRITE);
  if (legacyStrokeCsvFile) {
    legacyStrokeCsvFile.println(
        "BoatCode,StrokeLength(m),TotalDistance(m),ElapsedTime,Pace(/500m),"
        "Speed(m/s),StrokeRate(spm),StrokeCount,Lat,Lon,Timestamp");
    legacyStrokeCsvFile.flush();
  }

  // 鍒涘缓 NMEA 鍘熷鏁版嵁鏂囦欢锛圕SV鏍煎紡锛?
  String gnssFileName = "gnss_" + String(nextNumber) + "_" + exportDeviceId +
                        "_" + exportDate + "_" + exportTime + ".csv";
  nmeaCsvPath = currentSessionFolder + "/" + gnssFileName;
  nmeaCsvFile = SD_MMC.open(nmeaCsvPath.c_str(), FILE_WRITE);

  if (nmeaCsvFile) {
    Serial.printf("[SD] 鉁?Opened GNSS file: %s\n", nmeaCsvPath.c_str());
    // 浣跨敤position()鑰屼笉鏄痵ize()锛屾洿鍙潬鍦版娴嬫柊鏂囦欢
    if (nmeaCsvFile.position() == 0) {
      nmeaCsvFile.println("sys_ms,nmea_ms,nmea");
      nmeaCsvFile.flush();
      Serial.println("[SD] 鉁?GNSS CSV header written");
    } else {
      Serial.printf("[SD] 鈩癸笍 GNSS file exists, position=%d\n",
                    nmeaCsvFile.position());
    }
  } else {
    Serial.printf("[SD] 鉂?Failed to open GNSS file: %s\n",
                  nmeaCsvPath.c_str());
  }

  currentTrainId = safeTrainId;
  lastFlushTime = millis();
  consecutiveErrors = 0;
  lastNmeaMs_ = -1; // 閲嶇疆UTC缂撳瓨

  // 閲嶆柊鍚敤IMU寮傛鍐欏叆
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
    if (legacyStrokeCsvFile)
      legacyStrokeCsvFile.flush();
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
  if (!strokeCsvFile && !legacyStrokeCsvFile) {
    return;
  }

  float deltaTimeSec = 0.0f;
  if (snapshot.strokeIntervalSec > 0.0f) {
    deltaTimeSec = snapshot.strokeIntervalSec;
  } else if (lastStrokeSysTimestamp_ > 0 && snapshot.timestamp > lastStrokeSysTimestamp_) {
    deltaTimeSec = (snapshot.timestamp - lastStrokeSysTimestamp_) / 1000.0f;
  } else if (!lastStrokeCsvTimestamp_.isEmpty() && !snapshot.captureTime.isEmpty()) {
    deltaTimeSec =
        calculateTimeDifference(lastStrokeCsvTimestamp_, snapshot.captureTime);
  }
  if (deltaTimeSec < 0.0f) {
    deltaTimeSec = 0.0f;
  }

  // 璁粌鏃堕暱鐩存帴杈撳嚭涓虹鏁版牸寮忥紙淇濈暀1浣嶅皬鏁帮級锛屼緥濡? 29.1
  uint32_t elapsedMs = (snapshot.elapsedSeconds > 0.0f)
                           ? (uint32_t)(snapshot.elapsedSeconds * 1000.0f + 0.5f)
                           : 0;
  String elapsed = formatHhmmssTenths(elapsedMs);

  // 銆愯皟璇曘€戞墦鍗癈SV杈撳嚭鐨則d鍊?
  Serial.printf("[CSV] 妗ㄦ%d锛宔lapsedSeconds=%.3f 鈫?CSV杈撳嚭=%s\n",
                snapshot.strokeNumber, snapshot.elapsedSeconds,
                elapsed.c_str());

  String split = (snapshot.speed > 0.05f) ? formatSplit(500.0f / snapshot.speed)
                                          : String("--");
  String splitImp = "00:00.0";

  char line[768];
  snprintf(line, sizeof(line),
           "1,%.2f,0,%s,%s,%.2f,%s,0,%.1f,%d,%.2f,0,---,%.1f,---,---,---,---,"
           "---,---,---,---,%.7f,%.7f,%s,%.3f",
           snapshot.totalDistance, elapsed.c_str(), split.c_str(),
           snapshot.speed, splitImp.c_str(), snapshot.strokeRate,
           snapshot.strokeNumber, snapshot.strokeLength, snapshot.power,
           snapshot.lat, snapshot.lon, snapshot.captureTime.c_str(),
           deltaTimeSec);
  strokeCsvFile.println(line);

  if (legacyStrokeCsvFile) {
    String elapsedSimple = String(snapshot.elapsedSeconds, 1);
    char legacyLine[512];
    snprintf(legacyLine, sizeof(legacyLine),
             "%s,%.2f,%.2f,%s,%s,%.2f,%.1f,%d,%.7f,%.7f,%s",
             snapshot.boatCode.c_str(), snapshot.strokeLength,
             snapshot.totalDistance, elapsedSimple.c_str(), split.c_str(),
             snapshot.speed, snapshot.strokeRate, snapshot.strokeNumber,
             snapshot.lat, snapshot.lon, snapshot.captureTime.c_str());
    legacyStrokeCsvFile.println(legacyLine);
  }

  if (!snapshot.captureTime.isEmpty()) {
    lastStrokeCsvTimestamp_ = snapshot.captureTime;
  }
  if (snapshot.timestamp > 0) {
    lastStrokeSysTimestamp_ = snapshot.timestamp;
  }
}

void SDCardManager::logNmeaRaw(const String &nmea) {
  static unsigned long nmeaCount = 0;
  static unsigned long lastDebugPrint = 0;

  nmeaCount++;

  // 姣?0绉掓墦鍗颁竴娆＄粺璁′俊鎭?
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

  // 浣跨敤millis()浣滀负绯荤粺鏃堕棿鎴筹紱鍚屾椂瑙ｆ瀽NMEA鑷甫UTC锛堝鏋滄湁锛夛紝杞负姣
  unsigned long sys_ms = millis();
  long nmea_ms = -1;
  // 浠呭鍚?UTC 鐨勫彞瀛?GGA/RMC/GLL)瑙ｆ瀽鏃堕棿锛沄TG 涓嶈В鏋愪笖涓嶅埛鏂?lastNmeaMs_
  bool hasTimeField = false;
  if (nmea.startsWith("$GNGGA") || nmea.startsWith("$GPGGA") ||
      nmea.startsWith("$GNRMC") || nmea.startsWith("$GPRMC") ||
      nmea.startsWith("$GNGLL") || nmea.startsWith("$GPGLL")) {
    int firstComma = nmea.indexOf(',');
    if (firstComma > 0 && nmea.length() >= firstComma + 7) {
      String timeField = nmea.substring(firstComma + 1, firstComma + 11); // hhmmss.ss
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
  // 鑻ユ湰琛屾棤鏃堕棿锛屽皾璇曞鐢ㄤ笂涓€ UTC锛堜笉閫掑锛岄伩鍏嶈櫄鍋?00Hz锛?
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

  // 瀹氭湡 flush锛屼笌 stroke 鏂囦欢涓€璧?
  unsigned long now = millis();
  if (now - lastFlushTime > FLUSH_INTERVAL_MS) {
    if (nmeaCsvFile)
      nmeaCsvFile.flush();
    lastFlushTime = now; // 鏇存柊 flush 鏃堕棿锛岄伩鍏嶉绻佽惤鐩?
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
    if (legacyStrokeCsvFile)
      legacyStrokeCsvFile.flush();
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
  if (legacyStrokeCsvFile) {
    legacyStrokeCsvFile.flush();
    legacyStrokeCsvFile.close();
    legacyStrokeCsvFile = File();
  }
  if (nmeaCsvFile) {
    nmeaCsvFile.flush();
    nmeaCsvFile.close();
  }
  nmeaCsvFile = File();

  // 鍏抽棴璋冭瘯鏃ュ織鏂囦欢
  if (debugFile) {
    debugFile.println("=== Debug Log Ended ===");
    debugFile.close();
  }
  debugFile = File();

  currentLogFile = "";
  trainingJsonPath = "";
  trainingJsonFile = File();
  strokeCsvPath = "";
  legacyStrokeCsvPath = "";
  nmeaCsvPath = "";
  currentSessionFolder = "";
  currentTrainId = "";
  lastStrokeCsvTimestamp_ = "";
  lastStrokeSysTimestamp_ = 0;
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
    // 鏌ユ壘鏂囦欢澶硅€屼笉鏄疌SV鏂囦欢锛屽洜涓虹幇鍦↖MU鏂囦欢鍦ㄦ枃浠跺す閲?
    if (file.isDirectory() && name.startsWith("imu_log_")) {
      // 鎻愬彇缂栧彿锛歩mu_log_026_trainId -> 026
      int startIdx = name.indexOf("_") + 1;       // 璺宠繃 "imu"
      startIdx = name.indexOf("_", startIdx) + 1; // 璺宠繃 "log"
      int endIdx = name.indexOf("_", startIdx);   // 鎵惧埌璁粌ID鍓嶇殑涓嬪垝绾?

      if (endIdx < 0) {
        endIdx = name.length(); // 濡傛灉娌℃湁涓嬪垝绾匡紙涓嶅簲璇ュ彂鐢燂級锛岀敤鏁翠釜瀛楃涓?
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
  // 璋冭瘯鏃ュ織宸茬鐢?
  (void)msg;
  return;
}

// ===================== IMU 寮傛鍐欏叆浠诲姟 =====================
void SDCardManager::imuLogTask(void *param) {
  SDCardManager *self = static_cast<SDCardManager *>(param);
  static char buffer[2048];
  int bufLen = 0;
  int batchCount = 0;
  TickType_t lastFlush = xTaskGetTickCount();
  TickType_t lastDropPrint = lastFlush;

  while (true) {
    ImuSample sample;
    bool got = xQueueReceive(self->imuQueue, &sample, pdMS_TO_TICKS(100)) == pdTRUE;

    // 濡傛灉琚鐢ㄦ垨鏂囦欢涓嶅彲鐢紝鐩存帴璺宠繃
    if (self->disabled || !self->imuFile) {
      // nothing
    } else if (got) {
      int written = snprintf(buffer + bufLen, sizeof(buffer) - bufLen,
                             "%lu,%.4f,%.4f,%.4f\n",
                             (unsigned long)sample.ts_ms, sample.ax, sample.ay,
                             sample.az);
      if (written > 0 && written < (int)(sizeof(buffer) - bufLen)) {
        bufLen += written;
        batchCount++;
      }
    }

    bool timeFlush = (xTaskGetTickCount() - lastFlush) >= pdMS_TO_TICKS(FLUSH_INTERVAL_MS);
    bool needFlush = (bufLen > 1800) || (batchCount >= IMU_BATCH_SIZE) || (timeFlush && bufLen > 0);

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



