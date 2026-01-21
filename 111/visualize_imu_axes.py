"""
三轴IMU加速度可视化工具
用于分析不同轴的信号特征，找出最适合划桨检测的轴
"""
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'Arial']
plt.rcParams['axes.unicode_minus'] = False

# 加载数据
print("正在加载数据...")
df = pd.read_csv('111/data/imu_log_014.csv')

# 时间转换为秒（从0开始）
time_s = (df['timestamp'] - df['timestamp'].iloc[0]) / 1000.0

# 创建图形
fig = plt.figure(figsize=(16, 10))
gs = GridSpec(4, 2, figure=fig, hspace=0.3, wspace=0.3)

# 颜色定义
colors = {'x': '#FF4444', 'y': '#44FF44', 'z': '#4444FF'}

# ========== 1. 三轴原始波形对比 ==========
ax1 = fig.add_subplot(gs[0, :])
ax1.plot(time_s, df['acc_x'], label='X轴 (前后)', color=colors['x'], alpha=0.7, linewidth=1)
ax1.plot(time_s, df['acc_y'], label='Y轴 (左右)', color=colors['y'], alpha=0.7, linewidth=1)
ax1.plot(time_s, df['acc_z'], label='Z轴 (上下)', color=colors['z'], alpha=0.7, linewidth=1)
ax1.axhline(y=0, color='gray', linestyle='--', alpha=0.3)
ax1.set_xlabel('时间 (秒)', fontsize=11)
ax1.set_ylabel('加速度 (g)', fontsize=11)
ax1.set_title('三轴加速度原始波形对比 - 完整时间序列', fontsize=13, fontweight='bold')
ax1.legend(loc='upper right', fontsize=10)
ax1.grid(True, alpha=0.3)

# ========== 2. 局部放大（前10秒） ==========
ax2 = fig.add_subplot(gs[1, 0])
mask = time_s <= 10
ax2.plot(time_s[mask], df['acc_x'][mask], label='X轴', color=colors['x'], linewidth=1.5)
ax2.plot(time_s[mask], df['acc_y'][mask], label='Y轴', color=colors['y'], linewidth=1.5)
ax2.plot(time_s[mask], df['acc_z'][mask], label='Z轴', color=colors['z'], linewidth=1.5)
ax2.axhline(y=0, color='gray', linestyle='--', alpha=0.3)
ax2.set_xlabel('时间 (秒)', fontsize=10)
ax2.set_ylabel('加速度 (g)', fontsize=10)
ax2.set_title('局部放大视图 (前10秒)', fontsize=11, fontweight='bold')
ax2.legend(loc='upper right', fontsize=9)
ax2.grid(True, alpha=0.3)

# ========== 3. 信号强度对比（标准差） ==========
ax3 = fig.add_subplot(gs[1, 1])
axes_names = ['X轴\n(前后)', 'Y轴\n(左右)', 'Z轴\n(上下)']
std_values = [df['acc_x'].std(), df['acc_y'].std(), df['acc_z'].std()]
bars = ax3.bar(axes_names, std_values, color=[colors['x'], colors['y'], colors['z']], alpha=0.7, edgecolor='black')

# 添加数值标签
for i, (bar, val) in enumerate(zip(bars, std_values)):
    height = bar.get_height()
    ax3.text(bar.get_x() + bar.get_width()/2., height,
             f'{val:.4f}g',
             ha='center', va='bottom', fontsize=10, fontweight='bold')

# 标记最强轴
max_idx = np.argmax(std_values)
bars[max_idx].set_edgecolor('red')
bars[max_idx].set_linewidth(3)
ax3.text(max_idx, std_values[max_idx] * 1.1, '⭐ 最强', 
         ha='center', fontsize=11, fontweight='bold', color='red')

ax3.set_ylabel('标准差 (g)', fontsize=10)
ax3.set_title('信号强度对比 (标准差)', fontsize=11, fontweight='bold')
ax3.grid(True, alpha=0.3, axis='y')

# ========== 4-6. 每个轴的详细分析 ==========
for idx, (axis, axis_name, color) in enumerate([
    ('acc_x', 'X轴 (前后)', colors['x']),
    ('acc_y', 'Y轴 (左右)', colors['y']),
    ('acc_z', 'Z轴 (上下)', colors['z'])
]):
    ax = fig.add_subplot(gs[2 + idx // 2, idx % 2])
    
    # 绘制波形
    ax.plot(time_s, df[axis], color=color, linewidth=1, alpha=0.8)
    ax.axhline(y=0, color='gray', linestyle='--', alpha=0.3)
    ax.axhline(y=df[axis].mean(), color='orange', linestyle='--', 
               alpha=0.5, label=f'均值={df[axis].mean():.3f}g')
    
    # 标记峰峰值
    max_val = df[axis].max()
    min_val = df[axis].min()
    pp_val = max_val - min_val
    
    ax.fill_between(time_s, min_val, max_val, alpha=0.1, color=color)
    
    ax.set_xlabel('时间 (秒)', fontsize=9)
    ax.set_ylabel('加速度 (g)', fontsize=9)
    ax.set_title(f'{axis_name} 单轴分析', fontsize=10, fontweight='bold')
    
    # 添加统计信息
    stats_text = (f'标准差: {df[axis].std():.4f}g\n'
                  f'峰峰值: {pp_val:.4f}g\n'
                  f'范围: [{min_val:.3f}, {max_val:.3f}]')
    ax.text(0.02, 0.98, stats_text, transform=ax.transAxes,
            fontsize=8, verticalalignment='top',
            bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))
    
    ax.legend(loc='upper right', fontsize=8)
    ax.grid(True, alpha=0.3)

# 总结信息
summary_text = f"""
【数据分析总结】
总样本: {len(df)} | 时长: {time_s.iloc[-1]:.1f}秒 | 采样率: ~{len(df)/time_s.iloc[-1]:.0f}Hz

信号强度排名:
1️⃣ Z轴 (上下): {df['acc_z'].std():.4f}g ⭐⭐⭐
2️⃣ Y轴 (左右): {df['acc_y'].std():.4f}g ⭐⭐
3️⃣ X轴 (前后): {df['acc_x'].std():.4f}g ⭐

💡 建议: 使用 {'Z轴' if np.argmax([df['acc_x'].std(), df['acc_y'].std(), df['acc_z'].std()]) == 2 else 'Y轴' if np.argmax([df['acc_x'].std(), df['acc_y'].std(), df['acc_z'].std()]) == 1 else 'X轴'} 进行划桨检测
"""

fig.text(0.5, 0.02, summary_text, ha='center', fontsize=9,
         bbox=dict(boxstyle='round', facecolor='lightyellow', alpha=0.8),
         family='monospace')

plt.suptitle('IMU三轴加速度综合分析 - imu_log_011.csv', 
             fontsize=14, fontweight='bold', y=0.98)

# 保存图片
output_file = 'imu_axes_visualization.png'
plt.savefig(output_file, dpi=150, bbox_inches='tight')
print(f"\n✅ 可视化图表已保存: {output_file}")
print("\n正在显示图表...")
plt.show()
