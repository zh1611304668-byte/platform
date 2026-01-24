"""
实时划桨检测仿真器 - C++算法逻辑复现
按时间窗口逐步处理数据，模拟ESP32真实运行环境
"""
import sys
import os
import time
from collections import deque
import numpy as np
import pandas as pd
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QFileDialog, QSlider, QGroupBox, QGridLayout,
    QMessageBox, QFrame, QScrollArea, QDoubleSpinBox, QSpinBox,
    QComboBox, QFormLayout, QRadioButton, QButtonGroup
)

from PyQt5.QtCore import QTimer, Qt, QThread, pyqtSignal, QObject
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
        # Initialize state with first value to avoid startup transient
        if self.x1 == 0.0 and self.x2 == 0.0 and self.y1 == 0.0 and self.y2 == 0.0:
             self.x1 = input_val
             self.x2 = input_val
             self.y1 = input_val
             self.y2 = input_val
             
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
        self.WINDOW_SIZE = 125  # 滑动窗口大小 - 125点*16ms = 2秒


        
        # 动态阈值系数 (相对于背景标准差)
        self.PEAK_ENTER_FACTOR = 0.8   # 进入波峰区的阈值 = mean + factor * std (降低以提高灵敏度)
        self.PEAK_EXIT_FACTOR = 0.4    # 离开波峰区(进入波谷)的阈值
        self.TROUGH_THRESHOLD = -0.005 # 绝对波谷阈值(低于均值多少算波谷区)
        self.RECOVERY_FACTOR = 1.0     # 恢复到背景的阈值
        
        # 绝对阈值(防止噪声过小时误触发) - 与C++固件完全一致
        self.MIN_PEAK_ABSOLUTE = 0.04 # 波峰最小绝对高度 (g)
        self.MIN_AMPLITUDE = 0.05   # 最小峰谷振幅 (g)
        
        # 时间参数
        self.MIN_PEAK_DURATION = 15    # 波峰区最小持续时间(ms) - 适配62.5Hz
        self.MIN_TROUGH_DURATION = 30  # 波谷区最小持续时间(ms) - 适配62.5Hz
        self.STROKE_MIN_INTERVAL = 150 # 两次划桨最小间隔(ms)
        self.COOLDOWN_DURATION = 100   # 冷却时间(ms)
        
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
        # 使用前N毫秒作为静止期，快速建立背景统计
        self.CALIBRATION_DURATION = 500  # 校准期时长(ms)
        self._isCalibrating = True       # 是否处于校准阶段
        self._calibrationComplete = False
        
    def _get_calibration_samples(self):
        """根据当前采样率计算需要的校准样本数"""
        return int(self.CALIBRATION_DURATION * self.sample_rate / 1000.0)
        

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
            cal_samples = self._get_calibration_samples()
            # 收集样本用于建立初始背景统计
            if len(self._accelHistory[self._activeAxis]) >= cal_samples:
                # 校准完成
                data = list(self._accelHistory[self._activeAxis])
                # 只取最近的 cal_samples 个样本
                data = data[-cal_samples:]
                
                self._backgroundMean = np.mean(data)
                self._backgroundStd = max(np.std(data, ddof=1), 0.02)
                self._isCalibrating = False
                self._calibrationComplete = True
                
                if self.DEBUG:
                    print(f"[校准完成] {timestamp_ms/1000:.2f}s: Z轴 (Duration={self.CALIBRATION_DURATION}ms, Samples={cal_samples})")
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
            # 在波峰区: 跟踪最大值
            # 1. 跟踪算法用的偏差最大值
            if deviation > self._peakMaxValue:
                self._peakMaxValue = deviation
            
            # 2. 跟踪可视化用的滤波值最大值 (独立跟踪，确保标记在波峰尖端)
            if current_filtered > self._peakMaxFiltered:
                self._peakMaxFiltered = current_filtered
                self._peakMaxTime = timestamp_ms
                self._peakMaxRaw = current_raw
            

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
            # 在波谷区: 跟踪最小值
            # 1. 跟踪算法用的偏差最小值
            if deviation < self._troughMinValue:
                self._troughMinValue = deviation

            # 2. 跟踪可视化用的滤波值最小值 (独立跟踪)
            if current_filtered < self._troughMinFiltered:
                self._troughMinFiltered = current_filtered
                self._troughMinTime = timestamp_ms
                self._troughMinRaw = current_raw
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
            # 【改进】如果信号已经过零（变正），说明已经开始下一划的趋势，强制结束当前划桨
            if deviation > 0:
                self._recoveryCounter = self.RECOVERY_SAMPLES

            if self._recoveryCounter >= self.RECOVERY_SAMPLES:
                trough_duration = timestamp_ms - self._phaseStartTime
                if trough_duration >= self.MIN_TROUGH_DURATION:
                    # 计算振幅 (使用滤波后的峰峰值，物理意义更明确且稳定)
                    # 原逻辑: amplitude = self._peakMaxValue - self._troughMinValue (基于偏差)
                    amplitude = self._peakMaxFiltered - self._troughMinFiltered
                    
                    if self.DEBUG:
                        print(f"[恢复到背景] 振幅={amplitude:.3f}g (需要>{self.MIN_AMPLITUDE}g)")
                    
                    if amplitude >= self.MIN_AMPLITUDE:
                        # 检查间隔 (DISABLED: 为了对比SpeedCoach，完全依赖波形)
                        # if self._lastStrokeTime == 0 or \
                        #    (self._peakMaxTime - self._lastStrokeTime) >= self.STROKE_MIN_INTERVAL:
                        if True:
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



