import argparse
import csv
import json
import math
import os
import re
import statistics
from typing import Any, Dict, List, Optional, Tuple

TIME_SERIES_SUFFIX = "_data.csv"
ANALYSIS_SUFFIX = "_analysis.json"
TRAJECTORY_SUFFIX = "_trajectory.csv"

SENSOR_SCORE_WEIGHTS = {
    "s1": -3.5,
    "s2": -2.5,
    "s3": -1.5,
    "s4": -0.5,
    "s5": 0.5,
    "s6": 1.5,
    "s7": 2.5,
    "s8": 3.5,
}

TRACK_DATA_FIELDS = [
    "tick",
    "exp_id",
    "t_ms",
    "run",
    "ts",
    "st",
    "lt",
    "rt",
    "kp",
    "ki",
    "kd",
    "lp",
    "lpr",
    "y",
    "yr",
    "el",
    "er",
    "ed",
    "dl",
    "dr",
    "cl",
    "cr",
    "la",
    "ra",
    "lse",
    "rse",
    "lso",
    "rso",
    "lc",
    "rc",
    "cd",
    "cde",
    "cdi",
    "cdo",
    "L",
    "R",
    "pwm_max",
    "miss",
    "lost",
    "sensor_count",
    "sensor_score",
    "sensor_state",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "s8",
    "raw_line",
]

TRAJECTORY_FIELDS = [
    "tick",
    "t_s",
    "run",
    "el",
    "er",
    "ed",
    "lc",
    "rc",
    "cd",
    "yaw_deg",
    "sensor_score",
    "sensor_state",
    "forward_ds",
    "path_s",
    "traj_x",
    "traj_y",
    "sensor_y_accum",
]


def _active_sensor_names(row: Dict[str, Any]) -> List[str]:
    return [f"s{i}" for i in range(1, 9) if int(row.get(f"s{i}", 0)) == 1]


def _parse_value(text: str) -> Optional[float]:
    try:
        if text.lower().startswith("0x"):
            return float(int(text, 16))
        return float(text)
    except Exception:
        return None


def build_artifact_prefix(raw_path: str) -> str:
    if raw_path.endswith("_raw.txt"):
        return raw_path[:-8]
    return os.path.splitext(raw_path)[0]


def find_latest_raw_txt(data_dir: str) -> str:
    if not os.path.isdir(data_dir):
        return ""
    candidates: List[str] = []
    for name in os.listdir(data_dir):
        if name.endswith("_raw.txt"):
            candidates.append(os.path.join(data_dir, name))
    if not candidates:
        return ""
    candidates.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return candidates[0]


def parse_track_kv_line(line: str) -> Dict[str, float]:
    s = line.strip()
    if not (s.startswith("TRK ") or s.startswith("STAT ")):
        return {}
    out: Dict[str, float] = {}
    for token in s.split()[1:]:
        if "=" not in token:
            continue
        k, v = token.split("=", 1)
        num = _parse_value(v)
        if num is not None:
            out[k] = num
    return out


def _sensor_score_from_row(row: Dict[str, Any]) -> float:
    active = 0
    total = 0.0
    for key, weight in SENSOR_SCORE_WEIGHTS.items():
        value = int(row.get(key, 0))
        if value:
            total += weight
            active += 1
    if active <= 0:
        return 0.0
    return total / float(active)


def _sensor_state_from_row(row: Dict[str, Any], score: float) -> str:
    s4 = int(row.get("s4", 0))
    s5 = int(row.get("s5", 0))
    count = int(row.get("sensor_count", 0))
    if count <= 0:
        return "line_missing"
    if s4 == 1 and s5 == 1 and count == 2:
        return "center_best"
    if s4 == 1 and s5 == 1:
        return "center_overlap"
    if s4 == 1 and s5 == 0:
        return "left_bias"
    if s5 == 1 and s4 == 0:
        return "right_bias"
    if score > 0.25:
        return "right_bias"
    if score < -0.25:
        return "left_bias"
    return "balanced"


