/**
 * @file IMUManager.h
 * @brief 划桨检测管理器 - 基于QMI8658 IMU的划桨检测算法
 *
 * 核心功能:
 * - 自动选择活跃轴（标准差最大的轴）
 * - 双人桨峰值检测（窗口内选最大峰）
 * - 振幅验证（峰谷差≥0.25g）
 * - 桨频计算与EMA平滑
 * - GNSS坐标插值计算划距
 */

#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "SensorQMI8658.hpp"
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
 * @brief 单次划桨的完整数据
 */
struct StrokeMetrics {
  int strokeNumber;        ///< 桨数
  unsigned long timestamp; ///< 时间戳(ms)
  double startLat;         ///< 起点纬度
  double startLon;         ///< 起点经度
  double endLat;           ///< 终点纬度
  double endLon;           ///< 终点经度
  float strokeDistance;    ///< 本桨距离(m)
  float totalDistance;     ///< 累计距离(m)
  bool gnssValid;          ///< GNSS是否有效
  bool distanceValid;      ///< 距离是否有效

  StrokeMetrics()
      : strokeNumber(0), timestamp(0), startLat(0.0), startLon(0.0),
        endLat(0.0), endLon(0.0), strokeDistance(0.0f), totalDistance(0.0f),
        gnssValid(false), distanceValid(false) {}
};

/**
 * @brief IMU管理器 - 划桨检测核心类
 */
class IMUManager {
  friend void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);

public:
  // ==================== 可调参数 ====================

  /// @name 状态机常量
  /// @{
  static constexpr uint8_t STATE_BACKGROUND = 0;  ///< 背景(等待进入波峰区)
  static constexpr uint8_t STATE_PEAK_ZONE = 1;   ///< 波峰区
  static constexpr uint8_t STATE_TROUGH_ZONE = 2; ///< 波谷区
  static constexpr uint8_t STATE_COOLDOWN = 3;    ///< 冷却期
  /// @}

  /// @name 采样参数
  /// @{
  static constexpr int WINDOW_SIZE =
      200; ///< 滑动窗口(采样点) - 300点=3秒@100Hz

  static constexpr uint32_t SAMPLE_INTERVAL = 8;            ///< 采样间隔(ms)
  static constexpr uint32_t AXIS_SELECTION_INTERVAL = 1000; ///< 轴选择间隔(ms)
  /// @}

  /// @name 动态阈值参数 (与Python模拟器完全一致)
  /// @{
  static constexpr float PEAK_ENTER_FACTOR =
      1.5f; ///< 进入波峰区阈值系数 (factor * std)
  static constexpr float TROUGH_THRESHOLD = -0.02f; ///< 波谷阈值 (g)
  static constexpr float RECOVERY_FACTOR = 1.3f;    ///< 恢复阈值系数
  static constexpr float MIN_PEAK_ABSOLUTE = 0.08f; ///< 波峰最小绝对高度 (g)
  static constexpr float MIN_AMPLITUDE = 0.05f;     ///< 最小峰谷振幅 (g)

  /// @}

  /// @name 时间参数
  /// @{
  static constexpr uint32_t MIN_PEAK_DURATION = 30; ///< 波峰区最小持续时间(ms)
  static constexpr uint32_t MIN_TROUGH_DURATION =
      50; ///< 波谷区最小持续时间(ms)
  static constexpr uint32_t STROKE_MIN_INTERVAL = 800; ///< 两次划桨最小间隔(ms)
  static constexpr uint32_t COOLDOWN_DURATION = 300;   ///< 冷却时间(ms)
  static constexpr uint32_t STROKE_TIMEOUT = 8000;     ///< 超时重置(ms)
  static constexpr int RECOVERY_SAMPLES = 5;           ///< 恢复检测连续采样数
  /// @}

  /// @name 滤波参数
  /// @{
  static constexpr float EMA_ALPHA = 0.3f; ///< 桨频EMA平滑系数
  static constexpr float EMA_FILTER_ALPHA =
      0.25f; ///< 信号二次平滑系数 (提升响应速度)
  /// @}

  // ==================== I2C引脚 ====================
