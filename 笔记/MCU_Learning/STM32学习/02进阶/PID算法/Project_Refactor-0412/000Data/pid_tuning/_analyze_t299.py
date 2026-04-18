import json, sys
with open(r'T299_tune_jitter_test_20260413_113411_report.json', 'r', encoding='utf-8') as f:
    d = json.load(f)
a = d['analysis']

print("=== KEY METRICS ===")
print(f"sample_period: {a['sample_period_ms']:.1f}ms  samples: {a['run_samples']}")
print(f"pc: range=[{a['speed_pc_range'][0]},{a['speed_pc_range'][1]}]  mean={a['speed_mean_pc']:.1f}  std={a['speed_std_pc']:.1f}")
print(f"jitter: {a['jitter_severity']}  score={a['jitter_score']}  slew_max={a['max_pc_slew']}  slew_mean={a['mean_pc_slew']:.1f}")
print(f"yaw: drift={a['heading_yaw_drift_rate_dps']:.4f}  std={a['heading_yaw_std']:.3f}  max={a['heading_yaw_max_abs']}")
print(f"pc_osc: detected={a.get('fr_pc_osc_detected','?')}  period={a.get('fr_pc_osc_period_ms',0)}ms  amp={a.get('fr_pc_osc_amplitude',0)}")
print(f"spd_osc: detected={a.get('fr_spd_osc_detected','?')}  period={a.get('fr_spd_osc_period_ms',0)}ms")
print(f"cruise_lat: net={a.get('fr_cruise_lat_net_mm',0)}mm  rate={a.get('fr_cruise_lat_drift_rate_mm_s',0)}mm/s  dir={a.get('fr_cruise_lat_drift_dir','?')}")

print("\n=== WINDOW TREND (500ms) ===")
print(f"{'t_s':>5}  {'state':>10}  {'pc_std':>6}  {'pc_rng':>10}  {'el':>5}  {'hd':>5}  {'OL':>5}  {'OR':>5}")
for w in a.get('windows', []):
    t = w['t_start_ms'] / 1000
    state = w['motion_state']
    pc_std = w['pc_std']
    pc_rng = f"[{w['pc_min']},{w['pc_max']}]"
    el = w['el_mean']
    hd = w['hd_mean']
    ol = w['ol_mean']
    or_ = w['or_mean']
    print(f"{t:5.1f}  {state:>10}  {pc_std:6.1f}  {pc_rng:>10}  {el:5.0f}  {hd:+5.1f}  {ol:5.1f}  {or_:5.1f}")

# Phase analysis
print("\n=== PHASE DRIFT ===")
for ph in a.get('fr_phase_analysis', []):
    print(f"  {ph['name']:>4}  {ph['t_start']:.0f}-{ph['t_end']:.0f}s  lat:{ph['lat_start']:+.1f}->{ph['lat_end']:+.1f}mm  net={ph['net']:+.1f}  rate={ph['drift_rate']:+.2f}mm/s  {ph['dir']}")

# Timeline
print("\n=== TIMELINE (1s) ===")
print(f"{'t':>6}  {'lat':>8}  {'lat_v':>7}  {'yaw':>7}  {'yr':>6}  {'pc':>3}  {'hd':>3}  {'dp':>3}  {'OL':>3}  {'OR':>3}")
for e in a.get('fr_timeline', []):
    print(f"{e['t']:5.1f}s  {e['lat']:+8.2f}  {e['lat_v']:+7.2f}  {e['yaw']:+7.3f}  {e['yr']:+6.2f}  {e['pc']:+3d}  {e['hd']:+3d}  {e['dp']:+3d}  {e['ol']:3d}  {e['or']:3d}")
