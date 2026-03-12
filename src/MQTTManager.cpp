#include "MQTTManager.h"
#include "BluetoothManager.h"
#include "CellularManager.h"
#include "ConfigManager.h"
#include "DataFlowManager.h"
#include "GNSSProcessor.h"
#include "IMUManager.h"
#include "PowerManager.h"
#include "SDCardManager.h"
#include "TrainingMode.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <lvgl.h>
#include <time.h>
#include <ui.h>

static String reusableJsonBuffer;
static bool jsonBufferInitialized = false;

static String lastValidTimestamp = "";
static unsigned long lastValidTimeMillis = 0;

class TaskWdtSuspendGuard {
public:
  TaskWdtSuspendGuard(const char *tag, unsigned long warnMs = 4000)
      : guardName(tag), warnThresholdMs(warnMs) {
    taskHandle = xTaskGetCurrentTaskHandle();
    startMs = millis();
    if (taskHandle) {
      if (esp_task_wdt_delete(taskHandle) == ESP_OK) {
        suspended = true;
      }
    }
  }
  ~TaskWdtSuspendGuard() { resume(); }
  void resume() {
    if (suspended && taskHandle) {
      esp_task_wdt_add(taskHandle);
      esp_task_wdt_reset();
      unsigned long duration = millis() - startMs;
      if (guardName && duration > warnThresholdMs) {
        Serial.printf("[WDT] %s blocked %lu ms\n", guardName, duration);
      }
      suspended = false;
    }
  }

private:
  TaskHandle_t taskHandle = nullptr;
  bool suspended = false;
  const char *guardName = nullptr;
  unsigned long startMs = 0;
  unsigned long warnThresholdMs = 0;
};
void initJsonBuffer() {
  if (!jsonBufferInitialized) {
    reusableJsonBuffer.reserve(600);
    jsonBufferInitialized = true;
  }
}

String getValidTimestamp() {
  String currentTime;
  currentTime.reserve(24);

  if (rtcInitialized && rtcTimeSynced) {
    currentTime = getRTCFullDateTime();
  }

  if (currentTime.isEmpty() || currentTime.length() < 19) {
    currentTime = configManager.getCurrentFormattedDateTime();
  }

  bool timeValid = false;
  if (currentTime.length() >= 19) {

    String yearStr = currentTime.substring(0, 4);
    int year = yearStr.toInt();

    if (year >= 2025) {
      timeValid = true;
    }
  }

  if (!timeValid && !lastValidTimestamp.isEmpty()) {
    unsigned long elapsedMs = millis() - lastValidTimeMillis;
    unsigned long elapsedSec = elapsedMs / 1000;

    int year = lastValidTimestamp.substring(0, 4).toInt();
    int month = lastValidTimestamp.substring(5, 7).toInt();
    int day = lastValidTimestamp.substring(8, 10).toInt();
    int hour = lastValidTimestamp.substring(11, 13).toInt();
    int minute = lastValidTimestamp.substring(14, 16).toInt();
    int second = lastValidTimestamp.substring(17, 19).toInt();

    second += elapsedSec;
    if (second >= 60) {
      minute += second / 60;
      second = second % 60;
    }
    if (minute >= 60) {
      hour += minute / 60;
      minute = minute % 60;
    }
    if (hour >= 24) {
      day += hour / 24;
      hour = hour % 24;
    }

    char timeStr[20];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d", year,
             month, day, hour, minute, second);
    currentTime = String(timeStr);
    timeValid = true;
  }

  if (timeValid) {
    lastValidTimestamp = currentTime;
    lastValidTimeMillis = millis();
  } else {

    Serial.println("[TIME] ⚠️ 无有效时间源，请检查RTC和4G模块");

    unsigned long totalMs = millis();
    unsigned long seconds = totalMs / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    seconds = seconds % 60;
    minutes = minutes % 60;
    hours = hours % 24;
    char timeStr[20];
    snprintf(timeStr, sizeof(timeStr), "1970-01-01 %02lu:%02lu:%02lu", hours,
             minutes, seconds);
    currentTime = String(timeStr);
  }

  if (currentTime.length() == 19) {
    unsigned long ms = millis() % 1000;
    char msStr[5];
    snprintf(msStr, sizeof(msStr), ".%03lu", ms);
    currentTime += msStr;
  }

  return currentTime;
}

unsigned long parseTimestampToSeconds(const String &timestamp) {
  if (timestamp.length() < 19) {
    Serial.printf(
        "[TIME] parseTimestampToSeconds: 时间戳格式不正确 (长度=%d)\n",
        timestamp.length());
    return 0;
  }

  int year = timestamp.substring(0, 4).toInt();
  int month = timestamp.substring(5, 7).toInt();
  int day = timestamp.substring(8, 10).toInt();
  int hour = timestamp.substring(11, 13).toInt();
  int minute = timestamp.substring(14, 16).toInt();
  int second = timestamp.substring(17, 19).toInt();

  // 解析毫秒部分（如果存在）
  float milliseconds = 0.0f;
  if (timestamp.length() >= 23 && timestamp.charAt(19) == '.') {
    String msStr = timestamp.substring(20, 23);
    milliseconds = msStr.toInt() / 1000.0f; // 转换为秒的小数部分
  }

  // 使用 struct tm 和 mktime 转换为 Unix 时间戳
  struct tm timeinfo = {0};
  timeinfo.tm_year = year - 1900; // tm_year 是从 1900 年开始的
  timeinfo.tm_mon = month - 1;    // tm_mon 是 0-11
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hour;
  timeinfo.tm_min = minute;
  timeinfo.tm_sec = second;
  timeinfo.tm_isdst = -1; // 让 mktime 自动判断夏令时

  time_t timestamp_sec = mktime(&timeinfo);

  if (timestamp_sec == -1) {
    Serial.printf("[TIME] parseTimestampToSeconds: mktime 转换失败 (%s)\n",
                  timestamp.c_str());
    return 0;
  }
  // 返回秒数
  return (unsigned long)timestamp_sec;
}

