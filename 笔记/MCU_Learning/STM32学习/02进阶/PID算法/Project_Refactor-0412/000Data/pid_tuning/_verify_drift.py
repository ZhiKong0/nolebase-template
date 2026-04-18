"""Verify multi-method drift analysis on known data."""
import sys, os
sys.path.insert(0, r'F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0412\000Project_PC_Control')
from pid_tuner import parse_hb_line, estimate_drift_multi_method

def analyze_file(path, label):
    records = []
    for line in open(path, encoding='utf-8'):
        r = parse_hb_line(line.strip())
        if r and r.run == 1:
            records.append(r)
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"  File: {os.path.basename(path)}")
    print(f"  Records: {len(records)}")
    print(f"{'='*60}")
    
    if len(records) < 20:
        print("  Too few records!")
        return
    
    result = estimate_drift_multi_method(records)
    icon = '←' if '左' in result['consensus'] else ('→' if '右' in result['consensus'] else '·')
    print(f"\n  ★ 共识: {icon} {result['consensus']}  (置信度 {result['confidence']}%)")
    print(f"  投票: {result['votes']}")
    for name, m in result['methods'].items():
        label_map = {'enc_diff': '编码器差分', 'pwm_diff': 'PWM差分  ',
                     'hi': '航向积分  ', 'yaw_trend': 'Yaw趋势  ',
                     'odometry': '修正里程计'}
        print(f"    {label_map.get(name, name)}: [{m['dir']}] {m['desc']}")

# T330: HEADING_TRIM=-5, known physical LEFT drift
import glob
base = os.path.dirname(__file__)
t330 = glob.glob(os.path.join(base, 'T330_*_raw.txt'))
if t330:
    analyze_file(t330[0], "T330 (TRIM=-5, 已知物理左偏)")

# T331: AKI=0.035, user says slight RIGHT
t331 = glob.glob(os.path.join(base, 'T331_*_raw.txt'))
if t331:
    analyze_file(t331[0], "T331 (AKI=0.035, 用户观察微右偏)")

# T327: baseline, user says slight LEFT
t327 = glob.glob(os.path.join(base, 'T327_*_raw.txt'))
if t327:
    analyze_file(t327[0], "T327 (基线, 用户说左偏?)")
