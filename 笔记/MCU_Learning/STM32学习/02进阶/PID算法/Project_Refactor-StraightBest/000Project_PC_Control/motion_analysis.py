from __future__ import annotations

import math
from typing import Any

import numpy as np
import pandas as pd


def _cfg_get(cfg: dict[str, Any], *keys: str, default: Any = None) -> Any:
    node: Any = cfg
    for key in keys:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def _safe_float(value: Any, default: float = 0.0) -> float:
    try:
        if value is None:
            return default
        if isinstance(value, (float, int)):
            if math.isnan(float(value)):
                return default
            return float(value)
        return float(value)
    except Exception:
        return default


def _series_mean(series: pd.Series) -> float:
    if series.empty:
        return 0.0
    return _safe_float(series.mean())


def _series_max_abs(series: pd.Series) -> float:
    if series.empty:
        return 0.0
    return _safe_float(series.abs().max())


def _series_std(series: pd.Series) -> float:
    if series.empty:
        return 0.0
    return _safe_float(series.std(ddof=0))


def _build_running_mask(df: pd.DataFrame) -> pd.Series:
    if "is_running" in df.columns:
        return df["is_running"].astype(bool)
    if "flags" in df.columns:
        return (df["flags"].astype(int) & 0x01) != 0
    return pd.Series([False] * len(df), index=df.index, dtype=bool)


def _build_mode_mask(df: pd.DataFrame, mode: int) -> pd.Series:
    if "mode" not in df.columns:
        return pd.Series([False] * len(df), index=df.index, dtype=bool)
    return df["mode"].astype(int) == int(mode)


def _first_motion_delay_s(df: pd.DataFrame, running_mask: pd.Series, speed_threshold_mps: float) -> float:
    run_df = df.loc[running_mask]
    if run_df.empty:
        return 0.0
    first_run_t = _safe_float(run_df["timestamp_s"].iloc[0])
    moving_df = run_df.loc[run_df["linear_velocity_mps"].abs() >= speed_threshold_mps]
    if moving_df.empty:
        return _safe_float(run_df["timestamp_s"].iloc[-1]) - first_run_t
    return _safe_float(moving_df["timestamp_s"].iloc[0]) - first_run_t


def _wrap_angle_deg(value: float) -> float:
    wrapped = (_safe_float(value) + 180.0) % 360.0 - 180.0
    return _safe_float(wrapped)


def _fit_lateral_drift_xy(x: np.ndarray, y: np.ndarray) -> tuple[float, float]:
    if len(x) < 2 or len(y) < 2:
        return 0.0, 0.0
    if np.allclose(x, x[0]):
        return 0.0, _safe_float(y[-1] if len(y) > 0 else 0.0)
    slope, intercept = np.polyfit(x, y, 1)
    return _safe_float(slope), _safe_float(intercept)


def _fit_lateral_drift(df: pd.DataFrame) -> tuple[float, float]:
    if len(df) < 2:
        return 0.0, 0.0
    x = df["opt_x_m"].to_numpy(dtype=float)
    y = df["opt_y_m"].to_numpy(dtype=float)
    return _fit_lateral_drift_xy(x, y)


