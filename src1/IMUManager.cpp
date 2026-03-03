/*
 * File: IMUManager.cpp
 * Purpose: Implements runtime logic for the I M U Manager module.
 */
#include "IMUManager.h"
#include "GNSSProcessor.h"
#include "SDCardManager.h"
#include "SensorQMI8658.hpp"
#include <Arduino.h>
#include <Wire.h>

extern SDCardManager sdCardManager; // 鐢ㄤ簬璋冭瘯鏃ュ織

IMUManager::IMUManager(int sda, int scl, GNSSProcessor *gnss)
    : _sda(sda), _scl(scl), _gnss(gnss), _accX(0), _accY(0), _accZ(0),
      _dataValid(false), _strokeRate(0.0f),
      _activeAxis(2), // 鍥哄畾Z杞达紙鏂滄斁鏃朵俊鍙锋渶寮猴級
      _strokeState(STATE_BACKGROUND), _lastStrokeTime(0), _strokeCount(0),
      _totalDistance(0.0f), _lastStrokeCountForDistance(0), _sensorFound(false),
      _prevStrokeLat(0.0), _prevStrokeLon(0.0),
      _hasInitialStrokePosition(false), _lastValidGnssLat(0.0),
      _lastValidGnssLon(0.0), _phaseStartTime(0), _peakMaxValue(0.0f),
      _peakMaxTime(0), _peakMaxFiltered(0.0f), _troughMinValue(0.0f),
      _troughMinTime(0), _troughMinFiltered(0.0f), _peakHasGrowth(false), _recoveryCounter(0),
      _backgroundMean(0.0f), _backgroundStd(0.1f), _isCalibrating(true),
      _calibrationComplete(false) {

  // 鍒濆鍖栭槦鍒?
  for (int i = 0; i < 3; i++) {
    _accelHistory[i].clear();
    // 棰勫～鍏?
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

  // 妫€鏌ユ暟鎹槸鍚﹀噯澶囧ソ
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
    // _selectActiveAxis();  // DISABLED: 鍥哄畾X杞达紝涓嶈繘琛岃酱鍒囨崲
    _calculateStrokeRate();

    _dataValid = false;
  }
}

void IMUManager::_initSensor() {
  // 纭繚 Wire 鍒濆鍖栧苟鍦ㄦ鏃惰缃鐜?
  Wire.begin(_sda, _scl);
  Wire.setClock(100000); // 闄嶄綆鍒?100kHz 浠ユ彁楂樼ǔ瀹氭€?

  if (!_qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, _sda, _scl)) {
    _sensorFound = false;
    return;
  }

  // 閰嶇疆鍔犻€熷害璁? 62.5Hz, 4G閲忕▼ (绾?6ms闂撮殧)
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
  // 1. Butterworth 1Hz浣庨€氭护娉?
  float bw[3] = {_butterworthFilter(accX, 0), _butterworthFilter(accY, 1),
                 _butterworthFilter(accZ, 2)};

  // 2. EMA浜屾骞虫粦
  float filtered[3] = {_emaFilter(bw[0], 0), _emaFilter(bw[1], 1),
                       _emaFilter(bw[2], 2)};

  // 3. 鏇存柊鍘嗗彶鏁版嵁 (std::deque鎿嶄綔)
  for (int i = 0; i < 3; ++i) {
    if (_accelHistory[i].size() >= WINDOW_SIZE) {
      _accelHistory[i].pop_front();
    }
    _accelHistory[i].push_back(filtered[i]);
  }
}

