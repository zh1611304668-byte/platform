/*
 * File: StrokeDataManager.cpp
 * Purpose: Implements runtime logic for the Stroke Data Manager module.
 */
#include "StrokeDataManager.h"
#include "ConfigManager.h"
#include "GNSSProcessor.h"
#include "IMUManager.h"
#include "SDCardManager.h"
#include "TrainingMode.h"
#include <cmath>

extern GNSSProcessor gnss;


// 閻劋绨弫鐗堝祦鐎靛綊缍堥惃鍕瑐娑撯偓濡椼劎绮撻悙鐟版綏�?
static double s_prevOutputLat = 0.0;
static double s_prevOutputLon = 0.0;
static bool s_hasPrevOutput = false;
static unsigned long s_prevStrokeTimestampMs = 0;
static float s_strokeElapsedSec = 0.0f;
static String s_prevCaptureTime = "";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Haversine 鐠烘繄顬囩拋锛勭暬閿涘牅�?IMUManager 娑撯偓閼疯揪�?
static double haversineDistance(double lat1, double lon1, double lat2,
                                double lon2) {
  const double R = 6371000.0; // 閸︽壆鎮嗛崡濠傜窞閿涘牏鑳岄�?
  double dLat = (lat2 - lat1) * M_PI / 180.0;
  double dLon = (lon2 - lon1) * M_PI / 180.0;
  double lat1Rad = lat1 * M_PI / 180.0;
  double lat2Rad = lat2 * M_PI / 180.0;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1Rad) * cos(lat2Rad) * sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

static bool isLeapYearSimple(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int daysInMonthSimple(int year, int month) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 30;
  }
  if (month == 2 && isLeapYearSimple(year)) {
    return 29;
  }
  return kDays[month - 1];
}

static String addMsToTimestamp(const String &base, unsigned long deltaMs) {
  if (base.length() < 19) {
    return base;
  }

  int year = base.substring(0, 4).toInt();
  int month = base.substring(5, 7).toInt();
  int day = base.substring(8, 10).toInt();
  int hour = base.substring(11, 13).toInt();
  int minute = base.substring(14, 16).toInt();
  int second = base.substring(17, 19).toInt();
  int milli = 0;
  if (base.length() >= 23) {
    milli = base.substring(20, 23).toInt();
  }

  int64_t totalMs =
      (((int64_t)hour * 3600 + (int64_t)minute * 60 + (int64_t)second) * 1000) +
      milli + (int64_t)deltaMs;
  int dayDelta = 0;
  while (totalMs >= 86400000LL) {
    totalMs -= 86400000LL;
    dayDelta++;
  }
  while (totalMs < 0) {
    totalMs += 86400000LL;
    dayDelta--;
  }

  while (dayDelta > 0) {
    day++;
    int dim = daysInMonthSimple(year, month);
    if (day > dim) {
      day = 1;
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    }
    dayDelta--;
  }
  while (dayDelta < 0) {
    day--;
    if (day < 1) {
      month--;
      if (month < 1) {
        month = 12;
        year--;
      }
      day = daysInMonthSimple(year, month);
    }
    dayDelta++;
  }

  hour = (int)(totalMs / 3600000LL);
  totalMs %= 3600000LL;
  minute = (int)(totalMs / 60000LL);
  totalMs %= 60000LL;
  second = (int)(totalMs / 1000LL);
  milli = (int)(totalMs % 1000LL);

  char out[32];
  snprintf(out, sizeof(out), "%04d-%02d-%02d %02d:%02d:%02d.%03d", year, month,
           day, hour, minute, second, milli);
  return String(out);
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

  // Keep speed as captured from stroke snapshot chain.

  // 缁夊娅?deriveStrokeLength 鐠嬪啰鏁ら敍宀€娲块幒銉ゅ▏閻劍绁撮柌蹇撯偓?
}

// 婢舵牠鍎撮崣姗€鍣烘竟鐗堟
extern IMUManager imu;
extern TrainingMode training;
extern ConfigManager configManager;
extern SDCardManager sdCardManager;
extern float totalDistance;
extern float strokeLength;

// 閸忋劌鐪€圭偘�?
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
  // 閸掓稑缂撴禍鎺撴灱闁?
  queueMutex = xSemaphoreCreateMutex();
  if (!queueMutex) {
    Serial.println("[Stroke] Failed to create queue mutex");
    return false;
  }

  return true;
}