def _build_launch_frame(df: pd.DataFrame, speed_threshold_mps: float, startup_window_s: float) -> dict[str, Any]:
    heading_col: str | None
    moving_df: pd.DataFrame
    ref_df: pd.DataFrame
    anchor: pd.Series
    launch_x: float
    launch_y: float
    launch_heading_deg: float
    launch_t: float
    theta: float
    dx: np.ndarray
    dy: np.ndarray
    aligned_x: np.ndarray
    aligned_y: np.ndarray
    launch_direction_deg: float
    startup_mask: pd.Series
    startup_y: np.ndarray
    startup_lateral_peak_m: float
    final_lateral_offset_m: float
    lateral_recovery_ratio: float

    if df.empty or "opt_x_m" not in df.columns or "opt_y_m" not in df.columns or "timestamp_s" not in df.columns:
        return {}

    if "opt_theta_deg" in df.columns:
        heading_col = "opt_theta_deg"
    elif "yaw_deg" in df.columns:
        heading_col = "yaw_deg"
    else:
        return {}

    moving_df = df.loc[df["linear_velocity_mps"].abs() >= speed_threshold_mps] if "linear_velocity_mps" in df.columns else df
    ref_df = moving_df if not moving_df.empty else df
    anchor = ref_df.iloc[0]
    launch_x = _safe_float(anchor["opt_x_m"])
    launch_y = _safe_float(anchor["opt_y_m"])
    launch_heading_deg = _safe_float(anchor[heading_col])
    launch_t = _safe_float(anchor["timestamp_s"])

    theta = math.radians(launch_heading_deg)
    dx = df["opt_x_m"].to_numpy(dtype=float) - launch_x
    dy = df["opt_y_m"].to_numpy(dtype=float) - launch_y
    aligned_x = dx * math.cos(theta) + dy * math.sin(theta)
    aligned_y = -dx * math.sin(theta) + dy * math.cos(theta)

    launch_direction_deg = launch_heading_deg
    for _, row in ref_df.iloc[1:].iterrows():
        rel_x = _safe_float(row["opt_x_m"]) - launch_x
        rel_y = _safe_float(row["opt_y_m"]) - launch_y
        rel_dist = math.hypot(rel_x, rel_y)
        rel_t = _safe_float(row["timestamp_s"]) - launch_t
        if rel_dist >= 0.05 or rel_t >= max(0.3, min(startup_window_s, 1.0)):
            launch_direction_deg = math.degrees(math.atan2(rel_y, rel_x))
            break

    startup_mask = (df["timestamp_s"] >= launch_t) & (df["timestamp_s"] <= (launch_t + max(startup_window_s, 0.0)))
    startup_y = aligned_y[startup_mask.to_numpy(dtype=bool)]
    startup_lateral_peak_m = 0.0
    if len(startup_y) > 0:
        startup_lateral_peak_m = _safe_float(startup_y[int(np.argmax(np.abs(startup_y)))])

    final_lateral_offset_m = _safe_float(aligned_y[-1] if len(aligned_y) > 0 else 0.0)
    lateral_recovery_ratio = 0.0
    if abs(startup_lateral_peak_m) > 1e-6:
        lateral_recovery_ratio = _safe_float(abs(final_lateral_offset_m) / abs(startup_lateral_peak_m))

    return {
        "launch_heading_deg": launch_heading_deg,
        "launch_direction_deg": _safe_float(launch_direction_deg),
        "launch_direction_error_deg": _wrap_angle_deg(launch_direction_deg - launch_heading_deg),
        "aligned_x": aligned_x,
        "aligned_y": aligned_y,
        "startup_lateral_peak_m": startup_lateral_peak_m,
        "final_lateral_offset_m": final_lateral_offset_m,
        "lateral_recovery_ratio": lateral_recovery_ratio,
    }


