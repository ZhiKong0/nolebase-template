"""
trajectory_plot.py — 从遥测HB数据重建小车2D轨迹
原理:
  - IMU yaw 提供绝对航向角(度)
  - 编码器 el, er 提供左右轮速度(ticks/周期, 正比于线速度)
  - 前进速度 v ∝ (el + er) / 2
  - 位移积分: dx = v * cos(yaw) * dt,  dy = v * sin(yaw) * dt
"""
import math
import sys

def parse_hb_file(filepath):
    """解析HB遥测文件, 返回去重后的有效记录列表"""
    records = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith('HB:'):
                continue
            kv = {}
            for pair in line[3:].split(','):
                if '=' in pair:
                    k, v = pair.split('=', 1)
                    kv[k.strip()] = v.strip()
            try:
                records.append({
                    't':   int(kv.get('t', '0')),
                    'm':   kv.get('m', 'S'),
                    'run': int(kv.get('run', '0')),
                    'el':  int(kv.get('el', '0')),
                    'er':  int(kv.get('er', '0')),
                    'yaw': float(kv.get('yaw', '0')),
                    'yr':  float(kv.get('yr', '0')),
                    'pc':  int(kv.get('pc', '0')),
                    'hd':  int(kv.get('hd', '0')),
                    'OL':  int(kv.get('OL', '0')),
                    'OR':  int(kv.get('OR', '0')),
                    'sb':  int(kv.get('sb', '0')),
                    'lp':  float(kv.get('lp', '0')),
                })
            except:
                pass

    # 去重: 按时间戳, 只保留 tracking+running
    seen = set()
    unique = []
    for r in records:
        if r['t'] not in seen and r['m'] == 'T' and r['run'] == 1:
            seen.add(r['t'])
            unique.append(r)
    return unique


def unwrap_yaw(records):
    """
    将 ±180° 跳变的 yaw 展开为连续角度
    例: ..., 178, 179, -179, -178 → ..., 178, 179, 181, 182
    """
    if not records:
        return []
    unwrapped = [records[0]['yaw']]
    for i in range(1, len(records)):
        delta = records[i]['yaw'] - records[i-1]['yaw']
        # 处理 ±180° 跳变
        while delta > 180.0:  delta -= 360.0
        while delta < -180.0: delta += 360.0
        unwrapped.append(unwrapped[-1] + delta)
    return unwrapped


def reconstruct_trajectory(records):
    """
    从遥测记录重建2D轨迹
    原理:
      - IMU yaw 提供连续航向角(展开后无跳变)
      - el, er 是编码器瞬时速度(ticks/控制周期)
      - 距离 = 平均速度 * 时间间隔 * 比例系数
      - 航向约定: yaw=0°时小车朝+Y(上), yaw增大=逆时针
    返回: xs, ys, ts, yaws_unwrapped
    """
    if len(records) < 2:
        return [], [], [], []

    ENCODER_TO_DIST = 0.015  # 比例系数 (只影响缩放不影响形状)

    # 展开 yaw 避免 ±180° 跳变
    yaws = unwrap_yaw(records)

    xs, ys, ts = [0.0], [0.0], [records[0]['t']]
    x, y = 0.0, 0.0

    for i in range(1, len(records)):
        r = records[i]
        r_prev = records[i - 1]

        dt_ms = r['t'] - r_prev['t']
        if dt_ms <= 0 or dt_ms > 500:
            ts.append(r['t'])
            xs.append(x)
            ys.append(y)
            continue

        dt_s = dt_ms / 1000.0

        # 前进速度 = 左右编码器平均速度
        # 只取正值 (负值=倒退, 旋转时一轮反转不代表前进)
        el = max(r['el'], 0)
        er = max(r['er'], 0)
        v = (el + er) / 2.0

        # 距离 = 瞬时速度 × 时间间隔
        dist = v * dt_s * ENCODER_TO_DIST

        # 用当前和上一帧 yaw 的平均值作为这段的航向 (梯形积分)
        yaw_avg = (yaws[i] + yaws[i-1]) / 2.0
        # 坐标系: yaw=0 → 朝+Y(上), yaw>0 → 逆时针
        # 标准数学: θ=90°-yaw 从+X轴逆时针
        theta = math.radians(90.0 - yaw_avg)

        dx = dist * math.cos(theta)
        dy = dist * math.sin(theta)

        x += dx
        y += dy

        xs.append(x)
        ys.append(y)
        ts.append(r['t'])

    return xs, ys, ts, yaws