// 计算两个时间戳之间的时间差
float calculateTimeDifference(const String &timestamp1,
                              const String &timestamp2) {
  if (timestamp1.length() < 19 || timestamp2.length() < 19) {
    Serial.printf("[TIME] calculateTimeDifference: 时间戳格式错误\n");
    return 0.0f;
  }

  // 解析 timestamp1
  int y1 = timestamp1.substring(0, 4).toInt();
  int m1 = timestamp1.substring(5, 7).toInt();
  int d1 = timestamp1.substring(8, 10).toInt();
  int h1 = timestamp1.substring(11, 13).toInt();
  int min1 = timestamp1.substring(14, 16).toInt();
  int s1 = timestamp1.substring(17, 19).toInt();
  int ms1 = 0;
  if (timestamp1.length() >= 23 && timestamp1.charAt(19) == '.') {
    ms1 = timestamp1.substring(20, 23).toInt();
  }

  // 解析 timestamp2
  int y2 = timestamp2.substring(0, 4).toInt();
  int m2 = timestamp2.substring(5, 7).toInt();
  int d2 = timestamp2.substring(8, 10).toInt();
  int h2 = timestamp2.substring(11, 13).toInt();
  int min2 = timestamp2.substring(14, 16).toInt();
  int s2 = timestamp2.substring(17, 19).toInt();
  int ms2 = 0;
  if (timestamp2.length() >= 23 && timestamp2.charAt(19) == '.') {
    ms2 = timestamp2.substring(20, 23).toInt();
  }

  // 使用 mktime 计算日期部分差异
  struct tm tm1 = {0}, tm2 = {0};
  tm1.tm_year = y1 - 1900;
  tm1.tm_mon = m1 - 1;
  tm1.tm_mday = d1;
  tm1.tm_hour = h1;
  tm1.tm_min = min1;
  tm1.tm_sec = s1;
  tm1.tm_isdst = -1;

  tm2.tm_year = y2 - 1900;
  tm2.tm_mon = m2 - 1;
  tm2.tm_mday = d2;
  tm2.tm_hour = h2;
  tm2.tm_min = min2;
  tm2.tm_sec = s2;
  tm2.tm_isdst = -1;

  time_t t1 = mktime(&tm1);
  time_t t2 = mktime(&tm2);

  if (t1 == -1 || t2 == -1) {
    Serial.printf("[TIME] mktime 转换失败\n");
    return 0.0f;
  }

  // 计算差值（秒）+ 毫秒差
  double diff_sec = difftime(t2, t1);
  double diff_ms = (ms2 - ms1) / 1000.0;
  float result = (float)(diff_sec + diff_ms);

  return result;
}

#include <Arduino.h>
#include <functional>

extern float strokeRate;
extern float speedMps;
extern float totalDistance;
extern int strokeCount;
extern float strokeLength;
extern TrainingMode training;
extern GNSSProcessor gnss;
extern IMUManager imu;
extern CellularManager cellular;
extern PowerManager powerMgr;
extern ConfigManager configManager;
extern DataFlowManager dataFlow;
extern SDCardManager sdCardManager;
extern bool isMQTTConnected;
extern SemaphoreHandle_t serial4GMutex;
extern bool configLoadingInProgress;

String convertToBeijingTime(const String &utcTimeStr) {

  int commaIndex = utcTimeStr.indexOf(',');
  if (commaIndex == -1)
    return utcTimeStr;

  String datePart = utcTimeStr.substring(0, commaIndex);
  String timePart = utcTimeStr.substring(commaIndex + 1);

  int hour = timePart.substring(0, 2).toInt();
  int minute = timePart.substring(3, 5).toInt();
  int second = timePart.substring(6, 8).toInt();

  hour += 8;

  bool nextDay = false;
  if (hour >= 24) {
    hour -= 24;
    nextDay = true;
  }

  char newTimeStr[32];
  if (nextDay) {

    int day = datePart.substring(0, 2).toInt() + 1;
    String month = datePart.substring(3, 5);
    String year = datePart.substring(6, 8);
    snprintf(newTimeStr, sizeof(newTimeStr), "%02d/%s/%s,%02d:%02d:%02d+32",
             day, month.c_str(), year.c_str(), hour, minute, second);
  } else {
    snprintf(newTimeStr, sizeof(newTimeStr), "%s,%02d:%02d:%02d+32",
             datePart.c_str(), hour, minute, second);
  }

  return String(newTimeStr);
}

String convertQLTSToDateTime(const String &qltsTimeStr) {

  int commaIdx = qltsTimeStr.indexOf(',');
  if (commaIdx == -1)
    return "";

  String datePart = qltsTimeStr.substring(0, commaIdx);
  String timePart = qltsTimeStr.substring(commaIdx + 1);

  int tzOffsetQuarters = 0;
  int plusIdx = timePart.indexOf('+');
  int minusIdx = timePart.indexOf('-');

  if (plusIdx > 0 || minusIdx > 0) {

    int tzIdx = (plusIdx > 0) ? plusIdx : minusIdx;
    String tzStr = timePart.substring(tzIdx);
    int tzSign = (plusIdx > 0) ? 1 : -1;

    int commaIdx2 = tzStr.indexOf(',');
    if (commaIdx2 > 0) {
      tzOffsetQuarters = tzStr.substring(1, commaIdx2).toInt() * tzSign;
    }

    timePart = timePart.substring(0, tzIdx);
  }

  int slash1 = datePart.indexOf('/');
  int slash2 = datePart.lastIndexOf('/');
  if (slash1 == -1 || slash2 == -1 || slash1 == slash2)
    return "";

  int year = datePart.substring(0, slash1).toInt();
  int month = datePart.substring(slash1 + 1, slash2).toInt();
  int day = datePart.substring(slash2 + 1).toInt();

  if (year < 100) {
    year += 2000;
  }

  if (timePart.length() < 8)
    return "";
  int hour = timePart.substring(0, 2).toInt();
  int minute = timePart.substring(3, 5).toInt();
  int second = timePart.substring(6, 8).toInt();

  if (tzOffsetQuarters != 0) {

    hour += (tzOffsetQuarters * 15) / 60;
    minute += (tzOffsetQuarters * 15) % 60;

    if (minute >= 60) {
      hour += minute / 60;
      minute = minute % 60;
    }
    if (hour >= 24) {
      day += hour / 24;
      hour = hour % 24;

      if (day > 31) {
        day = 1;
        month++;
        if (month > 12) {
          month = 1;
          year++;
        }
      }
    }
  }

  char timeStr[20];
  snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d", year,
           month, day, hour, minute, second);

  return String(timeStr);
}