#ifndef SENSOR_SDA
#define SENSOR_SDA 8
#endif
#ifndef SENSOR_SCL
#define SENSOR_SCL 7
#endif

  // ==================== 公共接口 ====================

  IMUManager(int sda = SENSOR_SDA, int scl = SENSOR_SCL,
             GNSSProcessor *gnss = nullptr);

  void begin();  ///< 初始化传感器
  void update(); ///< 主循环调用，处理IMU数据

  /// @name 桨频数据
  /// @{
  float getStrokeRate() const; ///< 获取桨频(SPM)
  int getActiveAxis() const;   ///< 获取活跃轴(0:X, 1:Y, 2:Z)
  int getStrokeCount() const;  ///< 获取总桨数
  void resetStrokeCount();     ///< 重置桨数
  /// @}

  /// @name 距离数据
  /// @{
  float getStrokeDistance() const; ///< 获取单桨距离(m)
  float getTotalDistance() const;  ///< 获取累计距离(m)
  void resetTotalDistance();       ///< 重置距离
  /// @}

  /// @name 原始数据
  /// @{
  void getAcceleration(float &ax, float &ay, float &az); ///< 获取加速度(g)
  /// @}

  /// @name 划桨事件
  /// @{
  const StrokeMetrics &getLastStrokeMetrics() const; ///< 获取最后划桨数据
  bool hasNewStroke() const;                         ///< 是否有新划桨
  void clearNewStrokeFlag();                         ///< 清除新划桨标志
  bool hasLastStrokeSegment() const;
  double getLastStrokeEndLatitude() const;
  double getLastStrokeEndLongitude() const;
  /// @}

private:
  // ==================== 硬件相关 ====================
  int _sda, _scl;
  SensorQMI8658 _qmi;
  IMUdata _acc;
  bool _sensorFound;

  // ==================== IMU数据 ====================
  float _accX, _accY, _accZ; ///< 当前加速度(g)
  bool _dataValid;
  std::deque<float> _accelHistory[3]; ///< 三轴历史数据 (deque允许遍历)
  std::deque<float> _sortedAccel[3];
  float _lastAccel[3];
  float _axisVariances[3];

  // ==================== 划桨检测状态 ====================
  int _strokeCount = 0;
  float _strokeRate;
  int _activeAxis;      ///< 0:X, 1:Y, 2:Z
  uint8_t _strokeState; ///< 4阶段状态机
  uint32_t _lastStrokeTime = 0;
  uint32_t _lastAxisSelection;

  // ==================== 相位跟踪 (4阶段状态机) ====================
  uint32_t _phaseStartTime = 0;    ///< 当前相位开始时间
  float _peakMaxValue = 0.0f;      ///< 波峰区最大偏差
  uint32_t _peakMaxTime = 0;       ///< 波峰区最大值时间
  float _peakMaxFiltered = 0.0f;   ///< 波峰区最大滤波值
  float _troughMinValue = 0.0f;    ///< 波谷区最小偏差
  uint32_t _troughMinTime = 0;     ///< 波谷区最小值时间
  float _troughMinFiltered = 0.0f; ///< 波谷区最小滤波值

  // 恢复检测计数器
  int _recoveryCounter = 0;

  // 背景统计
  float _backgroundMean = 0.0f;
  float _backgroundStd = 0.1f;

  // 校准状态
  bool _isCalibrating = true;
  bool _calibrationComplete = false;
  static constexpr int CALIBRATION_SAMPLES =
      100; ///< 校准所需样本数 (100样本@125Hz=0.8秒)

  // ==================== Butterworth滤波器 (3Hz) ====================
  float _bw_x1[3] = {0}, _bw_x2[3] = {0};
  float _bw_y1[3] = {0}, _bw_y2[3] = {0};
  // fc=3Hz, fs=125Hz Butterworth系数
  static constexpr float _bw_b0 = 0.00515f;
  static constexpr float _bw_b1 = 0.01030f;
  static constexpr float _bw_b2 = 0.00515f;
  static constexpr float _bw_a1 = -1.7873f;
  static constexpr float _bw_a2 = 0.8080f;

  // ==================== EMA二次滤波器 ====================
  float _ema_value[3] = {0.0f, 0.0f, 0.0f};

  // ==================== GNSS与距离 ====================
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

  // ==================== 私有方法 ====================
  void _initSensor();
  void _selectActiveAxis();
  void _calculateStrokeRate();
  void _processAccelerationData(float accX, float accY, float accZ);
  double _haversine(double lat1, double lon1, double lat2, double lon2);
  bool _acquireStrokePosition(double &lat, double &lon);
  float _butterworthFilter(float input, int axis);
  float _emaFilter(float input, int axis); ///< EMA二次平滑滤波
};

#endif // IMU_MANAGER_H
