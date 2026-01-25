#include "BrightnessManager.h"

BrightnessManager::BrightnessManager(uint8_t pin, uint8_t channel)
    : _pin(pin), _channel(channel), _currentBrightness(BRIGHTNESS_FULL),
      _targetBrightness(BRIGHTNESS_FULL), _initialized(false) {}

bool BrightnessManager::begin() {
  // 配置LEDC PWM
  ledcSetup(_channel, PWM_FREQ, PWM_RESOLUTION);

  // 绑定GPIO到LEDC通道
  ledcAttachPin(_pin, _channel);

  // 初始化为最高亮度
  ledcWrite(_channel, BRIGHTNESS_FULL);

  _currentBrightness = BRIGHTNESS_FULL;
  _targetBrightness = BRIGHTNESS_FULL;
  _initialized = true;

  Serial.printf("[BrightnessManager] Initialized on pin %d, channel %d\n", _pin,
                _channel);
  Serial.printf("[BrightnessManager] PWM: %dHz, %d-bit resolution\n", PWM_FREQ,
                PWM_RESOLUTION);

  return true;
}

void BrightnessManager::update(unsigned long now, bool trainingActive,
                               unsigned long lastInteraction) {
  if (!_initialized) {
    return;
  }

  // 计算用户非活跃时间
  unsigned long inactiveTime = now - lastInteraction;

  // 计算目标亮度
  _targetBrightness = calculateTargetBrightness(inactiveTime, trainingActive);

  // 平滑过渡到目标亮度（避免突变）
  // 每次调整最多5个单位，实现渐变效果
  if (_currentBrightness < _targetBrightness) {
    _currentBrightness =
        min((uint8_t)(_currentBrightness + 5), _targetBrightness);
  } else if (_currentBrightness > _targetBrightness) {
    _currentBrightness =
        max((uint8_t)(_currentBrightness - 5), _targetBrightness);
  }

  // 应用亮度
  applyBrightness(_currentBrightness);
}

void BrightnessManager::resetInteraction() {
  if (!_initialized) {
    return;
  }

  // 立即恢复最高亮度
  _targetBrightness = BRIGHTNESS_FULL;
  _currentBrightness = BRIGHTNESS_FULL;
  applyBrightness(BRIGHTNESS_FULL);
}

uint8_t BrightnessManager::getCurrentBrightness() const {
  return _currentBrightness;
}

uint8_t BrightnessManager::getCurrentBrightnessPercent() const {
  return (_currentBrightness * 100) / 255;
}

uint8_t BrightnessManager::calculateTargetBrightness(unsigned long inactiveTime,
                                                     bool trainingActive) {
  // 训练模式下始终保持最高亮度
  if (trainingActive) {
    return BRIGHTNESS_FULL;
  }

  // 根据非活跃时间分级降低亮度
  if (inactiveTime < DIM_DELAY_1) {
    // < 30秒: 100%亮度
    return BRIGHTNESS_FULL;
  } else if (inactiveTime < DIM_DELAY_2) {
    // 30秒 - 1分钟: 70%亮度
    return BRIGHTNESS_HIGH;
  } else if (inactiveTime < DIM_DELAY_3) {
    // 1分钟 - 2分钟: 30%亮度
    return BRIGHTNESS_MED;
  } else {
    // > 2分钟: 10%亮度
    return BRIGHTNESS_LOW;
  }
}

void BrightnessManager::applyBrightness(uint8_t brightness) {
  static uint8_t lastAppliedBrightness = 255;

  // 只在亮度实际变化时写入硬件
  if (brightness != lastAppliedBrightness) {
    ledcWrite(_channel, brightness);
    lastAppliedBrightness = brightness;
  }
}
