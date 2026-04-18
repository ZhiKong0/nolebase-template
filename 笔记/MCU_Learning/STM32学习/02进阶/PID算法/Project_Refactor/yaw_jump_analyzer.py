import argparse
import json
import math
import os
import re
from collections import Counter
from typing import Any, Dict, List, Optional, Sequence, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

try:
    from scipy.signal import butter, filtfilt
except Exception:
    butter = None
    filtfilt = None

CONFIG: Dict[str, Any] = {
    "run_only": True,
    "desired_min_sample_hz": 10.0,
    "strict_resample_hz": 10.0,
    "window_sec": 0.5,
    "micro_window_sec": 0.5,
    "max_interp_gap_points": 3,
    "yaw_jump_rate_threshold_deg_s": 15.0,
    "yaw_angle_jump_threshold_deg": 8.0,
    "yaw_lowpass_cutoff_hz": 5.0,
    "state_turn_yaw_delta_deg": 3.0,
    "state_stop_encoder_abs_mean": 1.0,
    "state_stop_pid_abs_mean": 5.0,
    "state_shaking_yaw_rate_std_deg_s": 8.0,
    "state_shaking_sign_change_ratio": 0.35,
    "pid_oscillation_std_threshold": 6.0,
    "pid_diff_step_threshold": 8.0,
    "encoder_mismatch_mean_threshold": 0.30,
    "encoder_mismatch_surge_zscore": 3.0,
    "yaw_zscore_threshold": 3.0,
    "micro_zscore_threshold": 3.0,
    "trajectory_encoder_scale": 1.0,
    "positive_yaw_is_right": True,
}

FIELD_ALIASES: Dict[str, Sequence[str]] = {
    "timestamp": ("timestamp", "time_s", "t_s", "host_time_s", "time", "ts"),
    "timestamp_ms": ("t_ms",),
    "yaw_angle": ("yaw_angle", "yaw", "yaw_deg"),
    "yaw_rate": ("yaw_rate", "yr", "yaw_rate_deg_s"),
    "accel_x": ("accel_x", "ax"),
    "accel_y": ("accel_y", "ay"),
    "pid_output_l": ("pid_output_l", "left_output", "outL", "OL", "L"),
    "pid_output_r": ("pid_output_r", "right_output", "outR", "OR", "R"),
    "encoder_l": ("encoder_l", "left_encoder", "el"),
    "encoder_r": ("encoder_r", "right_encoder", "er"),
    "encoder_diff": ("encoder_diff", "ed"),
    "run": ("run",),
    "raw_line": ("raw_line",),
    "angle_output": ("ao",),
    "angle_error": ("ae",),
    "speed_output": ("so",),
    "speed_error": ("se",),
    "pwm_command": ("pwm",),
    "pwm_core": ("pc",),
    "heading_diff": ("hd",),
}

RAW_LINE_KEYS = ("ae", "ao", "so", "se", "pwm", "pc", "hd", "ax", "ay", "az", "yr", "L", "R", "OL", "OR", "el", "er", "ed")
RAW_KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([-+]?(?:\d+\.\d+|\d+|\.\d+))")
STATE_COLORS = {
    "straight": "#4caf50",
    "turning_left": "#2196f3",
    "turning_right": "#ff9800",
    "shaking": "#e91e63",
    "stopped": "#9e9e9e",
    "unknown": "#607d8b",
}


def choose_column(columns: Sequence[str], aliases: Sequence[str]) -> Optional[str]:
    for name in aliases:
        if name in columns:
            return name
    return None


def parse_raw_line(line: Any) -> Dict[str, float]:
    if not isinstance(line, str) or not line:
        return {}
    out: Dict[str, float] = {}
    for key, value in RAW_KV_RE.findall(line):
        if key in RAW_LINE_KEYS:
            try:
                out[key] = float(value)
            except Exception:
                continue
    return out


def load_input_file(path: str) -> pd.DataFrame:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".csv":
        return pd.read_csv(path)
    if ext == ".json":
        with open(path, "r", encoding="utf-8") as f:
            payload = json.load(f)
        if isinstance(payload, list):
            return pd.DataFrame(payload)
        if isinstance(payload, dict):
            for key in ("rows", "data", "samples", "records", "telemetry"):
                value = payload.get(key)
                if isinstance(value, list):
                    return pd.DataFrame(value)
            try:
                return pd.DataFrame(payload)
            except Exception as exc:
                raise RuntimeError(f"JSON 文件中未找到可展开的时序记录: {path}") from exc
    raise RuntimeError(f"暂不支持的文件格式: {path}")


def recover_from_raw_line(df: pd.DataFrame) -> pd.DataFrame:
    raw_col = choose_column(df.columns, FIELD_ALIASES["raw_line"])
    if raw_col is None:
        return df
    parsed = df[raw_col].apply(parse_raw_line)
    recovered = pd.DataFrame(parsed.tolist()) if len(parsed) else pd.DataFrame(index=df.index)
    if recovered.empty:
        return df
    recovered.index = df.index
    for key in RAW_LINE_KEYS:
        if key in recovered.columns and key not in df.columns:
            df[key] = recovered[key]
    return df


