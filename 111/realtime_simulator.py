"""
实时划桨检测仿真器 - C++算法逻辑复现
按时间窗口逐步处理数据，模拟ESP32真实运行环境
"""
import sys
import time
from collections import deque
import numpy as np
import pandas as pd
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QFileDialog, QSlider, QGroupBox, QGridLayout,
    QMessageBox, QFrame
)

from PyQt5.QtCore import QTimer, Qt
from PyQt5.QtGui import QFont
import pyqtgraph as pg

class ButterworthFilter:
    """二阶Butterworth低通滤波器"""
    def __init__(self, cutoff_hz=1.0, sample_rate=100.0):
        """
        Args:
            cutoff_hz: 截止频率 (Hz), 越低滤波越强
            sample_rate: 采样率 (Hz)
        """
        # 计算滤波器系数 (二阶Butterworth)
        import math
        wc = 2 * math.pi * cutoff_hz / sample_rate
        k = math.tan(wc / 2)
        k2 = k * k
        sqrt2 = math.sqrt(2)
        
        norm = 1 / (1 + sqrt2 * k + k2)
        self.b0 = k2 * norm
        self.b1 = 2 * self.b0
        self.b2 = self.b0
        self.a1 = 2 * (k2 - 1) * norm
        self.a2 = (1 - sqrt2 * k + k2) * norm
        
        self.x1 = 0.0
        self.x2 = 0.0
        self.y1 = 0.0
        self.y2 = 0.0
        
    def filter(self, input_val):
        output = (self.b0 * input_val + 
                  self.b1 * self.x1 + 
                  self.b2 * self.x2 - 
                  self.a1 * self.y1 - 
                  self.a2 * self.y2)
        
        self.x2 = self.x1
        self.x1 = input_val
        self.y2 = self.y1
        self.y1 = output
        
        return output


class ExponentialMovingAverage:
    """指数移动平均滤波器 (二次平滑)"""
    def __init__(self, alpha=0.1):
        """
        Args:
            alpha: 平滑系数, 越小越平滑 (0.1 = 非常平滑)
        """
        self.alpha = alpha
        self.value = None
        
    def filter(self, input_val):
        if self.value is None:
            self.value = input_val
        else:
            self.value = self.alpha * input_val + (1 - self.alpha) * self.value
        return self.value