def _setup_matplotlib():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import matplotlib.font_manager as fm
    import numpy as np
    for fname in ['Microsoft YaHei', 'SimHei', 'SimSun', 'KaiTi']:
        if any(fname.lower() in f.name.lower() for f in fm.fontManager.ttflist):
            plt.rcParams['font.sans-serif'] = [fname] + plt.rcParams['font.sans-serif']
            break
    plt.rcParams['axes.unicode_minus'] = False
    return plt, np


def find_lap_boundary(yaws, start_idx=0, threshold=340.0):
    """
    找一圈结束: 用净航向变化(unwrapped yaw差值)超过threshold度
    适用于小车沿闭合赛道跑一圈约转360°的场景
    """
    for i in range(start_idx + 1, len(yaws)):
        net = abs(yaws[i] - yaws[start_idx])
        if net >= threshold:
            return i
    return len(yaws) - 1


def build_trajectory(records, yaw_sign=1.0, scale=0.015):
    """用指定 yaw_sign 重建xy坐标 (+1=原始, -1=反转)"""
    yaws = unwrap_yaw(records)
    xs, ys = [0.0], [0.0]
    x, y = 0.0, 0.0
    for i in range(1, len(records)):
        dt_ms = records[i]['t'] - records[i-1]['t']
        if dt_ms <= 0 or dt_ms > 500:
            xs.append(x); ys.append(y)
            continue
        dt_s = dt_ms / 1000.0
        el = max(records[i]['el'], 0)
        er = max(records[i]['er'], 0)
        v = (el + er) / 2.0
        dist = v * dt_s * scale
        yaw_avg = yaw_sign * (yaws[i] + yaws[i-1]) / 2.0
        theta = math.radians(90.0 - yaw_avg)
        x += dist * math.cos(theta)
        y += dist * math.sin(theta)
        xs.append(x); ys.append(y)
    return xs, ys, yaws


