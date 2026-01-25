#include "IMUManager.h"
#include "GNSSProcessor.h"
#include "SDCardManager.h"
#include "SensorQMI8658.hpp"
#include <Arduino.h>
#include <Wire.h>

extern SDCardManager sdCardManager; // 用于调试日志

IMUManager::IMUManager(int sda, int scl, GNSSProcessor *gnss)
    : _sda(sda), _scl(scl), _gnss(gnss), _accX(0), _accY(0), _accZ(0),
      _dataValid(false), _strokeRate(0.0f),
      _activeAxis(2), // 固定Z轴（斜放时信号最强）
      _strokeState(STATE_BACKGROUND), _lastStrokeTime(0), _strokeCount(0),
      _totalDistance(0.0f), _lastStrokeCountForDistance(0), _sensorFound(false),
      _prevStrokeLat(0.0), _prevStrokeLon(0.0),
      _hasInitialStrokePosition(false), _lastValidGnssLat(0.0),
      _lastValidGnssLon(0.0), _phaseStartTime(0), _peakMaxValue(0.0f),
      _peakMaxTime(0), _peakMaxFiltered(0.0f), _troughMinValue(0.0f),
      _troughMinTime(0), _troughMinFiltered(0.0f), _recoveryCounter(0),
      _backgroundMean(0.0f), _backgroundStd(0.1f), _isCalibrating(true),
      _calibrationComplete(false) {

  // 初始化队列
  for (int i = 0; i < 3; i++) {
    _accelHistory[i].clear();
    // 预填充0
    for (int j = 0; j < WINDOW_SIZE; j++) {
      _accelHistory[i].push_back(0.0f);
    }

    _lastAccel[i] = 0.0f;
    _axisVariances[i] = 0.0f;

    _bw_x1[i] = 0.0f;
    _bw_x2[i] = 0.0f;
    _bw_y1[i] = 0.0f;
    _bw_y2[i] = 0.0f;
    _ema_value[i] = 0.0f;
  }
}

void IMUManager::begin() { _initSensor(); }

void IMUManager::update() {
  if (!_sensorFound)
    return;

  // 检查数据是否准备好
  if (_qmi.getDataReady()) {
    if (_qmi.getAccelerometer(_acc.x, _acc.y, _acc.z)) {
      _accX = _acc.x;
      _accY = _acc.y;
      _accZ = -_acc.z;
      _dataValid = true;
    }
  }

  if (_dataValid) {
    _processAccelerationData(_accX, _accY, _accZ);
    // _selectActiveAxis();  // DISABLED: 固定X轴，不进行轴切换
    _calculateStrokeRate();

    _dataValid = false;
  }
}

void IMUManager::_initSensor() {
  if (!_qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, _sda, _scl)) {
    _sensorFound = false;
    return;
  }

  // 配置加速度计: 62.5Hz, 4G量程 (约16ms间隔)
  if (_qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                               SensorQMI8658::ACC_ODR_62_5Hz,
                               SensorQMI8658::LPF_MODE_0) != 0) {
    _sensorFound = false;
    return;
  }

  if (!_qmi.enableAccelerometer()) {
    _sensorFound = false;
    return;
  }

  _sensorFound = true;
}

void IMUManager::_processAccelerationData(float accX, float accY, float accZ) {
  // 1. Butterworth 1Hz低通滤波
  float bw[3] = {_butterworthFilter(accX, 0), _butterworthFilter(accY, 1),
                 _butterworthFilter(accZ, 2)};

  // 2. EMA二次平滑
  float filtered[3] = {_emaFilter(bw[0], 0), _emaFilter(bw[1], 1),
                       _emaFilter(bw[2], 2)};

  // 3. 更新历史数据 (std::deque操作)
  for (int i = 0; i < 3; ++i) {
    if (_accelHistory[i].size() >= WINDOW_SIZE) {
      _accelHistory[i].pop_front();
    }
    _accelHistory[i].push_back(filtered[i]);
  }
}