class SimulationWorker(QThread):
    """
    Simulation Worker Thread
    Handles the data processing loop independently from the UI thread.
    """
    frame_update_signal = pyqtSignal()  # Signal to trigger UI update (30 FPS)
    finished_signal = pyqtSignal()      # Signal when simulation ends

    def __init__(self, detector):
        super().__init__()
        self.detector = detector
        self.timestamps = None
        self.acc_data = None
        self.timestamps = None
        self.acc_data = None
        
        self.current_idx = 0
        self.playback_speed = 1.0
        self.is_running = False
        self.is_paused = False
        
        # Timing control
        self.last_sim_time = 0
        self.wall_clock_start = 0
        self.sim_start_time = 0
        
    def load_data(self, timestamps, acc_data):
        self.timestamps = timestamps
        self.acc_data = acc_data
        self.current_idx = 0
        
    def run(self):
        self.is_running = True
        
        # Update interval for UI (33ms = ~30 FPS)
        ui_update_interval = 0.033
        last_ui_update = time.time()
        
        # Performance optimization:
        # Instead of strict time synchronization which can drift or lag,
        # we process a batch of samples corresponding to the elapsed time.
        
        self.wall_clock_start = time.time()
        # Ensure we have data
        if self.timestamps is None or len(self.timestamps) == 0:
            return

        self.sim_start_time = self.timestamps[self.current_idx]
        
        while self.is_running and self.current_idx < len(self.timestamps):
            if self.is_paused:
                time.sleep(0.1)
                # Reset clock when resuming
                self.wall_clock_start = time.time()
                self.sim_start_time = self.timestamps[self.current_idx]
                continue
            
            # 1. Calculate target simulation time
            now = time.time()
            elapsed_wall = now - self.wall_clock_start
            target_sim_time = self.sim_start_time + (elapsed_wall * 1000.0 * self.playback_speed)
            
            # 2. Fast-forward simulation to target time
            # Batch process to avoid function call overhead in loop if possible, 
            # but here we need to call process_sample
            
            start_idx = self.current_idx
            
            # Limit batch size to prevent blocking this thread for too long (e.g. if speed is huge)
            # But since this IS a background thread, blocking it is fine, 
            # as long as we yield eventually.
            
            while self.current_idx < len(self.timestamps):
                ts = self.timestamps[self.current_idx]
                if ts > target_sim_time:
                    break
                
                # Process sample
                ax = self.acc_data['x'][self.current_idx]
                ay = self.acc_data['y'][self.current_idx]
                az = self.acc_data['z'][self.current_idx]
                
                self.detector.process_sample(ts, ax, ay, az)
                self.current_idx += 1
            
            # 3. Check if we need to update UI
            if now - last_ui_update >= ui_update_interval:
                self.frame_update_signal.emit()
                last_ui_update = now
            
            # 4. Sleep a tiny bit to prevent 100% CPU usage on this core
            # If we are behind (processing < real time * speed), we shouldn't sleep ideally,
            # but a 1ms sleep is good for system stability.
            time.sleep(0.001)
            
        self.is_running = False
        self.finished_signal.emit()

    def set_speed(self, speed):
        # Adjust clocks to prevent jumping
        now = time.time()
        if self.current_idx < len(self.timestamps):
            current_sim_time = self.timestamps[self.current_idx]
            
            # Reset reference point
            self.playback_speed = float(speed)
            self.wall_clock_start = now
            self.sim_start_time = current_sim_time

    def stop(self):
        self.is_running = False
        self.wait()


class RealtimeSimulatorUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("实时划桨检测仿真器")

        self.setGeometry(100, 100, 1400, 800)
        
        self.detector = RealtimeStrokeDetector()
        self.data = None
        self.is_playing = False
        
        # Init Worker
        self.worker = SimulationWorker(self.detector)
        self.worker.frame_update_signal.connect(self.update_ui)
        self.worker.finished_signal.connect(self.on_simulation_finished)
        
        # 自动跟踪模式
        self.auto_tracking = True  # 默认启用自动跟踪
        
        self.init_ui()
        
        # 自动跟踪模式
        self.auto_tracking = True  # 默认启用自动跟踪
        
        self.init_ui()
        
    def init_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QHBoxLayout(central)
        
        # ============ 左侧控制面板 (Scrollable) ============
        sidebar = QWidget()
        sidebar_layout = QVBoxLayout(sidebar)
        sidebar_layout.setAlignment(Qt.AlignTop)
        
        scroll = QScrollArea()
        scroll.setWidget(sidebar)
        scroll.setWidgetResizable(True)
        scroll.setFixedWidth(380) # 固定宽度
        
        # 1. 基本控制
        self.create_control_panel(sidebar_layout)
        
        # 2. 轴选择
        self.create_axis_panel(sidebar_layout)
        
        # 3. 算法参数
        self.create_param_panel(sidebar_layout)
        
        # 4. 状态显示
        self.create_stats_panel(sidebar_layout)
        
        main_layout.addWidget(scroll)
        
        # ============ 右侧波形图 ============
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        
        # Use simple ViewBox update for performance? No, stick to PlotWidget but update Data efficiently.
        self.plot_widget = pg.PlotWidget(title="实时加速度波形 (30 FPS)")
        self.plot_widget.setBackground('w')
        self.plot_widget.setLabel('left', 'Acceleration (g)')
        self.plot_widget.setLabel('bottom', 'Time (s)')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.setYRange(-1.0, 1.0)
        self.plot_widget.enableAutoRange(axis='y', enable=False)
        
        # 图表元素初始化
        self.init_plot_elements()
        
        right_layout.addWidget(self.plot_widget)
        main_layout.addWidget(right_widget)
        
        # 定时器 (不再用于仿真，仅用于其他可能的UI刷新，或者移除)
        # self.timer = QTimer() 
        # self.timer.timeout.connect(self.update_frame) 

        
    def init_plot_elements(self):
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
        
        # 标记点
        self.peak_scatter = pg.ScatterPlotItem(
            size=14, brush=pg.mkBrush(255, 0, 0, 220), 
            symbol='t', pen=pg.mkPen('w', width=1)
        )
        self.plot_widget.addItem(self.peak_scatter)
        
        self.trough_scatter = pg.ScatterPlotItem(
            size=14, brush=pg.mkBrush(0, 180, 0, 220), 
            symbol='t1', pen=pg.mkPen('w', width=1)
        )
        self.plot_widget.addItem(self.trough_scatter)
        
        # Try to load the continuous pushing log by default for testing
        default_log = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data/imu_log_045.csv')
        if os.path.exists(default_log):
            QTimer.singleShot(100, lambda: self.load_data(default_log))
            
        self.stroke_regions = []
        
        # 图例
        legend = self.plot_widget.addLegend(offset=(30, 30))
        legend.addItem(pg.PlotDataItem(pen=pg.mkPen('#2196F3', width=1, style=Qt.DashLine)), '均值')
        legend.addItem(pg.PlotDataItem(pen=pg.mkPen('#F44336', width=1, style=Qt.DashLine)), '波峰阈值')

    def create_control_panel(self, parent_layout):
        group = QGroupBox("控制面板")
        layout = QVBoxLayout()
        
        # 按钮行
        btn_layout = QHBoxLayout()
        self.load_btn = QPushButton("📂 加载CSV")
        self.load_btn.clicked.connect(self.load_data)
        btn_layout.addWidget(self.load_btn)
        
        self.reset_btn = QPushButton("重置")
        self.reset_btn.clicked.connect(self.reset_simulation)
        btn_layout.addWidget(self.reset_btn)
        layout.addLayout(btn_layout)
        
        self.play_btn = QPushButton("▶ 开始")
        self.play_btn.clicked.connect(self.toggle_play)
        self.play_btn.setEnabled(False)
        self.play_btn.setStyleSheet("font-weight: bold; padding: 6px; font-size: 14px;")
        layout.addWidget(self.play_btn)
        
        # 速度控制
        speed_layout = QHBoxLayout()
        speed_layout.addWidget(QLabel("播放速度:"))
        self.speed_combo = QComboBox()
        self.speed_combo.addItems(["0.25x", "0.5x", "1.0x", "2.0x", "5.0x", "10x", "20x", "50x", "100x"])
        self.speed_combo.setCurrentText("1.0x")
        self.speed_combo.currentTextChanged.connect(self.on_speed_changed)
        speed_layout.addWidget(self.speed_combo)
        layout.addLayout(speed_layout)
        
        # 跟踪开关
        self.tracking_btn = QPushButton("自动跟踪")
        self.tracking_btn.setCheckable(True)
        self.tracking_btn.setChecked(True)
        self.tracking_btn.setStyleSheet("background-color: #4CAF50; color: white;")
        self.tracking_btn.toggled.connect(self.toggle_tracking)
        layout.addWidget(self.tracking_btn)
        
        group.setLayout(layout)
        parent_layout.addWidget(group)

    def create_axis_panel(self, parent_layout):
        group = QGroupBox("活动轴选择 (Active Axis)")
        layout = QHBoxLayout()
        
        self.axis_group = QButtonGroup(self)
        self.axis_radios = []
        for i, name in enumerate(['X 轴', 'Y 轴', 'Z 轴']):
            rb = QRadioButton(name)
            self.axis_group.addButton(rb, i)
            layout.addWidget(rb)
            self.axis_radios.append(rb)
            if i == 2: rb.setChecked(True) # 默认Z轴
            
        self.axis_group.buttonClicked[int].connect(self.change_axis)
        
        group.setLayout(layout)
        parent_layout.addWidget(group)

    def create_param_panel(self, parent_layout):
        group = QGroupBox("算法参数调节")
        layout = QFormLayout()
        
        self.param_inputs = {}

        def add_param(label, attr_name, val_type='float', min_v=0, max_v=100, step=0.01):
            if val_type == 'float':
                sb = QDoubleSpinBox()
                sb.setDecimals(3)
                sb.setSingleStep(step)
            else:
                sb = QSpinBox()
                sb.setSingleStep(int(step))
            
            sb.setRange(min_v, max_v)
            val = getattr(self.detector, attr_name)
            sb.setValue(val)
            # 绑定修改
            sb.valueChanged.connect(lambda v, attr=attr_name: setattr(self.detector, attr, v))
            layout.addRow(label, sb)
            self.param_inputs[attr_name] = sb
            return sb

        layout.addRow(QLabel("<b>阈值参数</b>"))
        add_param("波峰阈值 (g)", "MIN_PEAK_ABSOLUTE", 'float', 0.01, 2.0, 0.01)
        add_param("波谷阈值 (g)", "TROUGH_THRESHOLD", 'float', -2.0, 0.0, 0.01)
        add_param("最小振幅 (g)", "MIN_AMPLITUDE", 'float', 0.01, 2.0, 0.01)
        
        layout.addRow(QLabel("<b>动态系数</b>"))
        add_param("波峰进入系数", "PEAK_ENTER_FACTOR", 'float', 0.1, 10.0, 0.1)
        add_param("恢复系数", "RECOVERY_FACTOR", 'float', 0.1, 10.0, 0.1)
        
        layout.addRow(QLabel("<b>时间参数 (ms)</b>"))
        add_param("最小波峰持续", "MIN_PEAK_DURATION", 'int', 0, 500, 10)
        add_param("最小波谷持续", "MIN_TROUGH_DURATION", 'int', 0, 500, 10)
        add_param("冷却时间", "COOLDOWN_DURATION", 'int', 0, 2000, 50)
        add_param("最小划桨间隔", "STROKE_MIN_INTERVAL", 'int', 100, 3000, 50)
        add_param("窗口大小", "WINDOW_SIZE", 'int', 10, 1000, 10)
        add_param("校准时长(ms)", "CALIBRATION_DURATION", 'int', 0, 2000, 50)
        
        group.setLayout(layout)
        parent_layout.addWidget(group)

    def create_stats_panel(self, parent_layout):
        group = QGroupBox("实时状态")
        layout = QVBoxLayout()
        
        # 状态指示器
        self.state_indicator = QLabel("背景")
        self.state_indicator.setAlignment(Qt.AlignCenter)
        self.state_indicator.setFont(QFont("Arial", 14, QFont.Bold))
        self.state_indicator.setStyleSheet("background-color: #9E9E9E; color: white; border-radius: 5px; padding: 5px;")
        layout.addWidget(self.state_indicator)
        
        # 网格显示数值
        grid = QGridLayout()
        
        self.time_label = QLabel("时间: 0.0s")
        self.count_label = QLabel("划桨数: 0")
        self.rate_label = QLabel("桨频: 0 SPM")
        self.axis_label = QLabel("活跃轴: Z")
        
        grid.addWidget(self.time_label, 0, 0)
        grid.addWidget(self.count_label, 0, 1)
        grid.addWidget(self.rate_label, 1, 0)
        grid.addWidget(self.axis_label, 1, 1)
        
        layout.addLayout(grid)
        
        # 调试详情
        debug_group = QGroupBox("调试数据")
        d_layout = QGridLayout()
        
        self.deviation_label = QLabel("偏差: 0.000g")
        self.threshold_label = QLabel("阈值: 0.000g")
        self.peak_label = QLabel("波峰Max: --")
        self.trough_label = QLabel("波谷Min: --")
        self.duration_label = QLabel("持续: 0ms")
        self.recovery_label = QLabel("恢复: 0")
        
        d_layout.addWidget(self.deviation_label, 0, 0)
        d_layout.addWidget(self.threshold_label, 0, 1)
        d_layout.addWidget(self.peak_label, 1, 0)
        d_layout.addWidget(self.trough_label, 1, 1)
        d_layout.addWidget(self.duration_label, 2, 0)
        d_layout.addWidget(self.recovery_label, 2, 1)
        
        debug_group.setLayout(d_layout)
        layout.addWidget(debug_group)
        
        group.setLayout(layout)
        parent_layout.addWidget(group)

    def change_axis(self, axis_idx):
        if self.detector:
            self.detector._activeAxis = axis_idx
            names = ['X', 'Y', 'Z']
            print(f"切换活动轴: {names[axis_idx]}")
    
    def on_speed_changed(self, text):
        val = float(text.replace('x', ''))
        if hasattr(self, 'worker'):
            self.worker.set_speed(val)
    
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
    
    def load_data(self, file_path=None):
        if not file_path:
            file_path, _ = QFileDialog.getOpenFileName(self, "选择CSV", "", "CSV (*.csv)")
        
        if not file_path:
            return
        
        df = pd.read_csv(file_path)
        
        # 检测格式
        if 'timestamp' in df.columns and 'acc_y' in df.columns:
            df = df.dropna()
            timestamps_raw = df['timestamp'].values.astype(float)
            # timestamps_raw = timestamps_raw - timestamps_raw[0]  # DISABLED: Keep original timestamps
            
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
            # Auto-start for testing
            QTimer.singleShot(500, self.toggle_play)
        else:
            QMessageBox.warning(self, "错误", "CSV需要包含 timestamp, acc_x, acc_y, acc_z 列")
            return
        
        # 1. 重置仿真环境
        self.reset_simulation()
        self.play_btn.setEnabled(True)
        self.load_btn.setText(f"✓ {len(self.timestamps)} 样本")
        
        # 加载数据到Worker
        self.worker.load_data(self.timestamps, self.acc_data)
        
        # 2. 根据数据特性调整检测器滤波器
        # Nyquist定理：截止频率必须 < 采样率/2
        nyquist = actual_sample_rate / 2
        optimal_cutoff = min(3.0, nyquist * 0.15)
        
        if abs(actual_sample_rate - 125.0) > 10:
            print(f"[警告] 实际采样率({actual_sample_rate:.1f}Hz)与设计采样率(125Hz)差异较大")
            print(f"       调整滤波器: cutoff={optimal_cutoff:.2f}Hz, fs={actual_sample_rate:.1f}Hz")
            
            self.detector.sample_rate = actual_sample_rate
            self.detector.cutoff_hz = optimal_cutoff
            self.detector.filters = [
                ButterworthFilter(cutoff_hz=optimal_cutoff, sample_rate=actual_sample_rate) 
                for _ in range(3)
            ]
        else:
             print(f"       采样率接近设计值，使用默认滤波器")
        
    def toggle_play(self):
        if not self.is_playing:
            # Start
            self.is_playing = True
            self.play_btn.setText("⏸ 暂停")
            
            if not self.worker.isRunning():
                self.worker.start()
            self.worker.is_paused = False
            
        else:
            # Pause
            self.is_playing = False
            self.play_btn.setText("▶ 继续")
            self.worker.is_paused = True
    
    def on_simulation_finished(self):
        self.is_playing = False
        self.play_btn.setText("▶ 开始")
        QMessageBox.information(self, "完成", "仿真结束")
    
    def update_ui(self):
        # 此时 Worker 已经因为 signal 而稍微暂停/切出，我们可以读取 detector 状态
        
        # 使用最后处理的时间戳更新显示
        if not self.worker.timestamps is None and self.worker.current_idx > 0:
            # 安全读取 index
            idx = min(self.worker.current_idx - 1, len(self.worker.timestamps) - 1)
            ts = self.worker.timestamps[idx]
            
            # ============ 更新基本显示 ============
            self.time_label.setText(f"时间: {ts/1000:.2f}s")
        else:
            return
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
                    first_time = t_all[0]
                    # 防止显示起始时间之前的空白区域
                    min_x = max(first_time, latest_time - 10)
                    # 保持至少10秒的显示范围(可选，或者让它从小范围开始变大)
                    # 这里保持窗口总是至少10秒宽，这样一开始数据会在左边，右边是空白(未来)
                    max_x = max(min_x + 10, latest_time + 0.5)
                    self.plot_widget.setXRange(min_x, max_x, padding=0)
                
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
        # Stop worker
        if hasattr(self, 'worker'):
            self.worker.stop()
            self.worker.wait()
            
        # 保留当前的滤波器设置（如果有）
        current_sr = 125.0
        current_cutoff = 3.0
        if hasattr(self, 'detector'):
            current_sr = self.detector.sample_rate
            current_cutoff = self.detector.cutoff_hz
            
        self.detector = RealtimeStrokeDetector()
        
        # Re-assign detector to worker
        if hasattr(self, 'worker'):
            self.worker.detector = self.detector
            self.worker.current_idx = 0
        
        # 恢复滤波器设置
        if current_sr != 125.0 or current_cutoff != 3.0:
            self.detector.sample_rate = current_sr
            self.detector.cutoff_hz = current_cutoff
            self.detector.filters = [
                ButterworthFilter(cutoff_hz=current_cutoff, sample_rate=current_sr) 
                for _ in range(3)
            ]
        
        # 同步UI参数到检测器
        if hasattr(self, 'param_inputs'):
            for attr, spinbox in self.param_inputs.items():
                if hasattr(self.detector, attr):
                    setattr(self.detector, attr, spinbox.value())
        
        # 同步活动轴
        if hasattr(self, 'axis_group'):
            self.detector._activeAxis = self.axis_group.checkedId()
            
        self.is_playing = False
        self.play_btn.setText("▶ 开始")
        
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