static float roundSpeedForUpload(float speed) {
  return roundf(speed * 100.0f) / 100.0f;
}
static constexpr float kMinSpeedForPace = 0.05f;
static float paceFromRoundedSpeed(float roundedSpeed) {
  if (roundedSpeed <= kMinSpeedForPace) {
    return 0.0f;
  }
  return 500.0f / roundedSpeed;
}
String generateTrainingDataJSON(int customStrokeCount) {

  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  String boatCode = deviceConfig.isValid ? deviceConfig.boatCode : "01";

  String currentTime = getValidTimestamp();

  String trainId = training.getTrainId();
  double latitude = gnss.getLatitude();
  double longitude = gnss.getLongitude();
  double strokeLat = imu.getLastStrokeEndLatitude();
  double strokeLon = imu.getLastStrokeEndLongitude();

  // 如果获取到了有效的划桨位置（非0），优先使用划桨位置
  if (strokeLat != 0.0 || strokeLon != 0.0) {
    latitude = strokeLat;
    longitude = strokeLon;
  }
  float rawSpeed = gnss.getSpeed();
  float roundedSpeed = roundSpeedForUpload(rawSpeed);
  float paceValue = paceFromRoundedSpeed(roundedSpeed);
  // 直接使用全局 strokeLength (已由 IMUManager/StrokeMetrics 更新)
  float strokeLengthForUpload = strokeLength;
  int strokeCounter = customStrokeCount >= 0 ? customStrokeCount : strokeCount;

  // 强制第一桨桨频为0，与SD卡/StrokeSnapshot逻辑一致
  float strokeRateForUpload = strokeRate;
  if (strokeCounter == 1) {
    strokeRateForUpload = 0.0f;
  }

  initJsonBuffer();
  reusableJsonBuffer.clear();

  reusableJsonBuffer = "{\"ts\":\"";
  reusableJsonBuffer += currentTime;
  reusableJsonBuffer += "\",\"trainId\":\"";
  reusableJsonBuffer += trainId;
  reusableJsonBuffer += "\",\"boatCode\":\"";
  reusableJsonBuffer += boatCode;
  reusableJsonBuffer += "\",\"lat\":";
  reusableJsonBuffer += String(latitude, 7);
  reusableJsonBuffer += ",\"lon\":";
  reusableJsonBuffer += String(longitude, 7);
  reusableJsonBuffer += ",\"speed\":";
  reusableJsonBuffer += String(roundedSpeed, 2);
  reusableJsonBuffer += ",\"power\":";
  reusableJsonBuffer += String(gnss.getPower(), 2);
  reusableJsonBuffer += ",\"pace\":";
  reusableJsonBuffer += String(paceValue, 1);
  reusableJsonBuffer += ",\"strokeRate\":";
  reusableJsonBuffer += String(strokeRateForUpload, 1);
  reusableJsonBuffer += ",\"strokeCount\":";
  reusableJsonBuffer += String(strokeCounter);
  reusableJsonBuffer += ",\"strokeLength\":";
  reusableJsonBuffer += String(strokeLengthForUpload, 2);
  reusableJsonBuffer += ",\"distance\":";
  reusableJsonBuffer += String(totalDistance, 2);
  reusableJsonBuffer += ",\"td\":";

  // 计算td：使用时间戳差值而非millis
  String startTS = training.getTrainingStartTimestamp();
  float td = 0.0f;
  if (!startTS.isEmpty() && currentTime.length() >= 19) {
    extern float calculateTimeDifference(const String &, const String &);
    float elapsedRaw = calculateTimeDifference(startTS, currentTime);
    if (elapsedRaw > 0.0f) {
      td = elapsedRaw - (float)training.getTotalPausedSeconds();
    }
  }

  reusableJsonBuffer += String(td, 1);
  reusableJsonBuffer += "}";

  TrainingSample sample;
  sample.timestamp = currentTime;
  sample.lat = latitude;
  sample.lon = longitude;
  sample.speed = roundedSpeed;
  sample.pace = paceValue;
  sample.strokeRate = strokeRateForUpload;
  sample.strokeCount = strokeCounter;
  sample.strokeLength = strokeLengthForUpload;
  sample.totalDistance = totalDistance;
  sample.elapsedSeconds = td;
  sdCardManager.logTrainingSample(sample, reusableJsonBuffer);

  return reusableJsonBuffer;
}

String generateStrokeJSON(StrokeSnapshot snapshot) {

  String currentTime = snapshot.captureTime;

  if (currentTime.isEmpty()) {
    currentTime = getValidTimestamp();
  }

  normalizeStrokeSnapshot(snapshot);

  initJsonBuffer();
  reusableJsonBuffer.clear();

  reusableJsonBuffer = "{\"ts\":\"";
  reusableJsonBuffer += currentTime;
  reusableJsonBuffer += "\",\"trainId\":\"";
  reusableJsonBuffer += snapshot.trainId;
  reusableJsonBuffer += "\",\"boatCode\":\"";
  reusableJsonBuffer += snapshot.boatCode;
  reusableJsonBuffer += "\",\"lat\":";
  reusableJsonBuffer += String(snapshot.lat, 7);
  reusableJsonBuffer += ",\"lon\":";
  reusableJsonBuffer += String(snapshot.lon, 7);
  reusableJsonBuffer += ",\"speed\":";
  float roundedSpeed = roundSpeedForUpload(snapshot.speed);
  reusableJsonBuffer += String(roundedSpeed, 2);
  reusableJsonBuffer += ",\"power\":";
  reusableJsonBuffer += String(snapshot.power, 2);
  reusableJsonBuffer += ",\"pace\":";
  reusableJsonBuffer += String(paceFromRoundedSpeed(roundedSpeed), 1);
  reusableJsonBuffer += ",\"strokeRate\":";
  reusableJsonBuffer += String(snapshot.strokeRate, 1);
  reusableJsonBuffer += ",\"strokeCount\":";
  reusableJsonBuffer += String(snapshot.strokeNumber);
  reusableJsonBuffer += ",\"strokeLength\":";
  reusableJsonBuffer += String(snapshot.strokeLength, 2);
  reusableJsonBuffer += ",\"distance\":";
  reusableJsonBuffer += String(snapshot.totalDistance, 2);
  reusableJsonBuffer += ",\"td\":";
  reusableJsonBuffer += String(snapshot.elapsedSeconds, 3);
  reusableJsonBuffer += "}";

  return reusableJsonBuffer;
}

String buildStatusPayload() {

  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  String boatCode = deviceConfig.isValid ? deviceConfig.boatCode : "01";

  String currentTime = getValidTimestamp();

  String deviceIMEI = configManager.getDeviceIMEI();
  if (deviceIMEI.isEmpty() || deviceIMEI == "null") {
    deviceIMEI = "000000000000000";
  }

  String stdId = deviceConfig.isValid ? deviceConfig.stdId : "";

  initJsonBuffer();
  reusableJsonBuffer.clear();

  reusableJsonBuffer = "{\"ts\":\"";
  reusableJsonBuffer += currentTime;
  reusableJsonBuffer += "\",\"boatCode\":\"";
  reusableJsonBuffer += boatCode;
  reusableJsonBuffer += "\",\"deviceCode\":\"";
  reusableJsonBuffer += deviceIMEI;
  reusableJsonBuffer += "\",\"stdId\":\"";
  reusableJsonBuffer += stdId;
  reusableJsonBuffer += "\",\"hdop\":";
  String hdopValue = gnss.getHDOP();
  if (hdopValue.isEmpty() || hdopValue == "--")
    hdopValue = "0.0";
  reusableJsonBuffer += hdopValue;
  reusableJsonBuffer += ",\"usat\":";
  reusableJsonBuffer += String(gnss.getSolvingSatellites());
  reusableJsonBuffer += ",\"vsat\":";
  reusableJsonBuffer += String(gnss.getVisibleSatellites());
  reusableJsonBuffer += ",\"ageDiff\":";
  String ageDiffValue = gnss.getDiffAge();
  if (ageDiffValue.isEmpty() || ageDiffValue == "--")
    ageDiffValue = "0";
  reusableJsonBuffer += ageDiffValue;
  reusableJsonBuffer += ",\"fix\":\"";
  String fixValue = gnss.getFixStatus();
  if (fixValue.isEmpty())
    fixValue = "--";
  reusableJsonBuffer += fixValue;
  reusableJsonBuffer += "\",\"rsrp\":";
  int signalDbm = cellular.getSignaldBm();
  reusableJsonBuffer += String(signalDbm);
  reusableJsonBuffer += ",\"soc\":";
  reusableJsonBuffer += String(powerMgr.getBatteryPercent());
  reusableJsonBuffer += ",\"status\":";
  reusableJsonBuffer += training.isActive() ? "1" : "0";
  reusableJsonBuffer += "}";

  return reusableJsonBuffer;
}