void IMUManager::_calculateStrokeRate() {
  // ============ 鏍″噯闃舵 ============
  if (_isCalibrating) {
    if (_accelHistory[_activeAxis].size() >= CALIBRATION_SAMPLES) {
      // 璁＄畻鍒濆鑳屾櫙缁熻
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
    return; // 鏍″噯鏈熼棿涓嶈繘琛屾娴?
  }

  // 鏍″噯瀹屾垚鍚庯紝纭繚鑷冲皯鏈?0涓牱鏈墠寮€濮嬫娴嬶紙婊ゆ尝鍣ㄧǔ瀹氾級
  if (!_calibrationComplete && _accelHistory[_activeAxis].size() < 20) {
    return;
  }

  uint32_t now = millis();

  // 1. 璁＄畻鑳屾櫙缁熻 (鍧囧€煎拰鏍囧噯宸?
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
    _backgroundStd = 0.02f; // 鏈€灏忔爣鍑嗗樊

  // 2. 鑾峰彇褰撳墠鍊煎拰鍋忓樊
  float current_filtered = _accelHistory[_activeAxis].back();
  float deviation = current_filtered - _backgroundMean;

  // 3. 璁＄畻鍔ㄦ€侀槇鍊?
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
      _peakHasGrowth = false;
    }
  }

  else if (_strokeState == STATE_PEAK_ZONE) {
    // 在波峰区: 跟踪最大值
    if (deviation > _peakMaxValue) {
      const float growth_margin = 1e-4f;
      if ((deviation - _peakMaxValue) > growth_margin) {
        _peakHasGrowth = true;
      }
      _peakMaxValue = deviation;
      _peakMaxTime = now;
      _peakMaxFiltered = current_filtered;
    }

    // 检测是否进入波谷区(偏差变负)
    if (deviation < TROUGH_THRESHOLD) {
      if (!_peakHasGrowth) {
        _strokeState = STATE_BACKGROUND;
        _peakHasGrowth = false;
        _recoveryCounter = 0;
      } else {
        _strokeState = STATE_TROUGH_ZONE;
        _phaseStartTime = now;
        _troughMinValue = deviation;
        _troughMinTime = now;
        _troughMinFiltered = current_filtered;
        _recoveryCounter = 0;
      }
    }
  }

  else if (_strokeState == STATE_TROUGH_ZONE) {
    // 鍦ㄦ尝璋峰尯: 璺熻釜鏈€灏忓€?
    if (deviation < _troughMinValue) {
      _troughMinValue = deviation;
      _troughMinTime = now;
      _troughMinFiltered = current_filtered;
      _recoveryCounter = 0; // 鍑虹幇鏂颁綆鐐?閲嶇疆鎭㈠璁℃暟
    }

    // 妫€娴嬫槸鍚︽仮澶嶅埌鑳屾櫙 - 闇€瑕佽繛缁涓噰鏍风偣
    // 妫€娴嬫槸鍚︽仮澶嶅埌鑳屾櫙 - 鍔ㄦ€侀€昏緫
    // 濡傛灉娉㈣胺闈炲父娣?(< -0.1g)锛岄偅涔堟仮澶嶉槇鍊艰涓烘尝璋锋繁搴︾殑 50%
    // 绠€鍗曟潵璇达紝蹇呴』鍥炲崌涓€鑸墠鑳界畻鎭㈠锛岄槻姝腑闂寸殑灏忓弽寮硅Е鍙戞仮澶?
    bool in_recovery_zone = false;

    if (_troughMinValue < -0.1f) {
      if (deviation > (_troughMinValue * 0.5f)) {
        in_recovery_zone = true;
      }
    } else {
      // 瀵逛簬娴呮尝璋凤紝缁存寔鍘熸湁鐨勮儗鏅櫔澹伴€昏緫
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

    // 鍙湁杩炵画澶氫釜閲囨牱鐐归兘鍦ㄦ仮澶嶅尯鎵嶇‘璁ゆ仮澶?
    // 銆愭敼杩涖€戝鏋滀俊鍙峰凡缁忚繃闆讹紙鍙樻锛夛紝璇存槑宸茬粡寮€濮嬩笅涓€鍒掔殑瓒嬪娍锛屽己鍒剁粨鏉熷綋鍓嶅垝妗?
    if (deviation > 0.0f) {
      _recoveryCounter = RECOVERY_SAMPLES; // 寮哄埗婊¤冻鏉′欢
    }

    // 鎭㈠鏈熸鏌ワ細纭繚娉㈣胺宸茬粡缁撴潫锛屼俊鍙峰紑濮嬪洖鍗?
    if (_recoveryCounter >= RECOVERY_SAMPLES) {
      // 娉㈣胺鎸佺画鏃堕棿妫€鏌?(DISABLED: 绠€鍖栨娴嬶紝鍙鍥炲崌灏辩‘璁?
      // uint32_t trough_duration = now - _phaseStartTime;
      // if (trough_duration >= MIN_TROUGH_DURATION) {
      if (true) {
        // 璁＄畻鎸箙
        float amplitude = _peakMaxValue - _troughMinValue;

        if (amplitude >= MIN_AMPLITUDE) {
          // 妫€鏌ラ棿闅?(DISABLED: 涓轰簡瀵规瘮SpeedCoach锛屽畬鍏ㄤ緷璧栨尝褰?
          // if (_lastStrokeTime == 0 ||
          //     (_peakMaxTime - _lastStrokeTime) >= STROKE_MIN_INTERVAL) {
          if (true) {
            // 鉁?纭鍒掓〃!
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

            // 鑾峰彇鍒掓〃宄板€兼椂鍒荤殑绮剧‘GPS浣嶇疆锛堜娇鐢ㄦ彃鍊硷級
            double currentLat = 0.0;
            double currentLon = 0.0;
            if (_gnss != nullptr) {
              // 浣跨敤宄板€兼椂闂存埑杩涜鎻掑€硷紝娑堥櫎GNSS鏇存柊寤惰繜鐨勫奖鍝?
              GNSSPoint interpolated =
                  _gnss->getInterpolatedPosition(_peakMaxTime);

              if (interpolated.valid) {
                // 鎻掑€兼垚鍔燂紝浣跨敤鎻掑€肩粨鏋滐紙绮剧‘鍒板嘲鍊肩灛闂达級
                currentLat = interpolated.latitude;
                currentLon = interpolated.longitude;
              } else {
                // 鎻掑€煎け璐ワ紙渚嬪鍘嗗彶鏁版嵁涓嶈冻锛夛紝鍥為€€鍒版渶鏂颁綅缃?
                currentLat = _gnss->getLatitude();
                currentLon = _gnss->getLongitude();
              }
            }

            // 鏇存柊Metrics
            _lastStrokeMetrics.strokeNumber = _strokeCount;
            _lastStrokeMetrics.timestamp = _peakMaxTime;

            // 璁剧疆GPS鍧愭爣锛氳捣鐐规槸鍓嶄竴妗ㄧ殑浣嶇疆锛岀粓鐐规槸褰撳墠浣嶇疆
            _lastStrokeMetrics.startLat = _prevStrokeLat;
            _lastStrokeMetrics.startLon = _prevStrokeLon;
            _lastStrokeMetrics.endLat = currentLat;
            _lastStrokeMetrics.endLon = currentLon;

            // 鏇存柊鍓嶄竴妗ㄧ殑浣嶇疆涓哄綋鍓嶄綅缃紙鐢ㄤ簬涓嬩竴妗級
            if (currentLat != 0.0 && currentLon != 0.0) {
              _prevStrokeLat = currentLat;
              _prevStrokeLon = currentLon;
            }

            Serial.printf(
                "鉁?[鍒掓〃] #%d, 鎸箙=%.3fg, 妗ㄩ=%.1f, GPS[%.7f,%.7f]\n",
                _strokeCount, amplitude, _strokeRate, currentLat, currentLon);

            _strokeState = STATE_COOLDOWN;
            _phaseStartTime = now;
            _peakHasGrowth = false;
            _recoveryCounter = 0;
          } else {
            _strokeState = STATE_BACKGROUND;
            _peakHasGrowth = false;
            _recoveryCounter = 0;
          }
        } else {
          _strokeState = STATE_BACKGROUND;
          _peakHasGrowth = false;
          _recoveryCounter = 0;
        }
      } else {
        _strokeState = STATE_BACKGROUND;
        _peakHasGrowth = false;
        _recoveryCounter = 0;
      }
    }
  }

  else if (_strokeState == STATE_COOLDOWN) {
    // 鍐峰嵈鏈?
    uint32_t cooldown_time = now - _phaseStartTime;
    if (cooldown_time >= COOLDOWN_DURATION) {
      _strokeState = STATE_BACKGROUND;
      _peakHasGrowth = false;
      // Serial.println("[鍐峰嵈缁撴潫]");
    }
  }

  // 鍏ㄥ眬瓒呮椂澶勭悊 - 鍙湪闈炶儗鏅姸鎬佹椂妫€鏌ワ紙閬垮厤閲嶅瑙﹀彂锛?
  if (_strokeState != STATE_BACKGROUND && _phaseStartTime != 0) {
    if ((now - _phaseStartTime) > STROKE_TIMEOUT) {
      _strokeRate = 0.0f;
      _strokeState = STATE_BACKGROUND;
      _peakHasGrowth = false;
      _recoveryCounter = 0;
      Serial.println("[IMU] Stroke detection timeout; state reset");
    }
  }
}