def plot_trajectory(xs, ys, ts, yaws, records, output_path=None):
    """绘制多视图轨迹图: 4分格总览 + 单圈提取 + yaw方向对比"""
    plt, np = _setup_matplotlib()
    base = output_path.replace('.png', '') if output_path else 'trajectory'

    # ---------- 图1: 4分格总览 ----------
    fig, axes = plt.subplots(2, 2, figsize=(16, 14))
    fig.suptitle('小车巡线轨迹重建 (编码器+IMU)', fontsize=16, fontweight='bold')

    n = len(xs)
    colors = plt.cm.viridis(np.linspace(0, 1, n))

    ax1 = axes[0][0]
    for i in range(1, n):
        ax1.plot([xs[i-1], xs[i]], [ys[i-1], ys[i]], color=colors[i], linewidth=1.5)
    ax1.plot(xs[0], ys[0], 'go', markersize=12, label='Start', zorder=5)
    ax1.plot(xs[-1], ys[-1], 'r^', markersize=12, label='End', zorder=5)
    for r_idx, r in enumerate(records):
        if r['t'] % 10000 < 50 and r['t'] > 0 and r_idx < n:
            ax1.annotate(f"{r['t']//1000}s", (xs[r_idx], ys[r_idx]),
                        fontsize=8, color='red', fontweight='bold')
    ax1.set_xlabel('X'); ax1.set_ylabel('Y')
    ax1.set_title('全程轨迹 (颜色=时间)'); ax1.set_aspect('equal')
    ax1.legend(loc='upper right'); ax1.grid(True, alpha=0.3)

    ax2 = axes[0][1]
    t_sec = [(t - ts[0]) / 1000.0 for t in ts]
    ax2.plot(t_sec, yaws, 'b-', linewidth=0.8)
    ax2.set_xlabel('时间 (s)'); ax2.set_ylabel('Yaw (°, 展开)')
    ax2.set_title('航向角 vs 时间 (展开后)'); ax2.grid(True, alpha=0.3)

    ax3 = axes[1][0]
    t_rec = [(r['t'] - records[0]['t']) / 1000.0 for r in records]
    avg_v = [(max(r['el'],0) + max(r['er'],0)) / 2.0 for r in records]
    ax3.plot(t_rec, [r['el'] for r in records], 'b-', lw=0.5, alpha=0.5, label='Left')
    ax3.plot(t_rec, [r['er'] for r in records], 'r-', lw=0.5, alpha=0.5, label='Right')
    ax3.plot(t_rec, avg_v, 'k-', linewidth=1.0, label='Avg')
    ax3.set_xlabel('时间 (s)'); ax3.set_ylabel('Encoder ticks')
    ax3.set_title('轮速'); ax3.legend(); ax3.grid(True, alpha=0.3)

    ax4 = axes[1][1]
    ax4.plot(t_rec, [r['lp'] for r in records], 'g-', lw=0.6, alpha=0.7, label='lp')
    ax4.plot(t_rec, [r['hd'] for r in records], 'm-', lw=0.6, alpha=0.7, label='hd')
    ax4.axhline(y=0, color='k', lw=0.5, ls='--')
    ax4.set_xlabel('时间 (s)'); ax4.set_ylabel('值')
    ax4.set_title('传感器位置 & 纠偏'); ax4.legend(); ax4.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(f'{base}.png', dpi=150, bbox_inches='tight')
    print(f'保存: {base}.png')
    plt.close()

    # ---------- 图2: 单圈提取 (yaw=+1 和 yaw=-1 两种方向) ----------
    fig, axes = plt.subplots(1, 3, figsize=(24, 8))
    fig.suptitle('单圈轨迹提取 & yaw方向对比', fontsize=16, fontweight='bold')

    # 找第一圈边界 (用净航向变化 >= 340°)
    lap_end = find_lap_boundary(yaws, 0, 340.0)
    lap1_records = records[:lap_end+1]
    lap_yaw_net = abs(yaws[lap_end] - yaws[0])
    print(f'第一圈: 索引 0~{lap_end}, 时间 0~{records[lap_end]["t"]}ms ({records[lap_end]["t"]/1000:.1f}s), 净转角={lap_yaw_net:.0f}°')

    for ax_idx, (sign, label) in enumerate([(+1, 'yaw正向 (+1)'), (-1, 'yaw反向 (-1)')]):
        lxs, lys, _ = build_trajectory(lap1_records, yaw_sign=sign)
        ax = axes[ax_idx]
        ln = len(lxs)
        lcolors = plt.cm.cool(np.linspace(0, 1, ln))
        for i in range(1, ln):
            ax.plot([lxs[i-1], lxs[i]], [lys[i-1], lys[i]], color=lcolors[i], linewidth=2.5)
        ax.plot(lxs[0], lys[0], 'go', markersize=14, label='Start', zorder=5)
        ax.plot(lxs[-1], lys[-1], 'r^', markersize=14, label='End', zorder=5)
        # 标注方向箭头(每20个点)
        step = max(1, ln // 15)
        for i in range(0, ln-1, step):
            dx = lxs[min(i+1,ln-1)] - lxs[i]
            dy = lys[min(i+1,ln-1)] - lys[i]
            length = math.sqrt(dx*dx + dy*dy)
            if length > 0.01:
                ax.annotate('', xy=(lxs[i]+dx*0.5, lys[i]+dy*0.5),
                           xytext=(lxs[i], lys[i]),
                           arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
        ax.set_title(label, fontsize=14); ax.set_aspect('equal')
        ax.legend(); ax.grid(True, alpha=0.3)

    # 第三个子图: 全程多圈叠加 (每圈不同颜色)
    ax3 = axes[2]
    lap_starts = [0]
    idx = 0
    while idx < len(yaws) - 1:
        end = find_lap_boundary(yaws, idx, 340.0)
        if end <= idx:
            break
        lap_starts.append(end)
        idx = end
    lap_colors_list = plt.cm.Set1(np.linspace(0, 1, min(len(lap_starts), 10)))
    for lap_i in range(len(lap_starts) - 1):
        s, e = lap_starts[lap_i], lap_starts[lap_i + 1]
        lap_rec = records[s:e+1]
        if len(lap_rec) < 5:
            continue
        lxs, lys, _ = build_trajectory(lap_rec, yaw_sign=+1)
        c = lap_colors_list[lap_i % len(lap_colors_list)]
        ax3.plot(lxs, lys, color=c, linewidth=1.5, alpha=0.8, label=f'Lap {lap_i+1}')
    ax3.set_title(f'多圈叠加 ({len(lap_starts)-1} 圈)', fontsize=14)
    ax3.set_aspect('equal')
    if len(lap_starts) <= 8:
        ax3.legend(fontsize=8)
    ax3.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(f'{base}_laps.png', dpi=150, bbox_inches='tight')
    print(f'保存: {base}_laps.png')
    plt.close()

    # ---------- 图3: 大图全程轨迹 ----------
    fig, ax = plt.subplots(1, 1, figsize=(12, 12))
    for i in range(1, n):
        ax.plot([xs[i-1], xs[i]], [ys[i-1], ys[i]], color=colors[i], linewidth=2.0)
    ax.plot(xs[0], ys[0], 'go', markersize=15, label='Start', zorder=5)
    ax.plot(xs[-1], ys[-1], 'r^', markersize=15, label='End', zorder=5)
    for r_idx, r in enumerate(records):
        if r_idx < n and r['t'] % 10000 < 50 and r['t'] > 0:
            ax.annotate(f"{r['t']//1000}s", (xs[r_idx], ys[r_idx]),
                       fontsize=10, color='red', fontweight='bold',
                       bbox=dict(boxstyle='round,pad=0.2', facecolor='yellow', alpha=0.7))
    ax.set_xlabel('X', fontsize=14); ax.set_ylabel('Y', fontsize=14)
    ax.set_title('小车巡线轨迹 (编码器+IMU重建)', fontsize=16, fontweight='bold')
    ax.set_aspect('equal'); ax.legend(fontsize=12); ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(f'{base}_big.png', dpi=150, bbox_inches='tight')
    print(f'保存: {base}_big.png')
    plt.close()


def print_stats(records, xs, ys, ts):
    """打印轨迹统计"""
    duration_s = (records[-1]['t'] - records[0]['t']) / 1000.0
    total_dist = sum(math.sqrt((xs[i]-xs[i-1])**2 + (ys[i]-ys[i-1])**2)
                     for i in range(1, len(xs)))

    yaw_min = min(r['yaw'] for r in records)
    yaw_max = max(r['yaw'] for r in records)
    yaw_range = yaw_max - yaw_min

    # 检测转弯次数 (yaw变化超过30°的段)
    turn_count = 0
    yaw_accum = 0.0
    for i in range(1, len(records)):
        dy = records[i]['yaw'] - records[i-1]['yaw']
        if dy > 180: dy -= 360
        if dy < -180: dy += 360
        yaw_accum += dy
        if abs(yaw_accum) > 30:
            turn_count += 1
            yaw_accum = 0.0

    # 检测停顿次数 (编码器连续<5的段)
    stop_count = 0
    in_stop = False
    for r in records:
        avg_enc = (r['el'] + r['er']) / 2.0
        if avg_enc < 5:
            if not in_stop:
                stop_count += 1
                in_stop = True
        else:
            in_stop = False

    # 累计航向变化 (总共转了多少度)
    total_yaw_change = 0.0
    for i in range(1, len(records)):
        dy = records[i]['yaw'] - records[i-1]['yaw']
        if dy > 180: dy -= 360
        if dy < -180: dy += 360
        total_yaw_change += abs(dy)

    # 估算圈数 (总航向变化 / 360)
    est_laps = total_yaw_change / 360.0

    print(f"\n{'='*50}")
    print(f"{'轨迹统计':^50}")
    print(f"{'='*50}")
    print(f"运行时长:    {duration_s:.1f}s")
    print(f"有效样本:    {len(records)}")
    print(f"轨迹总长:    {total_dist:.1f} (任意单位)")
    print(f"航向范围:    {yaw_min:.1f}° ~ {yaw_max:.1f}° (跨度{yaw_range:.1f}°)")
    print(f"累计航向变化: {total_yaw_change:.1f}°")
    print(f"估算圈数:    {est_laps:.2f} 圈 (总转角/360°)")
    print(f"转弯次数:    {turn_count} (>30°的航向变化)")
    print(f"停顿次数:    {stop_count}")

    # 起终点距离 (越小说明越接近闭合)
    if xs and ys:
        close_dist = math.sqrt((xs[-1] - xs[0])**2 + (ys[-1] - ys[0])**2)
        print(f"起终点距离:  {close_dist:.2f} (越小越闭合)")


def main():
    datafile = r'f:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0412\000Data\trajectory_raw.txt'
    outfile = r'f:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0412\000Data\trajectory_plot.png'

    if len(sys.argv) > 1:
        datafile = sys.argv[1]

    print(f"加载数据: {datafile}")
    records = parse_hb_file(datafile)
    print(f"有效记录: {len(records)}")

    if len(records) < 10:
        print("数据太少，无法重建轨迹")
        return

    print("重建轨迹...")
    xs, ys, ts, yaws = reconstruct_trajectory(records)

    print_stats(records, xs, ys, ts)

    print("\n绘制轨迹图...")
    plot_trajectory(xs, ys, ts, yaws, records, outfile)
    print("完成!")


if __name__ == '__main__':
    main()
