/*
 * File: GNSSProcessor.cpp
 * Purpose: Implements runtime logic for the G N S S Processor module.
 */
#include "GNSSProcessor.h"
#include "ConfigManager.h"
#include "SDCardManager.h"
#include <cmath>
#include <vector>

GNSSProcessor::GNSSProcessor(HardwareSerial &serial, uint8_t rxPin,
                             uint8_t txPin, uint32_t baud)
    : gnssSerial(&serial), rxPin(rxPin), txPin(txPin), baudRate(baud) {}

void GNSSProcessor::begin() {
  gnssSerial->setRxBufferSize(8192); // 增大接收缓冲区至8KB，处理10Hz多星座数据
  gnssSerial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
}

void GNSSProcessor::process() {
  // 检查GNSS数据是否超时，但不影响数据处理
  if (millis() - lastValidGNSS > 10000) {
    gnssActive = false;
  }

  while (gnssSerial->available()) {
    char c = gnssSerial->read();

    if (c == '\n') {
      if (nmeaBuffer.length() > 5) { // 最小长度检查
        processNMEA(nmeaBuffer);
      }
      nmeaBuffer = "";
    } else if (c != '\r') {
      nmeaBuffer += c;
      // 限制单行缓冲区大小，防止异常数据耗尽内存
      if (nmeaBuffer.length() > 256) {
        nmeaBuffer = ""; // 丢弃过长行
      }
    }
  }
}

void GNSSProcessor::setSDCardManager(SDCardManager *sd) { _sdCard = sd; }

void GNSSProcessor::processNMEA(const String &nmea) {
  // 调试：打印接收到的NMEA数据
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 5000) { // 每5秒打印一次
    Serial.printf("[GNSS] Received NMEA: %s\n", nmea.c_str());
    lastDebugPrint = millis();
  }

  // 记录原始 NMEA 数据到 SD 卡
  if (_sdCard != nullptr) {
    _sdCard->logNmeaRaw(nmea);
  } else {
    static unsigned long lastSdWarning = 0;
    if (millis() - lastSdWarning > 10000) {
      Serial.println("[GNSS] Warning: SD card manager not set!");
      lastSdWarning = millis();
    }
  }

  if (nmea.startsWith("$GPVTG") || nmea.startsWith("$GNVTG")) {
    processVTG(nmea);
  } else if (nmea.startsWith("$GPGGA") || nmea.startsWith("$GNGGA")) {
    processGGA(nmea);
  } else if (nmea.startsWith("$GPRMC") || nmea.startsWith("$GNRMC")) {
    processRMC(nmea);
  }
}

void GNSSProcessor::processVTG(const String &nmea) {
  // 获取速度数据
  String kmhStr = parseNMEAField(nmea, 7);
  if (kmhStr.isEmpty()) {
    return; // 无速度数据，直接返回
  }

  float newSpeed = kmhStr.toFloat() / 3.6f;

  // 只要接收到VTG数据就更新速度和激活状态
  // 合理速度范围：0-50 m/s (0-180 km/h)，用于划船训练
  if (newSpeed >= 0.0f && newSpeed <= 50.0f) {
    speedMps = newSpeed;
  }

  // 只要接收到VTG数据就激活GNSS状态
  gnssActive = true;
  lastValidGNSS = millis();
}

