/*
 * File: GNSSProcessor.h
 * Purpose: Declares interfaces, types, and constants for the G N S S Processor module.
 */
#ifndef GNSS_PROCESSOR_H
#define GNSS_PROCESSOR_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <deque>
#include <map>

// 前置声明
class SDCardManager;

// 包含经纬度和时间戳的GNSS数据点
struct GNSSPoint {
  double latitude = 0.0;
  double longitude = 0.0;
  unsigned long timestamp = 0; // millis()
  bool valid = true;           // 坐标是否有效（来自有效定位）
};

class GNSSProcessor {
public:
  GNSSProcessor(HardwareSerial &serial, uint8_t rxPin, uint8_t txPin,
                uint32_t baud = 115200);
  void begin();
  void process();
  float getSpeed() const { return speedMps; }
  float getPower() const;
  double getPace() const;
  String getPaceString() const;
  double getLatitude() const { return latitude; }
  double getLongitude() const { return longitude; }
  String getHDOP() const { return hdop; }
  int getSolvingSatellites() const { return solvingSatellites; }
  int getVisibleSatellites() const { return visibleSatellites; }
  bool isActive() const { return gnssActive; }
  bool hasDataReceived() const {
    return lastValidGNSS > 0 && (millis() - lastValidGNSS) < 10000;
  } // 收到过数据且10秒内有效
  String getFixStatus() const { return fixStatus; }
  String getDiffAge() const { return diffAge; }
  GNSSPoint getInterpolatedPosition(unsigned long target_timestamp);
  String getDateTimeString() const;
  String getDateTimeStringForMillis(unsigned long targetMillis) const;
  bool isValidFix() const;                  // 判断当前定位是否有效
  void setSDCardManager(SDCardManager *sd); // 设置SD卡管理器引用

private:
  void processNMEA(const String &nmea);
  void processVTG(const String &nmea);
  void processGGA(const String &nmea);
  void processRMC(const String &nmea);
  String parseNMEAField(const String &s, uint8_t index);
  double convertCoordinate(const String &coord, const String &direction);
  bool isLeapYear(int year) const;
  int daysInMonth(int year, int month) const;
  void adjustDateByDays(int &year, int &month, int &day, int deltaDays) const;
  void clearData();
  HardwareSerial *gnssSerial;
  uint8_t rxPin, txPin;
  uint32_t baudRate;
  String nmeaBuffer;
  float speedMps = 0.0f;
  bool gnssActive = false;
  uint32_t lastValidGNSS = 0;

  double latitude = 0.0;
  double longitude = 0.0;
  String hdop;
  String fixStatus;
  String diffAge;
  int solvingSatellites = 0;
  int visibleSatellites = 0;
  int _fixQuality = 0; // fix quality
  bool _utcValid = false;
  int _utcYear = 0;
  int _utcMonth = 0;
  int _utcDay = 0;
  int _utcHour = 0;
  int _utcMinute = 0;
  int _utcSecond = 0;
  unsigned long _utcSyncMillis = 0;

  // 用于坐标插值的历史数据
  std::deque<GNSSPoint> _history;
  static const size_t MAX_HISTORY_SIZE = 4;

  // SD卡管理器引用（用于记录NMEA原始数据）
  SDCardManager *_sdCard = nullptr;
};

#endif