void IMUManager::_calculateStrokeRate() {
  // ============ 校准阶段 ============
  if (_isCalibrating) {
    if (_accelHistory[_activeAxis].size() >= CALIBRATION_SAMPLES) {
      // 计算初始背景统计
      float sum = 0.0f;
      for (float val : _accelHistory[_activeAxis]) {
        sum += val;
      }
      _backgroundMean = sum / _accelHistory[_activeAxis].size();

      float sum_sq_diff = 0.0f;
      for (float val : _accelHistory[_activeAxis]) {
        sum_sq_diff += (val - _backgroundMean) * (val - _backgroundMean);
      }
      _backgroundStd =
          sqrt(sum_sq_diff / (_accelHistory[_activeAxis].size() - 1));
      if (_backgroundStd < 0.02f)
        _backgroundStd = 0.02f;

      _isCalibrating = false;
      _calibrationComplete = true;
    }
    return; // 校准期间不进行检测
  }

  // 校准完成后，确保至少有20个样本才开始检测（滤波器稳定）
  if (!_calibrationComplete && _accelHistory[_activeAxis].size() < 20) {
    return;
  }

  uint32_t now = millis();

  // 1. 计算背景统计 (均值和标准差)
  float sum = 0.0f;
  for (float val : _accelHistory[_activeAxis]) {
    sum += val;
  }
  _backgroundMean = sum / _accelHistory[_activeAxis].size();

  float sum_sq_diff = 0.0f;
  for (float val : _accelHistory[_activeAxis]) {
    sum_sq_diff += (val - _backgroundMean) * (val - _backgroundMean);
  }
  _backgroundStd = sqrt(sum_sq_diff / (_accelHistory[_activeAxis].size() - 1));
  if (_backgroundStd < 0.02f)
    _backgroundStd = 0.02f; // 最小标准差

  // 2. 获取当前值和偏差
  float current_filtered = _accelHistory[_activeAxis].back();
  float deviation = current_filtered - _backgroundMean;

  // 3. 计算动态阈值
  float peak_threshold = PEAK_ENTER_FACTOR * _backgroundStd;
  if (peak_threshold < MIN_PEAK_ABSOLUTE) {
    peak_threshold = MIN_PEAK_ABSOLUTE;
  }

  if (_strokeState == STATE_BACKGROUND) {
    // 等待进入波峰区: 正向偏差超过阈值
    if (deviation > peak_threshold) {
      _strokeState = STATE_PEAK_ZONE;
      _phaseStartTime = now;
      _peakMaxValue = deviation;
      _peakMaxTime = now;
      _peakMaxFiltered = current_filtered;
    }
  }

  else if (_strokeState == STATE_PEAK_ZONE) {
    // 在波峰区: 跟踪最大值
    if (deviation > _peakMaxValue) {
      _peakMaxValue = deviation;
      _peakMaxTime = now;
      _peakMaxFiltered = current_filtered;
    }

    // 检测是否进入波谷区(偏差变负)
    if (deviation < TROUGH_THRESHOLD) {
      // 波峰持续时间检查 (DISABLED: 简化检测，只要进入波谷就确认波峰)
      // uint32_t peak_duration = now - _phaseStartTime;
      // if (peak_duration >= MIN_PEAK_DURATION) {
      if (true) {
        _strokeState = STATE_TROUGH_ZONE;
        _phaseStartTime = now;
        _troughMinValue = deviation;
        _troughMinTime = now;
        _troughMinFiltered = current_filtered;
        _recoveryCounter = 0;
        // Serial.printf("[进入波谷区] %.2fs: %.3fg\n", now/1000.0f,
        // deviation);
      } else {
        // 波峰持续太短
        _strokeState = STATE_BACKGROUND;
      }
    }
  }

  else if (_strokeState == STATE_TROUGH_ZONE) {
    // 在波谷区: 跟踪最小值
    if (deviation < _troughMinValue) {
      _troughMinValue = deviation;
      _troughMinTime = now;
      _troughMinFiltered = current_filtered;
      _recoveryCounter = 0; // 出现新低点,重置恢复计数
    }

    // 检测是否恢复到背景 - 需要连续多个采样点
    // 检测是否恢复到背景 - 动态逻辑
    // 如果波谷非常深 (< -0.1g)，那么恢复阈值设为波谷深度的 50%
    // 简单来说，必须回升一般才能算恢复，防止中间的小反弹触发恢复
    bool in_recovery_zone = false;

    if (_troughMinValue < -0.1f) {
      if (deviation > (_troughMinValue * 0.5f)) {
        in_recovery_zone = true;
      }
    } else {
      // 对于浅波谷，维持原有的背景噪声逻辑
      float recovery_threshold = RECOVERY_FACTOR * _backgroundStd;
      if (deviation > -recovery_threshold) {
        in_recovery_zone = true;
      }
    }

    if (in_recovery_zone) {
      _recoveryCounter++;
    } else {
      _recoveryCounter = 0;
    }

    // 只有连续多个采样点都在恢复区才确认恢复
    // 【改进】如果信号已经过零（变正），说明已经开始下一划的趋势，强制结束当前划桨
    if (deviation > 0.0f) {
      _recoveryCounter = RECOVERY_SAMPLES; // 强制满足条件
    }

    // 恢复期检查：确保波谷已经结束，信号开始回升
    if (_recoveryCounter >= RECOVERY_SAMPLES) {
      // 波谷持续时间检查 (DISABLED: 简化检测，只要回升就确认)
      // uint32_t trough_duration = now - _phaseStartTime;
      // if (trough_duration >= MIN_TROUGH_DURATION) {
      if (true) {
        // 计算振幅
        float amplitude = _peakMaxValue - _troughMinValue;

        if (amplitude >= MIN_AMPLITUDE) {
          // 检查间隔 (DISABLED: 为了对比SpeedCoach，完全依赖波形)
          // if (_lastStrokeTime == 0 ||
          //     (_peakMaxTime - _lastStrokeTime) >= STROKE_MIN_INTERVAL) {
          if (true) {
            // ✅ 确认划桨!
            if (_lastStrokeTime > 0) {
              uint32_t interval = _peakMaxTime - _lastStrokeTime;
              float instantRate = 60000.0f / interval;
              if (_strokeRate > 0) {
                _strokeRate =
                    EMA_ALPHA * instantRate + (1 - EMA_ALPHA) * _strokeRate;
              } else {
                _strokeRate = instantRate;
              }
            }

            _strokeCount++;
            _lastStrokeTime = _peakMaxTime;
            _hasNewStroke = true;

            // 更新Metrics
            _lastStrokeMetrics.strokeNumber = _strokeCount;
            _lastStrokeMetrics.timestamp = _peakMaxTime;
            _lastStrokeMetrics.totalDistance = _totalDistance;

            // GNSS
            if (_gnss != nullptr) {
              _gnss->getInterpolatedPosition(_peakMaxTime);
            }

            Serial.printf("✅ [划桨确认] #%d, 振幅=%.3fg, 桨频=%.1f\n",
                          _strokeCount, amplitude, _strokeRate);

            _strokeState = STATE_COOLDOWN;
            _phaseStartTime = now;
            _recoveryCounter = 0;
          } else {
            _strokeState = STATE_BACKGROUND;
            _recoveryCounter = 0;
          }
        } else {
          _strokeState = STATE_BACKGROUND;
          _recoveryCounter = 0;
        }
      } else {
        _strokeState = STATE_BACKGROUND;
        _recoveryCounter = 0;
      }
    }
  }

  else if (_strokeState == STATE_COOLDOWN) {
    // 冷却期
    uint32_t cooldown_time = now - _phaseStartTime;
    if (cooldown_time >= COOLDOWN_DURATION) {
      _strokeState = STATE_BACKGROUND;
      // Serial.println("[冷却结束]");
    }
  }

  // 全局超时处理 - 只在非背景状态时检查（避免重复触发）
  if (_strokeState != STATE_BACKGROUND && _phaseStartTime != 0) {
    if ((now - _phaseStartTime) > STROKE_TIMEOUT) {
      _strokeRate = 0.0f;
      _strokeState = STATE_BACKGROUND;
      _recoveryCounter = 0;
      Serial.println("[超时] 重置状态");
    }
  }
}