void GNSSProcessor::processGGA(const String &nmea) {
  String latStr = parseNMEAField(nmea, 2);
  String latDir = parseNMEAField(nmea, 3);
  String lonStr = parseNMEAField(nmea, 4);
  String lonDir = parseNMEAField(nmea, 5);
  String fixType = parseNMEAField(nmea, 6);
  String numSatUsedStr = parseNMEAField(nmea, 7);
  String hdopStr = parseNMEAField(nmea, 8);
  String diffAgeStr = parseNMEAField(nmea, 13);
  String utcTimeStr = parseNMEAField(nmea, 1);

  // 首先检查定位质量和卫星数量
  int fixQuality = fixType.toInt();
  int satCount = numSatUsedStr.isEmpty() ? 0 : numSatUsedStr.toInt();

  // 分层处理：即使信号质量不够高，也要更新可用的数据
  bool hasBasicData = !latStr.isEmpty() && !lonStr.isEmpty() &&
                      !latDir.isEmpty() && !lonDir.isEmpty();
  bool hasGoodSignal = (fixQuality > 0 && satCount >= 4);

  // 存储定位质量供isValidFix()使用
  _fixQuality = fixQuality;

  // 总是更新可用的基础数据字段，确保UI能及时反映状态变化
  // 更新解算卫星数和可见卫星数（即使信号质量不够好）
  solvingSatellites = satCount;
  visibleSatellites = satCount;

  // 有GNSS数据但无有效值时显示"--"
  if (!hdopStr.isEmpty() && hdopStr != "0.0" && hdopStr != "0") {
    hdop = hdopStr;
  } else {
    hdop = "--"; // 有GNSS数据但HDOP无效
  }

  if (!diffAgeStr.isEmpty() && diffAgeStr != "0.0" && diffAgeStr != "0") {
    diffAge = diffAgeStr;
  } else {
    diffAge = "--"; // 有GNSS数据但差分龄期无效
  }

  // 设置定位状态（基于定位质量）
  if (fixQuality == 4) {
    fixStatus = "固定";
  } else if (fixQuality == 5) {
    fixStatus = "浮动";
  } else if (fixQuality == 2) {
    fixStatus = "SBAS";
  } else if (fixQuality == 1) {
    fixStatus = "GPS";
  } else if (fixQuality == 0) {
    fixStatus = "--";
  } else {
    fixStatus = "--";
  }

  // 如果有基础定位数据，就更新经纬度
  if (hasBasicData) {
    latitude = convertCoordinate(latStr, latDir);
    longitude = convertCoordinate(lonStr, lonDir);

    // 关键修改：只有fixQuality > 0且坐标非0时，才添加到历史缓冲区
    if (fixQuality > 0 && latitude != 0.0 && longitude != 0.0) {
      GNSSPoint newPoint;
      newPoint.latitude = latitude;
      newPoint.longitude = longitude;
      newPoint.timestamp = millis();
      newPoint.valid = true; // 来自有效定位

      _history.push_back(newPoint);

      // 维护缓冲区大小
      while (_history.size() > MAX_HISTORY_SIZE) {
        _history.pop_front();
      }
    }
    // 如果fixQuality=0，不添加到缓冲，缓冲保持在最后一个有效点
  }
  gnssActive = true;
  lastValidGNSS = millis();
}


void GNSSProcessor::processRMC(const String &nmea) {
  String utcTimeStr = parseNMEAField(nmea, 1);
  String status = parseNMEAField(nmea, 2);
  String latStr = parseNMEAField(nmea, 3);
  String latDir = parseNMEAField(nmea, 4);
  String lonStr = parseNMEAField(nmea, 5);
  String lonDir = parseNMEAField(nmea, 6);
  String speedKnotsStr = parseNMEAField(nmea, 7);
  String dateStr = parseNMEAField(nmea, 9);

  if (utcTimeStr.length() >= 6 && dateStr.length() == 6) {
    int day = dateStr.substring(0, 2).toInt();
    int month = dateStr.substring(2, 4).toInt();
    int year2 = dateStr.substring(4, 6).toInt();
    int year = (year2 >= 80) ? (1900 + year2) : (2000 + year2);

    int hour = utcTimeStr.substring(0, 2).toInt();
    int minute = utcTimeStr.substring(2, 4).toInt();
    int second = utcTimeStr.substring(4, 6).toInt();

    if (year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
        hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 &&
        second < 60) {
      _utcYear = year;
      _utcMonth = month;
      _utcDay = day;
      _utcHour = hour;
      _utcMinute = minute;
      _utcSecond = second;
      _utcSyncMillis = millis();
      _utcValid = true;
    }
  }

  if (!speedKnotsStr.isEmpty()) {
    float speedFromRmc = speedKnotsStr.toFloat() * 0.514444f;
    if (speedFromRmc >= 0.0f && speedFromRmc <= 50.0f) {
      speedMps = speedFromRmc;
    }
  }

  if (status == "A" && !latStr.isEmpty() && !lonStr.isEmpty() &&
      !latDir.isEmpty() && !lonDir.isEmpty()) {
    double lat = convertCoordinate(latStr, latDir);
    double lon = convertCoordinate(lonStr, lonDir);
    if (lat != 0.0 || lon != 0.0) {
      latitude = lat;
      longitude = lon;
    }
  }

  if (utcTimeStr.length() >= 6 && _utcYear > 0) {
    _utcHour = utcTimeStr.substring(0, 2).toInt();
    _utcMinute = utcTimeStr.substring(2, 4).toInt();
    _utcSecond = utcTimeStr.substring(4, 6).toInt();
    _utcSyncMillis = millis();
    _utcValid = true;
  }

  gnssActive = true;
  lastValidGNSS = millis();
}
String GNSSProcessor::parseNMEAField(const String &s, uint8_t index) {
  uint8_t count = 0;
  int start = 0;
  for (int i = 0; i < s.length(); i++) {
    if (s[i] == ',' || s[i] == '*') {
      if (count++ == index)
        return s.substring(start, i);
      start = i + 1;
    }
  }
  return "";
}