String buildHeartRatePayload() {

  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  String boatCode = deviceConfig.isValid ? deviceConfig.boatCode : "01";

  String currentTime = getValidTimestamp();

  String trainId = training.getTrainId();

  initJsonBuffer();
  reusableJsonBuffer.clear();

  reusableJsonBuffer = "{\"ts\":\"";
  reusableJsonBuffer += currentTime;
  reusableJsonBuffer += "\",\"trainId\":\"";
  reusableJsonBuffer += trainId;
  reusableJsonBuffer += "\",\"boatCode\":\"";
  reusableJsonBuffer += boatCode;
  reusableJsonBuffer += "\",\"hr\":[";

  bool firstDevice = true;
  for (int i = 0; i < BT::getFoundDeviceCount(); i++) {
    auto &device = BT::devices()[i];
    if (device.connected && device.lastHeartRate > 0) {
      if (!firstDevice)
        reusableJsonBuffer += ",";

      String deviceCode, rowerName;
      String rowerId = "000";

      if (configManager.getDeviceInfoByAddress(String(device.address),
                                               deviceCode, rowerName)) {

        const auto &rowerList = configManager.getRowerList();
        for (const auto &rower : rowerList) {
          if (!rower.btAddr.isEmpty() &&
              rower.btAddr.equalsIgnoreCase(String(device.address))) {

            if (!rower.rowerId.isEmpty()) {
              rowerId = rower.rowerId;
            }
            break;
          }
        }
      }

      String heartRate = "0";
      if (device.lastHeartRate > 0 && device.lastHeartRate < 300) {
        heartRate = String(device.lastHeartRate);
      }

      String battery = "0";
      if (device.batteryLevel > 0 && device.batteryLevel <= 100) {
        battery = String(device.batteryLevel);
      }

      reusableJsonBuffer +=
          "\"" + rowerId + "|" + heartRate + "|" + battery + "\"";
      firstDevice = false;
    }
  }

  reusableJsonBuffer += "]}";

  return reusableJsonBuffer;
}

void publishStrokeEvent() {

  extern TrainingMode training;
  if (!training.isActive()) {
    return;
  }

  if (!hybridPppConnected || currentNetwork != NETWORK_4G) {
    return;
  }

  String payload = generateTrainingDataJSON(-1);

  if (!payload.isEmpty() && !topicTrainStroke.isEmpty()) {

    publishHybridSocketMQTT(topicTrainStroke, payload, 1);
  }
}

const char *apn = "3GNET";

String deviceIMEI = "";

String mqtt_server;
int mqtt_port;
String mqtt_user;
String mqtt_pass;

String topicTrainStroke;
String topicStatus;
String topicHR;

NetworkType currentNetwork = NETWORK_NONE;

HardwareSerial Serial4G(1);

TinyGsm *hybridModem = nullptr;
TinyGsmClient *hybridHttpClient = nullptr;
MQTTClient *hybridMqttClient = nullptr;
TinyGsmClient *mcuTcpClient = nullptr;
static bool gMqttConnectBusy = false;
bool hybridPppConnected = false;
unsigned long lastMqttPost = 0;
const unsigned long MQTT_POST_INTERVAL_MS = 1000;

