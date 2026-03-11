#ifndef BRIGHTNESS_MANAGER_H
#define BRIGHTNESS_MANAGER_H

#include <Arduino.h>

// 亮度等级常量 (PWM值: 0-255)
constexpr uint8_t BRIGHTNESS_FULL = 255;  // 100%
constexpr uint8_t BRIGHTNESS_HIGH = 178;  //  70%
constexpr uint8_t BRIGHTNESS_HALF = 127;  //  50%
constexpr uint8_t BRIGHTNESS_MED = 76;    //  30%
constexpr uint8_t BRIGHTNESS_LOW = 25;    //  10%

// 亮度降低时间阈值 (毫秒)
constexpr unsigned long DIM_DELAY_1 = 120000; // 2分钟
constexpr unsigned long DIM_DELAY_2 = 180000; // 3分钟
constexpr unsigned long DIM_DELAY_3 = 300000; // 5分钟
constexpr unsigned long DIM_DELAY_4 = 420000; // 7分钟
constexpr unsigned long DIM_DELAY_5 = 600000; // 10分钟 (自动关机阈值)

// PWM配置
constexpr uint32_t PWM_FREQ = 5000;   // 5kHz频率
constexpr uint8_t PWM_RESOLUTION = 8; // 8位分辨率 (0-255)

/**
 * @brief 屏幕背光亮度管理器
 *
 * 功能:
 * - 根据用户活动时间自动调节屏幕亮度
 * - 训练模式下保持最高亮度
 * - 按键/触摸交互时立即恢复最高亮度
 * - 使用PWM (LEDC) 控制背光亮度
 */
class BrightnessManager {
public:
  /**
   * @brief 构造函数
   * @param pin 背光控制GPIO引脚
   * @param channel LEDC通道号 (0-7)
   */
  BrightnessManager(uint8_t pin, uint8_t channel = 0);

  /**
   * @brief 初始化PWM控制
   * @return true 初始化成功, false 失败
   */
  bool begin();

  /**
   * @brief 更新亮度 (应在主循环中定期调用)
   * @param now 当前时间戳 (millis())
   * @param trainingActive 是否处于训练模式
   * @param lastInteraction 最后一次用户交互的时间戳
   */
  void update(unsigned long now, bool trainingActive,
              unsigned long lastInteraction);

  /**
   * @brief 用户交互时调用，立即恢复最高亮度
   */
  void resetInteraction();

  /**
   * @brief 获取当前亮度值
   * @return 当前PWM值 (0-255)
   */
  uint8_t getCurrentBrightness() const;

  /**
   * @brief 获取当前亮度百分比
   * @return 亮度百分比 (0-100)
   */
  uint8_t getCurrentBrightnessPercent() const;

private:
  uint8_t _pin;               // GPIO引脚
  uint8_t _channel;           // LEDC通道
  uint8_t _currentBrightness; // 当前亮度值
  uint8_t _targetBrightness;  // 目标亮度值
  bool _initialized;          // 初始化标志

  /**
   * @brief 计算目标亮度
   * @param inactiveTime 用户非活跃时间 (毫秒)
   * @param trainingActive 是否处于训练模式
   * @return 目标亮度值 (0-255)
   */
  uint8_t calculateTargetBrightness(unsigned long inactiveTime,
                                    bool trainingActive);

  /**
   * @brief 应用亮度值到硬件
   * @param brightness 亮度值 (0-255)
   */
  void applyBrightness(uint8_t brightness);
};

#endif // BRIGHTNESS_MANAGER_H