def _build_reference_frame(
    df: pd.DataFrame,
    speed_threshold_mps: float,
    startup_window_s: float,
    reference_heading_deg: float,
) -> dict[str, Any]:
    moving_df: pd.DataFrame
    ref_df: pd.DataFrame
    anchor: pd.Series
    anchor_x: float
    anchor_y: float
    anchor_t: float
    theta: float
    dx: np.ndarray
    dy: np.ndarray
    aligned_x: np.ndarray
    aligned_y: np.ndarray
    startup_mask: pd.Series
    startup_y: np.ndarray
    startup_lateral_peak_m: float
    final_lateral_offset_m: float
    lateral_recovery_ratio: float

    if df.empty or "opt_x_m" not in df.columns or "opt_y_m" not in df.columns or "timestamp_s" not in df.columns:
        return {}

    moving_df = df.loc[df["linear_velocity_mps"].abs() >= speed_threshold_mps] if "linear_velocity_mps" in df.columns else df
    ref_df = moving_df if not moving_df.empty else df
    anchor = ref_df.iloc[0]
    anchor_x = _safe_float(anchor["opt_x_m"])
    anchor_y = _safe_float(anchor["opt_y_m"])
    anchor_t = _safe_float(anchor["timestamp_s"])

    theta = math.radians(reference_heading_deg)
    dx = df["opt_x_m"].to_numpy(dtype=float) - anchor_x
    dy = df["opt_y_m"].to_numpy(dtype=float) - anchor_y
    aligned_x = dx * math.cos(theta) + dy * math.sin(theta)
    aligned_y = -dx * math.sin(theta) + dy * math.cos(theta)

    startup_mask = (df["timestamp_s"] >= anchor_t) & (df["timestamp_s"] <= (anchor_t + max(startup_window_s, 0.0)))
    startup_y = aligned_y[startup_mask.to_numpy(dtype=bool)]
    startup_lateral_peak_m = 0.0
    if len(startup_y) > 0:
        startup_lateral_peak_m = _safe_float(startup_y[int(np.argmax(np.abs(startup_y)))])

    final_lateral_offset_m = _safe_float(aligned_y[-1] if len(aligned_y) > 0 else 0.0)
    lateral_recovery_ratio = 0.0
    if abs(startup_lateral_peak_m) > 1e-6:
        lateral_recovery_ratio = _safe_float(abs(final_lateral_offset_m) / abs(startup_lateral_peak_m))

    return {
        "aligned_x": aligned_x,
        "aligned_y": aligned_y,
        "startup_lateral_peak_m": startup_lateral_peak_m,
        "final_lateral_offset_m": final_lateral_offset_m,
        "lateral_recovery_ratio": lateral_recovery_ratio,
    }


def _classify_pattern(
    summary: dict[str, Any],
    lateral_bias_threshold_m: float,
    heading_bias_threshold_deg: float,
    recovery_ratio_threshold: float,
) -> str:
    launch_delay = _safe_float(summary.get("launch_hesitation_s"))
    slip_ratio = _safe_float(summary.get("slip_ratio"))
    line_loss_ratio = 1.0 - _safe_float(summary.get("line_detect_ratio"))
    final_lateral = _safe_float(summary.get("final_lateral_offset_m"))
    drift_slope = _safe_float(summary.get("lateral_drift_slope"))
    track_ratio = _safe_float(summary.get("track_mode_ratio"))
    launch_direction_error_deg = _safe_float(summary.get("launch_direction_error_deg"))
    startup_lateral_peak_m = _safe_float(summary.get("startup_lateral_peak_m"))
    lateral_recovery_ratio = _safe_float(summary.get("lateral_recovery_ratio"), 1.0)
    launch_heading_offset_deg = _safe_float(summary.get("launch_heading_offset_deg"))
    final_heading_offset_deg = _safe_float(summary.get("final_heading_offset_deg"))
    startup_reference_lateral_peak_m = _safe_float(summary.get("startup_reference_lateral_peak_m"))
    reference_lateral_recovery_ratio = _safe_float(summary.get("reference_lateral_recovery_ratio"), 1.0)

    if abs(launch_heading_offset_deg) >= heading_bias_threshold_deg:
        if abs(final_heading_offset_deg) <= max(heading_bias_threshold_deg * 0.5, abs(launch_heading_offset_deg) * recovery_ratio_threshold):
            return "startup_heading_bias_with_recovery"
        return "startup_heading_bias_no_recovery"

    if abs(startup_reference_lateral_peak_m) >= lateral_bias_threshold_m:
        if abs(final_lateral) <= max(lateral_bias_threshold_m * 0.5, abs(startup_reference_lateral_peak_m) * recovery_ratio_threshold):
            return "startup_lateral_bias_with_recovery"
        return "startup_lateral_bias_no_recovery"

    if abs(startup_lateral_peak_m) >= lateral_bias_threshold_m or abs(launch_direction_error_deg) >= heading_bias_threshold_deg:
        startup_bias_side = startup_lateral_peak_m
        if abs(startup_bias_side) < lateral_bias_threshold_m:
            startup_bias_side = launch_direction_error_deg
        if abs(final_lateral) <= max(lateral_bias_threshold_m * 0.5, abs(startup_lateral_peak_m) * recovery_ratio_threshold):
            return "startup_left_bias_with_recovery" if startup_bias_side >= 0.0 else "startup_right_bias_with_recovery"
        return "startup_left_bias_no_recovery" if startup_bias_side >= 0.0 else "startup_right_bias_no_recovery"

    if slip_ratio >= 0.25:
        return "persistent_slip"
    if track_ratio > 0.2 and line_loss_ratio >= 0.4:
        return "line_loss_dominant"
    if launch_delay >= 0.35 and (abs(final_lateral) >= 0.05 or abs(drift_slope) >= 0.02):
        if final_lateral > 0.0:
            return "launch_hesitation_then_right_drift"
        if final_lateral < 0.0:
            return "launch_hesitation_then_left_drift"
        return "launch_hesitation_with_bias"
    if abs(final_lateral) >= 0.08 or abs(drift_slope) >= 0.03:
        return "steady_bias"
    return "nominal"