bool StrokeDataManager::captureStroke(int currentStrokeCount) {
  if (!training.isActive()) {
    return false;
  }

  if (currentStrokeCount <= lastCapturedStroke) {
    return false;
  }

  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  String boatCode = deviceConfig.isValid ? deviceConfig.boatCode : "01";
  String trainId = training.getTrainId();
  extern String getValidTimestamp();

  const StrokeMetrics &metrics = imu.getLastStrokeMetrics();

  StrokeSnapshot snapshot;
  snapshot.timestamp = metrics.timestamp;
  snapshot.strokeNumber = metrics.strokeNumber;
  snapshot.trainId = trainId;
  snapshot.boatCode = boatCode;
  String gnssTime = gnss.getDateTimeStringForMillis(snapshot.timestamp);
  snapshot.captureTime =
      (!gnssTime.isEmpty() && gnssTime.length() >= 19) ? gnssTime
                                                       : getValidTimestamp();

  if (snapshot.strokeNumber <= 1 || snapshot.timestamp <= s_prevStrokeTimestampMs) {
    s_prevStrokeTimestampMs = 0;
    s_strokeElapsedSec = 0.0f;
    s_prevCaptureTime = "";
  }

  float candidateIntervalSec = 0.0f;
  if (s_prevStrokeTimestampMs > 0 && snapshot.timestamp > s_prevStrokeTimestampMs) {
    candidateIntervalSec =
        (snapshot.timestamp - s_prevStrokeTimestampMs) / 1000.0f;
  }

  const float kMinValidStrokeIntervalSec = 1.0f;
  const float kMaxValidStrokeIntervalSec = 30.0f;
  if (snapshot.strokeNumber > 1 &&
      (candidateIntervalSec < kMinValidStrokeIntervalSec ||
       candidateIntervalSec > kMaxValidStrokeIntervalSec)) {
    lastCapturedStroke = metrics.strokeNumber;
    Serial.printf("[Stroke] Skip outlier stroke #%d: interval=%.3fs\n",
                  snapshot.strokeNumber, candidateIntervalSec);
    return false;
  }

  snapshot.strokeIntervalSec = candidateIntervalSec;
  if (snapshot.strokeNumber <= 1 || snapshot.strokeIntervalSec <= 0.0f) {
    snapshot.elapsedSeconds = 0.0f;
  } else {
    s_strokeElapsedSec += snapshot.strokeIntervalSec;
    snapshot.elapsedSeconds = s_strokeElapsedSec;
  }

  if (snapshot.timestamp > 0) {
    s_prevStrokeTimestampMs = snapshot.timestamp;
  }

  if (snapshot.strokeNumber > 1 && !s_prevCaptureTime.isEmpty() &&
      snapshot.strokeIntervalSec > 0.0f) {
    unsigned long stepMs =
        (unsigned long)(snapshot.strokeIntervalSec * 1000.0f + 0.5f);
    String predictedTime = addMsToTimestamp(s_prevCaptureTime, stepMs);

    if (!gnssTime.isEmpty() && gnssTime.length() >= 19) {
      extern float calculateTimeDifference(const String &timestamp1,
                                           const String &timestamp2);
      float driftSec = calculateTimeDifference(predictedTime, gnssTime);
      if (fabsf(driftSec) > 3.0f) {
        Serial.printf("[Stroke] GNSS time drift %.2fs (keeping monotonic)\n",
                      driftSec);
      }
    }

    snapshot.captureTime = predictedTime;
  } else if (snapshot.captureTime.isEmpty() || snapshot.captureTime.length() < 19) {
    snapshot.captureTime = getValidTimestamp();
  }
  if (!snapshot.captureTime.isEmpty()) {
    s_prevCaptureTime = snapshot.captureTime;
  }

  snapshot.startLat = metrics.startLat;
  snapshot.startLon = metrics.startLon;
  snapshot.endLat = metrics.endLat;
  snapshot.endLon = metrics.endLon;
  snapshot.lat = metrics.endLat;
  snapshot.lon = metrics.endLon;

  GNSSPoint interpPos = gnss.getInterpolatedPosition(snapshot.timestamp);
  if (interpPos.latitude != 0.0 || interpPos.longitude != 0.0) {
    snapshot.lat = interpPos.latitude;
    snapshot.lon = interpPos.longitude;
  }

  float gnssSpeed = gnss.getSpeed();
  normalizeStrokeSnapshot(snapshot);

  bool coordsComplete = (snapshot.lat != 0.0 && snapshot.lon != 0.0);
  double currLat =
      coordsComplete ? round(snapshot.lat * 10000000.0) / 10000000.0 : 0.0;
  double currLon =
      coordsComplete ? round(snapshot.lon * 10000000.0) / 10000000.0 : 0.0;

  if (snapshot.strokeNumber == 1) {
    s_prevOutputLat = 0.0;
    s_prevOutputLon = 0.0;
    s_hasPrevOutput = false;
  }

  if (!s_hasPrevOutput || !coordsComplete) {
    snapshot.strokeLength = 0.0f;
  } else {
    double rawDist =
        haversineDistance(s_prevOutputLat, s_prevOutputLon, currLat, currLon);
    snapshot.strokeLength =
        roundf(static_cast<float>(rawDist) * 100.0f) / 100.0f;
  }

  if (coordsComplete) {
    s_prevOutputLat = currLat;
    s_prevOutputLon = currLon;
    s_hasPrevOutput = true;
  }

  static float s_totalOutputDistance = 0.0f;
  if (snapshot.strokeNumber == 1) {
    s_totalOutputDistance = 0.0f;
  }
  s_totalOutputDistance += snapshot.strokeLength;
  snapshot.totalDistance = s_totalOutputDistance;

  float derivedSpeed = 0.0f;
  if (snapshot.strokeIntervalSec > 0.0f && snapshot.strokeLength > 0.05f) {
    derivedSpeed = snapshot.strokeLength / snapshot.strokeIntervalSec;
  }

  if (gnssSpeed > 0.05f && gnssSpeed < 12.0f) {
    snapshot.speed = gnssSpeed;
  } else if (derivedSpeed > 0.05f && derivedSpeed < 12.0f) {
    snapshot.speed = derivedSpeed;
  } else {
    snapshot.speed = 0.0f;
  }

  snapshot.pace = (snapshot.speed > 0.05f) ? (500.0f / snapshot.speed) : 0.0f;

  if (snapshot.strokeNumber <= 1 || snapshot.strokeIntervalSec <= 0.0f) {
    snapshot.strokeRate = 0.0f;
  } else {
    float spm = 60.0f / snapshot.strokeIntervalSec;
    if (spm < 0.0f) {
      spm = 0.0f;
    } else if (spm > 80.0f) {
      spm = 80.0f;
    }
    snapshot.strokeRate = spm;
  }

  snapshot.power = 2.8f * snapshot.speed * snapshot.speed * snapshot.speed;

  strokeLength = snapshot.strokeLength;
  totalDistance = snapshot.totalDistance;

  snapshot.isValid = true;
  snapshot.isSent = false;
  snapshot.retryCount = 0;

  lastCapturedStroke = metrics.strokeNumber;

  bool pushed = pushToQueue(snapshot);
  if (pushed) {
    totalCaptured++;
  } else {
    Serial.printf("[Stroke] Queue full, drop #%d\n", snapshot.strokeNumber);
    totalLost++;
    queueFullCount++;
  }

  return pushed;
}
bool StrokeDataManager::getNextStroke(StrokeSnapshot &snapshot) {
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (!strokeQueue.empty()) {
      // 閸欘亜褰囬崙鐚寸礉娑撳秴鍨归梽銈忕礄閸欐垿鈧焦鍨氶崝鐔锋倵閹靛秴鍨归梽銈忕礆
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
      Serial.printf("[Stroke] 閳跨媴绗?鎼村繐褰挎稉宥呭爱闁板稄绱掗張鐔告箿=%d, 鐎圭偤妾?%d\n",
                    front.strokeNumber, strokeNumber);
      xSemaphoreGive(queueMutex);
      return;
    }

    if (success) {
      // 閸欐垿鈧焦鍨氶崝鐕傜礉娴犲酣妲﹂崚妤€鍨归�?
      strokeQueue.pop();
      lastSentStroke = strokeNumber;
      totalSent++;
      xSemaphoreGive(queueMutex);
      // Serial.printf("[Stroke] �?瀹告彃褰傞柅?#%d\n", strokeNumber);
    } else {
      // 閸欐垿鈧礁銇戠拹銉礉鐠佹澘缍嶉柌宥堢槸濞嗏剝鏆熼敍灞肩箽閻ｆ瑦鏆熼幑?
      front.retryCount++;
      Serial.printf("[Stroke] �?闁插秷鐦?#%d (�?d�?\n", strokeNumber,
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

  Serial.println("[Stroke] Statistics reset");
}

void StrokeDataManager::reset() {
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // 濞撳懐鈹栭梼鐔峰�?
    while (!strokeQueue.empty()) {
      strokeQueue.pop();
    }

    // 闁插秶鐤嗙拋鈩冩殶閸?
    lastCapturedStroke = 0;
    lastSentStroke = 0;

    xSemaphoreGive(queueMutex);
  }

  resetStatistics();
  s_prevStrokeTimestampMs = 0;
  s_strokeElapsedSec = 0.0f;
  s_prevCaptureTime = "";
  s_prevOutputLat = 0.0;
  s_prevOutputLon = 0.0;
  s_hasPrevOutput = false;
  Serial.println("[Stroke] Queue reset complete");
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
