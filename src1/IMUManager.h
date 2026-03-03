/*
 * File: IMUManager.h
 * Purpose: Declares interfaces, types, and constants for the I M U Manager module.
 */
/**
 * @file IMUManager.h
 * @brief 鍒掓〃妫€娴嬬鐞嗗櫒 - 鍩轰簬QMI8658 IMU鐨勫垝妗ㄦ娴嬬畻娉?
 *
 * 鏍稿績鍔熻兘:
 * - 鑷姩閫夋嫨娲昏穬杞达紙鏍囧噯宸渶澶х殑杞达級
 * - 鍙屼汉妗ㄥ嘲鍊兼娴嬶紙绐楀彛鍐呴€夋渶澶у嘲锛?
 * - 鎸箙楠岃瘉锛堝嘲璋峰樊鈮?.25g锛?
 * - 妗ㄩ璁＄畻涓嶦MA骞虫粦
 * - GNSS鍧愭爣鎻掑€艰绠楀垝璺?
 */

#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "SensorQMI8658.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <queue>
#include <vector>

class GNSSProcessor;
void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);

/**
 * @brief 鍗曟鍒掓〃鐨勫畬鏁存暟鎹?
 */
struct StrokeMetrics {
  int strokeNumber;        ///< 妗ㄦ暟
  unsigned long timestamp; ///< 鏃堕棿鎴?ms)
  double startLat;         ///< 璧风偣绾害
  double startLon;         ///< 璧风偣缁忓害
  double endLat;           ///< 缁堢偣绾害
  double endLon;           ///< 缁堢偣缁忓害
  float strokeDistance;    ///< 鏈〃璺濈(m)
  float totalDistance;     ///< 绱璺濈(m)
  bool gnssValid;          ///< GNSS鏄惁鏈夋晥
  bool distanceValid;      ///< 璺濈鏄惁鏈夋晥

  StrokeMetrics()
      : strokeNumber(0), timestamp(0), startLat(0.0), startLon(0.0),
        endLat(0.0), endLon(0.0), strokeDistance(0.0f), totalDistance(0.0f),
        gnssValid(false), distanceValid(false) {}
};

/**
 * @brief IMU绠＄悊鍣?- 鍒掓〃妫€娴嬫牳蹇冪被
 */
class IMUManager {
  friend void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);

public:
  // ==================== 鍙皟鍙傛暟 ====================

  /// @name 鐘舵€佹満甯搁噺
  /// @{
  static constexpr uint8_t STATE_BACKGROUND = 0;  ///< 鑳屾櫙(绛夊緟杩涘叆娉㈠嘲鍖?
  static constexpr uint8_t STATE_PEAK_ZONE = 1;   ///< 娉㈠嘲鍖?
  static constexpr uint8_t STATE_TROUGH_ZONE = 2; ///< 娉㈣胺鍖?
  static constexpr uint8_t STATE_COOLDOWN = 3;    ///< 鍐峰嵈鏈?
  /// @}

  /// @name 閲囨牱鍙傛暟
  /// @{
  static constexpr int WINDOW_SIZE = 125;         ///< 婊戝姩绐楀彛(閲囨牱鐐?
  static constexpr uint32_t SAMPLE_INTERVAL = 16; ///< 閲囨牱闂撮殧(ms)
  /// @}

  /// @name 鍔ㄦ€侀槇鍊煎弬鏁?
  /// @{
  static constexpr float PEAK_ENTER_FACTOR = 1.1f;  ///< 杩涘叆娉㈠嘲鍖洪槇鍊肩郴鏁?
  static constexpr float TROUGH_THRESHOLD = -0.01f; ///< 娉㈣胺闃堝€?(g)
  static constexpr float RECOVERY_FACTOR = 1.05f;   ///< 鎭㈠闃堝€肩郴鏁?
  static constexpr float MIN_PEAK_ABSOLUTE = 0.04f; ///< 娉㈠嘲鏈€灏忕粷瀵归珮搴?(g)
  static constexpr float MIN_AMPLITUDE = 0.03f;     ///< 鏈€灏忓嘲璋锋尟骞?(g)
  /// @}

  /// @name 鏃堕棿鍙傛暟
  /// @{
  static constexpr uint32_t MIN_PEAK_DURATION = 15; ///< 娉㈠嘲鍖烘渶灏忔寔缁椂闂?ms)
  static constexpr uint32_t MIN_TROUGH_DURATION =
      30; ///< 娉㈣胺鍖烘渶灏忔寔缁椂闂?ms)
  static constexpr uint32_t STROKE_MIN_INTERVAL = 150; ///< 涓ゆ鍒掓〃鏈€灏忛棿闅?ms)
  static constexpr uint32_t COOLDOWN_DURATION = 100;   ///< 鍐峰嵈鏃堕棿(ms)
  static constexpr uint32_t STROKE_TIMEOUT = 8000;     ///< 瓒呮椂閲嶇疆(ms)
  static constexpr int RECOVERY_SAMPLES = 5;           ///< 鎭㈠妫€娴嬭繛缁噰鏍锋暟
  /// @}

  /// @name 婊ゆ尝鍙傛暟
  /// @{
  static constexpr float EMA_ALPHA = 0.3f; ///< 妗ㄩEMA骞虫粦绯绘暟
  static constexpr float EMA_FILTER_ALPHA =
      0.25f; ///< 淇″彿浜屾骞虫粦绯绘暟 (鎻愬崌鍝嶅簲閫熷害)
  /// @}

  // ==================== I2C寮曡剼 ====================