// 公共接口保持不变
float IMUManager::getStrokeRate() const { return _strokeRate; }
int IMUManager::getActiveAxis() const { return _activeAxis; }
int IMUManager::getStrokeCount() const { return _strokeCount; }
float IMUManager::getTotalDistance() const { return _totalDistance; }
float IMUManager::getStrokeDistance() const { return _strokeDistance; }

void IMUManager::resetStrokeCount() {
  _strokeCount = 0;
  _hasInitialStrokePosition = false;
  _hasLastStrokeSegment = false;
  _strokeDistance = 0.0f;
  _strokeRate = 0.0f;
  _lastStrokeTime = 0;
  _strokeState = STATE_BACKGROUND;
  _recoveryCounter = 0;
  _isCalibrating = true;        // 重置校准状态
  _calibrationComplete = false; // 重置校准状态
  Serial.println("[IMU] 计数重置");
}

void IMUManager::resetTotalDistance() {
  _totalDistance = 0.0f;
  _lastStrokeCountForDistance = 0;
  _prevStrokeLat = 0.0;
  _prevStrokeLon = 0.0;
  _hasInitialStrokePosition = false;
  _lastStrokeMetrics = StrokeMetrics();
  _hasNewStroke = false;
  Serial.println("[IMU] 距离重置");
}

