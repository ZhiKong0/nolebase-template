import json

r = json.load(open(r'T313_tune_core_min1_20260413_140214_report.json', encoding='utf-8'))

sections = {
    "Heading": ['heading_yaw_drift_rate_dps','heading_mean_hd','heading_correction_sign_ok',
                'heading_yaw_mean','heading_yaw_std'],
    "Trajectory": ['traj_final_lateral_dev','traj_rms_lateral','traj_max_lateral','traj_drift_direction'],
    "Speed": ['speed_mean','speed_std','speed_err_mean','speed_err_std'],
    "Smoothness": ['smooth_jitter_score','smooth_forward_pct','smooth_stop_pct',
                   'smooth_reverse_pct','smooth_pc_slew_max','smooth_ol_slew_max'],
    "Scores": ['score_speed','score_heading','score_trajectory','score_smooth','score_total'],
}

for section, keys in sections.items():
    print(f"=== {section} ===")
    for k in keys:
        v = r.get(k, "N/A")
        print(f"  {k}: {v}")
    print()
