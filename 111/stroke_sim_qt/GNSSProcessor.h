#ifndef GNSS_PROCESSOR_H
#define GNSS_PROCESSOR_H

#include "PlatformDefines.h"
#include <deque>
#include <map>
#ifndef ARDUINO
// PC includes
#endif

// Forward declarations
#ifdef ARDUINO
class HardwareSerial;
#endif

struct GNSSPoint {
  double latitude;
  double longitude;
  unsigned long timestamp;
  bool valid;
};

class GNSSProcessor {
public:
#ifdef ARDUINO
  GNSSProcessor(HardwareSerial &serial, uint8_t rxPin, uint8_t txPin,
                uint32_t baud = 115200);
#else
  GNSSProcessor(); // PC constructor
#endif
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
  String getDateTimeString() const { return ""; }
  bool isValidFix() const; // 判断当前定位是否有效
  void
  processNMEA(const String &nmea,
              unsigned long timestamp = 0); // Made public for simulator use

private:
  void processVTG(const String &nmea, unsigned long timestamp = 0);
  void processGGA(const String &nmea, unsigned long timestamp = 0);
  // Debug helper: print PRNs parsed from a single GSV sentence to Serial
  void printGSVPRNs(const String &nmea);
  String parseNMEAField(const String &s, uint8_t index);
  double convertCoordinate(const String &coord, const String &direction);
  void clearData();

#ifdef ARDUINO
  HardwareSerial *gnssSerial;
  uint8_t rxPin, txPin;
  uint32_t baudRate;
#endif

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
  uint32_t lastVisibleRecalc = 0;
  std::map<int, uint32_t> prnLastSeen;
  static const uint32_t PRN_TTL_MS = 3000;
  int _fixQuality = 0; // 当前定位质量（0=无定位，1=GPS，4=RTK固定等）

  // 用于坐标插值的历史数据
  std::deque<GNSSPoint> _history;
  static const size_t MAX_HISTORY_SIZE = 4;
};

#endif
