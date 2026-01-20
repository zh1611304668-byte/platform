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
  bool isValidFix() const;                  // 判断当前定位是否有效
  void setSDCardManager(SDCardManager *sd); // 设置SD卡管理器引用

private:
  void processNMEA(const String &nmea);
  void processVTG(const String &nmea);
  void processGGA(const String &nmea);
  void processGSV(const String &nmea);
  // Debug helper: print PRNs parsed from a single GSV sentence to Serial
  void printGSVPRNs(const String &nmea);
  String parseNMEAField(const String &s, uint8_t index);
  double convertCoordinate(const String &coord, const String &direction);
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
  int solvingSatellites;
  int visibleSatellites;
  uint32_t lastVisibleRecalc = 0;
  std::map<int, uint32_t> prnLastSeen;
  static const uint32_t PRN_TTL_MS = 3000;
  int _fixQuality = 0; // 当前定位质量（0=无定位，1=GPS，4=RTK固定等）

  // 用于坐标插值的历史数据
  std::deque<GNSSPoint> _history;
  static const size_t MAX_HISTORY_SIZE = 4;

  // SD卡管理器引用（用于记录NMEA原始数据）
  SDCardManager *_sdCard = nullptr;
};

#endif