def build_motion_summary(df: pd.DataFrame, cfg: dict[str, Any]) -> dict[str, Any]:
    if df.empty:
        return {
            "motion_state": {
                "pattern": "no_data",
            }
        }

    running_mask = _build_running_mask(df)
    run_df = df.loc[running_mask]
    straight_mask = _build_mode_mask(df, 0)
    track_mask = _build_mode_mask(df, 1)

    active_df = run_df if not run_df.empty else df
    path_length = _safe_float(df.get("path_length_m", pd.Series(dtype=float)).iloc[-1]) if "path_length_m" in df.columns and len(df) > 0 else 0.0
    if path_length == 0.0 and len(df) > 1:
        xy = df[["opt_x_m", "opt_y_m"]].to_numpy(dtype=float)
        deltas = np.diff(xy, axis=0)
        path_length = _safe_float(np.sum(np.linalg.norm(deltas, axis=1)))
    direct_distance = 0.0
    if len(df) > 0:
        xy = df[["opt_x_m", "opt_y_m"]].to_numpy(dtype=float)
        direct_distance = _safe_float(np.linalg.norm(xy[-1] - xy[0]))

    ref_error_series = df["ref_error_m"] if "ref_error_m" in df.columns else pd.Series(dtype=float)
    ref_valid_series = df["ref_error_valid"] if "ref_error_valid" in df.columns else pd.Series(dtype=bool)
    ref_active_df = df.loc[ref_valid_series.astype(bool)] if not ref_valid_series.empty else pd.DataFrame()

    motion_threshold = _safe_float(_cfg_get(cfg, "analysis", "motion_threshold_mps", default=0.05), 0.05)
    yaw_jump_threshold = _safe_float(_cfg_get(cfg, "analysis", "yaw_jump_threshold_deg", default=20.0), 20.0)
    pwm_bias_threshold = _safe_float(_cfg_get(cfg, "analysis", "pwm_bias_threshold", default=5.0), 5.0)
    startup_window_s = _safe_float(_cfg_get(cfg, "analysis", "startup_window_s", default=1.0), 1.0)
    lateral_bias_threshold_m = _safe_float(_cfg_get(cfg, "analysis", "lateral_bias_threshold_m", default=0.03), 0.03)
    heading_bias_threshold_deg = _safe_float(_cfg_get(cfg, "analysis", "heading_bias_threshold_deg", default=1.0), 1.0)
    recovery_ratio_threshold = _safe_float(_cfg_get(cfg, "analysis", "recovery_ratio_threshold", default=0.6), 0.6)
    initial_pose = _cfg_get(cfg, "robot", "initial_pose", default=[0.0, 0.0, 0.0])
    default_reference_heading_deg = _safe_float(initial_pose[2] if isinstance(initial_pose, (list, tuple)) and len(initial_pose) >= 3 else 0.0)
    reference_heading_deg = _safe_float(_cfg_get(cfg, "analysis", "reference_heading_deg", default=default_reference_heading_deg), default_reference_heading_deg)

    yaw_delta = active_df["opt_theta_deg"].diff().fillna(0.0) if "opt_theta_deg" in active_df.columns else pd.Series(dtype=float)
    pwm_balance = active_df["pwm_right"] - active_df["pwm_left"] if not active_df.empty else pd.Series(dtype=float)
    velocity_balance = active_df["distance_right_m"] - active_df["distance_left_m"] if not active_df.empty else pd.Series(dtype=float)
    launch_frame = _build_launch_frame(active_df if not active_df.empty else df, motion_threshold, startup_window_s)
    reference_frame = _build_reference_frame(active_df if not active_df.empty else df, motion_threshold, startup_window_s, reference_heading_deg)

    if launch_frame:
        drift_slope, drift_intercept = _fit_lateral_drift_xy(launch_frame["aligned_x"], launch_frame["aligned_y"])
        launch_heading_deg = _safe_float(launch_frame["launch_heading_deg"])
        launch_direction_deg = _safe_float(launch_frame["launch_direction_deg"])
        launch_direction_error_deg = _safe_float(launch_frame["launch_direction_error_deg"])
        startup_lateral_peak_m = _safe_float(launch_frame["startup_lateral_peak_m"])
        lateral_recovery_ratio = _safe_float(launch_frame["lateral_recovery_ratio"])
    else:
        drift_slope, drift_intercept = _fit_lateral_drift(active_df if not active_df.empty else df)
        launch_heading_deg = 0.0
        launch_direction_deg = 0.0
        launch_direction_error_deg = 0.0
        startup_lateral_peak_m = 0.0
        lateral_recovery_ratio = 0.0

    if reference_frame:
        final_lateral_offset_m = _safe_float(reference_frame["final_lateral_offset_m"])
        startup_reference_lateral_peak_m = _safe_float(reference_frame["startup_lateral_peak_m"])
        reference_lateral_recovery_ratio = _safe_float(reference_frame["lateral_recovery_ratio"])
    else:
        final_lateral_offset_m = _safe_float(df["opt_y_m"].iloc[-1]) if "opt_y_m" in df.columns and len(df) > 0 else 0.0
        startup_reference_lateral_peak_m = 0.0
        reference_lateral_recovery_ratio = 0.0

    launch_heading_offset_deg = _wrap_angle_deg(launch_heading_deg - reference_heading_deg)
    final_heading_offset_deg = _wrap_angle_deg(_safe_float(df["opt_theta_deg"].iloc[-1]) - reference_heading_deg) if "opt_theta_deg" in df.columns and len(df) > 0 else 0.0

    summary: dict[str, Any] = {
        "trajectory_quality": {
            "path_length_m": path_length,
            "direct_distance_m": direct_distance,
            "sinuosity": _safe_float(path_length / direct_distance, 1.0) if direct_distance > 1e-6 else 0.0,
            "final_opt_x_m": _safe_float(df["opt_x_m"].iloc[-1]),
            "final_opt_y_m": _safe_float(df["opt_y_m"].iloc[-1]),
            "final_opt_theta_deg": _safe_float(df["opt_theta_deg"].iloc[-1]),
            "final_lateral_offset_m": final_lateral_offset_m,
            "lateral_drift_slope": drift_slope,
            "lateral_drift_intercept": drift_intercept,
            "reference_heading_deg": reference_heading_deg,
            "launch_heading_deg": launch_heading_deg,
            "launch_heading_offset_deg": launch_heading_offset_deg,
            "final_heading_offset_deg": final_heading_offset_deg,
            "launch_direction_deg": launch_direction_deg,
            "launch_direction_error_deg": launch_direction_error_deg,
        },
        "motion_state": {
            "mean_linear_velocity_mps": _series_mean(active_df["linear_velocity_mps"].abs()) if not active_df.empty else 0.0,
            "max_linear_velocity_mps": _series_max_abs(active_df["linear_velocity_mps"]) if not active_df.empty else 0.0,
            "mean_angular_velocity_radps": _series_mean(active_df["angular_velocity_radps"].abs()) if not active_df.empty else 0.0,
            "max_angular_velocity_radps": _series_max_abs(active_df["angular_velocity_radps"]) if not active_df.empty else 0.0,
            "launch_hesitation_s": _first_motion_delay_s(df, running_mask, motion_threshold),
            "stationary_ratio": _safe_float((active_df["linear_velocity_mps"].abs() < motion_threshold).mean()) if not active_df.empty else 0.0,
            "yaw_change_deg": _safe_float(active_df["opt_theta_deg"].iloc[-1] - active_df["opt_theta_deg"].iloc[0]) if len(active_df) >= 2 else 0.0,
            "yaw_variation_deg": _series_std(active_df["opt_theta_deg"]) if not active_df.empty else 0.0,
            "yaw_jump_ratio": _safe_float((yaw_delta.abs() >= yaw_jump_threshold).mean()) if not yaw_delta.empty else 0.0,
            "slip_ratio": _safe_float(active_df["slip_detected"].mean()) if not active_df.empty else 0.0,
            "imu_valid_ratio": _safe_float(df["imu_valid"].mean()) if "imu_valid" in df.columns else 0.0,
            "line_detect_ratio": _safe_float((df["line_count"] > 0).mean()) if "line_count" in df.columns else 0.0,
            "running_ratio": _safe_float(running_mask.mean()),
            "straight_mode_ratio": _safe_float(straight_mask.mean()),
            "track_mode_ratio": _safe_float(track_mask.mean()),
            "pwm_balance_mean": _series_mean(pwm_balance) if not pwm_balance.empty else 0.0,
            "pwm_balance_abs_mean": _series_mean(pwm_balance.abs()) if not pwm_balance.empty else 0.0,
            "encoder_balance_mean_m": _series_mean(velocity_balance) if not velocity_balance.empty else 0.0,
            "encoder_balance_abs_mean_m": _series_mean(velocity_balance.abs()) if not velocity_balance.empty else 0.0,
            "strong_pwm_bias_ratio": _safe_float((pwm_balance.abs() >= pwm_bias_threshold).mean()) if not pwm_balance.empty else 0.0,
            "startup_lateral_peak_m": startup_lateral_peak_m,
            "startup_lateral_peak_abs_m": abs(startup_lateral_peak_m),
            "lateral_recovery_ratio": lateral_recovery_ratio,
            "startup_reference_lateral_peak_m": startup_reference_lateral_peak_m,
            "startup_reference_lateral_peak_abs_m": abs(startup_reference_lateral_peak_m),
            "reference_lateral_recovery_ratio": reference_lateral_recovery_ratio,
            "launch_heading_offset_deg": launch_heading_offset_deg,
            "final_heading_offset_deg": final_heading_offset_deg,
            "launch_direction_error_deg": launch_direction_error_deg,
        },
        "reference_tracking": {
            "enabled": bool(not ref_active_df.empty),
            "mean_abs_error_m": _series_mean(ref_active_df["ref_error_m"].abs()) if not ref_active_df.empty else 0.0,
            "max_abs_error_m": _series_max_abs(ref_active_df["ref_error_m"]) if not ref_active_df.empty else 0.0,
            "final_error_m": _safe_float(ref_active_df["ref_error_m"].iloc[-1]) if not ref_active_df.empty else 0.0,
        },
    }

    summary["motion_state"]["pattern"] = _classify_pattern(
        {
            **summary["motion_state"],
            **summary["trajectory_quality"],
        },
        lateral_bias_threshold_m,
        heading_bias_threshold_deg,
        recovery_ratio_threshold,
    )
    return summary
