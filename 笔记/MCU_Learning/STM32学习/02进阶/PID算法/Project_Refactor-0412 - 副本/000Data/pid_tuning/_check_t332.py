import sys, glob
sys.path.insert(0, r'F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0412\000Project_PC_Control')
from pid_tuner import parse_hb_line, estimate_drift_multi_method

tag = sys.argv[1] if len(sys.argv) > 1 else 'T332'
base = r'F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0412\000Data\pid_tuning'
files = glob.glob(f'{base}/{tag}_*_raw.txt')
if not files:
    print(f'No raw file for {tag}'); exit(1)
recs = [r for line in open(files[0]) for r in [parse_hb_line(line.strip())] if r and r.run == 1]
print(f'{tag}: {len(recs)} records')
result = estimate_drift_multi_method(recs)
icon = '<-' if '左' in result['consensus'] else ('->' if '右' in result['consensus'] else '--')
print(f'  {icon} {result["consensus"]} (confidence {result["confidence"]}%)')
print(f'  votes: {result["votes"]}')
for n, m in result['methods'].items():
    label = {'enc_diff': 'enc_diff ', 'pwm_diff': 'pwm_diff ', 'hi': 'hi       ',
             'yaw_trend': 'yaw_trend', 'odometry': 'odometry '}.get(n, n)
    print(f'    {label}: [{m["dir"]}] {m["desc"]}')