class RealtimeStrokeDetector:
    """
    基于相位状态机的划桨检测器
    
    划桨周期: 背景噪声 → 波峰区(多个波峰) → 波谷区(多个波谷) → 恢复到背景
    
    状态机:
    0: BACKGROUND - 等待进入波峰区 (检测正向突破)
    1: PEAK_ZONE  - 在波峰区 (等待下降到负值)
    2: TROUGH_ZONE - 在波谷区 (等待恢复到背景)
    3: COOLDOWN   - 冷却期
    """
    
    # 状态常量
    STATE_BACKGROUND = 0
    STATE_PEAK_ZONE = 1
    STATE_TROUGH_ZONE = 2
    STATE_COOLDOWN = 3
    
    def __init__(self):
        # ============ 可调参数 ============
        self.WINDOW_SIZE = 200  # 滑动窗口大小(用于计算背景均值) - 200点=2秒@100Hz


        
        # 动态阈值系数 (相对于背景标准差)
        self.PEAK_ENTER_FACTOR = 1.5   # 进入波峰区的阈值 = mean + factor * std (与C++固件一致)
        self.PEAK_EXIT_FACTOR = 0.5    # 离开波峰区(进入波谷)的阈值
        self.TROUGH_THRESHOLD = -0.02  # 绝对波谷阈值(低于均值多少算波谷区)
        self.RECOVERY_FACTOR = 1.3     # 恢复到背景的阈值
        
        # 绝对阈值(防止噪声过小时误触发) - 与C++固件完全一致
        self.MIN_PEAK_ABSOLUTE = 0.08 # 波峰最小绝对高度 (g)
        self.MIN_AMPLITUDE = 0.05   # 最小峰谷振幅 (g)
        
        # 时间参数
        self.MIN_PEAK_DURATION = 30    # 波峰区最小持续时间(ms) - 降低
        self.MIN_TROUGH_DURATION = 50  # 波谷区最小持续时间(ms) - 降低
        self.STROKE_MIN_INTERVAL = 800 # 两次划桨最小间隔(ms)
        self.COOLDOWN_DURATION = 300   # 冷却时间(ms)
        
        self.DEBUG = True
        
        # ============ 双重滤波器 ============
        # 注意：滤波器参数会在load_data()时根据实际采样率重新初始化
        # 第一级: Butterworth低通 (3Hz截止)
        self.cutoff_hz = 3.0  # 截止频率
        self.sample_rate = 125.0  # 默认采样率，会在加载数据时更新
        self.filters = [ButterworthFilter(cutoff_hz=self.cutoff_hz, sample_rate=self.sample_rate) for _ in range(3)]
        # 第二级: EMA平滑 (alpha=0.25)
        self.ema_filters = [ExponentialMovingAverage(alpha=0.25) for _ in range(3)]
        

        # ============ 状态变量 ============
        self._accelHistory = [deque(maxlen=self.WINDOW_SIZE) for _ in range(3)]
        self._activeAxis = 2  # 固定Z轴（斜放时信号最强）

        self._strokeState = self.STATE_BACKGROUND
        self._strokeCount = 0
        self._strokeRate = 0.0
        
        # 相位跟踪
        self._phaseStartTime = 0       # 当前相位开始时间
        self._peakMaxValue = 0.0       # 波峰区最大偏差值
        self._peakMaxTime = 0          # 波峰区最大值时间
        self._peakMaxRaw = 0.0         # 波峰区最大原始值
        self._peakMaxFiltered = 0.0    # 波峰区最大滤波值(绝对值,用于可视化)
        self._troughMinValue = 0.0     # 波谷区最小偏差值
        self._troughMinTime = 0        # 波谷区最小值时间
        self._troughMinRaw = 0.0       # 波谷区最小原始值
        self._troughMinFiltered = 0.0  # 波谷区最小滤波值(绝对值,用于可视化)
        

        # 恢复检测 (需要连续多个采样点)
        self._recoveryCounter = 0      # 连续恢复采样计数
        self.RECOVERY_SAMPLES = 5      # 需要连续多少个采样点才算恢复
        
        # 背景统计
        self._backgroundMean = 0.0
        self._backgroundStd = 0.1      # 初始值,避免除零
        
        # 时间戳
        self._lastStrokeTime = 0
        
        # 滤波数据历史 (用于可视化)
        self.filtered_data_history = {'x': [], 'y': [], 'z': [], 't': []}
        
        # 检测结果
        self.detected_strokes = []
        

        # 轴选择
        self._lastAxisSelection = 0
        
        # ============ 初始化阶段 ============
        # 使用前0.8秒(100个样本@125Hz)作为静止期，快速建立背景统计
        self.CALIBRATION_SAMPLES = 100  # 校准期样本数（与C++固件一致）
        self._isCalibrating = True       # 是否处于校准阶段
        self._calibrationComplete = False
        

    def _check_active_axis(self, timestamp_ms):
        """检查活跃轴（标准差最大）。首次检查在100ms后，之后每1000ms"""
        interval = 1000 if self._lastAxisSelection > 0 else 100
        
        if timestamp_ms - self._lastAxisSelection < interval:
            return
            
        self._lastAxisSelection = timestamp_ms
        
        std_devs = []
        for i in range(3):
            if len(self._accelHistory[i]) < 10:
                std_devs.append(0)
            else:
                std_devs.append(np.std(list(self._accelHistory[i]), ddof=1))
        
        max_axis = np.argmax(std_devs)
        max_std = std_devs[max_axis]
        current_std = std_devs[self._activeAxis]
        
        # 迟滞逻辑 (Hysteresis): 防止频繁跳变
        # 1. 新轴的标准差必须比当前轴大 20% (x1.2)
        # 2. 或者当前轴实在太弱 (<0.05)，而新轴很强
        
        switch_needed = False
        if max_axis != self._activeAxis:
            if max_std > 0.05: # 噪声门限
                if max_std > current_std * 1.2:
                    switch_needed = True
                
        if switch_needed:
            print(f"[轴切换] {self._activeAxis} -> {max_axis} (Std: {std_devs})")
            self._activeAxis = int(max_axis)
            self._strokeState = 0  # 重置状态
        
    def process_sample(self, timestamp_ms, acc_x, acc_y, acc_z):
        """
        处理单个IMU采样点 - 相位状态机检测
        
        状态机流程:
        BACKGROUND → (正向突破) → PEAK_ZONE → (下降到负值) → TROUGH_ZONE → (恢复) → COOLDOWN → BACKGROUND
        """
        # 1. 双重滤波: Butterworth低通 -> EMA平滑
        bw_x = self.filters[0].filter(acc_x)
        bw_y = self.filters[1].filter(acc_y)
        bw_z = self.filters[2].filter(acc_z)
        
        filtered_x = self.ema_filters[0].filter(bw_x)
        filtered_y = self.ema_filters[1].filter(bw_y)
        filtered_z = self.ema_filters[2].filter(bw_z)
        
        # 更新历史数据 (使用双重滤波后的值)
        self._accelHistory[0].append(filtered_x)
        self._accelHistory[1].append(filtered_y)
        self._accelHistory[2].append(filtered_z)
        
        # 保存滤波数据用于可视化
        self.filtered_data_history['x'].append(filtered_x)
        self.filtered_data_history['y'].append(filtered_y)
        self.filtered_data_history['z'].append(filtered_z)
        self.filtered_data_history['t'].append(timestamp_ms / 1000.0)
        

        # ============ 初始化校准阶段 ============
        if self._isCalibrating:
            # 收集前100个样本用于建立初始背景统计（固定Y轴）
            if len(self._accelHistory[self._activeAxis]) >= self.CALIBRATION_SAMPLES:
                # 校准完成，计算Y轴的背景统计
                data = list(self._accelHistory[self._activeAxis])
                self._backgroundMean = np.mean(data)
                self._backgroundStd = max(np.std(data, ddof=1), 0.02)
                self._isCalibrating = False
                self._calibrationComplete = True
                
                if self.DEBUG:
                    print(f"[校准完成] {timestamp_ms/1000:.2f}s: Z轴")
                    print(f"           均值={self._backgroundMean:.3f}g, 标准差={self._backgroundStd:.3f}g")
            return None  # 校准期间不进行检测
        
        # 固定Z轴，不进行轴切换
        # self._check_active_axis(timestamp_ms)  # 已禁用
        
        # 确保至少有20个样本才开始检测（滤波器稳定）
        if not self._calibrationComplete and len(self._accelHistory[self._activeAxis]) < 20:
            return None  # 数据不足

            
        # 获取当前值 (双重滤波后)
        current_filtered = [filtered_x, filtered_y, filtered_z][self._activeAxis]
        current_raw = [acc_x, acc_y, acc_z][self._activeAxis]

        
        # 计算背景统计(使用历史数据)
        data = list(self._accelHistory[self._activeAxis])
        self._backgroundMean = np.mean(data)
        self._backgroundStd = max(np.std(data, ddof=1), 0.02)  # 最小标准差防止过敏感
        
        # 当前偏差
        deviation = current_filtered - self._backgroundMean
        
        # 动态阈值
        peak_threshold = max(self.PEAK_ENTER_FACTOR * self._backgroundStd, self.MIN_PEAK_ABSOLUTE)
        
        # ================== 状态机 ==================
        
        if self._strokeState == self.STATE_BACKGROUND:
            # 等待进入波峰区: 偏差超过阈值
            if deviation > peak_threshold:
                self._strokeState = self.STATE_PEAK_ZONE
                self._phaseStartTime = timestamp_ms
                self._strokeStartTime = timestamp_ms  # 记录划桨周期开始时间
                self._peakMaxValue = deviation
                self._peakMaxTime = timestamp_ms
                self._peakMaxRaw = current_raw
                self._peakMaxFiltered = current_filtered  # 存储实际滤波值用于可视化
                if self.DEBUG:
                    print(f"[进入波峰区] {timestamp_ms/1000:.2f}s: +{deviation:.3f}g (阈值={peak_threshold:.3f}g)")
        
        elif self._strokeState == self.STATE_PEAK_ZONE:
            # 在波峰区: 跟踪最大值，等待下降到负值区域
            if deviation > self._peakMaxValue:
                self._peakMaxValue = deviation
                self._peakMaxTime = timestamp_ms
                self._peakMaxRaw = current_raw
                self._peakMaxFiltered = current_filtered  # 更新实际滤波值用于可视化
            

            # 检测是否进入波谷区(偏差变负)
            if deviation < self.TROUGH_THRESHOLD:
                peak_duration = timestamp_ms - self._phaseStartTime
                if peak_duration >= self.MIN_PEAK_DURATION:
                    self._strokeState = self.STATE_TROUGH_ZONE
                    self._phaseStartTime = timestamp_ms
                    self._troughMinValue = deviation
                    self._troughMinTime = timestamp_ms
                    self._troughMinRaw = current_raw
                    self._troughMinFiltered = current_filtered  # 初始化滤波值用于可视化
                    if self.DEBUG:
                        print(f"[进入波谷区] {timestamp_ms/1000:.2f}s: {deviation:.3f}g, 波峰最大={self._peakMaxValue:.3f}g")

                else:
                    # 波峰持续太短，可能是噪声，重置
                    if self.DEBUG:
                        print(f"[波峰太短] {peak_duration}ms < {self.MIN_PEAK_DURATION}ms, 重置")
                    self._strokeState = self.STATE_BACKGROUND
        
        elif self._strokeState == self.STATE_TROUGH_ZONE:
            # 在波谷区: 跟踪最小值，等待恢复到背景
            if deviation < self._troughMinValue:
                self._troughMinValue = deviation
                self._troughMinTime = timestamp_ms
                self._troughMinRaw = current_raw
                self._troughMinFiltered = current_filtered  # 更新实际滤波值
                self._recoveryCounter = 0  # 出现新低点，重置恢复计数

            
            # 检测是否恢复 (dynamic recovery)
            # 逻辑优化：恢复阈值不再是固定的背景噪声，而是相对于当前波谷深度的比例
            # 例如：必须回升到波谷深度的 40% 以上才算恢复
            # trough_value是负数，peak_value是正数
            
            current_depth = self._troughMinValue  # 这是一个负值，例如 -0.5
            recovery_target = current_depth * 0.4 # 如果回升到 0.4倍处 (例如 -0.2)，就算恢复
            
            # 或者使用简单的绝对值回升检查：
            # 如果当前deviation > (TroughMin + Abs(TroughMin)*0.5)
            
            in_recovery_zone = False
            
            # 如果波谷非常深 (< -0.1)，使用比例恢复逻辑
            if self._troughMinValue < -0.1:
                 # 回升幅度需达到波谷深度的 50%
                 if deviation > (self._troughMinValue * 0.5):
                     in_recovery_zone = True
            else:
                 # 对于浅波谷，维持原有的背景噪声逻辑
                 recovery_threshold = self.RECOVERY_FACTOR * self._backgroundStd
                 if deviation > -recovery_threshold:
                     in_recovery_zone = True

            if in_recovery_zone:
                self._recoveryCounter += 1
            else:
                self._recoveryCounter = 0  # 不在恢复区，重置计数
            
            # 只有连续多个采样点都在恢复区才确认恢复
            if self._recoveryCounter >= self.RECOVERY_SAMPLES:
                trough_duration = timestamp_ms - self._phaseStartTime
                if trough_duration >= self.MIN_TROUGH_DURATION:
                    # 计算振幅
                    amplitude = self._peakMaxValue - self._troughMinValue
                    
                    if self.DEBUG:
                        print(f"[恢复到背景] 振幅={amplitude:.3f}g (需要>{self.MIN_AMPLITUDE}g)")
                    
                    if amplitude >= self.MIN_AMPLITUDE:
                        # 检查间隔
                        if self._lastStrokeTime == 0 or \
                           (self._peakMaxTime - self._lastStrokeTime) >= self.STROKE_MIN_INTERVAL:
                            # ✅ 确认划桨!
                            if self._lastStrokeTime > 0:
                                interval = self._peakMaxTime - self._lastStrokeTime
                                self._strokeRate = 60000.0 / interval
                            
                            self._strokeCount += 1
                            self._lastStrokeTime = self._peakMaxTime
                            
                            # 存储检测结果 - 使用直接存储的滤波值用于可视化
                            self.detected_strokes.append({
                                'stroke_start_time': self._strokeStartTime / 1000,  # 划桨周期开始时间
                                'stroke_end_time': timestamp_ms / 1000,  # 划桨周期结束时间
                                'time': timestamp_ms / 1000,
                                'peak_time': self._peakMaxTime / 1000,
                                'peak_value': self._peakMaxValue,
                                'peak_raw': self._peakMaxRaw,
                                'peak_filtered': self._peakMaxFiltered,  # 直接使用存储的滤波值
                                'trough_time': self._troughMinTime / 1000,
                                'trough_value': self._troughMinValue,
                                'trough_raw': self._troughMinRaw,
                                'trough_filtered': self._troughMinFiltered,  # 直接使用存储的滤波值
                                'amplitude': amplitude,
                                'rate': self._strokeRate
                            })


                            
                            print(f"✅ [划桨确认] 第{self._strokeCount}桨, 振幅={amplitude:.3f}g, 桨频={self._strokeRate:.1f} SPM")
                            
                            self._strokeState = self.STATE_COOLDOWN
                            self._phaseStartTime = timestamp_ms
                            self._recoveryCounter = 0
                            
                            return {
                                'event': 'stroke_detected',
                                'count': self._strokeCount,
                                'rate': self._strokeRate,
                                'amplitude': amplitude
                            }
                        else:
                            if self.DEBUG:
                                print(f"[丢弃] 间隔不足")
                            self._strokeState = self.STATE_BACKGROUND
                            self._recoveryCounter = 0
                    else:
                        if self.DEBUG:
                            print(f"[丢弃] 振幅不足 {amplitude:.3f}g < {self.MIN_AMPLITUDE}g")
                        self._strokeState = self.STATE_BACKGROUND
                        self._recoveryCounter = 0
                else:
                    # 波谷持续太短
                    if self.DEBUG:
                        print(f"[波谷太短] {trough_duration}ms < {self.MIN_TROUGH_DURATION}ms")
                    self._strokeState = self.STATE_BACKGROUND
                    self._recoveryCounter = 0

        
        elif self._strokeState == self.STATE_COOLDOWN:
            # 冷却期
            cooldown_elapsed = timestamp_ms - self._phaseStartTime
            if cooldown_elapsed >= self.COOLDOWN_DURATION:
                self._strokeState = self.STATE_BACKGROUND
                if self.DEBUG:
                    print(f"[冷却结束]")
        
        return None



class RealtimeSimulatorUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("实时划桨检测仿真器 - C++算法模拟")
        self.setGeometry(100, 100, 1400, 800)
        
        self.detector = RealtimeStrokeDetector()
        self.data = None
        self.current_idx = 0
        self.is_playing = False
        self.playback_speed = 1.0  # 1x = 实时
        
        # UI性能优化：高速时减少UI刷新频率
        self.ui_update_interval = 1  # UI更新间隔（帧数）
        self.ui_update_counter = 0   # UI更新计数器
        
        # 自动跟踪模式
        self.auto_tracking = True  # 默认启用自动跟踪
        
        self.init_ui()
        
    def init_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        
        # ============ 控制面板 ============
        control_group = QGroupBox("控制面板")
        control_layout = QHBoxLayout()
        
        self.load_btn = QPushButton("📂 加载CSV")
        self.load_btn.clicked.connect(self.load_data)
        control_layout.addWidget(self.load_btn)
        
        self.play_btn = QPushButton("▶ 开始")
        self.play_btn.clicked.connect(self.toggle_play)
        self.play_btn.setEnabled(False)
        self.play_btn.setStyleSheet("font-weight: bold; padding: 5px 15px;")
        control_layout.addWidget(self.play_btn)
        
        self.reset_btn = QPushButton("🔄 重置")
        self.reset_btn.clicked.connect(self.reset_simulation)
        control_layout.addWidget(self.reset_btn)
        
        control_layout.addSpacing(20)
        
        # 速度预设按钮
        speed_label = QLabel("速度:")
        speed_label.setStyleSheet("font-weight: bold;")
        control_layout.addWidget(speed_label)
        
        self.speed_buttons = []
        speed_presets = [0.25, 0.5, 1, 2, 5, 10, 20, 50, 100]

        for speed in speed_presets:
            btn = QPushButton(f"{speed}x")
            btn.setCheckable(True)
            btn.setMinimumWidth(45)
            if speed == 1:  # 默认1x
                btn.setChecked(True)
                btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
            btn.clicked.connect(lambda checked, s=speed, b=btn: self.set_speed(s, b))
            control_layout.addWidget(btn)
            self.speed_buttons.append((speed, btn))
        
        control_layout.addStretch()
        
        # 自动跟踪开关
        self.tracking_btn = QPushButton("👁 跟踪")
        self.tracking_btn.setCheckable(True)
        self.tracking_btn.setChecked(True)
        self.tracking_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        self.tracking_btn.clicked.connect(self.toggle_tracking)
        control_layout.addWidget(self.tracking_btn)
        
        control_group.setLayout(control_layout)
        layout.addWidget(control_group)
        
        # ============ 状态显示面板 ============
        stats_group = QGroupBox("检测状态")
        stats_layout = QHBoxLayout()
        
        # 左侧：大型状态指示器
        state_frame = QFrame()
        state_frame.setFrameStyle(QFrame.Box | QFrame.Raised)
        state_frame.setMinimumWidth(120)
        state_frame.setMaximumWidth(150)
        state_vbox = QVBoxLayout(state_frame)
        
        self.state_indicator = QLabel("背景")
        self.state_indicator.setAlignment(Qt.AlignCenter)
        self.state_indicator.setFont(QFont("Arial", 16, QFont.Bold))
        self.state_indicator.setMinimumHeight(50)
        self.state_indicator.setStyleSheet("""
            background-color: #9E9E9E; 
            color: white; 
            border-radius: 8px;
            padding: 10px;
        """)
        state_vbox.addWidget(self.state_indicator)
        stats_layout.addWidget(state_frame)
        
        # 中间：基本信息
        info_frame = QFrame()
        info_layout = QGridLayout(info_frame)
        
        self.time_label = QLabel("时间: 0.0s")
        self.time_label.setFont(QFont("Arial", 11, QFont.Bold))
        info_layout.addWidget(self.time_label, 0, 0)
        
        self.count_label = QLabel("划桨数: 0")
        self.count_label.setFont(QFont("Arial", 11, QFont.Bold))
        self.count_label.setStyleSheet("color: #1976D2;")
        info_layout.addWidget(self.count_label, 0, 1)
        
        self.rate_label = QLabel("桨频: 0 SPM")
        self.rate_label.setFont(QFont("Arial", 11, QFont.Bold))
        self.rate_label.setStyleSheet("color: #388E3C;")
        info_layout.addWidget(self.rate_label, 0, 2)
        
        self.axis_label = QLabel("活跃轴: Y")
        self.axis_label.setStyleSheet("color: #E65100; font-weight: bold;")
        info_layout.addWidget(self.axis_label, 1, 0)
        
        stats_layout.addWidget(info_frame)
        
        # 右侧：算法调试信息
        debug_frame = QFrame()
        debug_frame.setFrameStyle(QFrame.StyledPanel)
        debug_frame.setStyleSheet("background-color: #FAFAFA; border-radius: 5px;")
        debug_layout = QGridLayout(debug_frame)
        debug_layout.setSpacing(3)
        
        self.deviation_label = QLabel("偏差: 0.000g")
        self.deviation_label.setFont(QFont("Consolas", 10))
        debug_layout.addWidget(self.deviation_label, 0, 0)
        
        self.threshold_label = QLabel("阈值: 0.000g")
        self.threshold_label.setFont(QFont("Consolas", 10))
        debug_layout.addWidget(self.threshold_label, 0, 1)
        
        self.duration_label = QLabel("持续: 0ms")
        self.duration_label.setFont(QFont("Consolas", 10))
        debug_layout.addWidget(self.duration_label, 0, 2)
        
        self.peak_label = QLabel("波峰最大: --")
        self.peak_label.setFont(QFont("Consolas", 10))
        self.peak_label.setStyleSheet("color: #D32F2F;")
        debug_layout.addWidget(self.peak_label, 1, 0)
        
        self.trough_label = QLabel("波谷最小: --")
        self.trough_label.setFont(QFont("Consolas", 10))
        self.trough_label.setStyleSheet("color: #388E3C;")
        debug_layout.addWidget(self.trough_label, 1, 1)
        
        self.recovery_label = QLabel("恢复: 0/5")
        self.recovery_label.setFont(QFont("Consolas", 10))
        debug_layout.addWidget(self.recovery_label, 1, 2)
        
        stats_layout.addWidget(debug_frame)
        stats_layout.setStretch(0, 1)  # state indicator
        stats_layout.setStretch(1, 2)  # basic info
        stats_layout.setStretch(2, 3)  # debug info
        
        stats_group.setLayout(stats_layout)
        layout.addWidget(stats_group)
        
        # ============ 波形图 ============
        self.plot_widget = pg.PlotWidget(title="实时加速度波形 (滑动窗口)")
        self.plot_widget.setBackground('w')
        self.plot_widget.setLabel('left', 'Acceleration (g)')
        self.plot_widget.setLabel('bottom', 'Time (s)')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.setYRange(-1.0, 1.0)  # 初始范围，会动态调整
        self.plot_widget.enableAutoRange(axis='y', enable=False)  # 禁用Y轴自动缩放
        

        # 主曲线
        self.curve = self.plot_widget.plot(pen=pg.mkPen('b', width=2))
        
        # 阈值线
        self.mean_line = pg.InfiniteLine(angle=0, pen=pg.mkPen('#2196F3', width=1, style=Qt.DashLine))
        self.peak_threshold_line = pg.InfiniteLine(angle=0, pen=pg.mkPen('#F44336', width=1, style=Qt.DashLine))
        self.recovery_upper_line = pg.InfiniteLine(angle=0, pen=pg.mkPen('#4CAF50', width=1, style=Qt.DotLine))
        self.recovery_lower_line = pg.InfiniteLine(angle=0, pen=pg.mkPen('#4CAF50', width=1, style=Qt.DotLine))
        self.plot_widget.addItem(self.mean_line)
        self.plot_widget.addItem(self.peak_threshold_line)
        self.plot_widget.addItem(self.recovery_upper_line)
        self.plot_widget.addItem(self.recovery_lower_line)
        
        # 波峰标记（红色向下三角）
        self.peak_scatter = pg.ScatterPlotItem(
            size=14, brush=pg.mkBrush(255, 0, 0, 220), 
            symbol='t', pen=pg.mkPen('w', width=1)
        )
        self.plot_widget.addItem(self.peak_scatter)
        
        # 波谷标记（绿色向上三角）
        self.trough_scatter = pg.ScatterPlotItem(
            size=14, brush=pg.mkBrush(0, 180, 0, 220), 
            symbol='t1', pen=pg.mkPen('w', width=1)
        )
        self.plot_widget.addItem(self.trough_scatter)
        
        # 划桨周期背景区域 (用列表存储多个LinearRegionItem)
        self.stroke_regions = []
        
        # 添加图例
        legend = self.plot_widget.addLegend(offset=(70, 30))
        legend.addItem(pg.PlotDataItem(pen=pg.mkPen('#2196F3', width=1, style=Qt.DashLine)), '均值')
        legend.addItem(pg.PlotDataItem(pen=pg.mkPen('#F44336', width=1, style=Qt.DashLine)), '波峰阈值')
        legend.addItem(pg.PlotDataItem(pen=pg.mkPen('#4CAF50', width=1, style=Qt.DotLine)), '恢复阈值')
        
        layout.addWidget(self.plot_widget)
        
        # 监听图表的视图变化（用户拖动时禁用跟踪）
        self.plot_widget.sigRangeChanged.connect(self.on_view_changed)
        
        # 定时器
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_frame)
    
    def set_speed(self, speed, clicked_btn):
        """设置播放速度"""
        self.playback_speed = speed
        
        # 根据速度调整UI刷新频率，避免高速时UI卡顿
        if speed >= 50:
            self.ui_update_interval = 10  # 高速时每10帧更新一次UI
        elif speed >= 10:
            self.ui_update_interval = 5   # 中速时每5帧更新一次UI
        else:
            self.ui_update_interval = 1   # 低速时每帧都更新UI
        
        # 重置计数器
        self.ui_update_counter = 0
        
        # 更新按钮状态
        for s, btn in self.speed_buttons:
            if btn == clicked_btn:
                btn.setChecked(True)
                btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
            else:
                btn.setChecked(False)
                btn.setStyleSheet("")
        # 如果正在播放,更新定时器间隔
        if self.is_playing:
            interval = max(1, int(8 / self.playback_speed))
            self.timer.setInterval(interval)
        

    
    def toggle_tracking(self):
        """切换自动跟踪模式"""
        self.auto_tracking = self.tracking_btn.isChecked()
        if self.auto_tracking:
            self.tracking_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
            self.tracking_btn.setText("👁 跟踪")
        else:
            self.tracking_btn.setStyleSheet("")
            self.tracking_btn.setText("❌ 跟踪")
    
    def on_view_changed(self):
        """视图变化时的回调（用户手动拖动时禁用自动跟踪）"""
        # 如果当前正在自动更新，则忽略（避免循环触发）
        if hasattr(self, '_updating_view') and self._updating_view:
            return
        
        # 用户手动拖动，禁用自动跟踪
        if self.auto_tracking:
            self.auto_tracking = False
            self.tracking_btn.setChecked(False)
            self.tracking_btn.setStyleSheet("")
            self.tracking_btn.setText("❌ 跟踪")
    
    def load_data(self):
        file, _ = QFileDialog.getOpenFileName(self, "选择CSV", "", "CSV (*.csv)")
        if not file:
            return
        
        df = pd.read_csv(file)
        
        # 检测格式
        if 'timestamp' in df.columns and 'acc_y' in df.columns:
            df = df.dropna()
            # ⚠️ 关键修复：时间戳归零化（减去初始值）并检测单位
            timestamps_raw = df['timestamp'].values.astype(float)
            timestamps_raw = timestamps_raw - timestamps_raw[0]  # 从0开始
            
            # 检测单位：如果平均间隔小于1.0，说明是秒，需要转毫秒
            mean_diff = np.mean(np.diff(timestamps_raw))
            if mean_diff < 1.0:
                print(f"[数据预处理] 检测到时间戳单位为秒 (mean_diff={mean_diff:.4f}s)，转换为毫秒")
                self.timestamps = timestamps_raw * 1000.0
            else:
                self.timestamps = timestamps_raw
            
            self.acc_data = {
                'x': df['acc_x'].values,
                'y': df['acc_y'].values,
                'z': df['acc_z'].values
            }
            print(f"加载数据: {len(self.timestamps)} 样本")
            print(f"时间范围: {self.timestamps[0]:.1f}ms - {self.timestamps[-1]:.1f}ms")
            
            # 计算实际采样率
            actual_sample_rate = 1000 / np.mean(np.diff(self.timestamps))
            print(f"采样率: ~{actual_sample_rate:.1f} Hz")
            
            # ⚠️ 关键修复：根据实际采样率调整截止频率
            # Nyquist定理：截止频率必须 < 采样率/2
            # 对于低采样率数据，需要降低截止频率
            nyquist = actual_sample_rate / 2
            optimal_cutoff = min(3.0, nyquist * 0.15)  # 不超过Nyquist频率的15%
            
            if abs(actual_sample_rate - 125.0) > 10:  # 如果采样率差异超过10Hz
                print(f"[警告] 实际采样率({actual_sample_rate:.1f}Hz)与设计采样率(125Hz)差异较大")
                print(f"       Nyquist频率={nyquist:.1f}Hz, 最优截止频率={optimal_cutoff:.2f}Hz")
                print(f"       正在根据实际采样率重新初始化Butterworth滤波器...")
                self.detector.sample_rate = actual_sample_rate
                self.detector.cutoff_hz = optimal_cutoff
                self.detector.filters = [
                    ButterworthFilter(cutoff_hz=optimal_cutoff, sample_rate=actual_sample_rate) 
                    for _ in range(3)
                ]
                print(f"       ✅ 滤波器已更新: cutoff={optimal_cutoff:.2f}Hz, fs={actual_sample_rate:.1f}Hz")
            else:
                print(f"       采样率接近设计值，使用默认滤波器: cutoff=3.0Hz, fs=125Hz")
        else:
            QMessageBox.warning(self, "错误", "CSV需要包含 timestamp, acc_x, acc_y, acc_z 列")
            return
        
        self.current_idx = 0
        self.detector = RealtimeStrokeDetector()  # 重置检测器
        self.play_btn.setEnabled(True)
        self.load_btn.setText(f"✓ {len(self.timestamps)} 样本")
        
        # 重置图表
        self.reset_simulation()
        
    def toggle_play(self):
        if not self.is_playing:
            self.is_playing = True
            self.play_btn.setText("⏸ 暂停")
            # 固定16ms刷新间隔(约60fps)，通过每帧处理样本数来控制速度
            self.timer.start(16)
        else:
            self.is_playing = False
            self.play_btn.setText("▶ 继续")
            self.timer.stop()
    
    def update_speed(self, value):
        self.playback_speed = value / 10.0
        self.speed_label.setText(f"速度: {self.playback_speed:.1f}x")
        # 不需要更新定时器间隔，因为我们通过每帧样本数控制速度
    
    def update_frame(self):
        if self.current_idx >= len(self.timestamps):
            self.toggle_play()
            return
        
        # 根据速度计算每帧处理的样本数
        # 16ms定时器，原始数据约10ms/样本(100Hz)
        # 1x速度 = 16ms/10ms = 1.6个样本
        # 100x速度 = 160个样本
        samples_per_frame = max(1, int(self.playback_speed * 1.6))
        
        # 处理多个样本（算法全速运行，不受UI刷新影响）
        for _ in range(samples_per_frame):
            if self.current_idx >= len(self.timestamps):
                break
            
            # 获取当前样本
            ts = self.timestamps[self.current_idx]
            ax = self.acc_data['x'][self.current_idx]
            ay = self.acc_data['y'][self.current_idx]
            az = self.acc_data['z'][self.current_idx]
            
            # 处理样本
            result = self.detector.process_sample(ts, ax, ay, az)
            self.current_idx += 1
        
        # UI性能优化：控制UI刷新频率
        self.ui_update_counter += 1
        if self.ui_update_counter >= self.ui_update_interval:
            # 重置计数器并更新UI
            self.ui_update_counter = 0
        else:
            return  # 跳过此次UI更新，但算法继续全速处理
        
        # 使用最后处理的样本更新显示
        ts = self.timestamps[min(self.current_idx - 1, len(self.timestamps) - 1)]
        

        # ============ 更新基本显示 ============
        self.time_label.setText(f"时间: {ts/1000:.2f}s")
        self.count_label.setText(f"划桨数: {self.detector._strokeCount}")
        self.rate_label.setText(f"桨频: {self.detector._strokeRate:.1f} SPM")
        
        axis_names = ['X', 'Y', 'Z']
        self.axis_label.setText(f"活跃轴: {axis_names[self.detector._activeAxis]}")
        
        # ============ 更新状态指示器（颜色+文字）
        state_config = {
            0: ("背景", "#9E9E9E"),
            1: ("🔴 波峰区", "#F44336"),
            2: ("🟢 波谷区", "#4CAF50"),
            3: ("⏳ 冷却", "#FF9800"),
        }
        state_idx = min(self.detector._strokeState, 3)
        state_name, state_color = state_config[state_idx]
        self.state_indicator.setText(state_name)
        self.state_indicator.setStyleSheet(f"""
            background-color: {state_color}; 
            color: white; 
            border-radius: 8px;
            padding: 10px;
        """)
        
        # ============ 更新调试信息 ============
        # 计算当前偏差值和阈值
        current_deviation = 0.0
        peak_threshold = 0.0
        recovery_threshold = 0.0
        phase_duration = 0
        
        if hasattr(self.detector, '_backgroundMean') and hasattr(self.detector, '_backgroundStd'):
            mean = self.detector._backgroundMean
            std = self.detector._backgroundStd
            peak_threshold = max(self.detector.PEAK_ENTER_FACTOR * std, self.detector.MIN_PEAK_ABSOLUTE)
            recovery_threshold = self.detector.RECOVERY_FACTOR * std
            
            # 获取当前滤波值
            if len(self.detector._accelHistory[self.detector._activeAxis]) > 0:
                current_filtered = self.detector._accelHistory[self.detector._activeAxis][-1]
                current_deviation = current_filtered - mean
                
            # 相位持续时间
            if self.detector._phaseStartTime > 0:
                phase_duration = int(ts - self.detector._phaseStartTime)
        
        self.deviation_label.setText(f"偏差: {current_deviation:+.3f}g")
        if current_deviation > peak_threshold:
            self.deviation_label.setStyleSheet("color: #D32F2F; font-weight: bold;")
        elif current_deviation < -recovery_threshold:
            self.deviation_label.setStyleSheet("color: #388E3C; font-weight: bold;")
        else:
            self.deviation_label.setStyleSheet("color: black;")
            
        self.threshold_label.setText(f"阈值: {peak_threshold:.3f}g")
        self.duration_label.setText(f"持续: {phase_duration}ms")
        
        # 波峰/波谷实时值
        if self.detector._strokeState == self.detector.STATE_PEAK_ZONE:
            self.peak_label.setText(f"波峰最大: {self.detector._peakMaxValue:+.3f}g")
        else:
            self.peak_label.setText("波峰最大: --")
        
        if self.detector._strokeState == self.detector.STATE_TROUGH_ZONE:
            self.trough_label.setText(f"波谷最小: {self.detector._troughMinValue:+.3f}g")
            self.recovery_label.setText(f"恢复: {self.detector._recoveryCounter}/{self.detector.RECOVERY_SAMPLES}")
        else:
            self.trough_label.setText("波谷最小: --")
            self.recovery_label.setText("恢复: 0/5")
        
        # ============ 更新阈值线位置 ============
        if hasattr(self.detector, '_backgroundMean'):
            mean = self.detector._backgroundMean
            std = max(self.detector._backgroundStd, 0.01)
            self.mean_line.setValue(mean)
            self.peak_threshold_line.setValue(mean + peak_threshold)
            self.recovery_upper_line.setValue(mean + recovery_threshold)
            self.recovery_lower_line.setValue(mean - recovery_threshold)
        
        # ============ 绘制波形 ============
        filtered_hist = self.detector.filtered_data_history
        if len(filtered_hist['t']) > 0:
            # 显示所有历史数据（不再是滑动窗口）
            t_all = filtered_hist['t']
            axis_key = ['x', 'y', 'z'][self.detector._activeAxis]
            y_all = filtered_hist[axis_key]
            
            if len(t_all) > 0:
                # 设置标志，避免触发on_view_changed
                self._updating_view = True
                
                self.curve.setData(t_all, y_all)
                
                # 自动跟踪模式：调整X轴显示最后10秒
                if self.auto_tracking and len(t_all) > 1:
                    latest_time = t_all[-1]
                    self.plot_widget.setXRange(max(0, latest_time - 10), latest_time + 0.5, padding=0)
                
                # 动态调整Y轴范围（确保包含所有数据）
                if len(y_all) > 0:
                    y_min = float(min(y_all))
                    y_max = float(max(y_all))
                    y_range = y_max - y_min
                    
                    # 如果范围太小，使用默认范围
                    if y_range < 0.1:
                        y_center = (y_min + y_max) / 2
                        self.plot_widget.setYRange(y_center - 0.5, y_center + 0.5, padding=0)
                    else:
                        # 添加10%边距
                        margin = y_range * 0.1
                        self.plot_widget.setYRange(y_min - margin, y_max + margin, padding=0)
                
                self._updating_view = False
            
            # 绘制检测到的波峰和波谷 （显示所有）
            if self.detector.detected_strokes:
                
                # 过滤波峰 - 使用滤波后的值用于标记
                visible_peaks = [(s['peak_time'], s.get('peak_filtered', s['peak_raw'])) for s in self.detector.detected_strokes]
                if visible_peaks:
                    p_t, p_v = zip(*visible_peaks)
                    self._updating_view = True
                    self.peak_scatter.setData(p_t, p_v)
                    self._updating_view = False
                else:
                    self.peak_scatter.clear()
                
                # 过滤波谷 - 使用滤波后的值用于标记
                visible_troughs = [(s['trough_time'], s.get('trough_filtered', s['trough_raw'])) for s in self.detector.detected_strokes]
                if visible_troughs:
                    t_t, t_v = zip(*visible_troughs)
                    self._updating_view = True
                    self.trough_scatter.setData(t_t, t_v)
                    self._updating_view = False
                else:
                    self.trough_scatter.clear()
                
                # ============ 绘制划桨周期背景色带 ============
                # 清除旧的背景区域
                for region in self.stroke_regions:
                    self.plot_widget.removeItem(region)
                self.stroke_regions.clear()
                
                # 添加划桨周期背景 (显示所有)
                for stroke in self.detector.detected_strokes:
                    start_t = stroke.get('stroke_start_time', stroke['peak_time'] - 0.3)
                    end_t = stroke.get('stroke_end_time', stroke['trough_time'] + 0.2)
                    
                    region = pg.LinearRegionItem(
                        values=[start_t, end_t],
                        brush=pg.mkBrush(100, 149, 237, 50),  # 淡蓝色半透明
                        pen=pg.mkPen(None),  # 无边框
                        movable=False
                    )
                    region.setZValue(-10)  # 放到最底层
                    self.plot_widget.addItem(region)
                    self.stroke_regions.append(region)
        
    

    def reset_simulation(self):
        self.current_idx = 0
        self.detector = RealtimeStrokeDetector()
        self.is_playing = False
        self.play_btn.setText("▶ 开始")
        self.timer.stop()
        self.curve.setData([], [])  # 清空波形
        self.peak_scatter.setData([], []) # 清空波峰
        self.trough_scatter.setData([], []) # 清空波谷
        # 清空划桨周期背景区域
        for region in self.stroke_regions:
            self.plot_widget.removeItem(region)
        self.stroke_regions.clear()


if __name__ == '__main__':
    app = QApplication(sys.argv)
    window = RealtimeSimulatorUI()
    window.show()
    sys.exit(app.exec_())