double GNSSProcessor::convertCoordinate(const String &coord,
                                        const String &direction) {
  if (coord.length() < 4)
    return 0.0;

  // NMEA格式: ddmm.mmmm (纬度) 或 dddmm.mmmm (经度)
  int dotIndex = coord.indexOf('.');
  if (dotIndex == -1)
    return 0.0;

  // 根据方向确定度数部分的长度
  int degreeLen;
  if (direction == "N" || direction == "S") {
    degreeLen = 2; // 纬度是2位度数
  } else {         // "E" or "W"
    degreeLen = 3; // 经度是3位度数
  }

  // 检查坐标字符串长度是否足够
  if (dotIndex < degreeLen + 2)
    return 0.0;

  // 提取度数部分
  double degrees = coord.substring(0, degreeLen).toDouble();

  // 提取分钟部分
  double minutes = coord.substring(degreeLen).toDouble();

  // 转换为十进制度数
  double decimal = degrees + (minutes / 60.0);

  // 应用方向
  if (direction == "S" || direction == "W") {
    decimal = -decimal;
  }

  return decimal;
}

double GNSSProcessor::getPace() const {
  if (speedMps <= 0.0f)
    return 0.0;
  // 返回每500米所需的秒数
  return 500.0 / speedMps;
}

String GNSSProcessor::getPaceString() const {
  // UI 显示逻辑：速度过低时显示默认值，避免显示不合理的配速
  // 当速度 < 0.85 m/s (约 3 km/h) 时，显示默认值 "00:00.0"
  // 注意：getPace() 仍返回真实值用于 JSON 数据记录
  if (speedMps < 0.85f)
    return "00:00.0";

  float timePer500m = 500.0f / speedMps;
  int minutes = static_cast<int>(timePer500m / 60);
  float seconds = timePer500m - minutes * 60;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%04.1f", minutes, seconds);
  return String(buf);
}

float GNSSProcessor::getPower() const {
  float roundedSpeed = roundf(speedMps * 100.0f) / 100.0f;
  return 2.8f * roundedSpeed * roundedSpeed * roundedSpeed;
}