def safe_numeric(series: pd.Series) -> pd.Series:
    return pd.to_numeric(series, errors="coerce")


def build_integrity_report(df: pd.DataFrame) -> Dict[str, Any]:
    cols = list(df.columns)
    report: Dict[str, Any] = {
        "row_count": int(len(df)),
        "columns": cols,
        "required_mapping": {},
        "missing_required_fields": [],
        "optional_missing_fields": [],
        "warnings": [],
    }
    required = ["yaw_angle", "pid_output_l", "pid_output_r", "encoder_l", "encoder_r"]
    for key, aliases in FIELD_ALIASES.items():
        mapped = choose_column(cols, aliases)
        report["required_mapping"][key] = mapped
    ts_col = report["required_mapping"].get("timestamp")
    ts_ms_col = report["required_mapping"].get("timestamp_ms")
    if ts_col is None and ts_ms_col is None:
        report["missing_required_fields"].append("timestamp/t_ms")
    for key in required:
        if report["required_mapping"].get(key) is None:
            report["missing_required_fields"].append(key)
    for key in ("yaw_rate", "accel_x", "accel_y", "angle_output", "angle_error", "speed_output", "speed_error", "pwm_command", "pwm_core", "heading_diff"):
        if report["required_mapping"].get(key) is None:
            report["optional_missing_fields"].append(key)
    return report


def normalize_timestamp(df: pd.DataFrame, integrity_report: Dict[str, Any]) -> pd.DataFrame:
    ts_col = integrity_report["required_mapping"].get("timestamp")
    ts_ms_col = integrity_report["required_mapping"].get("timestamp_ms")
    if ts_col is not None:
        ts = safe_numeric(df[ts_col])
    elif ts_ms_col is not None:
        ts = safe_numeric(df[ts_ms_col]) / 1000.0
    else:
        raise RuntimeError("缺少时间戳列，无法继续分析")
    if ts.notna().any():
        ts = ts - float(ts.dropna().iloc[0])
    df = df.copy()
    df["timestamp"] = ts
    return df


def estimate_effective_resample_hz(t: np.ndarray, cfg: Dict[str, Any]) -> Tuple[float, Dict[str, Any]]:
    if len(t) < 3:
        return float(cfg["strict_resample_hz"]), {"mode": "strict", "effective_hz": float(cfg["strict_resample_hz"])}
    dts = np.diff(t)
    dts = dts[dts > 1e-9]
    if len(dts) == 0:
        return float(cfg["strict_resample_hz"]), {"mode": "strict", "effective_hz": float(cfg["strict_resample_hz"])}
    q95_gap = float(np.quantile(dts, 0.95))
    strict_hz = float(cfg["strict_resample_hz"])
    max_gap_points = int(cfg["max_interp_gap_points"])
    feasible_hz = strict_hz
    degraded = False
    if q95_gap > (max_gap_points / strict_hz):
        feasible_hz = max(2.0, min(strict_hz, max_gap_points / max(q95_gap, 1e-9)))
        degraded = feasible_hz < strict_hz - 1e-9
    return feasible_hz, {
        "mode": "degraded" if degraded else "strict",
        "effective_hz": feasible_hz,
        "q95_gap_s": q95_gap,
        "max_interp_gap_points": max_gap_points,
    }


def unwrap_yaw_deg(values: Sequence[float]) -> np.ndarray:
    if not values:
        return np.array([], dtype=float)
    return np.rad2deg(np.unwrap(np.deg2rad(np.asarray(values, dtype=float))))


def lowpass_or_smooth(yaw: np.ndarray, sample_hz: float, cutoff_hz: float) -> np.ndarray:
    if len(yaw) < 5:
        return yaw.copy()
    cutoff = min(float(cutoff_hz), sample_hz * 0.4)
    if cutoff <= 0.0:
        return yaw.copy()
    if butter is not None and filtfilt is not None and sample_hz > 2.0 * cutoff + 1e-9:
        b, a = butter(2, cutoff / (0.5 * sample_hz), btype="low")
        try:
            return filtfilt(b, a, yaw)
        except Exception:
            pass
    win = max(3, int(round(sample_hz * 0.2)))
    if win % 2 == 0:
        win += 1
    return pd.Series(yaw).rolling(win, center=True, min_periods=1).mean().to_numpy(dtype=float)