void IMUManager::getAcceleration(float &ax, float &ay, float &az) {
  ax = _accX;
  ay = _accY;
  az = _accZ;
}

const StrokeMetrics &IMUManager::getLastStrokeMetrics() const {
  return _lastStrokeMetrics;
}

bool IMUManager::hasNewStroke() const { return _hasNewStroke; }
void IMUManager::clearNewStrokeFlag() { _hasNewStroke = false; }
bool IMUManager::hasLastStrokeSegment() const { return _hasLastStrokeSegment; }
double IMUManager::getLastStrokeEndLatitude() const {
  return _lastStrokeEndLat;
}
double IMUManager::getLastStrokeEndLongitude() const {
  return _lastStrokeEndLon;
}

double IMUManager::_haversine(double lat1, double lon1, double lat2,
                              double lon2) {
  const double R = 6371000.0;
  double dLat = (lat2 - lat1) * M_PI / 180.0;
  double dLon = (lon2 - lon1) * M_PI / 180.0;
  lat1 = lat1 * M_PI / 180.0;
  lat2 = lat2 * M_PI / 180.0;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             sin(dLon / 2) * sin(dLon / 2) * cos(lat1) * cos(lat2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

// Butterworth 2阶低通
float IMUManager::_butterworthFilter(float input, int axis) {
  if (axis < 0 || axis >= 3)
    return input;
  float output = _bw_b0 * input + _bw_b1 * _bw_x1[axis] +
                 _bw_b2 * _bw_x2[axis] - _bw_a1 * _bw_y1[axis] -
                 _bw_a2 * _bw_y2[axis];
  _bw_x2[axis] = _bw_x1[axis];
  _bw_x1[axis] = input;
  _bw_y2[axis] = _bw_y1[axis];
  _bw_y1[axis] = output;
  return output;
}

// EMA二次平滑滤波
float IMUManager::_emaFilter(float input, int axis) {
  if (axis < 0 || axis >= 3)
    return input;
  if (_ema_value[axis] == 0.0f) {
    _ema_value[axis] = input; // 首次初始化
  } else {
    _ema_value[axis] =
        EMA_FILTER_ALPHA * input + (1.0f - EMA_FILTER_ALPHA) * _ema_value[axis];
  }
  return _ema_value[axis];
}