bool sendATCommand(String command, String expectedResponse,
                   unsigned long timeout, String *response) {
  if (!serial4GMutex ||
      xSemaphoreTake(serial4GMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return false;
  }

  Serial4G.println(command);
  String responseStr = "";
  unsigned long startTime = millis();
  unsigned long lastYieldTime = millis();
  while (millis() - startTime < timeout) {
    while (Serial4G.available()) {
      char c = (char)Serial4G.read();
      responseStr += c;
    }
    if (responseStr.indexOf(expectedResponse) != -1) {
      if (response != nullptr) {
        *response = responseStr;
      }
      xSemaphoreGive(serial4GMutex);
      return true;
    }
    if (millis() - lastYieldTime > 20) {
      vTaskDelay(1);
      esp_task_wdt_reset();
      lastYieldTime = millis();
    }
    vTaskDelay(1);
  }
  xSemaphoreGive(serial4GMutex);
  return false;
}
static bool sendATWithTrace(const char *label, const String &command,
                            const char *expectedResponse, unsigned long timeout,
                            String *response = nullptr) {
  return sendATCommand(command, expectedResponse, timeout, response);
}

static bool waitForModemReady(unsigned long timeoutMs) {
  unsigned long start = millis();
  String bootBuf;
  while (millis() - start < timeoutMs) {

    while (Serial4G.available()) {
      char c = (char)Serial4G.read();
      bootBuf += c;
      if (bootBuf.length() > 256)
        bootBuf.remove(0, bootBuf.length() - 128);
    }

    if (bootBuf.indexOf("RDY") != -1 || bootBuf.indexOf("PB DONE") != -1 ||
        bootBuf.indexOf("READY") != -1) {
      if (sendATCommand("AT", "OK", 1000, nullptr))
        return true;
    }

    if (sendATCommand("AT", "OK", 500, nullptr))
      return true;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return false;
}
static void drainModemSerial(unsigned long ms) {
  unsigned long end = millis() + ms;
  while ((long)(end - millis()) > 0) {
    while (Serial4G.available()) {
      Serial4G.read();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
bool init4GModule() {
  Serial4G.begin(115200, SERIAL_8N1, RXD1, TXD1);
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_task_wdt_reset();
  auto failNoSignal = []() {
    extern CellularManager cellular;
    cellular.setSignalStrength(99, 99);
    return false;
  };

  drainModemSerial(50);

  if (!waitForModemReady(10000)) {
    return failNoSignal();
  }

  sendATWithTrace("AT Ping", "AT", "OK", 1000);
  sendATWithTrace("Echo Off", "ATE0", "OK", 1000);
  vTaskDelay(pdMS_TO_TICKS(20));
  sendATWithTrace("Enable CME", "AT+CMEE=2", "OK", 1000);
  vTaskDelay(pdMS_TO_TICKS(20));

  String imeiResponse;
  if (!sendATWithTrace("Read IMEI", "AT+GSN", "OK", 3000, &imeiResponse)) {
    return failNoSignal();
  }

  int startIdx = imeiResponse.indexOf("\r\n") + 2;
  int endIdx = imeiResponse.indexOf("\r\nOK");
  if (startIdx > 1 && endIdx > startIdx) {
    String deviceIMEI = imeiResponse.substring(startIdx, endIdx);
    deviceIMEI.trim();
    deviceIMEI.replace("\"", "");
    deviceIMEI.replace("\r", "");
    deviceIMEI.replace("\n", "");

    if (deviceIMEI.length() == 15) {
      configManager.setDeviceIMEI(deviceIMEI);
    } else {
      Serial.printf("[NET][AT] Unexpected IMEI response: %s\n",
                    imeiResponse.c_str());
    }
  } else {
    Serial.printf("[NET][AT] IMEI parse indices invalid (resp=%s)\n",
                  imeiResponse.c_str());
  }

  String simResponse;
  if (sendATWithTrace("SIM State", "AT+CPIN?", "OK", 1500, &simResponse)) {
    if (simResponse.indexOf("+CPIN: SIM PIN") != -1) {
      Serial.println("[NET][AT] SIM requires PIN");
      return failNoSignal();
    } else if (simResponse.indexOf("+CME ERROR") != -1) {
      Serial.printf("[NET][AT] SIM error: %s\n", simResponse.c_str());
      return failNoSignal();
    }
  } else {
    Serial.println("[NET][AT] Failed to query SIM state");
  }

  esp_task_wdt_reset();
  bool networkRegistered = false;
  unsigned long regStartTime = millis();
  const unsigned long regTimeout = 5000;

  while (millis() - regStartTime < regTimeout) {
    String cregResponse;
    if (sendATCommand("AT+CREG?", "+CREG:", 800, &cregResponse)) {
      int cregIndex = cregResponse.indexOf("+CREG:");
      if (cregIndex != -1) {
        String cregLine = cregResponse.substring(cregIndex + 7);
        int commaIndex = cregLine.indexOf(',');
        if (commaIndex != -1) {
          int regStatus = cregLine.substring(commaIndex + 1).toInt();

          if (regStatus == 1 || regStatus == 5) {
            networkRegistered = true;
            break;
          } else if (regStatus == 3) {
            return failNoSignal();
          }
        }
      }
    } else {
      Serial.println("[NET][AT] AT+CREG? timeout");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_task_wdt_reset();
  }

  if (!networkRegistered) {
    Serial.println("[NET][AT] Network registration timeout");
    return failNoSignal();
  }

  for (int retry = 0; retry < 3; ++retry) {
    String csqResponse;
    if (sendATCommand("AT+CSQ", "+CSQ:", 1200, &csqResponse)) {
      int csqIndex = csqResponse.indexOf("+CSQ:");
      if (csqIndex != -1) {
        String csqLine = csqResponse.substring(csqIndex + 6);
        int commaIndex = csqLine.indexOf(',');
        if (commaIndex != -1) {
          int rssi = csqLine.substring(0, commaIndex).toInt();
          int ber = csqLine.substring(commaIndex + 1).toInt();
          if (rssi >= 0 && rssi <= 31) {
            extern CellularManager cellular;
            cellular.setSignalStrength(rssi, ber);
            break;
          }
        }
      }
    } else {
      Serial.println("[NET][AT] AT+CSQ timeout");
    }
    if (retry < 2)
      vTaskDelay(pdMS_TO_TICKS(300));
  }
  return true;
}

void updateMQTTConfigFromManager() {
  if (!configManager.isMQTTConfigReady() ||
      !configManager.isDeviceConfigReady()) {
    return;
  }

  const MQTTConfig &config = configManager.getMQTTConfig();
  const RTKConfig &rtkConf = configManager.getRTKConfig();
  const DeviceConfig &deviceInfo = configManager.getDeviceConfig();

  mqtt_server = config.host;
  mqtt_port = config.port;
  mqtt_user = config.username;
  mqtt_pass = config.password;

  if (deviceInfo.isValid) {
    topicTrainStroke =
        "srm/train/stroke/" + deviceInfo.tenantCode + "/" + deviceInfo.boatCode;
    topicStatus =
        "srm/status/" + deviceInfo.tenantCode + "/" + deviceInfo.boatCode;
    topicHR = "srm/hr/" + deviceInfo.tenantCode + "/" + deviceInfo.boatCode;
  }
}

void setup_network() {

  NetworkStageState state{NET_STAGE_AT_INIT, 0, false};
  const uint32_t stageTimeouts[] = {15000, 20000, 30000, 10000};
  const int stageRetries[] = {3, 2, 2, 2};
  while (state.stage <= NET_STAGE_TIME_SYNC) {
    bool stepResult = false;
    switch (state.stage) {
    case NET_STAGE_AT_INIT:
      stepResult = runAtInitStep(stageTimeouts[state.stage],
                                 stageRetries[state.stage], state);
      break;
    case NET_STAGE_PPP_DIAL:
      stepResult = runPppDialStep(stageTimeouts[state.stage],
                                  stageRetries[state.stage], state);
      break;
    case NET_STAGE_API_FETCH:
      stepResult = runApiFetchStep(stageTimeouts[state.stage],
                                   stageRetries[state.stage], state);
      break;
    case NET_STAGE_TIME_SYNC:
      stepResult = runTimeSyncStep(stageTimeouts[state.stage],
                                   stageRetries[state.stage], state);
      break;
    }
    if (!stepResult) {
      cleanupHybridNetwork();
      return;
    }
    state.stage = static_cast<NetworkStage>(static_cast<int>(state.stage) + 1);
    state.attempt = 0;
  }
}
static bool waitForCondition(uint32_t timeoutMs,
                             std::function<bool()> condition,
                             uint32_t pollIntervalMs = 100) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (condition()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(pollIntervalMs));
  }
  return condition();
}
bool runAtInitStep(uint32_t timeoutMs, int maxRetries,
                   NetworkStageState &state) {
  for (state.attempt = 1; state.attempt <= maxRetries; ++state.attempt) {
    cleanupHybridNetwork();
    if (init4GModule()) {
      state.success = true;
      return true;
    }
  }
  return false;
}
bool runPppDialStep(uint32_t timeoutMs, int maxRetries,
                    NetworkStageState &state) {
  for (state.attempt = 1; state.attempt <= maxRetries; ++state.attempt) {
    if (hybridModem) {
      delete hybridModem;
      hybridModem = nullptr;
    }
    hybridModem = new (std::nothrow) TinyGsm(Serial4G);
    if (!hybridModem) {
      continue;
    }
    if (!waitForCondition(
            timeoutMs,
            []() {
              esp_task_wdt_reset();
              return hybridModem->waitForNetwork(2000);
            },
            200)) {
      continue;
    }
    const char *apnList[] = {"CMNET", "CTNET", "3GNET", "woiot.cn"};
    bool gprsConnected = false;
    for (int i = 0; i < 4; ++i) {
      esp_task_wdt_reset();
      if (hybridModem->gprsConnect(apnList[i])) {
        gprsConnected = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!gprsConnected) {
      continue;
    }
    if (hybridHttpClient) {
      delete hybridHttpClient;
      hybridHttpClient = nullptr;
    }
    hybridHttpClient = new (std::nothrow) TinyGsmClient(*hybridModem);
    if (!hybridHttpClient) {
      hybridModem->gprsDisconnect();
      continue;
    }
    hybridPppConnected = true;
    currentNetwork = NETWORK_4G;
    state.success = true;
    return true;
  }
  return false;
}
bool runApiFetchStep(uint32_t timeoutMs, int maxRetries,
                     NetworkStageState &state) {

  if (configManager.getPlatformAddress().isEmpty() ||
      configManager.getPlatformAddress() == ":0") {
    Serial.println("[NET] 平台地址未配置，等待用户设置...");
    Serial.println("[NET] 请使用串口命令: SETAPI=<IP:PORT>");
    Serial.println("[NET] 示例: SETAPI=117.83.111.19:10033");

    TickType_t waitStart = xTaskGetTickCount();
    const uint32_t maxWaitMs = 30000;

    while ((xTaskGetTickCount() - waitStart) < pdMS_TO_TICKS(maxWaitMs)) {

      if (!configManager.getPlatformAddress().isEmpty() &&
          configManager.getPlatformAddress() != ":0") {
        Serial.println("[NET] 平台地址已设置，继续 API Fetch");
        break;
      }

      if ((xTaskGetTickCount() - waitStart) % pdMS_TO_TICKS(5000) == 0) {
        uint32_t remainingSec = (maxWaitMs - (xTaskGetTickCount() - waitStart) *
                                                 portTICK_PERIOD_MS) /
                                1000;
        Serial.printf("[NET] 等待平台地址设置... 剩余%u秒\n", remainingSec);
      }

      vTaskDelay(pdMS_TO_TICKS(500));
      esp_task_wdt_reset();
    }

    if (configManager.getPlatformAddress().isEmpty() ||
        configManager.getPlatformAddress() == ":0") {
      Serial.println("[NET] 超时30秒仍未设置平台地址，跳过API获取");
      Serial.println(
          "[NET] 系统将以离线模式运行，可在后续使用 SETAPI 命令配置");
      return false;
    }
  }

  Serial.printf("[NET] 使用平台地址: %s\n",
                configManager.getPlatformAddress().c_str());

  for (state.attempt = 1; state.attempt <= maxRetries; ++state.attempt) {
    TickType_t startTick = xTaskGetTickCount();
    configManager.refreshConfig();
    configManager.startConfigLoading();
    while ((xTaskGetTickCount() - startTick) < pdMS_TO_TICKS(timeoutMs)) {
      if (configManager.isConfigReady()) {
        state.success = true;
        return true;
      }
      if (configManager.getDeviceConfigStatus() == CONFIG_FAILED ||
          configManager.getMQTTConfigStatus() == CONFIG_FAILED) {
        Serial.println("[NET] API Config FAIL");
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      esp_task_wdt_reset();
    }
  }
  return configManager.isConfigReady();
}
bool runTimeSyncStep(uint32_t timeoutMs, int maxRetries,
                     NetworkStageState &state) {
  for (state.attempt = 1; state.attempt <= maxRetries; ++state.attempt) {
    TickType_t startTick = xTaskGetTickCount();
    get4GTimeAndSync();
    while ((xTaskGetTickCount() - startTick) < pdMS_TO_TICKS(timeoutMs)) {
      if (rtcTimeSynced || configManager.isTimeValid()) {
        state.success = true;
        return true;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
  return rtcTimeSynced || configManager.isTimeValid();
}

static bool networkTaskSuspendRequested = false;
static bool networkTaskSuspended = false;
static SemaphoreHandle_t networkSuspendMutex = nullptr;

static void initNetworkSuspendMutex() {
  if (!networkSuspendMutex) {
    networkSuspendMutex = xSemaphoreCreateMutex();
  }
}

static void ensureTopicsReady();
static void setMqttIconVisible(bool visible);
static void refreshMqttIconState();
void mqttTask(void *pvParameters) {
  esp_task_wdt_add(NULL);

  if (!dataFlow.begin()) {
    return;
  }
  initNetworkSuspendMutex();

  unsigned long nextPostTime = millis() + MQTT_POST_INTERVAL_MS;
  unsigned long lastStrokeCount = 0;
  bool strokeCountInitialized = false;
  while (true) {

    esp_task_wdt_reset();

    if (xSemaphoreTake(networkSuspendMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (networkTaskSuspendRequested && !networkTaskSuspended) {
        Serial.println("[MQTT] 检测到暂停请求，停止MQTT任务...");

        if (hybridMqttClient && hybridMqttClient->connected()) {
          hybridMqttClient->disconnect();
          setMqttIconVisible(false);
        }

        networkTaskSuspended = true;
        Serial.println("[MQTT] 任务已暂停");
      }

      if (networkTaskSuspended) {
        xSemaphoreGive(networkSuspendMutex);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
        continue;
      }

      xSemaphoreGive(networkSuspendMutex);
    }

    bool networkStatus = (currentNetwork == NETWORK_4G && hybridPppConnected);
    dataFlow.setNetworkStatus(networkStatus);
    refreshMqttIconState();

    if (networkStatus) {

      unsigned long currentTime = millis();
      if (currentTime >= nextPostTime) {
        dataFlowMonitor.setState(FLOW_COLLECTING);

        if (hybridMqttClient) {
          for (int i = 0; i < 3; ++i) {
            hybridMqttClient->loop();
          }
          if (!hybridMqttClient->connected()) {
            setMqttIconVisible(false);
          }
        }

        if (configManager.isMQTTConfigReady() &&
            configManager.isDeviceConfigReady() && hybridPppConnected) {

          if (!hybridMqttClient || !hybridMqttClient->connected()) {
            setMqttIconVisible(false);
            static unsigned long lastConnectAttempt = 0;

            if (mqttGracefullyDegraded) {

            } else {
              if (millis() - lastConnectAttempt > 10000) {
                connectHybridSocketMQTT();
                esp_task_wdt_reset();
                lastConnectAttempt = millis();
              }
            }
          }

          if (hybridMqttClient && hybridMqttClient->connected()) {
            dataFlowMonitor.setState(FLOW_UPLOADING);
            ensureTopicsReady();

            static unsigned long lastCollectionTest = 0;
            if (millis() - lastCollectionTest > 10000) {
              DataFlowManager::CollectionResult testResult;
              dataFlow.collectAllCurrentData(testResult);
              lastCollectionTest = millis();
            }

            static unsigned long statusUploadCount = 0;
            static unsigned long heartRateUploadCount = 0;
            static unsigned long lastLogSummaryTime = 0;

            DataFlowManager::CollectionResult collectionResult;
            if (dataFlow.collectAllCurrentData(collectionResult)) {

              dataFlow.incrementProcessedCount();
              ensureTopicsReady();

              if (collectionResult.hasStatusData && !topicStatus.isEmpty()) {
                if (publishHybridSocketMQTT(topicStatus,
                                            collectionResult.statusPayload)) {
                  statusUploadCount++;
                }
                esp_task_wdt_reset();
              }

              if (collectionResult.hasHeartRateData && !topicHR.isEmpty()) {
                if (publishHybridSocketMQTT(
                        topicHR, collectionResult.heartRatePayload)) {
                  heartRateUploadCount++;
                }
                esp_task_wdt_reset();
              }
            }

            if (hybridMqttClient && hybridMqttClient->connected() &&
                training.isActive()) {
              ensureTopicsReady();

              StrokeSnapshot snapshot;
              int sentCount = 0;
              const int maxBatchSize = 5;

              while (sentCount < maxBatchSize &&
                     strokeDataMgr.getNextStroke(snapshot)) {

                esp_task_wdt_reset();

                // 异步写入SD卡（在MQTT任务中执行，不阻塞主循环）
                sdCardManager.logStrokeSnapshot(snapshot);

                String strokeJson = generateStrokeJSON(snapshot);

                if (!strokeJson.isEmpty() && !topicTrainStroke.isEmpty()) {

                  esp_task_wdt_reset();

                  bool success =
                      publishHybridSocketMQTT(topicTrainStroke, strokeJson, 1);

                  esp_task_wdt_reset();

                  strokeDataMgr.markStrokeSent(snapshot.strokeNumber, success);

                  sentCount++;
                  esp_task_wdt_reset();
                  vTaskDelay(pdMS_TO_TICKS(10));
                } else {

                  strokeDataMgr.markStrokeSent(snapshot.strokeNumber, false);
                }
              }

              static unsigned long lastStatsTime = 0;
              if (millis() - lastStatsTime > 30000) {
                size_t pending, sent, lost, queueFull;
                strokeDataMgr.getStatistics(pending, sent, lost, queueFull);
                if (sent > 0 || lost > 0 || pending > 0) {
                  Serial.printf(
                      "[Stroke] 统计: 待发=%u, 已发=%u, 丢失=%u, 队列满=%u\n",
                      pending, sent, lost, queueFull);
                }
                lastStatsTime = millis();
              }
            }

            DataPacket packet;
            int packetCount = 0;
            while (dataFlow.popUploadData(packet)) {
              if (packet.needsUpload && !packet.topic.isEmpty()) {

                publishHybridSocketMQTT(packet.topic, packet.payload);

                packetCount++;

                if (packetCount % 5 == 0) {
                  esp_task_wdt_reset();
                }
              }
            }
          }
        }

        nextPostTime += MQTT_POST_INTERVAL_MS;
        if (nextPostTime < currentTime) {
          nextPostTime = currentTime + MQTT_POST_INTERVAL_MS;
        }
      }
    }

    static unsigned long lastSignalCheck = 0;
    if (millis() - lastSignalCheck > 60000) {
      esp_task_wdt_reset();
      String csqResponse;
      if (sendATCommand("AT+CSQ", "OK", 3000, &csqResponse)) {
        int csqIndex = csqResponse.indexOf("+CSQ:");
        if (csqIndex != -1) {
          String csqLine = csqResponse.substring(csqIndex + 6);
          int commaIndex = csqLine.indexOf(',');
          if (commaIndex != -1) {
            int rssi = csqLine.substring(0, commaIndex).toInt();
            int ber = csqLine.substring(commaIndex + 1).toInt();
            extern CellularManager cellular;
            cellular.setSignalStrength(rssi, ber);
          }
        }
      }
      lastSignalCheck = millis();
      esp_task_wdt_reset();
    }

    if (hybridMqttClient) {
      hybridMqttClient->loop();
      if (!hybridMqttClient->connected()) {
        setMqttIconVisible(false);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    esp_task_wdt_reset();
  }
}
static void ensureTopicsReady() {
  if (!configManager.isDeviceConfigReady())
    return;
  const DeviceConfig &deviceInfo = configManager.getDeviceConfig();
  if (topicTrainStroke.isEmpty() || topicStatus.isEmpty() ||
      topicHR.isEmpty()) {
    topicTrainStroke =
        "srm/train/stroke/" + deviceInfo.tenantCode + "/" + deviceInfo.boatCode;
    topicStatus =
        "srm/status/" + deviceInfo.tenantCode + "/" + deviceInfo.boatCode;
    topicHR = "srm/hr/" + deviceInfo.tenantCode + "/" + deviceInfo.boatCode;
  }
}
static void setMqttIconVisible(bool visible) {
  static bool lastState = false;
  if (visible == lastState) {
    return;
  }
  lastState = visible;

  if (visible) {
    lv_obj_clear_flag(ui_Image6, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui_Image6, LV_OBJ_FLAG_HIDDEN);
  }
}
static void refreshMqttIconState() {
  extern CellularManager cellular;
  bool hasNetwork = (currentNetwork == NETWORK_4G) && hybridPppConnected &&
                    cellular.isConnected();
  bool visible =
      hasNetwork && hybridMqttClient && hybridMqttClient->connected();
  setMqttIconVisible(visible);
}

int mqttRetryCount = 0;
unsigned long mqttRetryStartTime = 0;
bool mqttGracefullyDegraded = false;
const int MAX_MQTT_RETRIES = 3;
const unsigned long RETRY_RESET_INTERVAL = 300000;
bool connectHybridSocketMQTT() {
  if (mqttGracefullyDegraded) {
    if (millis() - mqttRetryStartTime > RETRY_RESET_INTERVAL) {
      mqttRetryCount = 0;
      mqttGracefullyDegraded = false;
    } else {
      return false;
    }
  }
  if (gMqttConnectBusy)
    return false;
  gMqttConnectBusy = true;
  esp_task_wdt_reset();
  if (!hybridPppConnected || !hybridHttpClient || !hybridModem) {
    gMqttConnectBusy = false;
    return false;
  }
  const MQTTConfig &config = configManager.getMQTTConfig();
  if (!config.isValid) {
    gMqttConnectBusy = false;
    return false;
  }
  if (hybridMqttClient) {
    if (hybridMqttClient->connected()) {
      gMqttConnectBusy = false;
      return true;
    }
    delete hybridMqttClient;
    hybridMqttClient = nullptr;
  }
  if (mcuTcpClient) {
    delete mcuTcpClient;
    mcuTcpClient = nullptr;
  }
  mcuTcpClient = new (std::nothrow) TinyGsmClient(*hybridModem, 1);
  if (!mcuTcpClient) {
    gMqttConnectBusy = false;
    return false;
  }
  hybridMqttClient = new (std::nothrow) MQTTClient(4096);
  if (!hybridMqttClient) {
    delete mcuTcpClient;
    mcuTcpClient = nullptr;
    gMqttConnectBusy = false;
    return false;
  }
  hybridMqttClient->begin(config.host.c_str(), config.port, *mcuTcpClient);
  uint16_t keepAlive =
      config.keepAliveInterval > 0 ? (uint16_t)config.keepAliveInterval : 60;
  hybridMqttClient->setOptions(keepAlive, config.cleanSession, 15000);
  String clientId = configManager.getDeviceIMEI();
  ensureTopicsReady();
  for (int i = 0; i < 3; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_task_wdt_reset();
  }
  esp_task_wdt_reset();
  bool connectResult = false;
  {
    TaskWdtSuspendGuard wdtGuard("MQTT connect");
    connectResult = hybridMqttClient->connect(
        clientId.c_str(), config.username.c_str(), config.password.c_str());
  }
  esp_task_wdt_reset();
  if (connectResult) {
    Serial.println("[MQTT] Connected");
    BT::startTask();
    setMqttIconVisible(true);
    mqttRetryCount = 0;
    mqttGracefullyDegraded = false;
    gMqttConnectBusy = false;
    return true;
  } else {
    mqttRetryCount++;
    Serial.printf("[MQTT] Connect FAIL (%d/%d)\n", mqttRetryCount,
                  MAX_MQTT_RETRIES);
    if (mqttRetryCount >= MAX_MQTT_RETRIES) {
      mqttGracefullyDegraded = true;
      mqttRetryStartTime = millis();
      BT::startTask();
    }
    setMqttIconVisible(false);
    gMqttConnectBusy = false;
    return false;
  }
}
bool publishHybridSocketMQTT(const String &topic, const String &payload,
                             uint8_t qos) {
  if (!hybridMqttClient || !hybridMqttClient->connected()) {
    dataFlow.incrementFailureCount();
    return false;
  }
  if (qos > 0) {
    for (int i = 0; i < 3; ++i) {
      hybridMqttClient->loop();
      esp_task_wdt_reset();
    }
  }
  bool publishResult = false;
  const char *tag = (qos > 0) ? "[MQTT-QoS1]" : "[MQTT]";
  {
    TaskWdtSuspendGuard wdtGuard(
        (qos > 0) ? "Stroke publish" : "Hybrid publish", 1000);
    publishResult =
        hybridMqttClient->publish(topic.c_str(), payload.c_str(), false, qos);
  }
  if (publishResult) {
    // Serial.printf("%s %s\n%s\n", tag, topic.c_str(), payload.c_str());

    dataFlow.incrementUploadCount();
    if (qos > 0) {
      hybridMqttClient->loop();
      esp_task_wdt_reset();
    }
    return true;
  } else {
    Serial.printf("%s Fail %d: %s\n", tag, (int)hybridMqttClient->lastError(),
                  topic.c_str());
    dataFlow.incrementFailureCount();
    return false;
  }
}
void cleanupHybridNetwork() {

  if (hybridMqttClient) {
    if (hybridMqttClient->connected()) {
      hybridMqttClient->disconnect();
    }
    delete hybridMqttClient;
    hybridMqttClient = nullptr;
  }

  if (hybridHttpClient) {
    delete hybridHttpClient;
    hybridHttpClient = nullptr;
  }
  if (mcuTcpClient) {
    delete mcuTcpClient;
    mcuTcpClient = nullptr;
  }
  if (hybridModem) {
    delete hybridModem;
    hybridModem = nullptr;
  }

  hybridPppConnected = false;
  currentNetwork = NETWORK_NONE;
  refreshMqttIconState();
}

bool getTimeFromAT() {
  for (int retry = 0; retry < 5; retry++) {
    String resp;
    if (sendATCommand("AT+QLTS=2", "+QLTS:", 3000, &resp)) {

      int idx = resp.indexOf("+QLTS:");
      if (idx != -1) {
        String line = resp.substring(idx);
        int s = line.indexOf('"'), e = line.lastIndexOf('"');
        if (s != -1 && e > s) {
          String timeStr = line.substring(s + 1, e);

          int plusIdx = timeStr.indexOf('+');
          int minusIdx = timeStr.indexOf('-');
          if (plusIdx > 0 || minusIdx > 0) {
            int tzIdx = (plusIdx > 0) ? plusIdx : minusIdx;
            timeStr = timeStr.substring(0, tzIdx);
          }

          String formattedTime = convertQLTSToDateTime(timeStr);
          if (!formattedTime.isEmpty()) {
            String date = formattedTime.substring(0, 10);
            String time = formattedTime.substring(11);
            configManager.setDateTime(date, time, formattedTime);

            extern bool rtcInitialized, rtcTimeSynced;
            extern void syncRTCWithCellular();
            if (rtcInitialized && !rtcTimeSynced) {
              syncRTCWithCellular();
            }
            return true;
          }
        }
      }
    }

    if (retry < 4) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_task_wdt_reset();
    }
  }

  return false;
}

void get4GTimeAndSync() {
  getTimeFromAT();

  extern void syncRTCWithCellular();
  syncRTCWithCellular();
}

bool requestNetworkSuspend() {
  initNetworkSuspendMutex();

  if (xSemaphoreTake(networkSuspendMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    networkTaskSuspendRequested = true;
    xSemaphoreGive(networkSuspendMutex);

    Serial.println("[NET] 暂停请求已发送");

    unsigned long waitStart = millis();
    while (millis() - waitStart < 5000) {
      if (xSemaphoreTake(networkSuspendMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool suspended = networkTaskSuspended;
        xSemaphoreGive(networkSuspendMutex);

        if (suspended) {
          Serial.println("[NET] 任务已暂停");
          return true;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    Serial.println("[NET] 暂停超时");
    return false;
  }

  Serial.println("[NET] 无法获取互斥锁");
  return false;
}

bool requestNetworkResume() {
  initNetworkSuspendMutex();

  if (xSemaphoreTake(networkSuspendMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    networkTaskSuspendRequested = false;
    networkTaskSuspended = false;
    xSemaphoreGive(networkSuspendMutex);

    Serial.println("[NET] 恢复请求已发送");
    return true;
  }

  Serial.println("[NET] 无法获取互斥锁");
  return false;
}

bool isNetworkTaskSuspended() {
  initNetworkSuspendMutex();

  if (xSemaphoreTake(networkSuspendMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    bool suspended = networkTaskSuspended;
    xSemaphoreGive(networkSuspendMutex);
    return suspended;
  }

  return false;
}

bool disconnectMQTTGracefully() {
  Serial.println("[MQTT] 开始优雅断开...");

  if (!hybridMqttClient) {
    Serial.println("[MQTT] MQTT客户端不存在");
    return true;
  }

  extern int mqttRetryCount;
  extern bool mqttGracefullyDegraded;
  extern unsigned long mqttRetryStartTime;
  extern const int MAX_MQTT_RETRIES;

  mqttGracefullyDegraded = true;
  mqttRetryStartTime = millis();
  mqttRetryCount = MAX_MQTT_RETRIES;

  if (hybridMqttClient->connected()) {
    Serial.println("[MQTT] 断开连接...");
    hybridMqttClient->disconnect();

    unsigned long waitStart = millis();
    while (hybridMqttClient->connected() && millis() - waitStart < 2000) {
      hybridMqttClient->loop();
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (hybridMqttClient->connected()) {
      Serial.println("[MQTT] 断开超时，强制断开");
    } else {
      Serial.println("[MQTT] 断开成功");
    }
  } else {
    Serial.println("[MQTT] 已断开");
  }

  extern void setMqttIconVisible(bool visible);
  setMqttIconVisible(false);

  Serial.println("[MQTT] 优雅断开完成");
  return true;
}

bool disconnectPPPCompletely() {
  Serial.println("[NET] 开始彻底断开PPP连接...");

  if (hybridMqttClient) {
    if (hybridMqttClient->connected()) {
      Serial.println("[NET] 断开MQTT...");
      hybridMqttClient->disconnect();
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }

  if (hybridHttpClient) {
    Serial.println("[NET] 关闭HTTP连接...");
    hybridHttpClient->stop();
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (hybridModem && hybridPppConnected) {
    Serial.println("[NET] 断开GPRS...");

    extern SemaphoreHandle_t serial4GMutex;
    if (serial4GMutex &&
        xSemaphoreTake(serial4GMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      hybridModem->gprsDisconnect();
      vTaskDelay(pdMS_TO_TICKS(500));
      xSemaphoreGive(serial4GMutex);
      Serial.println("[NET] GPRS已断开");
    } else {
      Serial.println("[NET] 无法获取串口锁，跳过GPRS断开");
    }
  }

  hybridPppConnected = false;
  currentNetwork = NETWORK_NONE;

  extern void setMqttIconVisible(bool visible);
  setMqttIconVisible(false);
  extern void refreshMqttIconState();
  refreshMqttIconState();

  Serial.println("[NET] PPP连接已彻底断开");
  return true;
}
