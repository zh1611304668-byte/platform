import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# 加载数据
df = pd.read_csv('data/imu_log_010.csv')

print('=' * 60)
print('IMU数据分析 - 45度放置检测')
print('=' * 60)

print('\n📊 数据概览:')
print(f'  总样本数: {len(df)}')
ts_min = df['timestamp'].iloc[0]
ts_max = df['timestamp'].iloc[-1]
duration = (ts_max - ts_min) / 1000  # 转为秒
print(f'  时间范围: {ts_min:.0f}ms - {ts_max:.0f}ms')
print(f'  持续时间: {duration:.2f}秒')
print(f'  采样率: ~{len(df) / duration:.1f} Hz')

print('\n📈 三轴统计:')
stats = {}
for axis in ['acc_x', 'acc_y', 'acc_z']:
    mean_val = df[axis].mean()
    std_val = df[axis].std()
    max_val = df[axis].max()
    min_val = df[axis].min()
    pp_val = max_val - min_val
    
    print(f'\n  {axis}:')
    print(f'    均值: {mean_val:7.4f}g')
    print(f'    标准差: {std_val:7.4f}g ⭐')
    print(f'    最大值: {max_val:7.4f}g')
    print(f'    最小值: {min_val:7.4f}g')
    print(f'    峰峰值: {pp_val:7.4f}g')
    
    stats[axis] = {'mean': mean_val, 'std': std_val, 'max': max_val, 'min': min_val, 'pp': pp_val}

print('\n🎯 信号强度排序 (按标准差):')
strengths = [
    ('X轴', stats['acc_x']['std']),
    ('Y轴', stats['acc_y']['std']),
    ('Z轴', stats['acc_z']['std'])
]
strengths.sort(key=lambda x: x[1], reverse=True)

for i, (name, std) in enumerate(strengths, 1):
    marker = '⭐⭐⭐' if i == 1 else '⭐⭐' if i == 2 else '⭐'
    print(f'  {i}. {name}: {std:.4f}g {marker}')

print('\n🔄 旋转角度分析:')
std_x = stats['acc_x']['std']
std_y = stats['acc_y']['std']
std_z = stats['acc_z']['std']

# 计算XY平面的旋转角度
angle_xy = np.arctan2(std_y, std_x) * 180 / np.pi
print(f'  XY平面旋转角度: {angle_xy:.1f}°')

# 计算信号合成后的强度
combined_xy = np.sqrt(std_x**2 + std_y**2)
print(f'  X+Y合成强度: {combined_xy:.4f}g')

print('\n💡 结论:')
if abs(angle_xy - 45) < 10:
    print(f'  ✅ 设备确实大约旋转了45度 (实际{angle_xy:.1f}°)')
elif abs(angle_xy + 45) < 10:
    print(f'  ✅ 设备大约旋转了-45度 (实际{angle_xy:.1f}°)')
else:
    print(f'  ⚠️ 设备旋转角度约为 {angle_xy:.1f}°')

# 信号损失分析
loss_percent = (1 - strengths[0][1] / combined_xy) * 100
print(f'  ⚠️ 单轴信号损失: {loss_percent:.1f}%')
print(f'  📌 最强轴是: {strengths[0][0]} (标准差={strengths[0][1]:.4f}g)')

print('\n🔧 建议:')
if strengths[0][0] == 'X轴':
    print('  ✅ 当前算法已固定X轴，符合数据特征')
elif strengths[0][0] == 'Y轴':
    print('  ⚠️ Y轴信号更强！建议修改算法使用Y轴')
    print(f'     或者重新安装设备，使X轴与船前后方向对齐')
else:
    print('  ❌ Z轴信号最强，这不正常！请检查设备安装')

if loss_percent > 20:
    print(f'  ⚠️ 信号损失{loss_percent:.0f}%较大，建议：')
    print(f'     1. 重新安装设备使X轴对齐船的方向')
    print(f'     2. 或降低检测阈值 (MIN_PEAK_ABSOLUTE从0.08降到{0.08*0.7:.3f})')
    print(f'     3. 或使用软件旋转矩阵校正')

print('\n' + '=' * 60)