def parse_track_rows(raw_path: str) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    if not os.path.exists(raw_path):
        return rows
    with open(raw_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            kv = parse_track_kv_line(line)
            if not kv:
                continue
            row: Dict[str, Any] = {
                "tick": int(kv.get("tick", 0.0)),
                "exp_id": int(kv.get("exp_id", 0.0)),
                "t_ms": float(kv.get("t_ms", 0.0)),
                "run": int(kv.get("run", 0.0)),
                "ts": float(kv.get("ts", 0.0)),
                "st": float(kv.get("st", kv.get("ts", 0.0))),
                "lt": float(kv.get("lt", 0.0)),
                "rt": float(kv.get("rt", 0.0)),
                "kp": float(kv.get("kp", kv.get("akp", 0.0))),
                "ki": float(kv.get("ki", kv.get("aki", 0.0))),
                "kd": float(kv.get("kd", kv.get("akd", 0.0))),
                "lp": float(kv.get("lp", 0.0)),
                "lpr": float(kv.get("lpr", 0.0)),
                "y": float(kv.get("y", 0.0)),
                "yr": float(kv.get("yr", 0.0)),
                "el": float(kv.get("el", 0.0)),
                "er": float(kv.get("er", 0.0)),
                "ed": float(kv.get("ed", kv.get("el", 0.0) - kv.get("er", 0.0))),
                "dl": int(kv.get("dl", 0.0)),
                "dr": int(kv.get("dr", 0.0)),
                "cl": int(kv.get("cl", 0.0)),
                "cr": int(kv.get("cr", 0.0)),
                "la": float(kv.get("la", 0.0)),
                "ra": float(kv.get("ra", 0.0)),
                "lse": float(kv.get("lse", 0.0)),
                "rse": float(kv.get("rse", 0.0)),
                "lso": float(kv.get("lso", 0.0)),
                "rso": float(kv.get("rso", 0.0)),
                "lc": int(kv.get("lc", 0.0)),
                "rc": int(kv.get("rc", 0.0)),
                "cd": int(kv.get("cd", kv.get("lc", 0.0) - kv.get("rc", 0.0))),
                "cde": float(kv.get("cde", 0.0)),
                "cdi": float(kv.get("cdi", 0.0)),
                "cdo": float(kv.get("cdo", 0.0)),
                "L": float(kv.get("L", 0.0)),
                "R": float(kv.get("R", 0.0)),
                "pwm_max": float(kv.get("pwm_max", 0.0)),
                "miss": float(kv.get("miss", 0.0)),
                "lost": float(kv.get("lost", 0.0)),
                "s1": int(kv.get("s1", 0.0)),
                "s2": int(kv.get("s2", 0.0)),
                "s3": int(kv.get("s3", 0.0)),
                "s4": int(kv.get("s4", 0.0)),
                "s5": int(kv.get("s5", 0.0)),
                "s6": int(kv.get("s6", 0.0)),
                "s7": int(kv.get("s7", 0.0)),
                "s8": int(kv.get("s8", 0.0)),
                "raw_line": line.strip(),
            }
            row["sensor_count"] = sum(int(row[f"s{i}"]) for i in range(1, 9))
            row["sensor_score"] = _sensor_score_from_row(row)
            row["sensor_state"] = _sensor_state_from_row(row, float(row["sensor_score"]))
            rows.append(row)
    return rows


def write_csv(path: str, fieldnames: List[str], rows: List[Dict[str, Any]]) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_json(path: str, payload: Dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)


def build_track_trajectory(rows: List[Dict[str, Any]], speed_scale: float = 1.0) -> List[Dict[str, Any]]:
    if not rows:
        return []
    traj: List[Dict[str, Any]] = []
    x = 0.0
    y = 0.0
    s = 0.0
    sensor_y_accum = 0.0
    prev_tick: Optional[int] = None
    for row in rows:
        tick = int(row.get("tick", 0))
        dt_s = 0.0 if prev_tick is None else max(0.0, float(tick - prev_tick) / 1000.0)
        prev_tick = tick
        el = float(row.get("el", 0.0))
        er = float(row.get("er", 0.0))
        yaw_deg = float(row.get("y", 0.0))
        sensor_score = float(row.get("sensor_score", 0.0))
        forward_ds = 0.5 * (el + er) * dt_s * float(speed_scale)
        yaw_rad = math.radians(yaw_deg)
        x += forward_ds * math.cos(yaw_rad)
        y += forward_ds * math.sin(yaw_rad)
        s += abs(forward_ds)
        sensor_y_accum += sensor_score * abs(forward_ds)
        traj.append(
            {
                "tick": tick,
                "t_s": float(row.get("t_ms", 0.0)) / 1000.0,
                "run": int(row.get("run", 0)),
                "el": el,
                "er": er,
                "ed": float(row.get("ed", 0.0)),
                "lc": int(row.get("lc", 0)),
                "rc": int(row.get("rc", 0)),
                "cd": int(row.get("cd", 0)),
                "yaw_deg": yaw_deg,
                "sensor_score": sensor_score,
                "sensor_state": row.get("sensor_state", "unknown"),
                "forward_ds": forward_ds,
                "path_s": s,
                "traj_x": x,
                "traj_y": y,
                "sensor_y_accum": sensor_y_accum,
            }
        )
    return traj


def _mean(xs: List[float]) -> float:
    return float(sum(xs) / len(xs)) if xs else 0.0


def _mean_abs(xs: List[float]) -> float:
    return float(sum(abs(x) for x in xs) / len(xs)) if xs else 0.0


def _ratio(numerator: int, denominator: int) -> float:
    if denominator <= 0:
        return 0.0
    return float(numerator) / float(denominator)


def _stddev(xs: List[float]) -> float:
    if len(xs) <= 1:
        return 0.0
    return float(statistics.pstdev(xs))


def _rms(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return math.sqrt(_mean([x * x for x in xs]))


def _sign_changes(xs: List[float], deadband: float = 0.0) -> int:
    changes = 0
    prev_sign = 0
    for x in xs:
        if x > deadband:
            sign = 1
        elif x < -deadband:
            sign = -1
        else:
            sign = 0
        if sign == 0:
            continue
        if prev_sign != 0 and sign != prev_sign:
            changes += 1
        prev_sign = sign
    return changes


def _mean_step_abs(xs: List[float]) -> float:
    if len(xs) <= 1:
        return 0.0
    return _mean_abs([xs[i] - xs[i - 1] for i in range(1, len(xs))])


def analyze_track_rows(rows: List[Dict[str, Any]], speed_scale: float = 1.0) -> Tuple[Dict[str, Any], List[Dict[str, Any]]]:
    run_rows = [row for row in rows if int(row.get("run", 0)) == 1]
    use_rows = run_rows if run_rows else rows
    trajectory = build_track_trajectory(use_rows, speed_scale=speed_scale)

    valid_rows = [row for row in use_rows if int(row.get("sensor_count", 0)) > 0]
    sensor_scores = [float(row.get("sensor_score", 0.0)) for row in valid_rows]
    els = [float(row.get("el", 0.0)) for row in use_rows]
    ers = [float(row.get("er", 0.0)) for row in use_rows]
    eds = [float(row.get("ed", 0.0)) for row in use_rows]
    dls = [float(row.get("dl", 0.0)) for row in use_rows]
    drs = [float(row.get("dr", 0.0)) for row in use_rows]
    pwm_l = [float(row.get("L", 0.0)) for row in use_rows]
    pwm_r = [float(row.get("R", 0.0)) for row in use_rows]
    pwm_d = [float(row.get("L", 0.0)) - float(row.get("R", 0.0)) for row in use_rows]
    yaws = [float(row.get("y", 0.0)) for row in use_rows]
    yaw_rates = [float(row.get("yr", 0.0)) for row in use_rows]
    target_speeds = [float(row.get("st", row.get("ts", 0.0))) for row in use_rows]
    left_targets = [float(row.get("lt", 0.0)) for row in use_rows]
    right_targets = [float(row.get("rt", 0.0)) for row in use_rows]
    left_actuals = [float(row.get("la", 0.0)) for row in use_rows]
    right_actuals = [float(row.get("ra", 0.0)) for row in use_rows]
    misses = [float(row.get("miss", 0.0)) for row in use_rows]
    losts = [float(row.get("lost", 0.0)) for row in use_rows]
    states = [str(row.get("sensor_state", "unknown")) for row in valid_rows]
    both_on = sum(1 for row in valid_rows if int(row.get("s4", 0)) == 1 and int(row.get("s5", 0)) == 1)
    s4_only = sum(1 for row in valid_rows if int(row.get("s4", 0)) == 1 and int(row.get("s5", 0)) == 0)
    s5_only = sum(1 for row in valid_rows if int(row.get("s5", 0)) == 1 and int(row.get("s4", 0)) == 0)
    line_missing = sum(1 for row in use_rows if str(row.get("sensor_state", "unknown")) == "line_missing")
    right_bias = sum(1 for state in states if state == "right_bias")
    left_bias = sum(1 for state in states if state == "left_bias")
    balanced = sum(1 for state in states if state in ("balanced", "center_best", "center_overlap"))
    pulse_zero_count = sum(1 for row in use_rows if abs(float(row.get("el", 0.0))) < 1e-9 and abs(float(row.get("er", 0.0))) < 1e-9)
    raw_pulse_zero_count = sum(1 for row in use_rows if abs(float(row.get("dl", 0.0))) < 1e-9 and abs(float(row.get("dr", 0.0))) < 1e-9)
    clamped_count = sum(1 for row in use_rows if int(row.get("cl", 0)) != 0 or int(row.get("cr", 0)) != 0)

    startup_valid_rows = valid_rows[:5]
    first_valid_row = startup_valid_rows[0] if startup_valid_rows else None
    startup_side = "unknown"
    first_missing_after_valid_t_s = -1.0
    startup_sequence: List[Dict[str, Any]] = []

    if startup_valid_rows:
        startup_scores = [float(row.get("sensor_score", 0.0)) for row in startup_valid_rows]
        startup_mean_score = _mean(startup_scores)
        if startup_mean_score > 0.25:
            startup_side = "right"
        elif startup_mean_score < -0.25:
            startup_side = "left"
        else:
            startup_side = "balanced"

        first_valid_tick = int(first_valid_row.get("tick", 0)) if first_valid_row else 0
        for row in use_rows:
            if int(row.get("tick", 0)) <= first_valid_tick:
                continue
            if str(row.get("sensor_state", "unknown")) == "line_missing":
                first_missing_after_valid_t_s = float(row.get("t_ms", 0.0)) / 1000.0
                break

        for row in startup_valid_rows:
            startup_sequence.append(
                {
                    "t_s": float(row.get("t_ms", 0.0)) / 1000.0,
                    "sensor_state": str(row.get("sensor_state", "unknown")),
                    "sensor_score": float(row.get("sensor_score", 0.0)),
                    "active_sensors": _active_sensor_names(row),
                }
            )

    if sensor_scores:
        sensor_side = "balanced"
        mean_score = _mean(sensor_scores)
        if mean_score > 0.15:
            sensor_side = "right"
        elif mean_score < -0.15:
            sensor_side = "left"
    else:
        mean_score = 0.0
        sensor_side = "balanced"

    final_point = trajectory[-1] if trajectory else {
        "traj_x": 0.0,
        "traj_y": 0.0,
        "path_s": 0.0,
        "sensor_y_accum": 0.0,
    }

    mode_hint = "track_mode"
    if len(valid_rows) == 0:
        mode_hint = "straight_speed_loop_only"
    elif _ratio(line_missing, len(use_rows)) > 0.85:
        mode_hint = "mostly_line_missing"

    yaw_delta = (yaws[-1] - yaws[0]) if len(yaws) >= 2 else 0.0
    yaw_abs_peak = max((abs(v) for v in yaws), default=0.0)
    t_span_s = max(float(use_rows[-1].get("t_ms", 0.0)) - float(use_rows[0].get("t_ms", 0.0)), 0.0) / 1000.0 if len(use_rows) >= 2 else 0.0
    yaw_drift_rate = (yaw_delta / t_span_s) if t_span_s > 1e-9 else 0.0

    analysis = {
        "sample_count": len(rows),
        "run_sample_count": len(run_rows),
        "mode_hint": mode_hint,
        "sensor_semantics": {
            "sensor_order": "s1 到 s8 按从左到右排列",
            "center_best": "s4 与 s5 同时亮",
            "s4_only": "左偏",
            "s5_only": "右偏",
        },
        "parameters": {
            "ts_last": float(use_rows[-1].get("ts", 0.0)) if use_rows else 0.0,
            "st_last": float(use_rows[-1].get("st", use_rows[-1].get("ts", 0.0))) if use_rows else 0.0,
            "kp_last": float(use_rows[-1].get("kp", 0.0)) if use_rows else 0.0,
            "ki_last": float(use_rows[-1].get("ki", 0.0)) if use_rows else 0.0,
            "kd_last": float(use_rows[-1].get("kd", 0.0)) if use_rows else 0.0,
        },
        "sensor_summary": {
            "valid_sensor_sample_count": len(valid_rows),
            "score_mean": mean_score,
            "score_abs_mean": _mean_abs(sensor_scores),
            "score_max": max(sensor_scores) if sensor_scores else 0.0,
            "score_min": min(sensor_scores) if sensor_scores else 0.0,
            "side_vote": sensor_side,
            "s4s5_both_on_ratio": _ratio(both_on, len(valid_rows)),
            "s4_only_ratio": _ratio(s4_only, len(valid_rows)),
            "s5_only_ratio": _ratio(s5_only, len(valid_rows)),
            "right_bias_ratio": _ratio(right_bias, len(valid_rows)),
            "left_bias_ratio": _ratio(left_bias, len(valid_rows)),
            "balanced_ratio": _ratio(balanced, len(valid_rows)),
            "line_missing_ratio": _ratio(line_missing, len(use_rows)),
        },
        "startup_summary": {
            "startup_side": startup_side,
            "first_valid_t_s": (float(first_valid_row.get("t_ms", 0.0)) / 1000.0) if first_valid_row else -1.0,
            "first_valid_state": str(first_valid_row.get("sensor_state", "unknown")) if first_valid_row else "unknown",
            "first_valid_score": float(first_valid_row.get("sensor_score", 0.0)) if first_valid_row else 0.0,
            "first_valid_active_sensors": _active_sensor_names(first_valid_row) if first_valid_row else [],
            "first_missing_after_valid_t_s": first_missing_after_valid_t_s,
            "startup_sequence": startup_sequence,
        },
        "encoder_summary": {
            "mean_abs_el": _mean_abs(els),
            "mean_abs_er": _mean_abs(ers),
            "mean_ed": _mean(eds),
            "ed_rms": _rms(eds),
            "encoder_ratio_er_el": (_mean_abs(ers) / _mean_abs(els)) if _mean_abs(els) > 1e-9 else 0.0,
            "pulse_zero_ratio": _ratio(pulse_zero_count, len(use_rows)),
            "raw_pulse_zero_ratio": _ratio(raw_pulse_zero_count, len(use_rows)),
            "raw_dl_mean_abs": _mean_abs(dls),
            "raw_dr_mean_abs": _mean_abs(drs),
            "clamped_ratio": _ratio(clamped_count, len(use_rows)),
            "ed_sign_change_count": _sign_changes(eds, deadband=0.05),
        },
        "yaw_summary": {
            "yaw_start": yaws[0] if yaws else 0.0,
            "yaw_end": yaws[-1] if yaws else 0.0,
            "yaw_delta": yaw_delta,
            "yaw_abs_peak": yaw_abs_peak,
            "yaw_drift_rate_deg_s": yaw_drift_rate,
            "yaw_rate_mean": _mean(yaw_rates),
            "yaw_rate_abs_mean": _mean_abs(yaw_rates),
            "yaw_sign_change_count": _sign_changes(yaws, deadband=1.0),
        },
        "pwm_summary": {
            "left_pwm_mean": _mean(pwm_l),
            "right_pwm_mean": _mean(pwm_r),
            "left_pwm_std": _stddev(pwm_l),
            "right_pwm_std": _stddev(pwm_r),
            "pwm_diff_mean": _mean(pwm_d),
            "pwm_diff_abs_mean": _mean_abs(pwm_d),
            "pwm_diff_sign_change_count": _sign_changes(pwm_d, deadband=1.0),
            "left_pwm_step_abs_mean": _mean_step_abs(pwm_l),
            "right_pwm_step_abs_mean": _mean_step_abs(pwm_r),
        },
        "speed_loop_summary": {
            "target_speed_mean": _mean(target_speeds),
            "left_target_mean": _mean(left_targets),
            "right_target_mean": _mean(right_targets),
            "left_actual_mean": _mean(left_actuals),
            "right_actual_mean": _mean(right_actuals),
            "left_actual_abs_mean": _mean_abs(left_actuals),
            "right_actual_abs_mean": _mean_abs(right_actuals),
            "left_right_actual_diff_mean": _mean([la - ra for la, ra in zip(left_actuals, right_actuals)]),
            "left_right_actual_diff_abs_mean": _mean_abs([la - ra for la, ra in zip(left_actuals, right_actuals)]),
        },
        "line_guard_summary": {
            "miss_mean": _mean(misses),
            "miss_max": max(misses) if misses else 0.0,
            "lost_mean": _mean(losts),
            "lost_max": max(losts) if losts else 0.0,
        },
        "trajectory_summary": {
            "final_x": float(final_point.get("traj_x", 0.0)),
            "final_y": float(final_point.get("traj_y", 0.0)),
            "path_len": float(final_point.get("path_s", 0.0)),
            "sensor_y_accum": float(final_point.get("sensor_y_accum", 0.0)),
        },
    }

    return analysis, trajectory


def analyze_track_raw(raw_path: str, speed_scale: float = 1.0, write_outputs: bool = True) -> Tuple[str, str, str, Dict[str, Any]]:
    rows = parse_track_rows(raw_path)
    if not rows:
        raise RuntimeError(f"No TRK/STAT rows parsed from: {raw_path}")
    analysis, trajectory = analyze_track_rows(rows, speed_scale=speed_scale)
    prefix = build_artifact_prefix(raw_path)
    data_path = prefix + TIME_SERIES_SUFFIX
    analysis_path = prefix + ANALYSIS_SUFFIX
    trajectory_path = prefix + TRAJECTORY_SUFFIX
    if write_outputs:
        write_csv(data_path, TRACK_DATA_FIELDS, rows)
        write_json(analysis_path, analysis)
        write_csv(trajectory_path, TRAJECTORY_FIELDS, trajectory)
    return data_path, analysis_path, trajectory_path, analysis


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default="", help="循迹 raw txt 文件路径；为空则自动取最新")
    ap.add_argument(
        "--data-dir",
        default=os.path.join(os.path.dirname(__file__), "000Data", "循迹数据"),
        help="自动查找最新 raw 的目录",
    )
    ap.add_argument("--speed-scale", type=float, default=1.0, help="编码器到相对位移的缩放因子")
    args = ap.parse_args()

    raw_path = args.raw.strip() or find_latest_raw_txt(args.data_dir)
    if not raw_path:
        raise SystemExit("No track raw txt found")

    data_path, analysis_path, trajectory_path, analysis = analyze_track_raw(
        raw_path,
        speed_scale=float(args.speed_scale),
        write_outputs=True,
    )

    print(f"RAW={raw_path}")
    print(f"DATA={data_path}")
    print(f"ANALYSIS={analysis_path}")
    print(f"TRAJECTORY={trajectory_path}")
    sensor_summary = analysis.get("sensor_summary", {})
    encoder_summary = analysis.get("encoder_summary", {})
    traj_summary = analysis.get("trajectory_summary", {})
    print(
        "SENSOR side={} s4s5_both={:.3f} s4_only={:.3f} s5_only={:.3f} miss={:.3f}".format(
            sensor_summary.get("side_vote", "balanced"),
            float(sensor_summary.get("s4s5_both_on_ratio", 0.0)),
            float(sensor_summary.get("s4_only_ratio", 0.0)),
            float(sensor_summary.get("s5_only_ratio", 0.0)),
            float(sensor_summary.get("line_missing_ratio", 0.0)),
        )
    )
    startup_summary = analysis.get("startup_summary", {})
    print(
        "STARTUP side={} first_state={} first_sensors={} first_missing_after_valid={:.3f}s".format(
            startup_summary.get("startup_side", "unknown"),
            startup_summary.get("first_valid_state", "unknown"),
            "/".join(startup_summary.get("first_valid_active_sensors", [])),
            float(startup_summary.get("first_missing_after_valid_t_s", -1.0)),
        )
    )
    print(
        "ENC mean|el|={:.3f} mean|er|={:.3f} mean_ed={:.3f}".format(
            float(encoder_summary.get("mean_abs_el", 0.0)),
            float(encoder_summary.get("mean_abs_er", 0.0)),
            float(encoder_summary.get("mean_ed", 0.0)),
        )
    )
    print(
        "TRAJ final_x={:.3f} final_y={:.3f} path_len={:.3f} sensor_y_accum={:.3f}".format(
            float(traj_summary.get("final_x", 0.0)),
            float(traj_summary.get("final_y", 0.0)),
            float(traj_summary.get("path_len", 0.0)),
            float(traj_summary.get("sensor_y_accum", 0.0)),
        )
    )


if __name__ == "__main__":
    main()