#ifndef SENSOR_SDA
#define SENSOR_SDA 8
#endif
#ifndef SENSOR_SCL
#define SENSOR_SCL 7
#endif

  // ==================== 鍏叡鎺ュ彛 ====================

  IMUManager(int sda = SENSOR_SDA, int scl = SENSOR_SCL,
             GNSSProcessor *gnss = nullptr);

  void begin();  ///< 鍒濆鍖栦紶鎰熷櫒
  void update(); ///< 涓诲惊鐜皟鐢紝澶勭悊IMU鏁版嵁

  /// @name 妗ㄩ鏁版嵁
  /// @{
  float getStrokeRate() const; ///< 鑾峰彇妗ㄩ(SPM)
  int getActiveAxis() const;   ///< 鑾峰彇娲昏穬杞?0:X, 1:Y, 2:Z)
  int getStrokeCount() const;  ///< 鑾峰彇鎬绘〃鏁?
  void resetStrokeCount();     ///< 閲嶇疆妗ㄦ暟
  /// @}

  /// @name 璺濈鏁版嵁
  /// @{
  float getStrokeDistance() const; ///< 鑾峰彇鍗曟〃璺濈(m)
  float getTotalDistance() const;  ///< 鑾峰彇绱璺濈(m)
  void resetTotalDistance();       ///< 閲嶇疆璺濈
  /// @}

  /// @name 鍘熷鏁版嵁
  /// @{
  void getAcceleration(float &ax, float &ay, float &az); ///< 鑾峰彇鍔犻€熷害(g)
  /// @}

  /// @name 鍒掓〃浜嬩欢
  /// @{
  const StrokeMetrics &getLastStrokeMetrics() const; ///< 鑾峰彇鏈€鍚庡垝妗ㄦ暟鎹?
  bool hasNewStroke() const;                         ///< 鏄惁鏈夋柊鍒掓〃
  void clearNewStrokeFlag();                         ///< 娓呴櫎鏂板垝妗ㄦ爣蹇?
  bool hasLastStrokeSegment() const;
  double getLastStrokeEndLatitude() const;
  double getLastStrokeEndLongitude() const;
  /// @}