def resample_numeric_frame(df: pd.DataFrame, sample_hz: float, cfg: Dict[str, Any]) -> pd.DataFrame:
    if df.empty:
        return df.copy()
    t0 = float(df["timestamp"].iloc[0])
    t1 = float(df["timestamp"].iloc[-1])
    if t1 <= t0:
        return df.copy()
    step = 1.0 / max(sample_hz, 1e-9)
    grid = np.arange(t0, t1 + step * 0.5, step)
    numeric_cols = [c for c in df.columns if pd.api.types.is_numeric_dtype(df[c]) and c != "timestamp"]
    base = df[["timestamp"] + numeric_cols].drop_duplicates(subset=["timestamp"], keep="last").set_index("timestamp")
    union_index = pd.Index(sorted(set(base.index.tolist()) | set(grid.tolist())), dtype=float)
    expanded = base.reindex(union_index).sort_index()
    interpolated = expanded.interpolate(method="index", limit=int(cfg["max_interp_gap_points"]), limit_area="inside")
    sampled = interpolated.reindex(grid)
    sampled.index.name = "timestamp"
    sampled = sampled.reset_index()
    return sampled


def sign_change_ratio(values: Sequence[float], eps: float = 1e-6) -> float:
    arr = np.asarray(values, dtype=float)
    if len(arr) < 3:
        return 0.0
    signs = np.sign(arr)
    signs[np.abs(arr) <= eps] = 0.0
    valid = signs[signs != 0.0]
    if len(valid) < 2:
        return 0.0
    return float(np.mean(valid[1:] != valid[:-1]))


def dominant_motion_state(states: Sequence[str]) -> str:
    if not states:
        return "unknown"
    return Counter(states).most_common(1)[0][0]


def classify_motion_state(win: pd.DataFrame, cfg: Dict[str, Any]) -> str:
    yaw_delta = float(win["yaw_filtered"].iloc[-1] - win["yaw_filtered"].iloc[0]) if len(win) >= 2 else 0.0
    yaw_rate_std = float(win["yaw_rate_used"].std(ddof=0)) if len(win) >= 2 else 0.0
    enc_abs_mean = float((win["encoder_l"].abs() + win["encoder_r"].abs()).mean() / 2.0)
    pid_abs_mean = float((win["pid_output_l"].abs() + win["pid_output_r"].abs()).mean() / 2.0)
    if enc_abs_mean <= float(cfg["state_stop_encoder_abs_mean"]) and pid_abs_mean <= float(cfg["state_stop_pid_abs_mean"]):
        return "stopped"
    if yaw_rate_std >= float(cfg["state_shaking_yaw_rate_std_deg_s"]) and sign_change_ratio(win["yaw_rate_used"].to_numpy()) >= float(cfg["state_shaking_sign_change_ratio"]):
        return "shaking"
    if yaw_delta <= -float(cfg["state_turn_yaw_delta_deg"]):
        return "turning_left" if bool(cfg["positive_yaw_is_right"]) else "turning_right"
    if yaw_delta >= float(cfg["state_turn_yaw_delta_deg"]):
        return "turning_right" if bool(cfg["positive_yaw_is_right"]) else "turning_left"
    return "straight"