GNSSPoint
GNSSProcessor::getInterpolatedPosition(unsigned long target_timestamp) {
  GNSSPoint result;
  result.timestamp = target_timestamp;

  // GNSS 历史数据不足时无法插值，等待下一帧
  if (_history.empty()) {
    result.latitude = latitude;
    result.longitude = longitude;
    result.valid = false;
    return result;
  }

  if (_history.size() < 2) {
    result.latitude = _history.back().latitude;
    result.longitude = _history.back().longitude;
    result.valid = false;
    return result;
  }

  GNSSPoint *pointA = nullptr;
  GNSSPoint *pointB = nullptr;

  for (size_t i = 0; i < _history.size() - 1; i++) {
    if (_history[i].timestamp <= target_timestamp &&
        _history[i + 1].timestamp >= target_timestamp) {
      pointA = &_history[i];
      pointB = &_history[i + 1];
      break;
    }
  }

  if (!pointA || !pointB) {
    if (target_timestamp >= _history.back().timestamp) {
      result.latitude = _history.back().latitude;
      result.longitude = _history.back().longitude;
    } else {
      result.latitude = _history.front().latitude;
      result.longitude = _history.front().longitude;
    }
    result.valid = false;
    return result;
  }

  unsigned long timeA = pointA->timestamp;
  unsigned long timeB = pointB->timestamp;

  if (timeB == timeA) {
    result.latitude = pointA->latitude;
    result.longitude = pointA->longitude;
    result.valid = false;
    return result;
  }

  double ratio = static_cast<double>(target_timestamp - timeA) /
                 static_cast<double>(timeB - timeA);

  result.latitude =
      pointA->latitude + (pointB->latitude - pointA->latitude) * ratio;
  result.longitude =
      pointA->longitude + (pointB->longitude - pointA->longitude) * ratio;
  result.valid = true;

  return result;
}

bool GNSSProcessor::isLeapYear(int year) const {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int GNSSProcessor::daysInMonth(int year, int month) const {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 30;
  }
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return kDays[month - 1];
}

void GNSSProcessor::adjustDateByDays(int &year, int &month, int &day,
                                     int deltaDays) const {
  while (deltaDays > 0) {
    day++;
    int dim = daysInMonth(year, month);
    if (day > dim) {
      day = 1;
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    }
    deltaDays--;
  }

  while (deltaDays < 0) {
    day--;
    if (day < 1) {
      month--;
      if (month < 1) {
        month = 12;
        year--;
      }
      day = daysInMonth(year, month);
    }
    deltaDays++;
  }
}

String GNSSProcessor::getDateTimeString() const {
  return getDateTimeStringForMillis(millis());
}

String GNSSProcessor::getDateTimeStringForMillis(unsigned long targetMillis) const {
  if (!_utcValid || _utcYear < 2024) {
    return "";
  }

  int year = _utcYear;
  int month = _utcMonth;
  int day = _utcDay;
  int hour = _utcHour;
  int minute = _utcMinute;
  int second = _utcSecond;

  int64_t deltaMs = (int64_t)(int32_t)(targetMillis - _utcSyncMillis);
  int64_t deltaSec = deltaMs / 1000;
  int64_t remMs = deltaMs % 1000;
  if (remMs < 0) {
    remMs += 1000;
    deltaSec -= 1;
  }

  int64_t todSec = (int64_t)hour * 3600 + (int64_t)minute * 60 + second + deltaSec;
  int dayDelta = 0;
  while (todSec < 0) {
    todSec += 86400;
    dayDelta--;
  }
  while (todSec >= 86400) {
    todSec -= 86400;
    dayDelta++;
  }
  if (dayDelta != 0) {
    adjustDateByDays(year, month, day, dayDelta);
  }

  hour = (int)(todSec / 3600);
  minute = (int)((todSec % 3600) / 60);
  second = (int)(todSec % 60);

  char out[32];
  snprintf(out, sizeof(out), "%04d-%02d-%02d %02d:%02d:%02d.%03d", year, month,
           day, hour, minute, second, (int)remMs);
  return String(out);
}
void GNSSProcessor::clearData() {
  speedMps = 0.0f;
  gnssActive = false;
  latitude = 0.0;
  longitude = 0.0;
  _utcValid = false;
  _utcYear = 0;
  _utcMonth = 0;
  _utcDay = 0;
  _utcHour = 0;
  _utcMinute = 0;
  _utcSecond = 0;
  _utcSyncMillis = 0;
  // 不再设置默认值，让UI保持SquareLine Studio的默认值
  // hdop, fixStatus, diffAge 等字符串字段不清空
  // solvingSatellites, visibleSatellites 等计数字段不重置
  _history.clear(); // 清空历史数据
}

bool GNSSProcessor::isValidFix() const {
  return _fixQuality > 0 && latitude != 0.0 && longitude != 0.0 &&
         !isnan(latitude) && !isnan(longitude);
}