private:
  // ==================== 纭欢鐩稿叧 ====================
  int _sda, _scl;
  SensorQMI8658 _qmi;
  IMUdata _acc;
  bool _sensorFound;

  // ==================== IMU鏁版嵁 ====================
  float _accX, _accY, _accZ; ///< 褰撳墠鍔犻€熷害(g)
  bool _dataValid;
  std::deque<float> _accelHistory[3]; ///< 涓夎酱鍘嗗彶鏁版嵁 (deque鍏佽閬嶅巻)
  std::deque<float> _sortedAccel[3];
  float _lastAccel[3];
  float _axisVariances[3];

  // ==================== 鍒掓〃妫€娴嬬姸鎬?====================
  int _strokeCount = 0;
  float _strokeRate;
  int _activeAxis;      ///< 0:X, 1:Y, 2:Z (鍥哄畾涓篫杞?
  uint8_t _strokeState; ///< 4闃舵鐘舵€佹満
  uint32_t _lastStrokeTime = 0;

  // ==================== 鐩镐綅璺熻釜 (4闃舵鐘舵€佹満) ====================
  uint32_t _phaseStartTime = 0;    ///< 褰撳墠鐩镐綅寮€濮嬫椂闂?
  float _peakMaxValue = 0.0f;      ///< 娉㈠嘲鍖烘渶澶у亸宸?
  uint32_t _peakMaxTime = 0;       ///< 娉㈠嘲鍖烘渶澶у€兼椂闂?
  float _peakMaxFiltered = 0.0f;   ///< 娉㈠嘲鍖烘渶澶ф护娉㈠€?
  float _troughMinValue = 0.0f;    ///< 娉㈣胺鍖烘渶灏忓亸宸?
  uint32_t _troughMinTime = 0;     ///< 娉㈣胺鍖烘渶灏忓€兼椂闂?
  float _troughMinFiltered = 0.0f; ///< 娉㈣胺鍖烘渶灏忔护娉㈠€?
  bool _peakHasGrowth = false; ///< 波峰区是否出现过真实上升
  // 鎭㈠妫€娴嬭鏁板櫒
  int _recoveryCounter = 0;

  // 鑳屾櫙缁熻
  float _backgroundMean = 0.0f;
  float _backgroundStd = 0.1f;

  // 鏍″噯鐘舵€?
  bool _isCalibrating = true;
  bool _calibrationComplete = false;
  static constexpr int CALIBRATION_SAMPLES =
      100; ///< 鏍″噯鎵€闇€鏍锋湰鏁?(100鏍锋湰@125Hz=0.8绉?

  // ==================== Butterworth婊ゆ尝鍣?(3Hz) ====================
  float _bw_x1[3] = {0}, _bw_x2[3] = {0};
  float _bw_y1[3] = {0}, _bw_y2[3] = {0};
  // fc=3Hz, fs=125Hz Butterworth绯绘暟
  static constexpr float _bw_b0 = 0.00515f;
  static constexpr float _bw_b1 = 0.01030f;
  static constexpr float _bw_b2 = 0.00515f;
  static constexpr float _bw_a1 = -1.7873f;
  static constexpr float _bw_a2 = 0.8080f;

  // ==================== EMA浜屾婊ゆ尝鍣?====================
  float _ema_value[3] = {0.0f, 0.0f, 0.0f};

  // ==================== GNSS涓庤窛绂?====================
  GNSSProcessor *_gnss;
  float _strokeDistance = 0.0f;
  float _totalDistance = 0.0f;
  int _lastStrokeCountForDistance = 0;

  double _prevStrokeLat = 0.0, _prevStrokeLon = 0.0;
  double _lastStrokeStartLat = 0.0, _lastStrokeStartLon = 0.0;
  double _lastStrokeEndLat = 0.0, _lastStrokeEndLon = 0.0;
  double _lastValidGnssLat = 0.0, _lastValidGnssLon = 0.0;
  bool _hasInitialStrokePosition = false;
  bool _hasLastStrokeSegment = false;

  // ==================== StrokeMetrics ====================
  StrokeMetrics _lastStrokeMetrics;
  bool _hasNewStroke = false;

  // ==================== 绉佹湁鏂规硶 ====================
  void _initSensor();
  void _calculateStrokeRate();
  void _processAccelerationData(float accX, float accY, float accZ);
  bool _acquireStrokePosition(double &lat, double &lon);
  float _butterworthFilter(float input, int axis);
  float _emaFilter(float input, int axis); ///< EMA浜屾骞虫粦婊ゆ尝
};

#endif // IMU_MANAGER_H