def preprocess_data(df: pd.DataFrame, cfg: Dict[str, Any]) -> Tuple[pd.DataFrame, Dict[str, Any]]:
    df = recover_from_raw_line(df.copy())
    integrity = build_integrity_report(df)
    if integrity["missing_required_fields"]:
        return pd.DataFrame(), integrity
    df = normalize_timestamp(df, integrity)
    data = pd.DataFrame()
    data["timestamp"] = safe_numeric(df["timestamp"])
    data["yaw_angle"] = safe_numeric(df[choose_column(df.columns, FIELD_ALIASES["yaw_angle"])])
    yaw_rate_col = choose_column(df.columns, FIELD_ALIASES["yaw_rate"])
    data["yaw_rate"] = safe_numeric(df[yaw_rate_col]) if yaw_rate_col is not None else np.nan
    data["pid_output_l"] = safe_numeric(df[choose_column(df.columns, FIELD_ALIASES["pid_output_l"])])
    data["pid_output_r"] = safe_numeric(df[choose_column(df.columns, FIELD_ALIASES["pid_output_r"])])
    data["encoder_l"] = safe_numeric(df[choose_column(df.columns, FIELD_ALIASES["encoder_l"])])
    data["encoder_r"] = safe_numeric(df[choose_column(df.columns, FIELD_ALIASES["encoder_r"])])
    run_col = choose_column(df.columns, FIELD_ALIASES["run"])
    data["run"] = safe_numeric(df[run_col]) if run_col is not None else 1.0
    for extra_key in ("accel_x", "accel_y", "angle_output", "angle_error", "speed_output", "speed_error"):
        col = choose_column(df.columns, FIELD_ALIASES[extra_key])
        data[extra_key] = safe_numeric(df[col]) if col is not None else np.nan
    raw_line_col = choose_column(df.columns, FIELD_ALIASES["raw_line"])
    data["raw_line"] = df[raw_line_col].astype(str) if raw_line_col is not None else ""
    if bool(cfg["run_only"]) and "run" in data.columns:
        run_mask = data["run"].fillna(0.0) > 0.5
        if run_mask.any():
            data = data.loc[run_mask].copy()
        else:
            integrity["warnings"].append("未检测到 run=1 样本，已退回全量样本分析")
    data = data.sort_values("timestamp").drop_duplicates(subset=["timestamp"], keep="last")
    numeric_cols = [c for c in data.columns if c != "raw_line"]
    data[numeric_cols] = data[numeric_cols].apply(pd.to_numeric, errors="coerce")
    before_drop = len(data)
    data = data.dropna(subset=["timestamp", "yaw_angle", "pid_output_l", "pid_output_r", "encoder_l", "encoder_r"]).copy()
    integrity["dropped_rows_for_required_nan"] = int(before_drop - len(data))
    if len(data) < 3:
        integrity["warnings"].append("有效样本少于 3 个，无法进行稳定分析")
        return data, integrity
    data["timestamp"] = data["timestamp"] - float(data["timestamp"].iloc[0])
    dts = np.diff(data["timestamp"].to_numpy(dtype=float))
    dts = dts[dts > 1e-9]
    median_dt = float(np.median(dts)) if len(dts) else 0.0
    sample_hz = (1.0 / median_dt) if median_dt > 1e-9 else 0.0
    integrity["observed_sample_hz"] = sample_hz
    effective_hz, resample_meta = estimate_effective_resample_hz(data["timestamp"].to_numpy(dtype=float), cfg)
    integrity["resample"] = resample_meta
    if sample_hz < float(cfg["desired_min_sample_hz"]):
        integrity["warnings"].append("原始采样率低于 10Hz，已执行插值重采样或降级重采样")
    if resample_meta["mode"] == "degraded":
        integrity["warnings"].append(f"严格 10Hz 重采样会超过最大缺失点约束，已降级到 {effective_hz:.2f}Hz")
    data = resample_numeric_frame(data, effective_hz, cfg)
    for col in ("run",):
        if col in data.columns:
            data[col] = data[col].ffill().bfill().fillna(1.0)
    data["yaw_raw_unwrapped"] = unwrap_yaw_deg(data["yaw_angle"].ffill().bfill().to_list())
    data["yaw_filtered"] = lowpass_or_smooth(data["yaw_raw_unwrapped"].to_numpy(dtype=float), effective_hz, float(cfg["yaw_lowpass_cutoff_hz"]))
    dt_series = data["timestamp"].diff().replace(0.0, np.nan)
    data["yaw_rate_calc"] = data["yaw_filtered"].diff() / dt_series
    data["yaw_rate_raw_calc"] = data["yaw_raw_unwrapped"].diff() / dt_series
    data["yaw_rate_used"] = data["yaw_rate"]
    data.loc[data["yaw_rate_used"].isna(), "yaw_rate_used"] = data.loc[data["yaw_rate_used"].isna(), "yaw_rate_calc"]
    data["yaw_rate_used"] = data["yaw_rate_used"].fillna(0.0)
    data["pid_output_diff"] = data["pid_output_l"] - data["pid_output_r"]
    data["pid_output_diff_step"] = data["pid_output_diff"].diff().fillna(0.0)
    data["encoder_diff"] = data["encoder_l"] - data["encoder_r"]
    denom = data["encoder_l"].abs() + data["encoder_r"].abs()
    data["encoder_mismatch"] = (data["encoder_l"] - data["encoder_r"]).abs() / denom.replace(0.0, np.nan)
    data["encoder_mismatch"] = data["encoder_mismatch"].fillna(0.0)
    if data["accel_x"].notna().any() or data["accel_y"].notna().any():
        ax = data["accel_x"].fillna(0.0)
        ay = data["accel_y"].fillna(0.0)
        data["accel_mag"] = np.sqrt(ax * ax + ay * ay)
    else:
        data["accel_mag"] = np.nan
    data = data.dropna(subset=["timestamp", "yaw_filtered"]).reset_index(drop=True)
    integrity["analysis_sample_hz"] = effective_hz
    integrity["analysis_row_count"] = int(len(data))
    return data, integrity


