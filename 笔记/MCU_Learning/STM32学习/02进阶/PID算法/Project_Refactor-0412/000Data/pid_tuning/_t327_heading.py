"""Extract heading and trajectory metrics from T327 report."""
import json, glob, os

# Find T327 report
import sys
tag = sys.argv[1] if len(sys.argv) > 1 else 'T327'
files = glob.glob(os.path.join(os.path.dirname(__file__), f'{tag}_*_report.json'))
if not files:
    print("T327 report not found!")
    exit(1)
r = json.load(open(files[0], encoding='utf-8'))
a = r.get('analysis', r)

print("=== 航向指标 ===")
keys_h = ['heading_yaw_final','heading_yaw_drift_rate_dps','heading_yaw_std',
           'heading_yaw_max_abs','heading_mean_hd','heading_std_hd',
           'heading_hd_saturation_pct','heading_yaw_integral',
           'heading_yaw_zero_crossings','heading_yaw_monotonic_pct',
           'fr_yaw_bias_dps']
for k in keys_h:
    print(f"  {k}: {a.get(k, 'N/A')}")

print("\n=== 轨迹指标 ===")
keys_t = ['traj_max_lateral_dev','traj_final_lateral_dev','traj_sinuosity',
           'fr_lat_max_mm','fr_lat_final_mm','fr_lat_drift_rate_mm_s',
           'fr_lat_rms_mm','fr_lat_wander_mm','fr_lat_wander_rms_mm',
           'fr_cruise_lat_max_mm','fr_cruise_lat_rms_mm','fr_cruise_lat_net_mm',
           'fr_cruise_lat_drift_rate_mm_s','fr_cruise_lat_drift_dir',
           'fr_cruise_lat_direction_pct']
for k in keys_t:
    print(f"  {k}: {a.get(k, 'N/A')}")

print("\n=== 分阶段漂移 ===")
for p in a.get('fr_phase_analysis', []):
    print(f"  {p['name']}: {p['t_start']}-{p['t_end']}s  lat={p['lat_start']:.1f}→{p['lat_end']:.1f}mm  net={p['net']:.1f}mm  rate={p['drift_rate']:.2f}mm/s  {p['dir']}")

print("\n=== 原始速度连贯性 ===")
keys_s = ['raw_ol_std','raw_ol_slew_mean','raw_ol_slew_max','raw_ol_slew_gt20_pct',
           'raw_pc_stall_pct','raw_pc_std','raw_pc_range','raw_el_cv']
for k in keys_s:
    print(f"  {k}: {a.get(k, 'N/A')}")

print("\n=== 评分明细 ===")
for k in ['score_speed','score_heading','score_trajectory','score_smoothness','straightness_score']:
    print(f"  {k}: {a.get(k, 'N/A')}")
