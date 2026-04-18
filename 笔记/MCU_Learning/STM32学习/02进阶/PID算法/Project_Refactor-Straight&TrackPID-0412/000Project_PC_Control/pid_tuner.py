"""
pid_tuner.py  —  直线 PID 自动调参 & 深度分析工具
===================================================
用法:
    python pid_tuner.py                        # 使用 config.yaml 默认参数跑一次
    python pid_tuner.py --skp 0.8 --aki 0.003  # 覆盖部分 PID 参数
    python pid_tuner.py --duration 15           # 采集 15 秒
    python pid_tuner.py --port COM6             # 指定串口
    python pid_tuner.py --sweep skp 0.4 0.6 0.8 1.0  # 扫描 skp
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import statistics
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

import serial
import yaml

# Physical constant: 1320 CPR, 65mm wheel → π×65/1320 ≈ 0.155 mm per encoder count
ENC_TO_MM = 0.155


def _next_seq(output_dir: Path) -> int:
    """Read and increment a persistent test sequence number."""
    seq_file = output_dir / ".tune_seq"
    seq = 1
    if seq_file.exists():
        try:
            seq = int(seq_file.read_text().strip()) + 1
        except (ValueError, OSError):
            pass
    output_dir.mkdir(parents=True, exist_ok=True)
    seq_file.write_text(str(seq))
    return seq

# ---------------------------------------------------------------------------
# HB record — 直接解析固件文本行, 保留全部字段
# ---------------------------------------------------------------------------

@dataclass
class HBRecord:
    t_ms: int = 0
    mode: str = "S"
    run: int = 0
    el: int = 0        # encoder left delta (counts per period)
    er: int = 0        # encoder right delta
    yaw: float = 0.0   # degrees
    yr: float = 0.0    # yaw rate dps
    pc: int = 0         # pwmCore  (speed PID output)
    hd: int = 0         # headingDiffPWM (P+I heading output)
    dp: int = 0         # dTermPostDZ (D applied after deadzone)
    ol: int = 0         # final left PWM
    or_: int = 0        # final right PWM
    sb: int = 0         # sensor bits (track mode)
    lp: float = 0.0     # line position (track mode)


def parse_hb_line(line: str) -> Optional[HBRecord]:
    """Parse a single HB: line into HBRecord."""
    line = line.strip()
    if not line.startswith("HB:"):
        return None
    try:
        kv: dict[str, str] = {}
        for pair in line[3:].split(","):
            if "=" not in pair:
                continue
            k, v = pair.split("=", 1)
            kv[k.strip()] = v.strip()
        return HBRecord(
            t_ms=int(kv.get("t", "0")),
            mode=kv.get("m", "S").upper(),
            run=int(kv.get("run", "0")),
            el=int(kv.get("el", "0")),
            er=int(kv.get("er", "0")),
            yaw=float(kv.get("yaw", "0")),
            yr=float(kv.get("yr", "0")),
            pc=int(kv.get("pc", "0")),
            hd=int(kv.get("hd", "0")),
            dp=int(kv.get("dp", "0")),
            ol=int(kv.get("OL", "0")),
            or_=int(kv.get("OR", "0")),
            sb=int(kv.get("sb", "0")),
            lp=float(kv.get("lp", "0")),
        )
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------

def flush_serial(port: serial.Serial, wait_ms: int = 150) -> None:
    """Drain all pending data from serial input."""
    port.reset_input_buffer()
    time.sleep(wait_ms / 1000.0)
    while port.in_waiting > 0:
        port.read(port.in_waiting)
        time.sleep(0.02)


def send_cmd(port: serial.Serial, cmd: str, timeout_s: float = 1.5) -> list[str]:
    """Send command, filter out HB telemetry lines, return only response lines."""
    flush_serial(port, wait_ms=100)
    payload = cmd if cmd.endswith("!") else f"{cmd}!"
    port.write(payload.encode("utf-8"))
    port.flush()
    deadline = time.time() + timeout_s
    response_lines: list[str] = []
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        # Skip HB telemetry lines
        if line.startswith("HB:"):
            continue
        response_lines.append(line)
        if line.startswith("OK") or line == "ERR":
            break
    return response_lines


def ensure_ok(cmd: str, resp: list[str]) -> None:
    # Filter: only check lines that look like responses (not HB data)
    clean = [l for l in resp if not l.startswith("HB:")]
    if any(l == "ERR" or l.startswith("ERR") for l in clean):
        raise RuntimeError(f"CMD FAIL: {cmd} → {clean}")
    if not any("OK" in l for l in clean):
        raise RuntimeError(f"CMD NO-OK: {cmd} → {clean}")


# ---------------------------------------------------------------------------
# Capture
# ---------------------------------------------------------------------------

@dataclass
class CaptureResult:
    records: list[HBRecord] = field(default_factory=list)
    raw_lines: list[str] = field(default_factory=list)
    stat_response: list[str] = field(default_factory=list)
    duration_s: float = 0.0
    params: dict[str, Any] = field(default_factory=dict)


def run_capture(port_name: str, baudrate: int, duration_s: float,
                pid_params: dict[str, float], target_speed: float,
                print_live: bool = True) -> CaptureResult:
    """Connect, configure, start, capture HB lines, stop, return data."""
    result = CaptureResult(duration_s=duration_s, params={
        **pid_params, "target_speed": target_speed,
    })

    with serial.Serial(port_name, baudrate=baudrate, timeout=0.1) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        time.sleep(0.5)
        flush_serial(port, wait_ms=300)

        # 1) Set mode
        resp = send_cmd(port, "#MODE=STRAIGHT!")
        print(f"  MODE resp: {resp}")
        ensure_ok("#MODE=STRAIGHT!", resp)
        time.sleep(0.1)

        # 2) Set PID params
        param_cmds = {
            "skp": "#SKP=", "ski": "#SKI=", "skd": "#SKD=",
            "akp": "#AKP=", "aki": "#AKI=", "akd": "#AKD=",
            "sff": "#SFF=",
        }
        for key, prefix in param_cmds.items():
            if key in pid_params:
                val = pid_params[key]
                # MCU firmware cmd_parse_float handles 0.0 correctly
                cmd = f"{prefix}{val}!"
                ensure_ok(cmd, send_cmd(port, cmd))
                time.sleep(0.03)

        # 3) Set target speed
        cmd = f"#SPD={target_speed}!"
        ensure_ok(cmd, send_cmd(port, cmd))
        time.sleep(0.05)

        # 4) Verify with STAT
        result.stat_response = send_cmd(port, "#STAT!", timeout_s=1.0)
        print("  STAT:", result.stat_response)

        # 5) Start
        ensure_ok("#RUN!", send_cmd(port, "#RUN!"))
        start_t = time.time()
        hb_count = 0

        # 6) Capture loop
        while (time.time() - start_t) < duration_s:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            result.raw_lines.append(line)
            rec = parse_hb_line(line)
            if rec is not None:
                result.records.append(rec)
                hb_count += 1
                if print_live and hb_count % 25 == 0:
                    print(f"  [{rec.t_ms:>7}ms] yaw={rec.yaw:+7.1f} el={rec.el:+4d} er={rec.er:+4d} "
                          f"pc={rec.pc:+3d} hd={rec.hd:+3d} OL={rec.ol:+3d} OR={rec.or_:+3d}")

        # 7) Stop
        send_cmd(port, "#STOP!")
        time.sleep(0.2)

    return result


# ---------------------------------------------------------------------------
# Deep Analysis — v2 with jitter/reversal/phase diagnostics
# ---------------------------------------------------------------------------

def _linfit(xs: list[float], ys: list[float]) -> float:
    """Simple linear regression slope."""
    n = len(xs)
    if n < 2:
        return 0.0
    sx = sum(xs)
    sy = sum(ys)
    sxx = sum(x * x for x in xs)
    sxy = sum(x * y for x, y in zip(xs, ys))
    denom = n * sxx - sx * sx
    return (n * sxy - sx * sy) / denom if abs(denom) > 1e-12 else 0.0


def _count_sign_changes(vals: list) -> int:
    """Count how many times a sequence crosses zero."""
    c = 0
    for i in range(1, len(vals)):
        if vals[i] * vals[i - 1] < 0:
            c += 1
    return c


def _pct(part: int, total: int) -> float:
    return part / max(total, 1) * 100.0


def _moving_avg(vals: list[float], window: int = 5) -> list[float]:
    """Simple centred moving average for smoothing."""
    out = []
    for i in range(len(vals)):
        lo = max(0, i - window // 2)
        hi = min(len(vals), i + window // 2 + 1)
        out.append(sum(vals[lo:hi]) / (hi - lo))
    return out


def _detect_oscillation(values: list[float], mean_dt_ms: float = 85.0) -> dict:
    """Detect dominant oscillation via autocorrelation.
    Returns dict with osc_detected, period_ms, amplitude, acf_peak."""
    n = len(values)
    if n < 12:
        return {'osc_detected': False, 'osc_amplitude': 0.0}
    mean_v = statistics.mean(values)
    centered = [v - mean_v for v in values]
    var = sum(c * c for c in centered) / n
    if var < 0.01:
        return {'osc_detected': False, 'osc_amplitude': 0.0}
    max_lag = min(n // 3, 40)
    acf = []
    for lag in range(1, max_lag + 1):
        s = sum(centered[i] * centered[i + lag] for i in range(n - lag))
        acf.append(s / (n * var))
    peak_lag, peak_acf = None, 0.0
    for i in range(1, len(acf) - 1):
        if acf[i] > acf[i - 1] and acf[i] > acf[i + 1] and acf[i] > 0.15:
            peak_lag = i + 1
            peak_acf = acf[i]
            break
    if peak_lag is None:
        return {'osc_detected': False,
                'osc_amplitude': round(math.sqrt(var) * 2, 2)}
    return {
        'osc_detected': True,
        'osc_period_ms': round(peak_lag * mean_dt_ms),
        'osc_period_samples': peak_lag,
        'osc_amplitude': round(math.sqrt(var) * 2, 2),
        'osc_acf_peak': round(peak_acf, 3),
    }


@dataclass
class TrajectoryPoint:
    t_ms: int = 0
    x: float = 0.0
    y: float = 0.0
    yaw_deg: float = 0.0
    speed: float = 0.0
    lateral_dev: float = 0.0


def reconstruct_trajectory(records: list[HBRecord]) -> list[TrajectoryPoint]:
    """Estimate 2D path from encoder + IMU yaw.
    Uses trapezoidal integration (mid-heading) for higher accuracy.
    y = lateral deviation from initial heading direction."""
    if not records:
        return []
    pts: list[TrajectoryPoint] = []
    x, y = 0.0, 0.0
    t0 = records[0].t_ms
    prev_t = t0
    prev_yaw = records[0].yaw
    for r in records:
        dt_ms = max(r.t_ms - prev_t, 1)
        speed = (r.el + r.er) / 2.0
        distance = speed * (dt_ms / 10.0)
        # Trapezoidal: use average of previous and current heading
        mid_heading_rad = math.radians((prev_yaw + r.yaw) / 2.0)
        x += distance * math.cos(mid_heading_rad)
        y += distance * math.sin(mid_heading_rad)
        pts.append(TrajectoryPoint(
            t_ms=r.t_ms - t0, x=x, y=y,
            yaw_deg=r.yaw, speed=speed,
            lateral_dev=y,
        ))
        prev_t = r.t_ms
        prev_yaw = r.yaw
    return pts


def compute_speed_response(records: list[HBRecord], target_speed: float) -> dict:
    """Compute speed loop response: rise time, overshoot, settling, SS error.
    Uses pc (PID output) as primary speed metric since raw el/er can have
    telemetry-induced spikes from accumulated encoder counts."""
    if not records or target_speed <= 0:
        return {}
    # pc ≈ SFF * target when at speed; use pc as PID-quality metric
    pcs = [float(r.pc) for r in records]
    pc_smooth = _moving_avg(pcs, 5)

    # Also compute cleaned encoder speeds (cap outliers at 3x target)
    cap = target_speed * 3.0
    speeds_raw = [(r.el + r.er) / 2.0 for r in records]
    speeds = [min(s, cap) for s in speeds_raw]
    smoothed = _moving_avg(speeds, 7)

    t0 = records[0].t_ms
    ts = [r.t_ms - t0 for r in records]

    # Rise time, overshoot, settling ALL based on pc (immune to encoder artifacts)
    thr90, thr10 = target_speed * 0.9, target_speed * 0.1
    rise_start, rise_end, found10 = 0, 0, False
    for t, p in zip(ts, pc_smooth):
        if not found10 and p >= thr10:
            rise_start = t; found10 = True
        if found10 and p >= thr90:
            rise_end = t; break
    rise_time = rise_end - rise_start if found10 else 0

    # Overshoot: measure peak in 3s window after rise, using wider smoothing
    # Reference to pc_ss_mean (not target_speed) — SFF offset is intentional, not overshoot
    pc_smooth_wide = _moving_avg(pcs, 15)
    rise_idx = next((i for i, t in enumerate(ts) if t >= rise_end), len(pc_smooth_wide) // 4) if rise_end > 0 else len(pc_smooth_wide) // 4
    ov_end_idx = next((i for i, t in enumerate(ts) if t >= rise_end + 3000), len(pc_smooth_wide)) if rise_end > 0 else len(pc_smooth_wide)
    ov_window = pc_smooth_wide[rise_idx:ov_end_idx] if rise_idx < len(pc_smooth_wide) else pc_smooth_wide
    max_pc = max(ov_window) if ov_window else 0
    # Compute pc_ss_mean first for overshoot reference
    _half = len(pc_smooth) // 2
    _ss_pc = pc_smooth[_half:] if _half > 0 else pc_smooth
    _pc_ss_ref = statistics.mean(_ss_pc) if _ss_pc else target_speed
    overshoot = max(0, (max_pc - _pc_ss_ref) / max(_pc_ss_ref, 1) * 100)

    band = target_speed * 0.15
    settle_ms = 0
    for t, p in zip(reversed(ts), reversed(pc_smooth)):
        if abs(p - target_speed) > band:
            settle_ms = t; break

    half = len(smoothed) // 2
    ss_spd = smoothed[half:] if half > 0 else smoothed
    ss_mean = statistics.mean(ss_spd) if ss_spd else 0
    ss_err = target_speed - ss_mean
    ss_pct = (ss_err / target_speed * 100) if target_speed else 0

    errors_sq = [(target_speed - s) ** 2 for s in smoothed]
    rmse = math.sqrt(statistics.mean(errors_sq)) if errors_sq else 0

    # pc-based steady-state quality (expected pc ≈ target when SFF=1)
    ss_pc = pc_smooth[half:] if half > 0 else pc_smooth
    pc_ss_mean = statistics.mean(ss_pc) if ss_pc else 0
    pc_ss_std = statistics.stdev(ss_pc) if len(ss_pc) > 1 else 0

    max_speed = max(smoothed) if smoothed else 0
    return dict(rise_time_ms=rise_time, overshoot_pct=round(overshoot, 1),
                max_speed=round(max_speed, 2), settle_time_ms=settle_ms,
                ss_mean=round(ss_mean, 2), ss_error=round(ss_err, 2),
                ss_error_pct=round(ss_pct, 1), rmse=round(rmse, 2),
                pc_ss_mean=round(pc_ss_mean, 2), pc_ss_std=round(pc_ss_std, 2),
                smoothed=smoothed)


@dataclass
class WindowStats:
    t_start_ms: int = 0
    t_end_ms: int = 0
    n: int = 0
    el_mean: float = 0.0
    er_mean: float = 0.0
    pc_mean: float = 0.0
    pc_std: float = 0.0
    pc_min: int = 0
    pc_max: int = 0
    pc_sign_changes: int = 0
    el_negative_pct: float = 0.0
    ol_negative_pct: float = 0.0
    yaw_drift: float = 0.0
    yaw_mean: float = 0.0
    speed_mean: float = 0.0
    speed_err: float = 0.0
    hd_mean: float = 0.0
    dp_mean: float = 0.0
    ol_mean: float = 0.0
    or_mean: float = 0.0
    motion_state: str = ""


@dataclass
class StraightLineReport:
    # Basic
    total_samples: int = 0
    run_samples: int = 0
    duration_ms: int = 0
    sample_period_ms: float = 0.0

    # --- JITTER / DIRECTION REVERSAL (核心新增) ---
    jitter_severity: str = ""            # none / mild / moderate / severe
    jitter_score: float = 0.0            # 0=no jitter, 100=extreme
    pc_sign_changes: int = 0             # pc 过零次数
    pc_sign_change_freq_hz: float = 0.0
    pc_peak_to_peak: int = 0
    el_negative_count: int = 0           # el<0 的次数 (轮子反转)
    er_negative_count: int = 0
    el_negative_pct: float = 0.0         # 占运行样本的百分比
    er_negative_pct: float = 0.0
    ol_negative_count: int = 0           # PWM命令为负的次数
    or_negative_count: int = 0
    ol_negative_pct: float = 0.0
    or_negative_pct: float = 0.0
    pc_reversal_cycles: int = 0          # 完整正→负→正周期数
    reversal_period_ms: float = 0.0      # 平均反转周期
    max_pc_slew: int = 0                 # max |Δpc| between consecutive samples
    mean_pc_slew: float = 0.0
    max_ol_slew: int = 0

    # --- Speed PID ---
    speed_mean_el: float = 0.0
    speed_mean_er: float = 0.0
    speed_std_el: float = 0.0
    speed_std_er: float = 0.0
    speed_el_er_ratio: float = 0.0
    speed_asymmetry_pct: float = 0.0
    speed_mean_pc: float = 0.0
    speed_std_pc: float = 0.0
    speed_pc_range: tuple = (0, 0)
    speed_steady_pc: float = 0.0
    speed_oscillation_freq_hz: float = 0.0

    # --- Heading PID ---
    heading_yaw_final: float = 0.0
    heading_yaw_drift_rate_dps: float = 0.0
    heading_yaw_std: float = 0.0
    heading_yaw_max_abs: float = 0.0
    heading_mean_hd: float = 0.0
    heading_std_hd: float = 0.0
    heading_hd_range: tuple = (0, 0)
    heading_hd_saturation_pct: float = 0.0
    heading_correction_sign_ok: bool = True

    # --- PWM ---
    pwm_mean_ol: float = 0.0
    pwm_mean_or: float = 0.0
    pwm_std_ol: float = 0.0
    pwm_std_or: float = 0.0
    pwm_balance_mean: float = 0.0
    pwm_saturation_pct: float = 0.0

    # --- Straightness ---
    straightness_score: float = 0.0

    # --- Phase ---
    startup_duration_ms: int = 2000
    startup_yaw_peak: float = 0.0
    startup_pc_peak: int = 0
    steady_yaw_drift_rate: float = 0.0
    steady_pc_std: float = 0.0

    # --- 1s Window breakdown ---
    windows: list = field(default_factory=list)

    # --- Motion State ---
    state_forward_pct: float = 0.0
    state_reverse_pct: float = 0.0
    state_idle_pct: float = 0.0

    # --- Trajectory ---
    trajectory: list = field(default_factory=list)
    traj_total_distance: float = 0.0
    traj_max_lateral_dev: float = 0.0
    traj_mean_lateral_dev: float = 0.0
    traj_final_lateral_dev: float = 0.0
    traj_sinuosity: float = 1.0
    traj_max_lateral_vel: float = 0.0
    traj_rms_lateral_vel: float = 0.0
    traj_lateral_dev_pct: float = 0.0

    # --- Heading Precision ---
    heading_yaw_integral: float = 0.0
    heading_yaw_zero_crossings: int = 0
    heading_yaw_monotonic_pct: float = 0.0

    # --- Yaw Rate / D-term diagnostics ---
    yr_mean: float = 0.0
    yr_std: float = 0.0
    yr_max_abs: float = 0.0
    dp_mean: float = 0.0
    dp_std: float = 0.0
    dp_range: tuple = (0, 0)
    dp_nonzero_pct: float = 0.0

    # --- Full-rate speed analysis (mm/s) ---
    fr_speed_mean: float = 0.0
    fr_speed_std: float = 0.0
    fr_speed_min: float = 0.0
    fr_speed_max: float = 0.0
    fr_cruise_speed_mean: float = 0.0
    fr_cruise_speed_std: float = 0.0
    fr_cruise_speed_cv: float = 0.0
    fr_pc_osc_detected: bool = False
    fr_pc_osc_period_ms: float = 0.0
    fr_pc_osc_amplitude: float = 0.0
    fr_pc_osc_acf: float = 0.0
    fr_spd_osc_detected: bool = False
    fr_spd_osc_period_ms: float = 0.0
    fr_spd_osc_amplitude: float = 0.0

    # --- Full-rate lateral tracking (mm) ---
    fr_lat_max_mm: float = 0.0
    fr_lat_final_mm: float = 0.0
    fr_lat_drift_rate_mm_s: float = 0.0
    fr_lat_rms_mm: float = 0.0              # RMS lateral deviation (微偏移敏感)
    fr_lat_integral_mm_s: float = 0.0       # ∫|lat|dt — accumulated deviation area
    fr_lat_p90_mm: float = 0.0              # 90th percentile |lateral|
    fr_lat_mean_abs_mm: float = 0.0         # mean |lateral|
    fr_yaw_bias_dps: float = 0.0            # estimated yaw bias from cruise data
    fr_lat_per_meter: list = field(default_factory=list)  # per-meter lateral breakdown
    fr_lat_wander_mm: float = 0.0           # detrended wander amplitude (去趋势漂移)
    fr_lat_wander_rms_mm: float = 0.0       # detrended wander RMS

    # --- Cruise-only lateral (排除启动瞬态) ---
    fr_cruise_lat_start_mm: float = 0.0     # lateral at cruise start
    fr_cruise_lat_end_mm: float = 0.0       # lateral at cruise end
    fr_cruise_lat_net_mm: float = 0.0       # net drift during cruise = end - start
    fr_cruise_lat_max_mm: float = 0.0       # max |lateral| during cruise
    fr_cruise_lat_rms_mm: float = 0.0       # RMS during cruise
    fr_cruise_lat_drift_rate_mm_s: float = 0.0  # linear drift rate cruise-only
    fr_cruise_lat_direction_pct: float = 0.0  # % of cruise drifting same direction
    fr_cruise_lat_drift_dir: str = ""       # "左偏" / "右偏" / "稳定"

    # --- Lateral jitter (横向微抖动) ---
    fr_lat_jitter_hz: float = 0.0           # lateral velocity sign change frequency
    fr_lat_jitter_amp_mm: float = 0.0       # high-pass lateral jitter amplitude (RMS)
    fr_lat_jitter_max_mm: float = 0.0       # max single-sample lateral jump

    # --- Heading→lateral projection ---
    fr_yaw_bias_projected_mm: float = 0.0   # projected lat drift from yaw bias * distance

    # --- Phase analysis ---
    fr_phase_analysis: list = field(default_factory=list)  # per-phase drift summary

    fr_timeline: list = field(default_factory=list)

    # --- Speed Response ---
    speed_rise_time_ms: int = 0
    speed_overshoot_pct: float = 0.0
    speed_settle_time_ms: int = 0
    speed_ss_mean: float = 0.0
    speed_ss_error: float = 0.0
    speed_ss_error_pct: float = 0.0
    speed_tracking_rmse: float = 0.0
    speed_pc_ss_mean: float = 0.0
    speed_pc_ss_std: float = 0.0

    # --- Sub-scores (strict) ---
    score_speed: float = 0.0       # /25
    score_heading: float = 0.0     # /30
    score_trajectory: float = 0.0  # /25
    score_smoothness: float = 0.0  # /20

    # --- Output ---
    recommendations: list = field(default_factory=list)
    pid_suggestion: dict = field(default_factory=dict)


def _analyze_window(recs: list[HBRecord], t_start: int, t_end: int,
                    target_speed: float = 8.0) -> WindowStats:
    ws = WindowStats(t_start_ms=t_start, t_end_ms=t_end, n=len(recs))
    if not recs:
        ws.motion_state = "no_data"
        return ws
    els = [r.el for r in recs]
    ers = [r.er for r in recs]
    pcs = [r.pc for r in recs]
    ols = [r.ol for r in recs]
    ors = [r.or_ for r in recs]
    hds = [r.hd for r in recs]
    ws.el_mean = statistics.mean(els)
    ws.er_mean = statistics.mean(ers)
    ws.pc_mean = statistics.mean(pcs)
    ws.pc_std = statistics.stdev(pcs) if len(pcs) > 1 else 0.0
    ws.pc_min = min(pcs)
    ws.pc_max = max(pcs)
    ws.pc_sign_changes = _count_sign_changes(pcs)
    ws.el_negative_pct = _pct(sum(1 for e in els if e < 0), len(els))
    ws.ol_negative_pct = _pct(sum(1 for o in ols if o < 0), len(ols))
    yaws = [r.yaw for r in recs]
    ws.yaw_drift = yaws[-1] - yaws[0] if len(yaws) > 1 else 0.0
    ws.yaw_mean = statistics.mean(yaws)
    cap = target_speed * 3.0
    speeds = [min((r.el + r.er) / 2.0, cap) for r in recs]
    ws.speed_mean = statistics.mean(speeds)
    ws.speed_err = target_speed - ws.speed_mean
    ws.hd_mean = statistics.mean(hds)
    dps = [r.dp for r in recs]
    ws.dp_mean = statistics.mean(dps)
    ws.ol_mean = statistics.mean(ols)
    ws.or_mean = statistics.mean(ors)
    # classify
    if ws.pc_std > 15 and ws.el_negative_pct > 10:
        ws.motion_state = "JITTER"
    elif ws.pc_mean < -3:
        ws.motion_state = "BRAKING"
    elif ws.pc_std < 5 and ws.el_mean > 5:
        ws.motion_state = "CRUISE"
    elif ws.el_mean > 20:
        ws.motion_state = "ACCEL"
    else:
        ws.motion_state = "TRANSITION"
    return ws


def analyze_straight(records: list[HBRecord], params: dict[str, Any],
                     pwm_max: int = 60, diff_max: int = 20,
                     startup_ms: int = 2000) -> StraightLineReport:
    rpt = StraightLineReport()
    rpt.startup_duration_ms = startup_ms

    if not records:
        rpt.recommendations.append("无数据，请检查串口连接")
        return rpt

    run_recs = [r for r in records if r.run == 1]
    rpt.total_samples = len(records)
    rpt.run_samples = len(run_recs)

    if len(run_recs) < 10:
        rpt.recommendations.append(f"运行样本过少({len(run_recs)})")
        return rpt

    t0 = run_recs[0].t_ms
    rpt.duration_ms = run_recs[-1].t_ms - t0
    if len(run_recs) > 1:
        dts = [run_recs[i].t_ms - run_recs[i - 1].t_ms for i in range(1, len(run_recs))]
        rpt.sample_period_ms = statistics.mean(dts)

    els = [r.el for r in run_recs]
    ers = [r.er for r in run_recs]
    yaws = [r.yaw for r in run_recs]
    pcs = [r.pc for r in run_recs]
    hds = [r.hd for r in run_recs]
    ols = [r.ol for r in run_recs]
    ors = [r.or_ for r in run_recs]
    ts_s = [(r.t_ms - t0) / 1000.0 for r in run_recs]
    N = len(run_recs)

    # ============ JITTER / REVERSAL ANALYSIS ============
    rpt.pc_sign_changes = _count_sign_changes(pcs)
    dur_s = max(rpt.duration_ms / 1000.0, 0.01)
    rpt.pc_sign_change_freq_hz = rpt.pc_sign_changes / (2.0 * dur_s)
    rpt.pc_peak_to_peak = (max(pcs) - min(pcs)) if pcs else 0

    rpt.el_negative_count = sum(1 for e in els if e < 0)
    rpt.er_negative_count = sum(1 for e in ers if e < 0)
    rpt.el_negative_pct = _pct(rpt.el_negative_count, N)
    rpt.er_negative_pct = _pct(rpt.er_negative_count, N)

    rpt.ol_negative_count = sum(1 for o in ols if o < 0)
    rpt.or_negative_count = sum(1 for o in ors if o < 0)
    rpt.ol_negative_pct = _pct(rpt.ol_negative_count, N)
    rpt.or_negative_pct = _pct(rpt.or_negative_count, N)

    # Count full reversal cycles: positive → negative → positive in pc
    cycles = 0
    in_neg = False
    for p in pcs:
        if p < 0 and not in_neg:
            in_neg = True
        elif p > 0 and in_neg:
            in_neg = False
            cycles += 1
    rpt.pc_reversal_cycles = cycles
    rpt.reversal_period_ms = (rpt.duration_ms / max(cycles, 1)) if cycles > 0 else 0

    # PWM slew rate
    pc_slews = [abs(pcs[i] - pcs[i - 1]) for i in range(1, N)]
    ol_slews = [abs(ols[i] - ols[i - 1]) for i in range(1, N)]
    rpt.max_pc_slew = max(pc_slews) if pc_slews else 0
    rpt.mean_pc_slew = statistics.mean(pc_slews) if pc_slews else 0.0
    rpt.max_ol_slew = max(ol_slews) if ol_slews else 0

    # Jitter score (0-100)
    rev_score = min(rpt.el_negative_pct * 2.5, 40)       # up to 40 for reversal
    osc_score = min(rpt.pc_sign_change_freq_hz * 5, 30)  # up to 30 for freq
    amp_score = min(rpt.pc_peak_to_peak / 3.0, 30)       # up to 30 for amplitude
    rpt.jitter_score = min(rev_score + osc_score + amp_score, 100)
    if rpt.jitter_score < 10:
        rpt.jitter_severity = "none"
    elif rpt.jitter_score < 30:
        rpt.jitter_severity = "mild"
    elif rpt.jitter_score < 60:
        rpt.jitter_severity = "moderate"
    else:
        rpt.jitter_severity = "severe"

    # ============ SPEED PID ============
    rpt.speed_mean_el = statistics.mean(els)
    rpt.speed_mean_er = statistics.mean(ers)
    rpt.speed_std_el = statistics.stdev(els) if N > 1 else 0.0
    rpt.speed_std_er = statistics.stdev(ers) if N > 1 else 0.0
    mean_el_abs = statistics.mean([abs(e) for e in els]) or 1e-9
    mean_er_abs = statistics.mean([abs(e) for e in ers]) or 1e-9
    rpt.speed_el_er_ratio = mean_er_abs / max(mean_el_abs, 1e-9)
    rpt.speed_asymmetry_pct = abs(mean_el_abs - mean_er_abs) / max(mean_el_abs + mean_er_abs, 1e-9) * 200.0
    rpt.speed_mean_pc = statistics.mean(pcs)
    rpt.speed_std_pc = statistics.stdev(pcs) if N > 1 else 0.0
    rpt.speed_pc_range = (min(pcs), max(pcs))
    half = N // 2
    if half > 0:
        rpt.speed_steady_pc = statistics.mean(pcs[half:])
    rpt.speed_oscillation_freq_hz = rpt.pc_sign_change_freq_hz

    # ============ HEADING PID ============
    rpt.heading_yaw_final = yaws[-1]
    rpt.heading_yaw_std = statistics.stdev(yaws) if N > 1 else 0.0
    rpt.heading_yaw_max_abs = max(abs(y) for y in yaws)
    rpt.heading_yaw_drift_rate_dps = _linfit(ts_s, yaws)
    rpt.heading_mean_hd = statistics.mean(hds)
    rpt.heading_std_hd = statistics.stdev(hds) if N > 1 else 0.0
    rpt.heading_hd_range = (min(hds), max(hds))
    rpt.heading_hd_saturation_pct = _pct(sum(1 for h in hds if abs(h) >= diff_max), N)
    # Post-startup heading metrics for scoring (exclude startup transient)
    ss_recs = [r for r in run_recs if r.t_ms - t0 >= startup_ms]
    if len(ss_recs) > 2:
        ss_yaws = [r.yaw for r in ss_recs]
        ss_ts = [(r.t_ms - t0) / 1000.0 for r in ss_recs]
        rpt._ss_yaw_max = max(abs(y) for y in ss_yaws)
        rpt._ss_yaw_std = statistics.stdev(ss_yaws)
        rpt._ss_yaw_integral = sum(abs(ss_yaws[i]) * (ss_ts[i] - ss_ts[i-1])
                                    for i in range(1, len(ss_yaws)))
    else:
        rpt._ss_yaw_max = rpt.heading_yaw_max_abs
        rpt._ss_yaw_std = rpt.heading_yaw_std
        rpt._ss_yaw_integral = rpt.heading_yaw_integral
    yaw_mean = statistics.mean([r.yaw for r in run_recs])
    if abs(yaw_mean) > 1.0 and abs(rpt.heading_mean_hd) > 0.1:
        # If car points right (yaw_mean>0), hd should be negative (correct left)
        rpt.heading_correction_sign_ok = (
            (yaw_mean > 0) != (rpt.heading_mean_hd > 0)
        )

    # ============ YAW RATE / D-TERM DIAGNOSTICS ============
    yrs = [r.yr for r in run_recs]
    dps = [r.dp for r in run_recs]
    rpt.yr_mean = statistics.mean(yrs) if yrs else 0.0
    rpt.yr_std = statistics.stdev(yrs) if len(yrs) > 1 else 0.0
    rpt.yr_max_abs = max(abs(y) for y in yrs) if yrs else 0.0
    rpt.dp_mean = statistics.mean(dps) if dps else 0.0
    rpt.dp_std = statistics.stdev(dps) if len(dps) > 1 else 0.0
    rpt.dp_range = (min(dps), max(dps)) if dps else (0, 0)
    rpt.dp_nonzero_pct = _pct(sum(1 for d in dps if d != 0), N)

    # ============ PWM ============
    rpt.pwm_mean_ol = statistics.mean(ols)
    rpt.pwm_mean_or = statistics.mean(ors)
    rpt.pwm_std_ol = statistics.stdev(ols) if N > 1 else 0.0
    rpt.pwm_std_or = statistics.stdev(ors) if N > 1 else 0.0
    rpt.pwm_balance_mean = statistics.mean([o - l for l, o in zip(ols, ors)])
    rpt.pwm_saturation_pct = _pct(sum(1 for l, r in zip(ols, ors) if abs(l) >= pwm_max or abs(r) >= pwm_max), N)

    # ============ MOTION STATE ============
    fwd = sum(1 for o in ols if o > 0)
    rev = sum(1 for o in ols if o < 0)
    idle = sum(1 for o in ols if o == 0)
    rpt.state_forward_pct = _pct(fwd, N)
    rpt.state_reverse_pct = _pct(rev, N)
    rpt.state_idle_pct = _pct(idle, N)

    # ============ PHASE ANALYSIS ============
    startup_recs = [r for r in run_recs if (r.t_ms - t0) <= startup_ms]
    steady_recs = [r for r in run_recs if (r.t_ms - t0) > startup_ms]
    if startup_recs:
        rpt.startup_yaw_peak = max(abs(r.yaw) for r in startup_recs)
        rpt.startup_pc_peak = max(abs(r.pc) for r in startup_recs)
    if len(steady_recs) > 5:
        s_ts = [(r.t_ms - t0) / 1000.0 for r in steady_recs]
        s_yaws = [r.yaw for r in steady_recs]
        rpt.steady_yaw_drift_rate = _linfit(s_ts, s_yaws)
        rpt.steady_pc_std = statistics.stdev([r.pc for r in steady_recs])

    # ============ 500ms WINDOW BREAKDOWN ============
    target_spd = params.get("target_speed", 8.0)
    window_size_ms = 500
    for ws_start in range(0, rpt.duration_ms + 1, window_size_ms):
        ws_end = ws_start + window_size_ms
        w_recs = [r for r in run_recs if ws_start <= (r.t_ms - t0) < ws_end]
        if w_recs:
            rpt.windows.append(_analyze_window(w_recs, ws_start, ws_end, target_spd))

    # ============ TRAJECTORY ============
    traj = reconstruct_trajectory(run_recs)
    rpt.trajectory = traj
    if traj:
        lat_devs = [abs(p.lateral_dev) for p in traj]
        rpt.traj_max_lateral_dev = max(lat_devs)
        rpt.traj_mean_lateral_dev = statistics.mean(lat_devs)
        rpt.traj_final_lateral_dev = traj[-1].lateral_dev
        rpt.traj_total_distance = abs(traj[-1].x) if traj[-1].x != 0 else 0.001
        path_len = sum(
            math.sqrt((traj[i].x - traj[i-1].x)**2 + (traj[i].y - traj[i-1].y)**2)
            for i in range(1, len(traj))
        )
        rpt.traj_sinuosity = path_len / max(rpt.traj_total_distance, 1e-6)
        rpt.traj_lateral_dev_pct = rpt.traj_max_lateral_dev / max(rpt.traj_total_distance, 1e-6) * 100
        # Lateral velocity (enc/sample)
        lat_vels = []
        for i in range(1, len(traj)):
            dt_s = max((traj[i].t_ms - traj[i-1].t_ms) / 1000.0, 0.001)
            lat_vels.append(abs(traj[i].lateral_dev - traj[i-1].lateral_dev) / dt_s)
        if lat_vels:
            rpt.traj_max_lateral_vel = max(lat_vels)
            rpt.traj_rms_lateral_vel = math.sqrt(statistics.mean([v**2 for v in lat_vels]))

    # ============ FULL-RATE ANALYSIS (per-sample, high-precision) ============
    if traj and len(traj) > 2:
        # --- Per-sample speed in mm/s (from trajectory deltas) ---
        fr_speeds_raw = []
        fr_dts = []
        for i in range(1, len(traj)):
            dt_ms = max(traj[i].t_ms - traj[i-1].t_ms, 1)
            dt_s = dt_ms / 1000.0
            dx = (traj[i].x - traj[i-1].x) * ENC_TO_MM
            dy = (traj[i].y - traj[i-1].y) * ENC_TO_MM
            fr_speeds_raw.append(math.sqrt(dx*dx + dy*dy) / dt_s)
            fr_dts.append(dt_ms)
        # Smoothed speed (5-pt moving avg) to filter single-sample encoder noise
        fr_speeds = _moving_avg(fr_speeds_raw, 5)
        if fr_speeds:
            rpt.fr_speed_mean = round(statistics.mean(fr_speeds), 2)
            rpt.fr_speed_std = round(statistics.stdev(fr_speeds) if len(fr_speeds) > 1 else 0, 2)
            rpt.fr_speed_min = round(min(fr_speeds), 2)
            rpt.fr_speed_max = round(max(fr_speeds), 2)
        # Cruise-only speed stats (skip startup transient)
        cruise_start_idx = next((i for i, p in enumerate(traj) if p.t_ms >= startup_ms), 0)
        cruise_speeds = fr_speeds[max(cruise_start_idx - 1, 0):]
        if len(cruise_speeds) > 2:
            rpt.fr_cruise_speed_mean = round(statistics.mean(cruise_speeds), 2)
            rpt.fr_cruise_speed_std = round(statistics.stdev(cruise_speeds), 2)
            rpt.fr_cruise_speed_cv = round(
                rpt.fr_cruise_speed_std / max(rpt.fr_cruise_speed_mean, 0.1) * 100, 2)

        # --- Sampling interval statistics ---
        mean_dt = statistics.mean(fr_dts) if fr_dts else 85.0
        dt_std = statistics.stdev(fr_dts) if len(fr_dts) > 1 else 0.0

        # --- Speed oscillation detection (on pc and smoothed speed) ---
        cruise_pcs = [r.pc for r in run_recs[cruise_start_idx:]]
        if len(cruise_pcs) > 12:
            osc_pc = _detect_oscillation([float(p) for p in cruise_pcs], mean_dt)
            rpt.fr_pc_osc_detected = osc_pc.get('osc_detected', False)
            rpt.fr_pc_osc_period_ms = osc_pc.get('osc_period_ms', 0)
            rpt.fr_pc_osc_amplitude = osc_pc.get('osc_amplitude', 0)
            rpt.fr_pc_osc_acf = osc_pc.get('osc_acf_peak', 0)
        if len(cruise_speeds) > 12:
            osc_spd = _detect_oscillation(cruise_speeds, mean_dt)
            rpt.fr_spd_osc_detected = osc_spd.get('osc_detected', False)
            rpt.fr_spd_osc_period_ms = osc_spd.get('osc_period_ms', 0)
            rpt.fr_spd_osc_amplitude = osc_spd.get('osc_amplitude', 0)

        # --- High-precision lateral tracking (mm) ---
        lat_mms = [p.lateral_dev * ENC_TO_MM for p in traj]
        abs_lat_mms = [abs(l) for l in lat_mms]
        lat_ts = [p.t_ms / 1000.0 for p in traj]

        rpt.fr_lat_max_mm = round(max(abs_lat_mms), 2)
        rpt.fr_lat_final_mm = round(lat_mms[-1], 2)
        rpt.fr_lat_drift_rate_mm_s = round(_linfit(lat_ts, lat_mms), 3)
        rpt.fr_lat_rms_mm = round(math.sqrt(statistics.mean([l*l for l in lat_mms])), 2)
        rpt.fr_lat_mean_abs_mm = round(statistics.mean(abs_lat_mms), 2)

        # Lateral deviation integral: ∫|lat|dt (mm·s) — total accumulated error
        lat_integral = 0.0
        for i in range(1, len(traj)):
            dt_s = max((traj[i].t_ms - traj[i-1].t_ms) / 1000.0, 0.001)
            lat_integral += (abs_lat_mms[i-1] + abs_lat_mms[i]) / 2.0 * dt_s  # trapezoidal
        rpt.fr_lat_integral_mm_s = round(lat_integral, 2)

        # 90th percentile |lateral|
        sorted_abs = sorted(abs_lat_mms)
        p90_idx = int(len(sorted_abs) * 0.90)
        rpt.fr_lat_p90_mm = round(sorted_abs[min(p90_idx, len(sorted_abs)-1)], 2)

        # Yaw bias estimation from cruise steady-state
        cruise_yaws = [r.yaw for r in run_recs[cruise_start_idx:]]
        if len(cruise_yaws) > 10:
            cruise_ts = [r.t_ms / 1000.0 for r in run_recs[cruise_start_idx:]]
            rpt.fr_yaw_bias_dps = round(_linfit(cruise_ts, cruise_yaws), 4)

        # Detrended wander: remove linear drift to see residual oscillation
        drift_slope = _linfit(lat_ts, lat_mms)
        t0_s = lat_ts[0] if lat_ts else 0.0
        detrended = [lat_mms[i] - drift_slope * (lat_ts[i] - t0_s)
                     for i in range(len(lat_mms))]
        if detrended:
            rpt.fr_lat_wander_mm = round(max(abs(d) for d in detrended), 2)
            rpt.fr_lat_wander_rms_mm = round(
                math.sqrt(statistics.mean([d*d for d in detrended])), 2)

        # ============ CRUISE-ONLY LATERAL ANALYSIS (核心修复) ============
        cruise_lats = lat_mms[cruise_start_idx:]
        cruise_lat_ts = lat_ts[cruise_start_idx:]
        if len(cruise_lats) > 5:
            rpt.fr_cruise_lat_start_mm = round(cruise_lats[0], 2)
            rpt.fr_cruise_lat_end_mm = round(cruise_lats[-1], 2)
            rpt.fr_cruise_lat_net_mm = round(cruise_lats[-1] - cruise_lats[0], 2)
            cruise_abs = [abs(l) for l in cruise_lats]
            rpt.fr_cruise_lat_max_mm = round(max(cruise_abs), 2)
            rpt.fr_cruise_lat_rms_mm = round(
                math.sqrt(statistics.mean([l*l for l in cruise_lats])), 2)
            rpt.fr_cruise_lat_drift_rate_mm_s = round(
                _linfit(cruise_lat_ts, cruise_lats), 3)

            # Drift direction consistency: what % of cruise time lat is moving same direction
            lat_deltas = [cruise_lats[i] - cruise_lats[i-1] for i in range(1, len(cruise_lats))]
            if lat_deltas:
                pos_count = sum(1 for d in lat_deltas if d > 0)
                neg_count = sum(1 for d in lat_deltas if d < 0)
                dominant = max(pos_count, neg_count)
                rpt.fr_cruise_lat_direction_pct = round(
                    dominant / len(lat_deltas) * 100, 1)
                net = rpt.fr_cruise_lat_net_mm
                if abs(net) > 5:
                    rpt.fr_cruise_lat_drift_dir = "左偏(+)" if net > 0 else "右偏(-)"
                else:
                    rpt.fr_cruise_lat_drift_dir = "稳定"

        # ============ LATERAL JITTER DETECTION (横向微抖动) ============
        # Compute per-sample lateral velocity
        lat_vs = []
        for i in range(1, len(traj)):
            dt_s = max((traj[i].t_ms - traj[i-1].t_ms) / 1000.0, 0.001)
            lat_vs.append((lat_mms[i] - lat_mms[i-1]) / dt_s)
        if len(lat_vs) > 10:
            # Sign change frequency → jitter indicator
            sign_changes = sum(1 for i in range(1, len(lat_vs))
                              if lat_vs[i] * lat_vs[i-1] < 0)
            total_time_s = (traj[-1].t_ms - traj[0].t_ms) / 1000.0
            rpt.fr_lat_jitter_hz = round(sign_changes / max(total_time_s, 0.1) / 2, 2)

            # High-pass jitter: remove 5-pt moving avg trend, measure residual
            lat_smooth = _moving_avg(lat_mms, 7)
            if len(lat_smooth) == len(lat_mms):
                hp_residual = [lat_mms[i] - lat_smooth[i] for i in range(len(lat_mms))]
            else:
                # moving_avg might return shorter list
                offset = len(lat_mms) - len(lat_smooth)
                hp_residual = [lat_mms[i+offset] - lat_smooth[i] for i in range(len(lat_smooth))]
            if hp_residual:
                rpt.fr_lat_jitter_amp_mm = round(
                    math.sqrt(statistics.mean([r*r for r in hp_residual])), 2)
                rpt.fr_lat_jitter_max_mm = round(max(abs(r) for r in hp_residual), 2)

        # ============ HEADING BIAS → LATERAL PROJECTION ============
        if abs(rpt.fr_yaw_bias_dps) > 0.001 and traj:
            fwd_mm_total = traj[-1].x * ENC_TO_MM
            # Average yaw during cruise → projected lateral drift
            cruise_yaw_mean = statistics.mean([r.yaw for r in run_recs[cruise_start_idx:]]) if cruise_start_idx < len(run_recs) else 0
            projected_lat = fwd_mm_total * math.sin(math.radians(cruise_yaw_mean))
            rpt.fr_yaw_bias_projected_mm = round(projected_lat, 2)

        # ============ PHASE ANALYSIS (分阶段漂移) ============
        if traj and len(traj) > 10:
            duration_s = traj[-1].t_ms / 1000.0
            phases = [
                ("启动", 0, min(startup_ms / 1000.0, duration_s)),
                ("前巡航", startup_ms / 1000.0, min(duration_s * 0.4, duration_s)),
                ("中巡航", duration_s * 0.4, min(duration_s * 0.7, duration_s)),
                ("后巡航", duration_s * 0.7, duration_s),
            ]
            for name, t_start, t_end in phases:
                phase_pts = [(lat_mms[i], lat_ts[i]) for i in range(len(traj))
                            if t_start <= lat_ts[i] < t_end]
                if len(phase_pts) > 3:
                    p_lats = [p[0] for p in phase_pts]
                    p_ts = [p[1] for p in phase_pts]
                    p_drift_rate = _linfit(p_ts, p_lats)
                    p_net = p_lats[-1] - p_lats[0]
                    rpt.fr_phase_analysis.append({
                        'name': name,
                        't_start': round(t_start, 1),
                        't_end': round(t_end, 1),
                        'lat_start': round(p_lats[0], 1),
                        'lat_end': round(p_lats[-1], 1),
                        'net': round(p_net, 1),
                        'drift_rate': round(p_drift_rate, 2),
                        'dir': "→左" if p_net > 3 else ("→右" if p_net < -3 else "稳"),
                    })

        # Per-meter lateral deviation breakdown
        fwd_mms = [p.x * ENC_TO_MM for p in traj]
        meter_boundary = 1000.0  # 1 meter
        seg_start = 0
        seg_meter = 1
        for i in range(len(traj)):
            if fwd_mms[i] >= seg_meter * meter_boundary or i == len(traj) - 1:
                seg_lats = abs_lat_mms[seg_start:i+1]
                seg_lat_raw = lat_mms[seg_start:i+1]
                if seg_lats:
                    rpt.fr_lat_per_meter.append({
                        'meter': seg_meter,
                        'mean_abs': round(statistics.mean(seg_lats), 2),
                        'max': round(max(seg_lats), 2),
                        'rms': round(math.sqrt(statistics.mean([l*l for l in seg_lat_raw])), 2),
                        'final': round(seg_lat_raw[-1], 2),
                    })
                seg_start = i
                seg_meter += 1

        # --- Per-sample timeline for display (every ~1s) ---
        display_dt = 1.0
        next_display = 0.0
        for i, p in enumerate(traj):
            t_s = p.t_ms / 1000.0
            if t_s >= next_display:
                spd = fr_speeds[min(i - 1, len(fr_speeds) - 1)] if i > 0 and fr_speeds else 0.0
                rec = run_recs[i] if i < len(run_recs) else run_recs[-1]
                lat_spd = 0.0
                if i > 0:
                    prev_lat = traj[i-1].lateral_dev * ENC_TO_MM
                    cur_lat = p.lateral_dev * ENC_TO_MM
                    dt_here = max((p.t_ms - traj[i-1].t_ms) / 1000.0, 0.001)
                    lat_spd = (cur_lat - prev_lat) / dt_here
                rpt.fr_timeline.append({
                    't': round(t_s, 2),
                    'fwd': round(p.x * ENC_TO_MM, 1),
                    'lat': round(p.lateral_dev * ENC_TO_MM, 2),
                    'lat_v': round(lat_spd, 2),
                    'spd': round(spd, 1),
                    'yaw': round(p.yaw_deg, 3),
                    'yr': round(rec.yr, 2),
                    'pc': rec.pc,
                    'hd': rec.hd,
                    'dp': rec.dp,
                    'ol': rec.ol,
                    'or': rec.or_,
                })
                next_display = t_s + display_dt

    # ============ HEADING PRECISION ============
    # Yaw integral: accumulated |yaw| * dt (lower = better heading hold)
    yaw_integral = 0.0
    for i in range(1, len(run_recs)):
        dt_s = max((run_recs[i].t_ms - run_recs[i-1].t_ms) / 1000.0, 0.001)
        yaw_integral += abs(run_recs[i].yaw) * dt_s
    rpt.heading_yaw_integral = round(yaw_integral, 2)
    # Zero crossings: how many times yaw crosses zero (oscillation indicator)
    zc = 0
    for i in range(1, len(yaws)):
        if (yaws[i-1] > 0 and yaws[i] <= 0) or (yaws[i-1] < 0 and yaws[i] >= 0):
            zc += 1
    rpt.heading_yaw_zero_crossings = zc
    # Monotonic percentage: fraction of consecutive yaw samples with same sign derivative
    if len(yaws) > 2:
        same_dir = sum(1 for i in range(2, len(yaws))
                       if (yaws[i] - yaws[i-1]) * (yaws[i-1] - yaws[i-2]) > 0)
        rpt.heading_yaw_monotonic_pct = round(same_dir / max(len(yaws) - 2, 1) * 100, 1)

    # ============ SPEED RESPONSE ============
    sr = compute_speed_response(run_recs, target_spd)
    if sr:
        rpt.speed_rise_time_ms = sr.get("rise_time_ms", 0)
        rpt.speed_overshoot_pct = sr.get("overshoot_pct", 0)
        rpt.speed_settle_time_ms = sr.get("settle_time_ms", 0)
        rpt.speed_ss_mean = sr.get("ss_mean", 0)
        rpt.speed_ss_error = sr.get("ss_error", 0)
        rpt.speed_ss_error_pct = sr.get("ss_error_pct", 0)
        rpt.speed_tracking_rmse = sr.get("rmse", 0)
        rpt.speed_pc_ss_mean = sr.get("pc_ss_mean", 0)
        rpt.speed_pc_ss_std = sr.get("pc_ss_std", 0)

    # ============ STRICT v8 SCORING (cruise-based, drift-aware) ============
    # Speed (25 pts): pc SS quality, std, overshoot, rise time, cruise CV, oscillation
    pc_err_pct = abs(target_spd - rpt.speed_pc_ss_mean) / max(target_spd, 1) * 100 if sr else 50
    se_pen = min(pc_err_pct * 0.25, 12)           # v7: 0.2→0.25 stricter SS error
    sstd_pen = min(rpt.speed_pc_ss_std * 1.0, 6) if sr else min(rpt.speed_std_pc * 0.8, 6)  # v7: 0.8→1.0
    sov_pen = min(rpt.speed_overshoot_pct * 0.04, 6)  # v7: 0.03→0.04
    srise_pen = min(max(rpt.speed_rise_time_ms - 400, 0) / 300, 5)  # v7: threshold 600→400, divisor 400→300
    scv_pen = min(max(rpt.fr_cruise_speed_cv - 3, 0) * 0.15, 4)  # v7: threshold 5→3%, coeff 0.1→0.15, cap 3→4
    sosc_pen = min(rpt.fr_pc_osc_amplitude * 0.4, 3) if rpt.fr_pc_osc_detected else 0  # v7: 0.3→0.4, cap 2→3
    rpt.score_speed = round(max(0, 25 - se_pen - sstd_pen - sov_pen - srise_pen - scv_pen - sosc_pen), 2)

    # Heading (30 pts): uses post-startup metrics
    hdr_pen = min(abs(rpt.heading_yaw_drift_rate_dps) * 6, 12)   # v7: 4→6 stricter drift
    _ss_max = getattr(rpt, '_ss_yaw_max', rpt.heading_yaw_max_abs)
    _ss_std = getattr(rpt, '_ss_yaw_std', rpt.heading_yaw_std)
    _ss_int = getattr(rpt, '_ss_yaw_integral', rpt.heading_yaw_integral)
    hmax_pen = min(_ss_max * 0.6, 10)             # v7: 0.4→0.6
    hstd_pen = min(_ss_std * 0.6, 8)              # v7: 0.4→0.6
    hyint_pen = min(_ss_int * 0.06, 5)            # v7: 0.04→0.06
    hsat_pen = min(rpt.heading_hd_saturation_pct * 0.5, 5)
    hsign_pen = 5 if not rpt.heading_correction_sign_ok and abs(rpt.heading_yaw_drift_rate_dps) > 1 else 0
    hbias_pen = min(abs(rpt.fr_yaw_bias_dps) * 30, 8)  # v8: 8→30 coeff, cap 4→8 (0.03°/s=0.9pt)
    rpt.score_heading = round(max(0, 30 - hdr_pen - hmax_pen - hstd_pen - hyint_pen - hsat_pen - hsign_pen - hbias_pen), 2)

    # Trajectory (25 pts): v8 — uses CRUISE-ONLY lateral metrics to avoid startup masking
    fwd_mm = rpt.traj_total_distance * ENC_TO_MM
    fwd_m = max(fwd_mm / 1000.0, 0.01)
    # v8: use cruise lateral instead of full-run (startup transient no longer masks drift)
    c_lat_max = rpt.fr_cruise_lat_max_mm if rpt.fr_cruise_lat_max_mm > 0 else rpt.fr_lat_max_mm
    c_lat_rms = rpt.fr_cruise_lat_rms_mm if rpt.fr_cruise_lat_rms_mm > 0 else rpt.fr_lat_rms_mm
    c_lat_net = abs(rpt.fr_cruise_lat_net_mm) if rpt.fr_cruise_lat_net_mm != 0 else abs(rpt.fr_lat_final_mm)
    c_drift_rate = abs(rpt.fr_cruise_lat_drift_rate_mm_s) if rpt.fr_cruise_lat_drift_rate_mm_s != 0 else abs(rpt.fr_lat_drift_rate_mm_s)
    tlat_pen = min(c_lat_max / fwd_m * 0.5, 10) if traj else 6     # v8: cruise max, 0.4→0.5
    tlatm_pen = min(c_lat_net / fwd_m * 1.0, 8) if traj else 3    # v8: cruise net drift, 0.8→1.0
    tlatv_pen = min(c_drift_rate * 1.5, 6) if traj else 2          # v8: cruise drift rate, 1.2→1.5, cap 5→6
    tsin_pen = min(max(rpt.traj_sinuosity - 1.0, 0) * 150, 5) if traj else 3
    tasym_pen = min(rpt.speed_asymmetry_pct * 0.4, 5)
    # v8: cruise RMS — real sustained deviation without startup noise
    trms_pen = min(c_lat_rms / fwd_m * 0.6, 5) if traj else 2   # 0.5→0.6, cap 4→5
    # v8: lateral integral penalty
    tint_pen = min(rpt.fr_lat_integral_mm_s / max(rpt.duration_ms / 1000.0, 1) * 0.04, 3) if traj else 1
    # v8: detrended wander
    twander_pen = min(rpt.fr_lat_wander_rms_mm / fwd_m * 0.3, 3) if traj else 1
    # v8 new: cruise drift direction consistency — monotonic drift is very bad
    tdir_pen = 0
    if rpt.fr_cruise_lat_direction_pct > 60 and c_lat_net > 10:
        tdir_pen = min((rpt.fr_cruise_lat_direction_pct - 55) * 0.08, 4)
    # v8 new: yaw bias → lateral projection penalty (small bias = big drift over distance)
    tproj_pen = min(abs(rpt.fr_yaw_bias_projected_mm) / fwd_m * 0.6, 5) if traj else 0
    rpt.score_trajectory = round(max(0, 25 - tlat_pen - tlatm_pen - tlatv_pen - tsin_pen - tasym_pen - trms_pen - tint_pen - twander_pen - tdir_pen - tproj_pen), 2)

    # Smoothness (20 pts): jitter, slew, reversal, idle, lateral jitter
    smj_pen = min(rpt.jitter_score * 0.06, 10)    # v7: 0.05→0.06
    smslew_pen = min(rpt.mean_pc_slew * 0.2, 6)   # v7: 0.15→0.2
    smrev_pen = min(rpt.el_negative_pct * 0.8, 5)  # v7: 0.6→0.8
    smidle_pen = min(max(rpt.state_idle_pct - 2, 0) * 0.3, 4)  # v7: threshold 3→2, coeff 0.2→0.3
    # v8 new: lateral jitter penalty — visible side-to-side wobble
    smlj_pen = min(rpt.fr_lat_jitter_amp_mm * 0.3, 4) if rpt.fr_lat_jitter_amp_mm > 0.5 else 0
    rpt.score_smoothness = round(max(0, 20 - smj_pen - smslew_pen - smrev_pen - smidle_pen - smlj_pen), 2)

    rpt.straightness_score = round(rpt.score_speed + rpt.score_heading + rpt.score_trajectory + rpt.score_smoothness, 1)

    # ============ RECOMMENDATIONS ============
    recs = rpt.recommendations
    suggestion = dict(params)

    # ----- Jitter / Reversal -----
    if rpt.jitter_severity == "severe":
        recs.append(f"🔴 严重抖动! 轮子前后反转 (el<0 占 {rpt.el_negative_pct:.0f}%, "
                    f"pc 反转周期={rpt.reversal_period_ms:.0f}ms, "
                    f"pc 幅值={rpt.pc_peak_to_peak})")
        recs.append("  根因: 速度PID增益过高, 电机在正转↔反转间震荡")
        recs.append("  方案1: 大幅降低 SKP (当前值的 30-50%)")
        recs.append("  方案2: 大幅降低 SKI (减少积分风暴)")
        recs.append("  方案3: 增大 SKD (增加阻尼)")
        recs.append("  方案4: 降低 SPEED_OUTPUT_LIMIT (固件 config.h, 当前=100)")
        if "skp" in suggestion:
            suggestion["skp"] = round(suggestion["skp"] * 0.35, 4)
        if "ski" in suggestion:
            suggestion["ski"] = round(suggestion["ski"] * 0.3, 6)
        if "skd" in suggestion:
            suggestion["skd"] = round(max(suggestion["skd"], 0.01) * 3.0, 4)
    elif rpt.jitter_severity == "moderate":
        recs.append(f"⚠ 中度抖动 (el<0 占 {rpt.el_negative_pct:.0f}%, "
                    f"pc 幅值={rpt.pc_peak_to_peak})")
        recs.append("  建议: 降低 SKP 约 40%, 增大 SKD")
        if "skp" in suggestion:
            suggestion["skp"] = round(suggestion["skp"] * 0.6, 4)
        if "skd" in suggestion:
            suggestion["skd"] = round(max(suggestion["skd"], 0.01) * 2.0, 4)
    elif rpt.jitter_severity == "mild":
        recs.append(f"  轻微抖动 (el<0 占 {rpt.el_negative_pct:.0f}%)")

    if rpt.el_negative_pct > 5 and rpt.jitter_severity in ("none", "mild"):
        recs.append(f"⚠ 编码器出现负值 ({rpt.el_negative_pct:.1f}%), 轮子有反转")

    # ----- Speed PID -----
    if rpt.speed_std_pc > 15 and rpt.jitter_severity in ("none", "mild"):
        recs.append(f"⚠ pwmCore 波动大 (std={rpt.speed_std_pc:.1f}), 增大 SKD")

    # ----- Heading PID -----
    if abs(rpt.heading_yaw_drift_rate_dps) > 2.0:
        side = "右" if rpt.heading_yaw_drift_rate_dps > 0 else "左"
        recs.append(f"⚠ 航向持续偏{side} ({rpt.heading_yaw_drift_rate_dps:+.2f}°/s), 增大 AKI")
        if "aki" in suggestion:
            suggestion["aki"] = round(suggestion["aki"] * 1.5, 6)
    if rpt.heading_hd_saturation_pct > 10:
        recs.append(f"⚠ 航向差速饱和 ({rpt.heading_hd_saturation_pct:.1f}%), 降低 AKP")
        if "akp" in suggestion:
            suggestion["akp"] = round(suggestion["akp"] * 0.7, 4)
    if not rpt.heading_correction_sign_ok and abs(rpt.heading_yaw_drift_rate_dps) > 1.0:
        recs.append("🔴 航向纠偏方向异常!")
    if abs(rpt.heading_yaw_drift_rate_dps) < 0.5 and rpt.heading_hd_saturation_pct < 2:
        recs.append("✅ 航向控制良好")

    # ----- Encoder -----
    if rpt.speed_asymmetry_pct > 15:
        recs.append(f"⚠ 编码器不对称 ({rpt.speed_asymmetry_pct:.1f}%)")

    # ----- Overall -----
    if rpt.straightness_score >= 80:
        recs.append(f"✅ 综合评分 {rpt.straightness_score:.0f}/100")
    elif rpt.straightness_score >= 50:
        recs.append(f"⚠ 综合评分 {rpt.straightness_score:.0f}/100")
    else:
        recs.append(f"🔴 综合评分 {rpt.straightness_score:.0f}/100")

    rpt.pid_suggestion = suggestion
    return rpt


# ---------------------------------------------------------------------------
# Report formatter
# ---------------------------------------------------------------------------

def format_report(rpt: StraightLineReport, params: dict[str, Any], seq: int = 0) -> str:
    W = 75
    L = []
    L.append("=" * W)
    seq_tag = f"  [T{seq:03d}]" if seq else ""
    L.append(f"  直线 PID 深度分析报告 v6  (high-res + strict v8){seq_tag}")
    L.append("=" * W)

    L.append(f"\n【基本信息】")
    L.append(f"  样本: {rpt.run_samples}/{rpt.total_samples}  持续: {rpt.duration_ms}ms  "
             f"采样: {rpt.sample_period_ms:.1f}ms")

    L.append(f"\n【PID 参数】")
    L.append(f"  SKP={params.get('skp','?')}  SKI={params.get('ski','?')}  SKD={params.get('skd','?')}")
    L.append(f"  AKP={params.get('akp','?')}  AKI={params.get('aki','?')}  AKD={params.get('akd','?')}")
    L.append(f"  SFF={params.get('sff','?')}  SPD={params.get('target_speed','?')}")

    # ===== SPEED RESPONSE =====
    L.append(f"\n{'━' * W}")
    L.append(f"【速度响应】  (目标={params.get('target_speed','?')})")
    L.append(f"  上升时间(10%→90%): {rpt.speed_rise_time_ms}ms  "
             f"超调: {rpt.speed_overshoot_pct:.1f}%")
    L.append(f"  建立时间(±15%): {rpt.speed_settle_time_ms}ms")
    L.append(f"  稳态均值: {rpt.speed_ss_mean:.2f}  "
             f"稳态误差: {rpt.speed_ss_error:+.2f} ({rpt.speed_ss_error_pct:+.1f}%) [capped@3x]")
    L.append(f"  跟踪RMSE: {rpt.speed_tracking_rmse:.2f}")
    L.append(f"  pc稳态: mean={rpt.speed_pc_ss_mean:.2f}  std={rpt.speed_pc_ss_std:.2f}  "
             f"(理想值≈SPD*SFF={params.get('target_speed','?')})")
    L.append(f"  编码器: L={rpt.speed_mean_el:+.1f}±{rpt.speed_std_el:.1f}  "
             f"R={rpt.speed_mean_er:+.1f}±{rpt.speed_std_er:.1f}  "
             f"L/R={rpt.speed_el_er_ratio:.3f}  asym={rpt.speed_asymmetry_pct:.1f}%")
    L.append(f"  pc: mean={rpt.speed_mean_pc:.1f}  std={rpt.speed_std_pc:.1f}  "
             f"range=[{rpt.speed_pc_range[0]},{rpt.speed_pc_range[1]}]")

    # ===== HEADING =====
    L.append(f"\n【航向控制】")
    L.append(f"  yaw: final={rpt.heading_yaw_final:+.3f}°  drift={rpt.heading_yaw_drift_rate_dps:+.4f}°/s  "
             f"std={rpt.heading_yaw_std:.3f}°  max={rpt.heading_yaw_max_abs:.3f}°")
    L.append(f"  hd: mean={rpt.heading_mean_hd:+.1f}  std={rpt.heading_std_hd:.1f}  "
             f"range=[{rpt.heading_hd_range[0]},{rpt.heading_hd_range[1]}]  "
             f"sat={rpt.heading_hd_saturation_pct:.1f}%  "
             f"sign={'OK' if rpt.heading_correction_sign_ok else 'BAD'}")
    L.append(f"  起步yaw峰值: {rpt.startup_yaw_peak:.3f}°  "
             f"稳态drift: {rpt.steady_yaw_drift_rate:+.4f}°/s")
    L.append(f"  yaw积分: {rpt.heading_yaw_integral:.3f}°·s  "
             f"过零: {rpt.heading_yaw_zero_crossings}次  "
             f"单调: {rpt.heading_yaw_monotonic_pct:.1f}%")
    if abs(rpt.fr_yaw_bias_dps) > 0.0001:
        L.append(f"  ★ yaw偏置估计: {rpt.fr_yaw_bias_dps:+.4f}°/s (巡航线性拟合)")
    L.append(f"  yr(D输入): mean={rpt.yr_mean:+.1f}  std={rpt.yr_std:.1f}  "
             f"max={rpt.yr_max_abs:.1f}°/s")
    L.append(f"  dp(D输出): mean={rpt.dp_mean:+.1f}  std={rpt.dp_std:.1f}  "
             f"range=[{rpt.dp_range[0]},{rpt.dp_range[1]}]  "
             f"active={rpt.dp_nonzero_pct:.0f}%")

    # ===== TRAJECTORY =====
    L.append(f"\n【轨迹重建】  (编码器积分)")
    L.append(f"  前进距离: {rpt.traj_total_distance:.0f} enc  "
             f"蛇行度: {rpt.traj_sinuosity:.4f}")
    L.append(f"  横向偏移: max={rpt.traj_max_lateral_dev:.1f}  "
             f"mean={rpt.traj_mean_lateral_dev:.1f}  "
             f"final={rpt.traj_final_lateral_dev:+.1f} enc")
    L.append(f"  横向速度: max={rpt.traj_max_lateral_vel:.1f}  "
             f"RMS={rpt.traj_rms_lateral_vel:.1f} enc/s")
    if rpt.traj_total_distance > 0:
        drift_per_m = rpt.traj_final_lateral_dev / rpt.traj_total_distance * 100
        L.append(f"  横偏率: {drift_per_m:+.2f}% (横偏/前进距离)")
        final_mm = rpt.traj_final_lateral_dev * ENC_TO_MM
        max_mm = rpt.traj_max_lateral_dev * ENC_TO_MM
        fwd_m = rpt.traj_total_distance * ENC_TO_MM / 1000.0
        if abs(final_mm) > 3.0:  # >3mm drift is noticeable
            direction = "偏左(+y)" if rpt.traj_final_lateral_dev > 0 else "偏右(-y)"
            L.append(f"  ⚠ 系统性漂移: {direction}  "
                     f"终点横偏≈{final_mm:+.1f}mm  "
                     f"(前进{fwd_m:.1f}m, 每米偏{abs(final_mm/fwd_m):.1f}mm)")
            # Cause analysis
            lr_ratio = rpt.speed_el_er_ratio
            asym = rpt.speed_asymmetry_pct
            if asym > 0.5:
                L.append(f"    可能原因: 左右电机不对称 (L/R={lr_ratio:.3f}, asym={asym:.1f}%)")
            else:
                L.append(f"    可能原因: IMU零偏或地面倾斜 (L/R={lr_ratio:.3f}对称, 非electrical)")
        elif abs(final_mm) <= 3.0:
            L.append(f"  ✅ 轨迹笔直: 终点横偏≈{final_mm:+.1f}mm (优秀)")

    # ===== FULL-RATE SPEED ANALYSIS =====
    L.append(f"\n【逐帧速度分析】  (全帧率, N={rpt.run_samples}, Δt≈{rpt.sample_period_ms:.1f}ms)")
    L.append(f"  瞬时速度(mm/s): mean={rpt.fr_speed_mean:.1f}  std={rpt.fr_speed_std:.1f}  "
             f"min={rpt.fr_speed_min:.1f}  max={rpt.fr_speed_max:.1f}")
    if rpt.fr_cruise_speed_mean > 0:
        L.append(f"  巡航段(>{rpt.startup_duration_ms}ms): "
                 f"mean={rpt.fr_cruise_speed_mean:.1f}±{rpt.fr_cruise_speed_std:.1f} mm/s  "
                 f"CV={rpt.fr_cruise_speed_cv:.2f}%")
    if rpt.fr_pc_osc_detected:
        L.append(f"  ⚠ pc振荡检出: 周期≈{rpt.fr_pc_osc_period_ms:.0f}ms  "
                 f"幅度±{rpt.fr_pc_osc_amplitude:.1f} PWM  "
                 f"ACF={rpt.fr_pc_osc_acf:.2f}")
    else:
        L.append(f"  pc振荡: 未检出显著周期 (幅度±{rpt.fr_pc_osc_amplitude:.1f})")
    if rpt.fr_spd_osc_detected:
        L.append(f"  ⚠ 速度振荡检出: 周期≈{rpt.fr_spd_osc_period_ms:.0f}ms  "
                 f"幅度±{rpt.fr_spd_osc_amplitude:.0f} mm/s")

    # ===== FULL-RATE TRAJECTORY TRACKING =====
    L.append(f"\n【逐帧轨迹追踪】  (1enc≈{ENC_TO_MM}mm, 梯形积分)")
    L.append(f"  全程横偏(mm): max={rpt.fr_lat_max_mm:.2f}  "
             f"终点={rpt.fr_lat_final_mm:+.2f}  "
             f"漂移率={rpt.fr_lat_drift_rate_mm_s:+.3f} mm/s")

    # ===== CRUISE-ONLY LATERAL (核心: 去掉启动瞬态后的真实漂移) =====
    if rpt.fr_cruise_lat_max_mm > 0:
        drift_icon = "⚠" if abs(rpt.fr_cruise_lat_net_mm) > 15 else "✅"
        L.append(f"  ★ 巡航段横偏: {drift_icon}  "
                 f"net={rpt.fr_cruise_lat_net_mm:+.1f}mm  "
                 f"max={rpt.fr_cruise_lat_max_mm:.1f}mm  "
                 f"RMS={rpt.fr_cruise_lat_rms_mm:.1f}mm  "
                 f"漂移率={rpt.fr_cruise_lat_drift_rate_mm_s:+.3f}mm/s")
        if rpt.fr_cruise_lat_drift_dir:
            L.append(f"    漂移方向: {rpt.fr_cruise_lat_drift_dir}  "
                     f"方向一致性={rpt.fr_cruise_lat_direction_pct:.0f}%  "
                     f"(起={rpt.fr_cruise_lat_start_mm:+.1f} → 终={rpt.fr_cruise_lat_end_mm:+.1f}mm)")

    # ===== HEADING BIAS → LATERAL PROJECTION =====
    if abs(rpt.fr_yaw_bias_projected_mm) > 1:
        L.append(f"  ★ yaw偏置投影: {rpt.fr_yaw_bias_projected_mm:+.1f}mm  "
                 f"(yaw均偏→累积横偏, bias={rpt.fr_yaw_bias_dps:+.4f}°/s)")

    # ===== PHASE-BY-PHASE DRIFT (分阶段漂移趋势) =====
    if rpt.fr_phase_analysis:
        L.append(f"  分阶段漂移:")
        for ph in rpt.fr_phase_analysis:
            L.append(f"    {ph['name']:>4s} ({ph['t_start']:.0f}-{ph['t_end']:.0f}s): "
                     f"{ph['lat_start']:+6.1f}→{ph['lat_end']:+6.1f}mm  "
                     f"净偏={ph['net']:+.1f}mm  "
                     f"速率={ph['drift_rate']:+.2f}mm/s  {ph['dir']}")

    # ===== LATERAL JITTER (横向微抖动) =====
    if rpt.fr_lat_jitter_amp_mm > 0:
        jit_icon = "⚠" if rpt.fr_lat_jitter_amp_mm > 2.0 else "✅"
        L.append(f"  横向抖动: {jit_icon}  "
                 f"RMS={rpt.fr_lat_jitter_amp_mm:.2f}mm  "
                 f"max={rpt.fr_lat_jitter_max_mm:.2f}mm  "
                 f"频率={rpt.fr_lat_jitter_hz:.1f}Hz")

    L.append(f"  微偏移:  RMS={rpt.fr_lat_rms_mm:.2f}mm  "
             f"均值|偏|={rpt.fr_lat_mean_abs_mm:.2f}mm  "
             f"P90={rpt.fr_lat_p90_mm:.2f}mm")
    L.append(f"  偏移积分: {rpt.fr_lat_integral_mm_s:.2f} mm·s  "
             f"(每秒均偏: {rpt.fr_lat_integral_mm_s / max(rpt.duration_ms / 1000.0, 1):.2f}mm)")
    L.append(f"  去趋势漂移: wander_max={rpt.fr_lat_wander_mm:.2f}mm  "
             f"wander_RMS={rpt.fr_lat_wander_rms_mm:.2f}mm")
    # Per-meter breakdown (compact: every 5m summary + worst segment)
    if rpt.fr_lat_per_meter and len(rpt.fr_lat_per_meter) > 3:
        segs = rpt.fr_lat_per_meter
        total_m = segs[-1]['meter']
        L.append(f"  逐米横偏 ({total_m}m):")
        L.append(f"    {'区间':>8s} | {'|均偏|':>7s} | {'最大':>7s} | {'RMS':>7s} | {'终点':>8s}")
        L.append(f"    {'─'*8} | {'─'*7} | {'─'*7} | {'─'*7} | {'─'*8}")
        # Aggregate in 5-meter chunks
        chunk = 5
        i = 0
        while i < len(segs):
            chunk_segs = segs[i:i+chunk]
            m_start = chunk_segs[0]['meter']
            m_end = chunk_segs[-1]['meter']
            c_mean = sum(s['mean_abs'] for s in chunk_segs) / len(chunk_segs)
            c_max = max(s['max'] for s in chunk_segs)
            c_rms = math.sqrt(sum(s['rms']**2 for s in chunk_segs) / len(chunk_segs))
            c_final = chunk_segs[-1]['final']
            label = f"{m_start}-{m_end}m" if m_start != m_end else f"{m_start}m"
            L.append(f"    {label:>8s} | {c_mean:>7.1f} | {c_max:>7.1f} | {c_rms:>7.1f} | {c_final:>+8.1f}")
            i += chunk
        # Highlight worst segment
        worst = max(segs, key=lambda s: s['max'])
        L.append(f"    ★ 最差段: 第{worst['meter']}m  max={worst['max']:.1f}mm  RMS={worst['rms']:.1f}mm")
    if rpt.fr_timeline:
        hdr = (f"  {'t':>6s} | {'fwd':>7s} | {'lat':>8s} | {'lat_v':>7s} | "
               f"{'spd':>6s} | {'yaw':>7s} | {'yr':>6s} | "
               f"{'pc':>3s} | {'hd':>3s} | {'dp':>3s} | {'OL':>3s} | {'OR':>3s}")
        L.append(hdr)
        L.append(f"  {'─'*6} | {'─'*7} | {'─'*8} | {'─'*7} | "
                 f"{'─'*6} | {'─'*7} | {'─'*6} | "
                 f"{'─'*3} | {'─'*3} | {'─'*3} | {'─'*3} | {'─'*3}")
        for e in rpt.fr_timeline:
            L.append(f"  {e['t']:5.1f}s | {e['fwd']:>7.0f} | {e['lat']:>+8.2f} | "
                     f"{e['lat_v']:>+7.2f} | "
                     f"{e['spd']:>6.0f} | {e['yaw']:>+7.3f} | {e['yr']:>+6.2f} | "
                     f"{e['pc']:>+3d} | {e['hd']:>+3d} | {e['dp']:>+3d} | "
                     f"{e['ol']:>3d} | {e['or']:>3d}")

    # ===== PWM =====
    L.append(f"\n【PWM 输出】")
    L.append(f"  OL: {rpt.pwm_mean_ol:+.1f}±{rpt.pwm_std_ol:.1f}  "
             f"OR: {rpt.pwm_mean_or:+.1f}±{rpt.pwm_std_or:.1f}  "
             f"balance={rpt.pwm_balance_mean:+.1f}  sat={rpt.pwm_saturation_pct:.1f}%")

    # ===== SMOOTHNESS =====
    sev_icon = {"none": "✅", "mild": "⚠", "moderate": "🟠", "severe": "🔴"}
    L.append(f"\n【平滑度】  {sev_icon.get(rpt.jitter_severity,'?')} jitter={rpt.jitter_score:.0f}/100")
    L.append(f"  正转: {rpt.state_forward_pct:.1f}%  停止: {rpt.state_idle_pct:.1f}%  "
             f"反转: {rpt.state_reverse_pct:.1f}%")
    L.append(f"  pc slew: max={rpt.max_pc_slew} mean={rpt.mean_pc_slew:.1f}  "
             f"OL slew max={rpt.max_ol_slew}")

    # ===== 500ms WINDOWS =====
    if rpt.windows:
        L.append(f"\n【窗口分析】")
        hdr = (f"  {'Time':>5s} | {'State':>6s} | {'spd':>5s} | {'s_err':>5s} | "
               f"{'pc':>5s} | {'p_std':>5s} | {'yaw':>6s} | {'y_dft':>6s} | "
               f"{'hd':>4s} | {'dp':>3s} | {'OL':>4s} | {'OR':>4s}")
        L.append(hdr)
        L.append(f"  {'-'*5} | {'-'*6} | {'-'*5} | {'-'*5} | "
                 f"{'-'*5} | {'-'*5} | {'-'*6} | {'-'*6} | "
                 f"{'-'*4} | {'-'*3} | {'-'*4} | {'-'*4}")
        for w in rpt.windows:
            L.append(f"  {w.t_start_ms/1000:4.1f}s | {w.motion_state:>6.6s} | "
                     f"{w.speed_mean:>5.1f} | {w.speed_err:>+5.1f} | "
                     f"{w.pc_mean:>+5.1f} | {w.pc_std:>5.1f} | "
                     f"{w.yaw_mean:>+6.1f} | {w.yaw_drift:>+6.2f} | "
                     f"{w.hd_mean:>+4.1f} | {w.dp_mean:>+3.0f} | "
                     f"{w.ol_mean:>4.1f} | {w.or_mean:>4.1f}")

    # ===== SCORE =====
    L.append(f"\n{'━' * W}")
    L.append(f"【综合评分】  {rpt.straightness_score:.1f} / 100  (strict v8){seq_tag}")
    L.append(f"  速度({rpt.score_speed:.2f}/25)  航向({rpt.score_heading:.2f}/30)  "
             f"轨迹({rpt.score_trajectory:.2f}/25)  平滑({rpt.score_smoothness:.2f}/20)")

    # ===== RECOMMENDATIONS =====
    L.append(f"\n【诊断建议】")
    for r in rpt.recommendations:
        L.append(f"  {r}")

    if rpt.pid_suggestion and rpt.pid_suggestion != params:
        L.append(f"\n【建议参数】")
        for k in ("skp", "ski", "skd", "akp", "aki", "akd"):
            old = params.get(k, 0)
            new = rpt.pid_suggestion.get(k, old)
            if old != new:
                L.append(f"  {k.upper()}: {old} → {new}")

    L.append("=" * W)
    return "\n".join(L)


# ---------------------------------------------------------------------------
# Save results
# ---------------------------------------------------------------------------

def save_results(result: CaptureResult, rpt: StraightLineReport,
                 output_dir: Path, tag: str, seq: int = 0) -> dict[str, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    seq_prefix = f"T{seq:03d}_" if seq else ""
    prefix = f"{seq_prefix}tune_{tag}_{stamp}" if tag else f"{seq_prefix}tune_{stamp}"

    # Raw HB lines
    raw_path = output_dir / f"{prefix}_raw.txt"
    raw_path.write_text("\n".join(result.raw_lines), encoding="utf-8")

    # JSON report
    json_data = {
        "params": result.params,
        "stat": result.stat_response,
        "analysis": {
            k: v for k, v in asdict(rpt).items()
            if k not in ("recommendations", "pid_suggestion")
        },
        "recommendations": rpt.recommendations,
        "pid_suggestion": rpt.pid_suggestion,
    }
    json_path = output_dir / f"{prefix}_report.json"
    json_path.write_text(json.dumps(json_data, ensure_ascii=False, indent=2, default=str),
                         encoding="utf-8")

    # HB CSV
    csv_path = output_dir / f"{prefix}_hb.csv"
    with csv_path.open("w", encoding="utf-8") as f:
        f.write("t_ms,mode,run,el,er,yaw,yr,pc,hd,dp,OL,OR,sb,lp\n")
        for r in result.records:
            f.write(f"{r.t_ms},{r.mode},{r.run},{r.el},{r.er},"
                    f"{r.yaw:.1f},{r.yr:.1f},{r.pc},{r.hd},{r.dp},"
                    f"{r.ol},{r.or_},{r.sb},{r.lp:.2f}\n")

    return {"raw": raw_path, "json": json_path, "csv": csv_path}


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def load_defaults(config_path: Path) -> dict[str, Any]:
    """Load defaults from config.yaml."""
    if not config_path.exists():
        return {}
    with config_path.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}
    straight = (cfg.get("commands") or {}).get("straight") or {}
    serial_cfg = cfg.get("serial") or {}
    defaults = {
        "port": serial_cfg.get("port", "COM18"),
        "baudrate": serial_cfg.get("baudrate", 115200),
        "duration": serial_cfg.get("duration_s", 10.0),
        "target_speed": straight.get("target_speed", 8.0),
    }
    spid = straight.get("speed_pid", [0.3, 0.0, 0.0])
    if isinstance(spid, list) and len(spid) == 3:
        defaults["skp"] = spid[0]
        defaults["ski"] = spid[1]
        defaults["skd"] = spid[2]
    apid = straight.get("angle_pid", [0.25, 0.0, 0.08])
    if isinstance(apid, list) and len(apid) == 3:
        defaults["akp"] = apid[0]
        defaults["aki"] = apid[1]
        defaults["akd"] = apid[2]
    defaults["sff"] = straight.get("feedforward_gain", 1.0)
    return defaults


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="直线 PID 调参 & 深度分析工具")
    p.add_argument("--port", help="串口 (e.g. COM18)")
    p.add_argument("--baudrate", type=int, default=None)
    p.add_argument("--duration", type=float, help="采集秒数")
    p.add_argument("--spd", type=float, help="目标速度")

    # PID overrides
    p.add_argument("--skp", type=float)
    p.add_argument("--ski", type=float)
    p.add_argument("--skd", type=float)
    p.add_argument("--akp", type=float)
    p.add_argument("--aki", type=float)
    p.add_argument("--akd", type=float)
    p.add_argument("--sff", type=float, help="Speed feedforward gain")

    p.add_argument("--tag", default="", help="输出文件标签")
    p.add_argument("--output-dir", default=None)
    p.add_argument("--config", default=str(Path(__file__).with_name("config.yaml")))
    p.add_argument("--quiet", action="store_true")

    # Sweep mode
    p.add_argument("--sweep", nargs="+", help="扫描参数: <param> <v1> <v2> ...")

    return p


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    defaults = load_defaults(config_path)

    # Merge defaults with CLI overrides
    port = args.port or defaults.get("port", "COM18")
    baudrate = args.baudrate or defaults.get("baudrate", 115200)
    duration = args.duration or defaults.get("duration", 10.0)
    target_speed = args.spd or defaults.get("target_speed", 1.0)

    pid_params: dict[str, float] = {}
    for k in ("skp", "ski", "skd", "akp", "aki", "akd", "sff"):
        cli_val = getattr(args, k, None)
        if cli_val is not None:
            pid_params[k] = cli_val
        elif k in defaults:
            pid_params[k] = defaults[k]

    output_dir = Path(args.output_dir) if args.output_dir else (
        config_path.parent.parent / "000Data" / "pid_tuning"
    )

    # Sweep mode?
    if args.sweep and len(args.sweep) >= 2:
        sweep_param = args.sweep[0].lower()
        sweep_values = [float(v) for v in args.sweep[1:]]
        print(f"=== 扫描模式: {sweep_param.upper()} = {sweep_values} ===\n")
        all_reports = []
        for val in sweep_values:
            pid_params[sweep_param] = val
            tag = f"{sweep_param}{val}"
            print(f"\n--- {sweep_param.upper()} = {val} ---")
            result = run_capture(port, baudrate, duration, pid_params, target_speed,
                                print_live=not args.quiet)
            rpt = analyze_straight(result.records, {**pid_params, "target_speed": target_speed})
            print(format_report(rpt, {**pid_params, "target_speed": target_speed}))
            paths = save_results(result, rpt, output_dir, tag)
            all_reports.append((val, rpt))
            print(f"  保存: {paths['json']}")

        # Summary
        print("\n" + "=" * 60)
        print("  扫描结果汇总")
        print("=" * 60)
        print(f"  {'Value':>8s} | {'Score':>6s} | {'YawDrift':>10s} | {'hdSat%':>7s} | {'pcStd':>6s}")
        print(f"  {'-'*8} | {'-'*6} | {'-'*10} | {'-'*7} | {'-'*6}")
        best_val, best_score = None, -1
        for val, rpt in all_reports:
            print(f"  {val:>8.4f} | {rpt.straightness_score:>6.0f} | "
                  f"{rpt.heading_yaw_drift_rate_dps:>+10.3f} | "
                  f"{rpt.heading_hd_saturation_pct:>7.1f} | "
                  f"{rpt.speed_std_pc:>6.1f}")
            if rpt.straightness_score > best_score:
                best_score = rpt.straightness_score
                best_val = val
        print(f"\n  最佳: {sweep_param.upper()} = {best_val} (score={best_score:.0f})")
        return

    # Single run mode
    seq = _next_seq(output_dir)
    print(f"=== 直线 PID 调参  [T{seq:03d}] ===")
    print(f"  串口: {port}  速率: {baudrate}  采集: {duration}s")
    print(f"  PID: SKP={pid_params.get('skp','?')} SKI={pid_params.get('ski','?')} SKD={pid_params.get('skd','?')}")
    print(f"       AKP={pid_params.get('akp','?')} AKI={pid_params.get('aki','?')} AKD={pid_params.get('akd','?')}")
    if 'sff' in pid_params:
        print(f"  前馈: SFF={pid_params['sff']}")
    print(f"  目标速度: {target_speed}\n")

    result = run_capture(port, baudrate, duration, pid_params, target_speed,
                        print_live=not args.quiet)

    rpt = analyze_straight(result.records, {**pid_params, "target_speed": target_speed})
    print("\n" + format_report(rpt, {**pid_params, "target_speed": target_speed}, seq=seq))

    paths = save_results(result, rpt, output_dir, args.tag, seq=seq)
    print(f"\n文件保存 [T{seq:03d}]:")
    for k, v in paths.items():
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()