def rolling_zscore(series: pd.Series, window_samples: int) -> pd.Series:
    if window_samples < 3:
        return pd.Series(np.zeros(len(series)), index=series.index)
    mean = series.rolling(window_samples, center=True, min_periods=max(3, window_samples // 2)).mean()
    std = series.rolling(window_samples, center=True, min_periods=max(3, window_samples // 2)).std(ddof=0)
    z = (series - mean) / std.replace(0.0, np.nan)
    return z.replace([np.inf, -np.inf], np.nan).fillna(0.0)


def detect_micro_changes(df: pd.DataFrame, cfg: Dict[str, Any], sample_hz: float) -> List[Dict[str, Any]]:
    signals = {
        "yaw_rate": df["yaw_rate_used"],
        "pid_output_diff": df["pid_output_diff"],
        "encoder_mismatch": df["encoder_mismatch"],
    }
    if df["accel_mag"].notna().any():
        signals["accel_mag"] = df["accel_mag"].fillna(0.0)
    window_samples = max(3, int(round(float(cfg["micro_window_sec"]) * max(sample_hz, 1.0))))
    threshold = float(cfg["micro_zscore_threshold"])
    events: List[Dict[str, Any]] = []
    for name, series in signals.items():
        z = rolling_zscore(series.astype(float), window_samples)
        hits = np.flatnonzero(np.abs(z.to_numpy(dtype=float)) >= threshold)
        for idx in hits.tolist():
            events.append(
                {
                    "timestamp": float(df.loc[idx, "timestamp"]),
                    "signal": name,
                    "value": float(series.iloc[idx]),
                    "zscore": float(z.iloc[idx]),
                }
            )
    events.sort(key=lambda item: (item["timestamp"], item["signal"]))
    return events


def extract_window_features(df: pd.DataFrame, cfg: Dict[str, Any]) -> List[Dict[str, Any]]:
    if df.empty:
        return []
    window_s = float(cfg["window_sec"])
    base_t = float(df["timestamp"].iloc[0])
    bucket = np.floor((df["timestamp"] - base_t) / window_s).astype(int)
    out: List[Dict[str, Any]] = []
    for _, win in df.groupby(bucket):
        win = win.sort_values("timestamp")
        if len(win) < 2:
            continue
        state = classify_motion_state(win, cfg)
        out.append(
            {
                "window_start": float(win["timestamp"].iloc[0]),
                "window_end": float(win["timestamp"].iloc[-1]),
                "yaw_delta": float(win["yaw_filtered"].iloc[-1] - win["yaw_filtered"].iloc[0]),
                "yaw_rate_max": float(win["yaw_rate_used"].abs().max()),
                "yaw_rate_std": float(win["yaw_rate_used"].std(ddof=0)),
                "pid_output_diff": float(win["pid_output_diff"].mean()),
                "encoder_mismatch": float(win["encoder_mismatch"].mean()),
                "motion_state": state,
            }
        )
    return out


def stitch_state_sequence(window_features: Sequence[Dict[str, Any]]) -> List[Dict[str, Any]]:
    if not window_features:
        return []
    seq: List[Dict[str, Any]] = []
    cur = dict(window_features[0])
    for item in window_features[1:]:
        if item["motion_state"] == cur["motion_state"]:
            cur["window_end"] = item["window_end"]
            cur["yaw_delta"] = float(cur["yaw_delta"]) + float(item["yaw_delta"])
            cur["yaw_rate_max"] = max(float(cur["yaw_rate_max"]), float(item["yaw_rate_max"]))
            cur["yaw_rate_std"] = max(float(cur["yaw_rate_std"]), float(item["yaw_rate_std"]))
            cur["pid_output_diff"] = float(item["pid_output_diff"])
            cur["encoder_mismatch"] = float(item["encoder_mismatch"])
        else:
            seq.append(cur)
            cur = dict(item)
    seq.append(cur)
    return seq


def contiguous_groups(indices: Sequence[int]) -> List[List[int]]:
    if not indices:
        return []
    groups: List[List[int]] = [[int(indices[0])]]
    for idx in indices[1:]:
        if int(idx) == groups[-1][-1] + 1:
            groups[-1].append(int(idx))
        else:
            groups.append([int(idx)])
    return groups


def normalize_scores(scores: Dict[str, float]) -> Dict[str, float]:
    total = float(sum(max(v, 0.0) for v in scores.values()))
    if total <= 1e-9:
        n = float(len(scores)) if scores else 1.0
        return {k: 1.0 / n for k in scores}
    return {k: float(max(v, 0.0) / total) for k, v in scores.items()}


def suggestion_for_cause(cause: str) -> str:
    mapping = {
        "pid_gain_or_integral_saturation": "建议先将 yaw/转向相关 P 增益下调约 10%，并复测 pid_output_diff 是否仍高频摆动；同时检查是否存在积分累积或饱和后释放。",
        "wheel_slip_or_mechanical_stiction": "建议检查左右轮胎抓地、编码器安装与传动阻力，重点复测低速起步与单侧负载变化时 encoder_mismatch 是否重复突增。",
        "external_disturbance_or_ground_irregularity": "建议在更平整地面重复实验，并同步观察加速度突变点与 yaw_jump 是否重合，排除碰撞或地面不平输入。",
        "sensor_noise_or_packet_loss": "建议优先审查 IMU/BNO 航向数据链路、包完整性与零点重置逻辑；对原始 yaw 做时间连续性检查，并比对 gyro/yaw_rate 是否同步跳变。",
    }
    return mapping.get(cause, "建议先复测并扩展日志字段，再对异常窗口做局部回放。")


def classify_single_yaw_jump(event_df: pd.DataFrame, full_df: pd.DataFrame, cfg: Dict[str, Any]) -> Dict[str, Any]:
    start_idx = int(event_df.index[0])
    end_idx = int(event_df.index[-1])
    pad0 = max(0, start_idx - 2)
    pad1 = min(len(full_df) - 1, end_idx + 2)
    ctx = full_df.iloc[pad0 : pad1 + 1].copy()
    duration = float(event_df["timestamp"].iloc[-1] - event_df["timestamp"].iloc[0])
    if len(event_df) == 1 and len(full_df) >= 2:
        dt_candidates = np.diff(full_df["timestamp"].to_numpy(dtype=float))
        dt_candidates = dt_candidates[dt_candidates > 1e-9]
        if len(dt_candidates):
            duration = float(np.median(dt_candidates))
    yaw_change = float(event_df["yaw_filtered"].iloc[-1] - event_df["yaw_filtered"].iloc[0]) if len(event_df) >= 2 else float(event_df["yaw_rate_raw_calc"].iloc[0] * max(duration, 0.0))
    raw_step = float(ctx["yaw_raw_unwrapped"].diff().abs().max()) if len(ctx) >= 2 else 0.0
    used_rate_peak = float(event_df["yaw_rate_used"].abs().max())
    raw_rate_peak = float(event_df["yaw_rate_raw_calc"].abs().max()) if event_df["yaw_rate_raw_calc"].notna().any() else 0.0
    pid_std = float(ctx["pid_output_diff"].std(ddof=0)) if len(ctx) >= 2 else 0.0
    pid_step_peak = float(ctx["pid_output_diff_step"].abs().max()) if len(ctx) >= 2 else 0.0
    enc_mismatch_mean = float(ctx["encoder_mismatch"].mean())
    enc_mismatch_peak = float(ctx["encoder_mismatch"].max())
    outputs_abs_mean = float((ctx["pid_output_l"].abs() + ctx["pid_output_r"].abs()).mean() / 2.0)
    encoder_abs_mean = float((ctx["encoder_l"].abs() + ctx["encoder_r"].abs()).mean() / 2.0)
    accel_peak = float(ctx["accel_mag"].max()) if ctx["accel_mag"].notna().any() else float("nan")
    scores = {
        "pid_gain_or_integral_saturation": 0.2,
        "wheel_slip_or_mechanical_stiction": 0.2,
        "external_disturbance_or_ground_irregularity": 0.2,
        "sensor_noise_or_packet_loss": 0.2,
    }
    evidence: List[str] = []
    if used_rate_peak >= float(cfg["yaw_jump_rate_threshold_deg_s"]):
        evidence.append(f"yaw_rate 峰值 {used_rate_peak:.2f}°/s 超过阈值")
    if pid_std >= float(cfg["pid_oscillation_std_threshold"]) or pid_step_peak >= float(cfg["pid_diff_step_threshold"]):
        scores["pid_gain_or_integral_saturation"] += 1.2
        evidence.append(f"pid_output_diff 波动明显，std={pid_std:.2f}，step_peak={pid_step_peak:.2f}")
    if enc_mismatch_mean >= float(cfg["encoder_mismatch_mean_threshold"]) or enc_mismatch_peak >= float(cfg["encoder_mismatch_mean_threshold"]) + 0.15:
        scores["wheel_slip_or_mechanical_stiction"] += 1.2
        evidence.append(f"encoder_mismatch 偏高，mean={enc_mismatch_mean:.2f}，peak={enc_mismatch_peak:.2f}")
    if raw_step >= float(cfg["yaw_angle_jump_threshold_deg"]) and used_rate_peak < float(cfg["yaw_jump_rate_threshold_deg_s"]):
        scores["sensor_noise_or_packet_loss"] += 1.4
        evidence.append(f"yaw_angle 单步跳变 {raw_step:.2f}°，但 yaw_rate 未同步异常")
    if outputs_abs_mean <= float(cfg["state_stop_pid_abs_mean"]) and encoder_abs_mean <= float(cfg["state_stop_encoder_abs_mean"]):
        scores["sensor_noise_or_packet_loss"] += 0.8
        evidence.append("输出与编码器都接近静止，异常更像传感器链路跳变")
    if (not math.isnan(accel_peak)) and accel_peak > 3.0:
        scores["external_disturbance_or_ground_irregularity"] += 0.9
        evidence.append(f"加速度幅值峰值 {accel_peak:.2f} 偏高")
    if used_rate_peak >= float(cfg["yaw_jump_rate_threshold_deg_s"]) and pid_std < float(cfg["pid_oscillation_std_threshold"]) and enc_mismatch_mean < float(cfg["encoder_mismatch_mean_threshold"]):
        scores["external_disturbance_or_ground_irregularity"] += 0.8
        evidence.append("yaw_rate 突变存在，但 PID/编码器未见同步异常")
    if raw_rate_peak >= float(cfg["yaw_jump_rate_threshold_deg_s"]) and used_rate_peak < float(cfg["yaw_jump_rate_threshold_deg_s"]) * 0.5:
        scores["sensor_noise_or_packet_loss"] += 0.8
        evidence.append("原始 yaw 差分突增，但滤波/测得 yaw_rate 较平稳")
    probs = normalize_scores(scores)
    most_likely = max(probs.items(), key=lambda item: item[1])[0]
    return {
        "timestamp": float(event_df["timestamp"].iloc[0]),
        "duration_sec": duration,
        "yaw_change_deg": yaw_change,
        "root_cause": {
            "most_likely": most_likely,
            "confidence": float(probs[most_likely]),
            "probability_distribution": probs,
            "evidence": evidence or ["未发现决定性特征，建议扩大日志字段后复测"],
        },
        "suggestion": suggestion_for_cause(most_likely),
    }


def detect_yaw_jump_events(df: pd.DataFrame, cfg: Dict[str, Any]) -> List[Dict[str, Any]]:
    if len(df) < 2:
        return []
    rate_mask = df["yaw_rate_used"].abs() >= float(cfg["yaw_jump_rate_threshold_deg_s"])
    raw_rate_mask = df["yaw_rate_raw_calc"].abs() >= float(cfg["yaw_jump_rate_threshold_deg_s"])
    raw_step_mask = df["yaw_raw_unwrapped"].diff().abs().fillna(0.0) >= float(cfg["yaw_angle_jump_threshold_deg"])
    hit_idx = np.flatnonzero((rate_mask | raw_rate_mask | raw_step_mask).to_numpy(dtype=bool)).tolist()
    events: List[Dict[str, Any]] = []
    for group in contiguous_groups(hit_idx):
        event_df = df.iloc[group].copy()
        events.append(classify_single_yaw_jump(event_df, df, cfg))
    return events


def build_trajectory(df: pd.DataFrame, cfg: Dict[str, Any]) -> List[Dict[str, float]]:
    if df.empty:
        return []
    xs = [0.0]
    ys = [0.0]
    scale = float(cfg["trajectory_encoder_scale"])
    sign = -1.0 if bool(cfg["positive_yaw_is_right"]) else 1.0
    timestamps = df["timestamp"].to_numpy(dtype=float)
    enc_l = df["encoder_l"].to_numpy(dtype=float)
    enc_r = df["encoder_r"].to_numpy(dtype=float)
    yaw = df["yaw_filtered"].to_numpy(dtype=float)
    for i in range(1, len(df)):
        dt = max(timestamps[i] - timestamps[i - 1], 0.0)
        ds = ((enc_l[i] + enc_r[i]) * 0.5) * dt * scale
        heading = math.radians(sign * yaw[i])
        xs.append(xs[-1] + ds * math.cos(heading))
        ys.append(ys[-1] + ds * math.sin(heading))
    return [
        {"timestamp": float(t), "x": float(x), "y": float(y)}
        for t, x, y in zip(timestamps, xs, ys)
    ]


def to_builtin(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k): to_builtin(v) for k, v in value.items()}
    if isinstance(value, list):
        return [to_builtin(v) for v in value]
    if isinstance(value, tuple):
        return [to_builtin(v) for v in value]
    if isinstance(value, (np.integer,)):
        return int(value)
    if isinstance(value, (np.floating,)):
        if math.isnan(float(value)) or math.isinf(float(value)):
            return None
        return float(value)
    if isinstance(value, pd.Timestamp):
        return value.isoformat()
    if isinstance(value, float):
        if math.isnan(value) or math.isinf(value):
            return None
        return value
    return value


def plot_results(df: pd.DataFrame, window_features: Sequence[Dict[str, Any]], yaw_events: Sequence[Dict[str, Any]], out_dir: str) -> Dict[str, str]:
    os.makedirs(out_dir, exist_ok=True)
    outputs: Dict[str, str] = {}

    fig, ax1 = plt.subplots(figsize=(12, 5))
    ax2 = ax1.twinx()
    ax1.plot(df["timestamp"], df["yaw_filtered"], color="#1f77b4", label="yaw_angle")
    ax2.plot(df["timestamp"], df["yaw_rate_used"], color="#d62728", alpha=0.7, label="yaw_rate")
    for event in yaw_events:
        ax1.axvline(float(event["timestamp"]), color="#ff5722", linestyle="--", alpha=0.7)
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Yaw Angle (deg)")
    ax2.set_ylabel("Yaw Rate (deg/s)")
    ax1.set_title("Yaw Angle + Yaw Rate")
    fig.tight_layout()
    path = os.path.join(out_dir, "yaw_angle_yaw_rate.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    outputs["yaw_angle_yaw_rate"] = path

    fig, ax = plt.subplots(figsize=(12, 2.8))
    for win in window_features:
        start = float(win["window_start"])
        end = float(win["window_end"])
        color = STATE_COLORS.get(str(win["motion_state"]), STATE_COLORS["unknown"])
        ax.axvspan(start, end, color=color, alpha=0.85)
    ax.set_ylim(0, 1)
    ax.set_yticks([])
    ax.set_xlabel("Time (s)")
    ax.set_title("Motion State Timeline")
    handles = [plt.Rectangle((0, 0), 1, 1, color=color) for color in STATE_COLORS.values()]
    labels = list(STATE_COLORS.keys())
    ax.legend(handles, labels, loc="upper center", ncol=6, bbox_to_anchor=(0.5, -0.18), frameon=False)
    fig.tight_layout()
    path = os.path.join(out_dir, "motion_state_timeline.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    outputs["motion_state_timeline"] = path

    scatter = pd.DataFrame(window_features)
    fig, ax = plt.subplots(figsize=(7, 5))
    if not scatter.empty:
        colors = [STATE_COLORS.get(str(s), STATE_COLORS["unknown"]) for s in scatter["motion_state"]]
        ax.scatter(scatter["pid_output_diff"], scatter["encoder_mismatch"], c=colors, alpha=0.85)
    ax.set_xlabel("Mean PID Output Diff (L-R)")
    ax.set_ylabel("Encoder Mismatch Ratio")
    ax.set_title("PID Diff vs Encoder Mismatch")
    fig.tight_layout()
    path = os.path.join(out_dir, "pid_vs_encoder_scatter.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    outputs["pid_vs_encoder_scatter"] = path

    return outputs


def analyze_file(path: str, out_dir: Optional[str] = None, make_plots: bool = True) -> Dict[str, Any]:
    raw_df = load_input_file(path)
    processed_df, integrity = preprocess_data(raw_df, CONFIG)
    result: Dict[str, Any] = {
        "integrity_report": integrity,
        "analysis_summary": {
            "total_duration_sec": 0.0,
            "yaw_jump_events_count": 0,
            "dominant_motion_state": "unknown",
        },
        "yaw_jump_events": [],
        "window_features": [],
        "state_sequence": [],
        "micro_anomalies": [],
        "trajectory_points": [],
        "code_snippet": "python yaw_jump_analyzer.py --input <data.csv> --out <output_dir>",
        "visualization_instructions": "运行脚本后会生成 yaw_angle_yaw_rate.png、motion_state_timeline.png、pid_vs_encoder_scatter.png。",
    }
    if integrity.get("missing_required_fields"):
        result["visualization_instructions"] += " 当前字段不完整，只输出数据完整性检查报告。"
        return result
    if processed_df.empty or len(processed_df) < 3:
        result["analysis_summary"]["total_duration_sec"] = float(processed_df["timestamp"].iloc[-1]) if not processed_df.empty else 0.0
        result["visualization_instructions"] += " 有效样本过少，无法生成可靠窗口分析。"
        return result
    sample_hz = float(integrity.get("analysis_sample_hz", integrity.get("observed_sample_hz", 0.0)) or 0.0)
    yaw_events = detect_yaw_jump_events(processed_df, CONFIG)
    micro_events = detect_micro_changes(processed_df, CONFIG, sample_hz)
    windows = extract_window_features(processed_df, CONFIG)
    state_seq = stitch_state_sequence(windows)
    trajectory = build_trajectory(processed_df, CONFIG)
    dominant_state = dominant_motion_state([str(w["motion_state"]) for w in windows])
    result["analysis_summary"] = {
        "total_duration_sec": float(processed_df["timestamp"].iloc[-1] - processed_df["timestamp"].iloc[0]),
        "yaw_jump_events_count": int(len(yaw_events)),
        "dominant_motion_state": dominant_state,
    }
    result["yaw_jump_events"] = yaw_events
    result["window_features"] = windows
    result["state_sequence"] = state_seq
    result["micro_anomalies"] = micro_events
    result["trajectory_points"] = trajectory
    if out_dir is None:
        out_dir = os.path.join(os.path.dirname(os.path.abspath(path)), "analysis_output")
    os.makedirs(out_dir, exist_ok=True)
    if make_plots:
        plot_paths = plot_results(processed_df, windows, yaw_events, out_dir)
        result["plot_files"] = plot_paths
    result["visualization_instructions"] += f" 输出目录: {out_dir}。如需关闭绘图可添加 --no-plots。"
    return to_builtin(result)


def save_result(result: Dict[str, Any], out_dir: str, input_path: str) -> str:
    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(input_path))[0]
    out_path = os.path.join(out_dir, stem + "_yaw_jump_analysis.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(to_builtin(result), f, ensure_ascii=False, indent=2)
    return out_path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="CSV 或 JSON 数据文件路径")
    ap.add_argument("--out", default="", help="分析结果输出目录")
    ap.add_argument("--no-plots", action="store_true", help="仅输出 JSON，不生成图片")
    args = ap.parse_args()

    input_path = os.path.abspath(args.input)
    out_dir = os.path.abspath(args.out) if args.out else os.path.join(os.path.dirname(input_path), "analysis_output")
    result = analyze_file(input_path, out_dir=out_dir, make_plots=(not args.no_plots))
    result_path = save_result(result, out_dir, input_path)
    print(f"ANALYSIS_JSON={result_path}")
    if "plot_files" in result:
        for name, path in result["plot_files"].items():
            print(f"PLOT_{name.upper()}={path}")
    print(json.dumps(result["analysis_summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