// 鍏叡鎺ュ彛淇濇寔涓嶅彉
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
  _peakHasGrowth = false;
  _recoveryCounter = 0;
  _isCalibrating = true;        // 閲嶇疆鏍″噯鐘舵€?
  _calibrationComplete = false; // 閲嶇疆鏍″噯鐘舵€?
  Serial.println("[IMU] 璁℃暟閲嶇疆");
}

void IMUManager::resetTotalDistance() {
  _totalDistance = 0.0f;
  _lastStrokeCountForDistance = 0;
  _prevStrokeLat = 0.0;
  _prevStrokeLon = 0.0;
  _hasInitialStrokePosition = false;
  _lastStrokeMetrics = StrokeMetrics();
  _hasNewStroke = false;
  Serial.println("[IMU] 璺濈閲嶇疆");
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

// Butterworth 2闃朵綆閫?
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

// EMA浜屾骞虫粦婊ゆ尝
float IMUManager::_emaFilter(float input, int axis) {
  if (axis < 0 || axis >= 3)
    return input;
  if (_ema_value[axis] == 0.0f) {
    _ema_value[axis] = input; // 棣栨鍒濆鍖?
  } else {
    _ema_value[axis] =
        EMA_FILTER_ALPHA * input + (1.0f - EMA_FILTER_ALPHA) * _ema_value[axis];
  }
  return _ema_value[axis];
}






