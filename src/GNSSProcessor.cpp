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

  // 卫星数据清理现在在processGSV()中处理，这里不需要重复计算
}

void GNSSProcessor::setSDCardManager(SDCardManager *sd) { _sdCard = sd; }

void GNSSProcessor::processNMEA(const String &nmea) {
  // 记录原始 NMEA 数据到 SD 卡
  if (_sdCard != nullptr) {
    _sdCard->logNmeaRaw(nmea);
  }

  if (nmea.startsWith("$GPVTG") || nmea.startsWith("$GNVTG")) {
    processVTG(nmea);
  } else if (nmea.startsWith("$GPGGA") || nmea.startsWith("$GNGGA")) {
    processGGA(nmea);
  } else if (nmea.indexOf("GSV") == 3) {
    // 匹配所有 $xxGSV 格式的句子（GPS/GLONASS/BeiDou/Galileo/QZSS/GNSS）
    // $GPGSV, $GLGSV, $BDGSV, $GBGSV, $GAGSV, $GQGSV, $GNGSV 等
    processGSV(nmea);
#ifdef DEBUG_GSV_PRINT
    printGSVPRNs(nmea);
#endif
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
  // 更新解算卫星数（即使信号质量不够好）
  solvingSatellites = satCount;

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

  // **优化3：兜底校验，确保逻辑一致性**
  // 如果参与解算的卫星数大于可见卫星数，说明可见数统计有问题
  // 使用参与解算数作为可见数的下限
  if (solvingSatellites > visibleSatellites) {
    visibleSatellites = solvingSatellites;
  }
}

void GNSSProcessor::processGSV(const String &nmea) {
  // 解析 GSV 字段：字段 1=total msgs, 2=msg num, 3=sats in view, 后续每 4
  // 字段为一个卫星 (PRN, Elev, Az, SNR)
  int totalMsg = parseNMEAField(nmea, 1).toInt();
  int msgNum = parseNMEAField(nmea, 2).toInt();
  int satsInView = parseNMEAField(nmea, 3).toInt();

  // 确定星座类型的偏移量，避免PRN重复
  // 基于实际数据中存在的 GSV 类型：$GPGSV/$GLGSV/$GBGSV/$GAGSV/$GQGSV
  int constellationOffset = 0;
  if (nmea.startsWith("$GPGSV")) {
    constellationOffset = 0; // GPS: 1-32
  } else if (nmea.startsWith("$GLGSV")) {
    constellationOffset = 100; // GLONASS: 101-132
  } else if (nmea.startsWith("$GBGSV")) {
    constellationOffset = 200; // BeiDou: 201-232
  } else if (nmea.startsWith("$GAGSV")) {
    constellationOffset = 300; // Galileo: 301-332
  } else if (nmea.startsWith("$GQGSV")) {
    constellationOffset = 500; // QZSS: 501-532
  } else if (nmea.startsWith("$GNGSV")) {
    constellationOffset = 400; // 混合: 401+
  }

  // 更新每个卫星的最后看到时间，使用偏移量避免PRN冲突
  uint32_t now = millis();
  for (int field = 4; field < 20; field += 4) {
    String prnStr = parseNMEAField(nmea, field);
    if (!prnStr.isEmpty()) {
      int prn = prnStr.toInt();
      // 过滤无效 PRN（0 或负数）
      if (prn > 0) {
        int uniquePrn = prn + constellationOffset;
        prnLastSeen[uniquePrn] = now;
      }
    }
    // 即使 PRN 为空，也不影响 satsInView 的使用
  }

  // **优化1：立即更新可见卫星数**
  // 取三个来源的最大值：
  // 1. GSV 报告的 satsInView（权威来源）
  // 2. 当前统计的唯一 PRN 数量
  // 3. 已有的 visibleSatellites（避免回退）
  int currentUniquePRNs = static_cast<int>(prnLastSeen.size());
  visibleSatellites =
      max(visibleSatellites, max(satsInView, currentUniquePRNs));

  // **优化2：定期清理过期PRN，使用 PRN_TTL_MS 常量**
  static uint32_t lastCleanup = 0;
  // 使用 PRN_TTL_MS 的一半作为清理周期（1500ms），确保及时响应
  if (now - lastCleanup > PRN_TTL_MS / 2) {
    auto it = prnLastSeen.begin();
    while (it != prnLastSeen.end()) {
      if (now - it->second > PRN_TTL_MS) { // 使用头文件定义的常量（3000ms）
        it = prnLastSeen.erase(it);
      } else {
        ++it;
      }
    }
    lastCleanup = now;

    // 清理后更新可见卫星数
    visibleSatellites = static_cast<int>(prnLastSeen.size());
  }
}

// Debug: 打印单条 GSV 中解析到的 PRN，方便与上位机对比
void GNSSProcessor::printGSVPRNs(const String &nmea) {
  std::vector<int> prns;
  for (int field = 4; field < 20; field += 4) {
    String prnStr = parseNMEAField(nmea, field);
    if (!prnStr.isEmpty()) {
      prns.push_back(prnStr.toInt());
    }
  }

  // 打印原始句子和提取到的 PRN 列表
  Serial.print("GSV: ");
  Serial.println(nmea);
  Serial.print("Parsed PRNs: ");
  if (prns.empty()) {
    Serial.println("(none)");
  } else {
    for (size_t i = 0; i < prns.size(); ++i) {
      if (i)
        Serial.print(",");
      Serial.print(prns[i]);
    }
    Serial.println();
  }
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
void GNSSProcessor::clearData() {
  speedMps = 0.0f;
  gnssActive = false;
  latitude = 0.0;
  longitude = 0.0;
  // 不再设置默认值，让UI保持SquareLine Studio的默认值
  // hdop, fixStatus, diffAge 等字符串字段不清空
  // solvingSatellites, visibleSatellites 等计数字段不重置
  prnLastSeen.clear();
  _history.clear(); // 清空历史数据
}

bool GNSSProcessor::isValidFix() const {
  return _fixQuality > 0 && latitude != 0.0 && longitude != 0.0 &&
         !isnan(latitude) && !isnan(longitude);
}
