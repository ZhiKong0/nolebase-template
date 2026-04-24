#!/usr/bin/env python3
"""
track_adaptive_tuner.py
=======================

自适应式 TRACK 在线调参：
1. 通过串口 `#TCFG` / `#RUN!` / `#STOP!` 直接下发参数。
2. 用阶段化坐标搜索替代全排列网格，优先减少试验次数。
3. 评分重点放在“中心占比、左右摆动、丢线/找线比例”，目标是尽量把车钳在 12 路中心 A6/A7。
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

import serial
import yaml


PROJECT_ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = PROJECT_ROOT / "000Project_PC_Control" / "config.yaml"
OUTPUT_ROOT = PROJECT_ROOT / "000Data" / "track_adaptive_tune"

LT_MASK_CENTER = 0x0060
LT_MASK_OUTER = 0x0F0F
LT_CENTER_CORE_STATES = (0x0020, 0x0040, 0x0060)

DEFAULT_SCORE_WEIGHTS = {
    "mean_forward_speed": 0.18,
    "center_band_ratio": 24.0,
    "exact_center_ratio": 18.0,
    "center_core_ratio": 165.0,
    "center_single_ratio": -32.0,
    "straight_ratio": 0.0,
    "scurve_ratio": 12.0,
    "outer_ratio": -165.0,
    "corner_ratio": -150.0,
    "search_ratio": -240.0,
    "trim_ratio": -140.0,
    "loss_ratio": -160.0,
    "mean_abs_lp": -0.22,
    "mean_abs_lp_delta": -0.70,
    "center_mean_abs_lp": -1.35,
    "center_mean_abs_lp_delta": -3.90,
    "lp_flip_rate_hz": -16.0,
    "center_flip_rate_hz": -76.0,
    "scurve_center_band_ratio": 110.0,
    "scurve_center_core_ratio": 210.0,
    "scurve_outer_ratio": -240.0,
    "scurve_mean_abs_lp": -1.80,
    "scurve_mean_abs_lp_delta": -5.60,
    "scurve_flip_rate_hz": -100.0,
    "edge_recovery_success_ratio": 85.0,
    "edge_recovery_mean_s": -55.0,
    "search_recovery_success_ratio": 125.0,
    "search_recovery_mean_s": -70.0,
}


@dataclass
class ParamSpec:
    name: str
    key: str
    start: float
    bounds: tuple[float, float]
    steps: list[float]
    digits: int


@dataclass
class PhaseSpec:
    name: str
    params: list[str]
    repeats: int
    confirm_best_repeats: int
    min_improve: float


@dataclass
class TrackRecord:
    t_ms: int
    mode: str
    run: int
    yaw: float
    el: int
    er: int
    yr: float
    ol: int
    or_: int
    sb: int
    lp: float
    st: str
    sc: int
    tf: int
    gs: int
    rt: int


@dataclass
class EarlyStopConfig:
    enabled: bool
    min_runtime_s: float
    max_loss_streak: int
    max_search_streak: int
    max_stall_streak: int
    stall_speed_threshold: float


@dataclass
class TurnaroundConfig:
    enabled: bool
    timeout_s: float
    settle_s: float
    retries: int


@dataclass
class TrialResult:
    trial_index: int
    stage: int
    source: str
    params_json: str
    raw_path: str
    sample_count: int
    duration_s: float
    mean_forward_speed: float
    mean_abs_lp: float
    mean_abs_lp_delta: float
    mean_abs_yr: float
    mean_abs_yr_delta: float
    center_band_ratio: float
    exact_center_ratio: float
    center_core_ratio: float
    center_single_ratio: float
    center_mean_abs_lp: float
    center_mean_abs_lp_delta: float
    center_mean_abs_yr: float
    center_mean_abs_yr_delta: float
    straight_ratio: float
    outer_ratio: float
    corner_ratio: float
    search_ratio: float
    scurve_ratio: float
    trim_ratio: float
    loss_ratio: float
    lp_flip_rate_hz: float
    center_flip_rate_hz: float
    scurve_center_band_ratio: float
    scurve_center_core_ratio: float
    scurve_outer_ratio: float
    scurve_mean_abs_lp: float
    scurve_mean_abs_lp_delta: float
    scurve_flip_rate_hz: float
    edge_event_count: int
    edge_recovery_success_ratio: float
    edge_recovery_mean_s: float
    search_event_count: int
    search_recovery_success_ratio: float
    search_recovery_mean_s: float
    score: float
    stopped_early: int
    stop_reason: str
    stop_elapsed_s: float


@dataclass
class SearchContext:
    port: str
    baud: int
    duration_s: float
    settle_skip_s: float
    manual_reset: bool
    early_stop: EarlyStopConfig
    turnaround: TurnaroundConfig
    out_dir: Path
    param_specs: list[ParamSpec]
    score_weights: dict[str, float]
    trial_index: int = 0
    trials_by_key: dict[tuple[float, ...], list[TrialResult]] = field(default_factory=dict)
    all_trials: list[TrialResult] = field(default_factory=list)


def robust_center(values: list[float]) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]
    return statistics.median(values)


def load_score_weights(cfg: dict[str, Any]) -> dict[str, float]:
    adaptive_cfg = cfg_get(cfg, "autotune", "track_adaptive", default={}) or {}
    score_cfg = adaptive_cfg.get("score", {}) or {}
    weights = dict(DEFAULT_SCORE_WEIGHTS)
    for key, value in (score_cfg.get("weights", {}) or {}).items():
        try:
            weights[str(key)] = float(value)
        except (TypeError, ValueError):
            continue
    return weights


def empty_metrics(sample_count: float, duration_s: float) -> dict[str, float]:
    return {
        "sample_count": sample_count,
        "duration_s": duration_s,
        "mean_forward_speed": 0.0,
        "mean_abs_lp": 999.0,
        "mean_abs_lp_delta": 999.0,
        "mean_abs_yr": 999.0,
        "mean_abs_yr_delta": 999.0,
        "center_band_ratio": 0.0,
        "exact_center_ratio": 0.0,
        "center_core_ratio": 0.0,
        "center_single_ratio": 0.0,
        "center_mean_abs_lp": 350.0,
        "center_mean_abs_lp_delta": 350.0,
        "center_mean_abs_yr": 999.0,
        "center_mean_abs_yr_delta": 999.0,
        "straight_ratio": 0.0,
        "outer_ratio": 1.0,
        "corner_ratio": 1.0,
        "search_ratio": 1.0,
        "scurve_ratio": 0.0,
        "trim_ratio": 1.0,
        "loss_ratio": 1.0,
        "lp_flip_rate_hz": 0.0,
        "center_flip_rate_hz": 0.0,
        "scurve_center_band_ratio": 0.0,
        "scurve_center_core_ratio": 0.0,
        "scurve_outer_ratio": 1.0,
        "scurve_mean_abs_lp": 350.0,
        "scurve_mean_abs_lp_delta": 350.0,
        "scurve_flip_rate_hz": 0.0,
        "edge_event_count": 0.0,
        "edge_recovery_success_ratio": 0.0,
        "edge_recovery_mean_s": 2.0,
        "search_event_count": 0.0,
        "search_recovery_success_ratio": 0.0,
        "search_recovery_mean_s": 2.0,
        "score": -9999.0,
    }


def score_metrics(metrics: dict[str, float], weights: dict[str, float]) -> float:
    score = 0.0
    for key, weight in weights.items():
        if key in metrics:
            score += float(weight) * float(metrics[key])
    return score


def load_config(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle) or {}
    return data if isinstance(data, dict) else {}


def cfg_get(cfg: dict[str, Any], *keys: str, default: Any = None) -> Any:
    cur: Any = cfg
    for key in keys:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur


def decode_serial_text(raw: bytes) -> str:
    line = raw.decode("utf-8", errors="ignore")
    line = "".join(ch for ch in line if ch.isprintable())
    return line.strip()


def flush_serial(port: serial.Serial, wait_ms: int = 120) -> None:
    port.reset_input_buffer()
    time.sleep(wait_ms / 1000.0)
    while port.in_waiting > 0:
        port.read(port.in_waiting)
        time.sleep(0.02)


def send_cmd(port: serial.Serial, cmd: str, timeout_s: float = 1.5, retries: int = 2) -> list[str]:
    payload = cmd if cmd.endswith("!") else f"{cmd}!"
    valid_prefixes = ("OK:", "ERR:", "STAT:", "HB:")
    last_lines: list[str] = []

    for attempt in range(retries + 1):
        flush_serial(port, wait_ms=80 if attempt == 0 else 120)
        port.write(payload.encode("ascii"))
        port.flush()

        deadline = time.time() + timeout_s
        lines: list[str] = []
        while time.time() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = decode_serial_text(raw)
            if not line or not line.startswith(valid_prefixes):
                continue
            if line.startswith("HB:"):
                continue
            lines.append(line)
            if line.startswith("OK:") or line.startswith("ERR:") or line.startswith("STAT:"):
                return lines
        last_lines = lines
        if attempt < retries:
            time.sleep(0.05)

    return last_lines


def ensure_ok(cmd: str, lines: list[str]) -> None:
    if any(line.startswith("ERR") for line in lines):
        raise RuntimeError(f"{cmd} failed: {lines}")
    if not any(line.startswith("OK") or line.startswith("STAT:") for line in lines):
        raise RuntimeError(f"{cmd} no ack: {lines}")


def stat_reports_stop(lines: list[str]) -> bool:
    return any(line.startswith("STAT:") and "state=STOP" in line for line in lines)


def wait_for_stop_signal(port: serial.Serial, timeout_s: float = 0.8) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = decode_serial_text(raw)
        if not line:
            continue
        if line.startswith("OK:STOP") or line.startswith("EVT:EXP_STOP"):
            return True
        if line.startswith("STAT:") and "state=STOP" in line:
            return True
        if line.startswith("HB:") and ",run=0," in line:
            return True
    return False


def ensure_stop(port: serial.Serial, attempts: int = 3, timeout_s: float = 2.0) -> None:
    last_error = "STOP no ack: []"

    for attempt in range(attempts):
        if attempt == 0:
            flush_serial(port, wait_ms=60)
        port.write(b"#STOP!")
        port.flush()
        if wait_for_stop_signal(port, timeout_s=timeout_s):
            return

        stat_lines = send_cmd(port, "#STAT!", timeout_s=1.2, retries=0)
        if stat_reports_stop(stat_lines):
            return

        last_error = f"STOP no ack: {stat_lines}"
        if attempt + 1 < attempts:
            print(f"[adaptive] stop retry {attempt + 1}/{attempts - 1}", flush=True)
            time.sleep(0.20)

    raise RuntimeError(last_error)


def load_defaults_best_effort(port: serial.Serial) -> None:
    lines = send_cmd(port, "#TCFG LOAD_DEFAULTS!", timeout_s=2.0, retries=0)
    if any(line.startswith("OK") for line in lines):
        return
    if any(line.startswith("ERR") for line in lines):
        raise RuntimeError(f"#TCFG LOAD_DEFAULTS! failed: {lines}")
    print("[adaptive] warn: LOAD_DEFAULTS no ack, continue with explicit param writes", flush=True)


def parse_hb_line(line: str) -> TrackRecord | None:
    if not line.startswith("HB:"):
        return None
    kv: dict[str, str] = {}
    for pair in line[3:].split(","):
        if "=" not in pair:
            continue
        key, value = pair.split("=", 1)
        kv[key.strip()] = value.strip()

    try:
        return TrackRecord(
            t_ms=int(kv.get("t", "0")),
            mode=kv.get("m", "S"),
            run=int(kv.get("run", "0")),
            yaw=float(kv.get("yaw", "0")),
            el=int(kv.get("el", "0")),
            er=int(kv.get("er", "0")),
            yr=float(kv.get("yr", "0")),
            ol=int(kv.get("OL", "0")),
            or_=int(kv.get("OR", "0")),
            sb=int(kv.get("sb", "0")),
            lp=float(kv.get("lp", "0")),
            st=kv.get("st", "TRK"),
            sc=int(kv.get("sc", "-1")),
            tf=int(kv.get("tf", "0")),
            gs=int(kv.get("gs", "0")),
            rt=int(kv.get("rt", "0")),
        )
    except (TypeError, ValueError):
        return None


def dedupe_records(records: list[TrackRecord]) -> list[TrackRecord]:
    latest: dict[int, TrackRecord] = {}
    for rec in records:
        if rec.mode != "T" or rec.run != 1:
            continue
        latest[rec.t_ms] = rec
    return [latest[key] for key in sorted(latest.keys())]


def sign_changes(values: list[float]) -> int:
    prev = 0
    flips = 0
    for value in values:
        sign = 1 if value > 0 else -1 if value < 0 else 0
        if sign == 0:
            continue
        if prev != 0 and sign != prev:
            flips += 1
        prev = sign
    return flips


def sign_changes_with_deadband(values: list[float], deadband: float) -> int:
    prev = 0
    flips = 0
    for value in values:
        if abs(value) <= deadband:
            continue
        sign = 1 if value > 0 else -1
        if prev != 0 and sign != prev:
            flips += 1
        prev = sign
    return flips


def mean_abs_delta(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    return statistics.fmean(abs(cur - prev) for prev, cur in zip(values, values[1:]))


def wrap_angle_deg(angle: float) -> float:
    while angle > 180.0:
        angle -= 360.0
    while angle < -180.0:
        angle += 360.0
    return angle


def angle_delta_deg(current: float, reference: float) -> float:
    return wrap_angle_deg(current - reference)


def is_search_state(state: str) -> bool:
    state = state.upper()
    return state.startswith("FND")


def is_trim_state(state: str) -> bool:
    return state.upper().startswith("TRM")


def is_cross_state(state: str) -> bool:
    return state.upper() == "CROSS"


def is_corner_state(state: str) -> bool:
    return state.upper() in {"CORN", "EDGE"}


def is_skip_state(state: str) -> bool:
    return is_search_state(state) or is_cross_state(state) or is_trim_state(state)


def is_scorable_state(state: str) -> bool:
    return not is_skip_state(state)


def is_follow_eval_record(rec: TrackRecord) -> bool:
    return not is_skip_state(rec.st)


def is_center_eval_record(rec: TrackRecord) -> bool:
    if rec.sc in (0, 1):
        return rec.sc == 1
    return (not is_skip_state(rec.st)) and (not is_corner_state(rec.st))


def has_center_band(bits: int) -> bool:
    return ((bits & LT_MASK_CENTER) != 0) and ((bits & LT_MASK_OUTER) == 0)


def has_center_core(bits: int) -> bool:
    return bits in LT_CENTER_CORE_STATES


def collect_recovery_metrics(records: list[TrackRecord],
                             start_predicate,
                             recovered_predicate,
                             timeout_s: float) -> tuple[int, float, float]:
    if not records:
        return 0, 1.0, 0.0

    total = 0
    success = 0
    durations: list[float] = []
    active = False
    active_start_ms = 0
    prev_start = False
    timeout_ms = int(timeout_s * 1000.0)

    for rec in records:
        current_start = bool(start_predicate(rec))
        if (not active) and current_start and (not prev_start):
            active = True
            active_start_ms = rec.t_ms
            total += 1

        if active:
            if recovered_predicate(rec):
                durations.append(max(0.0, (rec.t_ms - active_start_ms) / 1000.0))
                success += 1
                active = False
            elif timeout_ms > 0 and (rec.t_ms - active_start_ms) >= timeout_ms:
                active = False

        prev_start = current_start

    if total == 0:
        return 0, 1.0, 0.0

    return (
        total,
        success / float(total),
        statistics.fmean(durations) if durations else timeout_s,
    )


def clamp_value(value: float, bounds: tuple[float, float], digits: int) -> float:
    lo, hi = bounds
    if value < lo:
        value = lo
    if value > hi:
        value = hi
    return round(value, digits)


def make_candidate_key(params: dict[str, float], specs: list[ParamSpec]) -> tuple[float, ...]:
    values: list[float] = []
    for spec in specs:
        values.append(round(float(params[spec.name]), spec.digits))
    return tuple(values)


def candidate_label(params: dict[str, float], specs: list[ParamSpec]) -> str:
    return " ".join(f"{spec.name}={params[spec.name]:.{spec.digits}f}" for spec in specs)


def serialize_params(params: dict[str, float], specs: list[ParamSpec]) -> str:
    payload = {spec.name: round(float(params[spec.name]), spec.digits) for spec in specs}
    return json.dumps(payload, ensure_ascii=False, sort_keys=True)


def build_set_command(spec: ParamSpec, value: float) -> str:
    match = re.fullmatch(r"track\.sensor_scale([1-9]|1[0-2])", spec.key)
    if match:
        milli = int(round(value * 1000.0))
        return f"#TS{match.group(1)}={milli}!"
    short_map = {
        "track.speed_target": "#SPD",
        "track.lkp": "#LKP",
        "track.lkd": "#LKD",
        "track.dev_ratio": "#TDR",
        "track.static_bias": "#STB",
        "track.deadband": "#TDB",
        "track.pos_lpf": "#PLF",
        "track.d_lpf": "#DLF",
        "track.recover_ticks": "#RCT",
        "track.search_turn_fast": "#STF",
        "track.search_turn_slow": "#STS",
        "track.search_timeout": "#STO",
    }
    prefix = short_map.get(spec.key)
    if prefix:
        return f"{prefix}={value:.3f}!"
    return f"#TCFG SET {spec.key} {value:.6f}!"


def build_get_command(spec: ParamSpec) -> str:
    match = re.fullmatch(r"track\.sensor_scale([1-9]|1[0-2])", spec.key)
    if match:
        return f"#TS{match.group(1)}?!"
    short_map = {
        "track.speed_target": "#SPD",
        "track.lkp": "#LKP",
        "track.lkd": "#LKD",
        "track.dev_ratio": "#TDR",
        "track.static_bias": "#STB",
        "track.deadband": "#TDB",
        "track.pos_lpf": "#PLF",
        "track.d_lpf": "#DLF",
        "track.recover_ticks": "#RCT",
        "track.search_turn_fast": "#STF",
        "track.search_turn_slow": "#STS",
        "track.search_timeout": "#STO",
    }
    prefix = short_map.get(spec.key)
    if prefix:
        return f"{prefix}?!"
    return f"#TCFG GET {spec.key}!"


def parse_reply_value(spec: ParamSpec, lines: list[str]) -> float | None:
    for line in reversed(lines):
        if "=" not in line:
            continue
        payload = line.split("=", 1)[1].replace("!", ".")
        match = re.search(r"[-+]?\d+(?:\.\d+)?", payload)
        if not match:
            continue
        value = float(match.group(0))
        if re.fullmatch(r"track\.sensor_scale([1-9]|1[0-2])", spec.key) and abs(value) > 10.0:
            value *= 0.001
        return value
    return None


def verify_param_write(port: serial.Serial, spec: ParamSpec, expected: float) -> bool:
    tolerance = max(0.001, 1.5 * (10.0 ** (-spec.digits)))
    query = build_get_command(spec)
    for _ in range(3):
        lines = send_cmd(port, query, timeout_s=1.5, retries=0)
        actual = parse_reply_value(spec, lines)
        if actual is not None and abs(actual - expected) <= tolerance:
            return True
        time.sleep(0.05)
    return False


def apply_param_with_verify(port: serial.Serial, spec: ParamSpec, value: float, attempts: int = 3) -> None:
    cmd = build_set_command(spec, value)
    last_error = f"{cmd} no ack: []"

    for attempt in range(attempts):
        lines = send_cmd(port, cmd, timeout_s=2.0, retries=0)
        if any(line.startswith("ERR") for line in lines):
            last_error = f"{cmd} failed: {lines}"
        else:
            if any(line.startswith("OK") or line.startswith("STAT:") for line in lines):
                return
            if verify_param_write(port, spec, value):
                return
            last_error = f"{cmd} no ack and verify failed"

        if attempt + 1 < attempts:
            print(f"[adaptive] param retry {attempt + 1}/{attempts - 1}: {spec.name}", flush=True)
            time.sleep(0.08)

    raise RuntimeError(last_error)


def analyze_trial(records: list[TrackRecord],
                  settle_skip_s: float,
                  score_weights: dict[str, float]) -> dict[str, float]:
    unique = dedupe_records(records)
    if len(unique) < 8:
        return empty_metrics(float(len(unique)), 0.0)

    start_ms = unique[0].t_ms
    analyzed = [
        rec for rec in unique
        if (rec.t_ms - start_ms) >= int(settle_skip_s * 1000.0)
    ]
    if len(analyzed) < 8:
        analyzed = unique

    first_follow_idx = next((idx for idx, rec in enumerate(analyzed) if is_follow_eval_record(rec)), None)
    if first_follow_idx is not None:
        analyzed = analyzed[first_follow_idx:]

    duration_s = max(0.01, (analyzed[-1].t_ms - analyzed[0].t_ms) / 1000.0)
    follow_records = [rec for rec in analyzed if is_follow_eval_record(rec)]
    if len(follow_records) < 8:
        return empty_metrics(float(len(follow_records)), duration_s)

    forward_speeds = [max(0.0, (rec.el + rec.er) * 0.5) for rec in follow_records]
    valid = [rec for rec in follow_records if rec.sb != 0]
    valid_lp = [abs(rec.lp) for rec in valid]
    valid_lp_signed = [rec.lp for rec in valid]
    center_records = [rec for rec in follow_records if is_center_eval_record(rec)]
    center_valid = [rec for rec in center_records if rec.sb != 0]
    scurve_records = [rec for rec in follow_records if rec.st.upper() == "SCRV"]
    scurve_valid = [rec for rec in scurve_records if rec.sb != 0]

    center_band_ratio = (
        sum(1 for rec in center_valid if (rec.sb & LT_MASK_CENTER) and (rec.sb & LT_MASK_OUTER) == 0) / len(center_valid)
        if center_valid else 0.0
    )
    exact_center_ratio = (
        sum(1 for rec in center_valid if rec.sb == LT_MASK_CENTER) / len(center_valid)
        if center_valid else 0.0
    )
    center_core_ratio = (
        sum(1 for rec in center_valid if rec.sb in LT_CENTER_CORE_STATES) / len(center_valid)
        if center_valid else 0.0
    )
    center_single_ratio = (
        sum(1 for rec in center_valid if rec.sb in (0x08, 0x10)) / len(center_valid)
        if center_valid else 0.0
    )
    scurve_center_band_ratio = (
        sum(1 for rec in scurve_valid if (rec.sb & LT_MASK_CENTER) and (rec.sb & LT_MASK_OUTER) == 0) / len(scurve_valid)
        if scurve_valid else 0.0
    )
    scurve_center_core_ratio = (
        sum(1 for rec in scurve_valid if rec.sb in LT_CENTER_CORE_STATES) / len(scurve_valid)
        if scurve_valid else 0.0
    )
    outer_ratio = (
        sum(1 for rec in valid if (rec.sb & LT_MASK_OUTER) != 0) / len(valid)
        if valid else 1.0
    )
    scurve_outer_ratio = (
        sum(1 for rec in scurve_valid if (rec.sb & LT_MASK_OUTER) != 0) / len(scurve_valid)
        if scurve_valid else 1.0
    )
    corner_ratio = sum(1 for rec in follow_records if is_corner_state(rec.st)) / len(follow_records)
    search_ratio = sum(1 for rec in analyzed if is_search_state(rec.st)) / len(analyzed)
    scurve_ratio = sum(1 for rec in follow_records if rec.st.upper() == "SCRV") / len(follow_records)
    trim_ratio = sum(1 for rec in analyzed if is_trim_state(rec.st)) / len(analyzed)
    straight_ratio = sum(1 for rec in follow_records if rec.st.upper() == "STRA") / len(follow_records)
    loss_ratio = sum(1 for rec in follow_records if rec.sb == 0) / len(follow_records)
    mean_forward_speed = statistics.fmean(forward_speeds)
    mean_abs_lp = statistics.fmean(valid_lp) if valid_lp else 350.0
    mean_abs_lp_delta = mean_abs_delta(valid_lp_signed) if valid_lp_signed else 350.0
    yr_values = [rec.yr for rec in follow_records]
    mean_abs_yr = statistics.fmean(abs(value) for value in yr_values)
    mean_abs_yr_delta = mean_abs_delta(yr_values)
    lp_flip_rate_hz = sign_changes_with_deadband(valid_lp_signed, 80.0) / duration_s if valid_lp_signed else 0.0
    center_focus = [
        rec for rec in center_valid
        if ((rec.sb & LT_MASK_CENTER) != 0) or abs(rec.lp) <= 70.0
    ]
    center_focus_lp = [abs(rec.lp) for rec in center_focus]
    center_focus_lp_signed = [rec.lp for rec in center_focus]
    center_mean_abs_lp = statistics.fmean(center_focus_lp) if center_focus_lp else 350.0
    center_mean_abs_lp_delta = mean_abs_delta(center_focus_lp_signed) if center_focus_lp_signed else 350.0
    center_focus_yr = [rec.yr for rec in center_focus]
    center_mean_abs_yr = statistics.fmean(abs(value) for value in center_focus_yr) if center_focus_yr else 999.0
    center_mean_abs_yr_delta = mean_abs_delta(center_focus_yr) if center_focus_yr else 999.0
    center_flip_rate_hz = (
        sign_changes_with_deadband(center_focus_lp_signed, 12.0) / duration_s
        if center_focus_lp_signed else 0.0
    )
    scurve_lp = [abs(rec.lp) for rec in scurve_valid]
    scurve_lp_signed = [rec.lp for rec in scurve_valid]
    scurve_mean_abs_lp = statistics.fmean(scurve_lp) if scurve_lp else 350.0
    scurve_mean_abs_lp_delta = mean_abs_delta(scurve_lp_signed) if scurve_lp_signed else 350.0
    scurve_flip_rate_hz = (
        sign_changes_with_deadband(scurve_lp_signed, 18.0) / duration_s
        if scurve_lp_signed else 0.0
    )
    edge_event_count, edge_recovery_success_ratio, edge_recovery_mean_s = collect_recovery_metrics(
        analyzed,
        lambda rec: rec.st.upper() == "EDGE",
        lambda rec: has_center_core(rec.sb),
        1.40,
    )
    search_event_count, search_recovery_success_ratio, search_recovery_mean_s = collect_recovery_metrics(
        analyzed,
        lambda rec: is_search_state(rec.st),
        lambda rec: has_center_core(rec.sb),
        2.10,
    )

    metrics = {
        "sample_count": float(len(analyzed)),
        "duration_s": duration_s,
        "mean_forward_speed": mean_forward_speed,
        "mean_abs_lp": mean_abs_lp,
        "mean_abs_lp_delta": mean_abs_lp_delta,
        "mean_abs_yr": mean_abs_yr,
        "mean_abs_yr_delta": mean_abs_yr_delta,
        "center_band_ratio": center_band_ratio,
        "exact_center_ratio": exact_center_ratio,
        "center_core_ratio": center_core_ratio,
        "center_single_ratio": center_single_ratio,
        "center_mean_abs_lp": center_mean_abs_lp,
        "center_mean_abs_lp_delta": center_mean_abs_lp_delta,
        "center_mean_abs_yr": center_mean_abs_yr,
        "center_mean_abs_yr_delta": center_mean_abs_yr_delta,
        "straight_ratio": straight_ratio,
        "outer_ratio": outer_ratio,
        "corner_ratio": corner_ratio,
        "search_ratio": search_ratio,
        "scurve_ratio": scurve_ratio,
        "trim_ratio": trim_ratio,
        "loss_ratio": loss_ratio,
        "lp_flip_rate_hz": lp_flip_rate_hz,
        "center_flip_rate_hz": center_flip_rate_hz,
        "scurve_center_band_ratio": scurve_center_band_ratio,
        "scurve_center_core_ratio": scurve_center_core_ratio,
        "scurve_outer_ratio": scurve_outer_ratio,
        "scurve_mean_abs_lp": scurve_mean_abs_lp,
        "scurve_mean_abs_lp_delta": scurve_mean_abs_lp_delta,
        "scurve_flip_rate_hz": scurve_flip_rate_hz,
        "edge_event_count": float(edge_event_count),
        "edge_recovery_success_ratio": edge_recovery_success_ratio,
        "edge_recovery_mean_s": edge_recovery_mean_s,
        "search_event_count": float(search_event_count),
        "search_recovery_success_ratio": search_recovery_success_ratio,
        "search_recovery_mean_s": search_recovery_mean_s,
    }
    metrics["score"] = score_metrics(metrics, score_weights)
    return metrics


def detect_early_stop(cfg: EarlyStopConfig,
                      elapsed_s: float,
                      loss_streak: int,
                      search_streak: int,
                      stall_streak: int) -> str:
    if not cfg.enabled or elapsed_s < cfg.min_runtime_s:
        return ""
    if cfg.max_loss_streak > 0 and loss_streak >= cfg.max_loss_streak:
        return f"loss_streak>={cfg.max_loss_streak}"
    if cfg.max_search_streak > 0 and search_streak >= cfg.max_search_streak:
        return f"search_streak>={cfg.max_search_streak}"
    if cfg.max_stall_streak > 0 and stall_streak >= cfg.max_stall_streak:
        return f"stall_streak>={cfg.max_stall_streak}"
    return ""


def perform_turnaround(port: serial.Serial, cfg: TurnaroundConfig) -> bool:
    if not cfg.enabled:
        return True

    last_error = "turnback timeout waiting for EVT:TURNBACK,DONE"

    for attempt in range(max(1, cfg.retries + 1)):
        ensure_stop(port, attempts=3, timeout_s=2.0)
        ensure_ok("#TURNBACK!", send_cmd(port, "#TURNBACK!", timeout_s=2.0))
        deadline = time.time() + cfg.timeout_s

        while time.time() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = decode_serial_text(raw)
            if not line:
                continue
            if line == "EVT:TURNBACK,START":
                continue
            if line.startswith("EVT:TURNBACK,DONE"):
                time.sleep(cfg.settle_s)
                return True
            if line.startswith("EVT:TURNBACK,"):
                last_error = f"turnback failed: {line}"
                break
        else:
            last_error = "turnback timeout waiting for EVT:TURNBACK,DONE"

        if attempt < cfg.retries:
            print(f"[adaptive] turnaround retry {attempt + 1}/{cfg.retries}", flush=True)
            time.sleep(0.35)

    print(f"[adaptive] turnaround fallback: {last_error}", flush=True)
    ensure_stop(port, attempts=3, timeout_s=2.0)
    time.sleep(cfg.settle_s)
    return False


def run_trial(ctx: SearchContext, params: dict[str, float]) -> tuple[list[str], list[TrackRecord], bool, str, float]:
    raw_lines: list[str] = []
    records: list[TrackRecord] = []
    stopped_early = False
    stop_reason = ""
    stop_elapsed_s = 0.0

    with serial.Serial(ctx.port, baudrate=ctx.baud, timeout=0.1) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        time.sleep(0.25)

        ensure_stop(port, attempts=3, timeout_s=2.0)
        ensure_ok("#MODE=TRACK!", send_cmd(port, "#MODE=TRACK!", timeout_s=2.0))
        load_defaults_best_effort(port)

        for spec in ctx.param_specs:
            value = params[spec.name]
            apply_param_with_verify(port, spec, value, attempts=3)
            time.sleep(0.04)

        send_cmd(port, "#STAT!", timeout_s=1.0)
        ensure_ok("#RUN!", send_cmd(port, "#RUN!", timeout_s=2.0))

        first_t_ms: int | None = None
        last_unique_t_ms: int | None = None
        loss_streak = 0
        search_streak = 0
        stall_streak = 0
        wall_start = time.time()

        while (time.time() - wall_start) < ctx.duration_s:
            raw = port.readline()
            if not raw:
                continue
            line = decode_serial_text(raw)
            if not line:
                continue
            raw_lines.append(line)
            rec = parse_hb_line(line)
            if rec is None:
                continue
            records.append(rec)

            if rec.mode == "T" and rec.run == 1 and rec.t_ms != last_unique_t_ms:
                last_unique_t_ms = rec.t_ms
                if first_t_ms is None:
                    first_t_ms = rec.t_ms

                loss_streak = loss_streak + 1 if rec.sb == 0 else 0
                search_active = is_search_state(rec.st)
                search_streak = search_streak + 1 if search_active else 0
                avg_speed = max(0.0, (rec.el + rec.er) * 0.5)
                if (not search_active) and avg_speed < ctx.early_stop.stall_speed_threshold:
                    stall_streak += 1
                else:
                    stall_streak = 0

                elapsed_s = max(0.0, (rec.t_ms - first_t_ms) / 1000.0)
                reason = detect_early_stop(
                    ctx.early_stop,
                    elapsed_s,
                    loss_streak,
                    search_streak,
                    stall_streak,
                )
                if reason:
                    stopped_early = True
                    stop_reason = reason
                    stop_elapsed_s = elapsed_s
                    print(f"[adaptive] early-stop: {reason} at {elapsed_s:.2f}s", flush=True)
                    break

        try:
            ensure_stop(port, attempts=3, timeout_s=2.0)
        except RuntimeError as exc:
            print(f"[adaptive] warn: trial-end stop confirm failed: {exc}", flush=True)
        time.sleep(0.15)
        perform_turnaround(port, ctx.turnaround)

    return raw_lines, records, stopped_early, stop_reason, stop_elapsed_s


def append_trial(ctx: SearchContext, params: dict[str, float], stage: int, source: str) -> TrialResult:
    ctx.trial_index += 1
    label = candidate_label(params, ctx.param_specs)
    if ctx.manual_reset:
        input(f"\n[adaptive] trial {ctx.trial_index}: {label}. 放回起点后回车...")

    print(f"\n[adaptive] run {ctx.trial_index}: stage={stage} source={source} {label}", flush=True)
    raw_lines, records, stopped_early, stop_reason, stop_elapsed_s = run_trial(ctx, params)
    metrics = analyze_trial(records, ctx.settle_skip_s, ctx.score_weights)
    score = metrics["score"] - (60.0 if stopped_early else 0.0)
    params_json = serialize_params(params, ctx.param_specs)

    raw_path = ctx.out_dir / f"trial_{ctx.trial_index:03d}.txt"
    with raw_path.open("w", encoding="utf-8") as handle:
        handle.write(f"# trial_index={ctx.trial_index}\n")
        handle.write(f"# stage={stage}\n")
        handle.write(f"# source={source}\n")
        handle.write(f"# params={params_json}\n")
        handle.write(
            f"# stopped_early={1 if stopped_early else 0} "
            f"stop_reason={stop_reason or '-'} "
            f"stop_elapsed_s={stop_elapsed_s:.2f}\n"
        )
        for line in raw_lines:
            handle.write(line + "\n")

    result = TrialResult(
        trial_index=ctx.trial_index,
        stage=stage,
        source=source,
        params_json=params_json,
        raw_path=str(raw_path),
        sample_count=int(metrics["sample_count"]),
        duration_s=metrics["duration_s"],
        mean_forward_speed=metrics["mean_forward_speed"],
        mean_abs_lp=metrics["mean_abs_lp"],
        mean_abs_lp_delta=metrics["mean_abs_lp_delta"],
        mean_abs_yr=metrics["mean_abs_yr"],
        mean_abs_yr_delta=metrics["mean_abs_yr_delta"],
        center_band_ratio=metrics["center_band_ratio"],
        exact_center_ratio=metrics["exact_center_ratio"],
        center_core_ratio=metrics["center_core_ratio"],
        center_single_ratio=metrics["center_single_ratio"],
        center_mean_abs_lp=metrics["center_mean_abs_lp"],
        center_mean_abs_lp_delta=metrics["center_mean_abs_lp_delta"],
        center_mean_abs_yr=metrics["center_mean_abs_yr"],
        center_mean_abs_yr_delta=metrics["center_mean_abs_yr_delta"],
        straight_ratio=metrics["straight_ratio"],
        outer_ratio=metrics["outer_ratio"],
        corner_ratio=metrics["corner_ratio"],
        search_ratio=metrics["search_ratio"],
        scurve_ratio=metrics["scurve_ratio"],
        trim_ratio=metrics["trim_ratio"],
        loss_ratio=metrics["loss_ratio"],
        lp_flip_rate_hz=metrics["lp_flip_rate_hz"],
        center_flip_rate_hz=metrics["center_flip_rate_hz"],
        scurve_center_band_ratio=metrics["scurve_center_band_ratio"],
        scurve_center_core_ratio=metrics["scurve_center_core_ratio"],
        scurve_outer_ratio=metrics["scurve_outer_ratio"],
        scurve_mean_abs_lp=metrics["scurve_mean_abs_lp"],
        scurve_mean_abs_lp_delta=metrics["scurve_mean_abs_lp_delta"],
        scurve_flip_rate_hz=metrics["scurve_flip_rate_hz"],
        edge_event_count=int(metrics["edge_event_count"]),
        edge_recovery_success_ratio=metrics["edge_recovery_success_ratio"],
        edge_recovery_mean_s=metrics["edge_recovery_mean_s"],
        search_event_count=int(metrics["search_event_count"]),
        search_recovery_success_ratio=metrics["search_recovery_success_ratio"],
        search_recovery_mean_s=metrics["search_recovery_mean_s"],
        score=score,
        stopped_early=1 if stopped_early else 0,
        stop_reason=stop_reason,
        stop_elapsed_s=stop_elapsed_s,
    )

    key = make_candidate_key(params, ctx.param_specs)
    ctx.trials_by_key.setdefault(key, []).append(result)
    ctx.all_trials.append(result)
    write_progress_snapshot(ctx)
    print(
        "[adaptive] "
        f"score={result.score:.2f} core={result.center_core_ratio:.2%} "
        f"center={result.center_band_ratio:.2%} sc_core={result.scurve_center_core_ratio:.2%} "
        f"outer={result.outer_ratio:.2%} sc_outer={result.scurve_outer_ratio:.2%} "
        f"corner={result.corner_ratio:.2%} wob={result.center_flip_rate_hz:.2f}Hz "
        f"sc_wob={result.scurve_flip_rate_hz:.2f}Hz "
        f"edge_rec={result.edge_recovery_mean_s:.2f}s/{result.edge_recovery_success_ratio:.0%} "
        f"search_rec={result.search_recovery_mean_s:.2f}s/{result.search_recovery_success_ratio:.0%} "
        f"c|lp|={result.center_mean_abs_lp:.1f} c|yr|={result.center_mean_abs_yr:.1f} "
        f"c|dyr|={result.center_mean_abs_yr_delta:.1f} sc|lp|={result.scurve_mean_abs_lp:.1f} "
        f"|lp|={result.mean_abs_lp:.1f}",
        flush=True,
    )
    return result


def summarize_candidate(ctx: SearchContext, params: dict[str, float]) -> dict[str, Any]:
    key = make_candidate_key(params, ctx.param_specs)
    trials = ctx.trials_by_key.get(key, [])
    if not trials:
        raise ValueError("empty candidate")
    return {
        "params": json.loads(trials[0].params_json),
        "runs": len(trials),
        "score": robust_center([item.score for item in trials]),
        "score_mean": statistics.fmean(item.score for item in trials),
        "center_band_ratio": robust_center([item.center_band_ratio for item in trials]),
        "exact_center_ratio": robust_center([item.exact_center_ratio for item in trials]),
        "center_core_ratio": robust_center([item.center_core_ratio for item in trials]),
        "center_single_ratio": robust_center([item.center_single_ratio for item in trials]),
        "center_mean_abs_lp": robust_center([item.center_mean_abs_lp for item in trials]),
        "center_mean_abs_lp_delta": robust_center([item.center_mean_abs_lp_delta for item in trials]),
        "center_mean_abs_yr": robust_center([item.center_mean_abs_yr for item in trials]),
        "center_mean_abs_yr_delta": robust_center([item.center_mean_abs_yr_delta for item in trials]),
        "outer_ratio": robust_center([item.outer_ratio for item in trials]),
        "corner_ratio": robust_center([item.corner_ratio for item in trials]),
        "search_ratio": robust_center([item.search_ratio for item in trials]),
        "scurve_ratio": robust_center([item.scurve_ratio for item in trials]),
        "straight_ratio": robust_center([item.straight_ratio for item in trials]),
        "loss_ratio": robust_center([item.loss_ratio for item in trials]),
        "scurve_center_band_ratio": robust_center([item.scurve_center_band_ratio for item in trials]),
        "scurve_center_core_ratio": robust_center([item.scurve_center_core_ratio for item in trials]),
        "scurve_outer_ratio": robust_center([item.scurve_outer_ratio for item in trials]),
        "scurve_mean_abs_lp": robust_center([item.scurve_mean_abs_lp for item in trials]),
        "scurve_mean_abs_lp_delta": robust_center([item.scurve_mean_abs_lp_delta for item in trials]),
        "scurve_flip_rate_hz": robust_center([item.scurve_flip_rate_hz for item in trials]),
        "mean_abs_lp": robust_center([item.mean_abs_lp for item in trials]),
        "mean_abs_lp_delta": robust_center([item.mean_abs_lp_delta for item in trials]),
        "mean_abs_yr": robust_center([item.mean_abs_yr for item in trials]),
        "mean_abs_yr_delta": robust_center([item.mean_abs_yr_delta for item in trials]),
        "lp_flip_rate_hz": robust_center([item.lp_flip_rate_hz for item in trials]),
        "center_flip_rate_hz": robust_center([item.center_flip_rate_hz for item in trials]),
        "trial_indices": [item.trial_index for item in trials],
    }


def ensure_candidate_runs(ctx: SearchContext,
                          params: dict[str, float],
                          desired_runs: int,
                          stage: int,
                          source: str) -> dict[str, Any]:
    key = make_candidate_key(params, ctx.param_specs)
    while len(ctx.trials_by_key.get(key, [])) < desired_runs:
        append_trial(ctx, params, stage, source)
    return summarize_candidate(ctx, params)


def stage_step(spec: ParamSpec, stage_idx: int) -> float:
    if stage_idx < len(spec.steps):
        return float(spec.steps[stage_idx])
    return 0.0


def build_phase_specs(cfg: dict[str, Any],
                      param_specs: list[ParamSpec],
                      repeats: int,
                      confirm_best_repeats: int) -> list[PhaseSpec]:
    adaptive_cfg = cfg_get(cfg, "autotune", "track_adaptive", default={}) or {}
    phase_cfg = adaptive_cfg.get("phases", []) or []
    known_names = {spec.name for spec in param_specs}
    phases: list[PhaseSpec] = []

    if not phase_cfg:
        phases.append(
            PhaseSpec(
                name="all",
                params=[spec.name for spec in param_specs],
                repeats=max(1, repeats),
                confirm_best_repeats=max(confirm_best_repeats, repeats),
                min_improve=0.2,
            )
        )
        return phases

    for item in phase_cfg:
        if not isinstance(item, dict):
            continue
        names = [str(name) for name in (item.get("params", []) or []) if str(name) in known_names]
        if not names:
            continue
        phases.append(
            PhaseSpec(
                name=str(item.get("name", f"phase{len(phases) + 1}")),
                params=names,
                repeats=max(1, int(item.get("repeats", repeats))),
                confirm_best_repeats=max(1, int(item.get("confirm_best_repeats", confirm_best_repeats))),
                min_improve=float(item.get("min_improve", 0.2)),
            )
        )

    if not phases:
        phases.append(
            PhaseSpec(
                name="all",
                params=[spec.name for spec in param_specs],
                repeats=max(1, repeats),
                confirm_best_repeats=max(confirm_best_repeats, repeats),
                min_improve=0.2,
            )
        )
    return phases


def run_phase_coordinate_search(ctx: SearchContext,
                                phase: PhaseSpec,
                                start_params: dict[str, float],
                                stage_offset: int) -> tuple[dict[str, float], dict[str, Any], list[dict[str, Any]], int]:
    best_params = dict(start_params)
    phase_specs = [spec for spec in ctx.param_specs if spec.name in phase.params]
    best_summary = ensure_candidate_runs(ctx, best_params, phase.repeats, stage_offset, f"{phase.name}:seed")
    history: list[dict[str, Any]] = []
    stage_count = max((len(spec.steps) for spec in phase_specs), default=0)
    local_stage = stage_offset

    for stage_idx in range(stage_count):
        local_stage += 1
        print(f"\n[adaptive] phase={phase.name} stage {stage_idx + 1}/{stage_count}", flush=True)
        improved_any = True
        while improved_any:
            improved_any = False
            for spec in phase_specs:
                step = stage_step(spec, stage_idx)
                if step <= 0.0:
                    continue

                candidates: list[dict[str, float]] = [dict(best_params)]
                for direction in (-1.0, 1.0):
                    candidate = dict(best_params)
                    candidate[spec.name] = clamp_value(
                        best_params[spec.name] + direction * step,
                        spec.bounds,
                        spec.digits,
                    )
                    if make_candidate_key(candidate, ctx.param_specs) != make_candidate_key(best_params, ctx.param_specs):
                        candidates.append(candidate)

                summaries = [
                    ensure_candidate_runs(ctx, candidate, phase.repeats, local_stage, f"{phase.name}:{spec.name}")
                    for candidate in candidates
                ]
                stage_best = max(summaries, key=lambda item: item["score"])
                if stage_best["score"] > best_summary["score"] + phase.min_improve:
                    best_params = dict(stage_best["params"])
                    best_summary = stage_best
                    improved_any = True
                    history.append({
                        "phase": phase.name,
                        "stage": stage_idx + 1,
                        "param": spec.name,
                        "step": step,
                        "best": stage_best,
                    })
                    print(
                        f"[adaptive] phase={phase.name} improve {spec.name}: "
                        f"score -> {best_summary['score']:.2f}",
                        flush=True,
                    )

        print(
            f"[adaptive] phase={phase.name} stage {stage_idx + 1} best: "
            f"{candidate_label(best_params, ctx.param_specs)} "
            f"score={best_summary['score']:.2f}",
            flush=True,
        )

    local_stage += 1
    ensure_candidate_runs(ctx, best_params, phase.confirm_best_repeats, local_stage, f"{phase.name}:confirm")
    best_summary = summarize_candidate(ctx, best_params)
    return best_params, best_summary, history, local_stage


def write_trials_csv(path: Path, rows: list[TrialResult]) -> None:
    if not rows:
        return
    fieldnames = list(rows[0].__dataclass_fields__.keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def build_ranked_summaries(ctx: SearchContext) -> list[dict[str, Any]]:
    return sorted(
        (summarize_candidate(ctx, json.loads(rows[0].params_json)) for rows in ctx.trials_by_key.values()),
        key=lambda item: item["score"],
        reverse=True,
    )


def write_progress_snapshot(ctx: SearchContext) -> None:
    if not ctx.all_trials:
        return
    ranked = build_ranked_summaries(ctx)
    write_trials_csv(ctx.out_dir / "trials.csv", ctx.all_trials)
    with (ctx.out_dir / "progress.json").open("w", encoding="utf-8") as handle:
        json.dump(
            {
                "generated_at": datetime.now().isoformat(timespec="seconds"),
                "trial_count": len(ctx.all_trials),
                "best_summary": ranked[0] if ranked else None,
                "top5": ranked[:5],
            },
            handle,
            ensure_ascii=False,
            indent=2,
        )


def build_default_specs(cfg: dict[str, Any]) -> list[ParamSpec]:
    adaptive_cfg = cfg_get(cfg, "autotune", "track_adaptive", default={}) or {}
    param_cfg = adaptive_cfg.get("params", {}) or {}
    if not param_cfg:
        param_cfg = {
            f"sensor_scale{i}": {
                "key": f"track.sensor_scale{i}",
                "start": 1.0,
                "bounds": [0.6, 1.4],
                "steps": [0.15, 0.08, 0.04],
                "digits": 3,
            }
            for i in range(1, 9)
        }

    result: list[ParamSpec] = []
    for name, item in param_cfg.items():
        item = item or {}
        result.append(
            ParamSpec(
                name=str(name),
                key=str(item.get("key", name)),
                start=float(item.get("start", 1.0)),
                bounds=tuple(float(v) for v in item.get("bounds", [0.6, 1.4])),
                steps=[float(v) for v in item.get("steps", [0.15, 0.08, 0.04])],
                digits=int(item.get("digits", 3)),
            )
        )
    return result


def main() -> int:
    cfg = load_config(CONFIG_PATH)
    default_port = str(cfg_get(cfg, "serial", "port", default="COM18"))
    default_baud = int(cfg_get(cfg, "serial", "baudrate", default=115200))
    adaptive_cfg = cfg_get(cfg, "autotune", "track_adaptive", default={}) or {}
    duration = float(adaptive_cfg.get("duration_s", 6.0))
    settle_skip = float(adaptive_cfg.get("settle_skip_s", 0.8))
    repeats = int(adaptive_cfg.get("repeats", 1))
    confirm_best_repeats = int(adaptive_cfg.get("confirm_best_repeats", 2))
    early_cfg = adaptive_cfg.get("early_stop", {}) or {}
    turnaround_cfg = adaptive_cfg.get("turnaround", {}) or {}

    ap = argparse.ArgumentParser(description="Adaptive TRACK tuning via serial telemetry")
    ap.add_argument("--port", default=default_port)
    ap.add_argument("--baud", type=int, default=default_baud)
    ap.add_argument("--duration", type=float, default=duration)
    ap.add_argument("--settle-skip", type=float, default=settle_skip)
    ap.add_argument("--repeats", type=int, default=repeats)
    ap.add_argument("--confirm-best-repeats", type=int, default=confirm_best_repeats)
    ap.add_argument("--manual-reset", action="store_true")
    ap.add_argument("--no-early-stop", action="store_true")
    ap.add_argument("--no-turnaround", action="store_true")
    args = ap.parse_args()

    param_specs = build_default_specs(cfg)
    phase_specs = build_phase_specs(cfg, param_specs, args.repeats, args.confirm_best_repeats)
    score_weights = load_score_weights(cfg)
    start_params = {spec.name: spec.start for spec in param_specs}
    out_dir = OUTPUT_ROOT / datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    ctx = SearchContext(
        port=args.port,
        baud=args.baud,
        duration_s=args.duration,
        settle_skip_s=args.settle_skip,
        manual_reset=args.manual_reset,
        early_stop=EarlyStopConfig(
            enabled=(not args.no_early_stop) and bool(early_cfg.get("enabled", True)),
            min_runtime_s=float(early_cfg.get("min_runtime_s", 1.2)),
            max_loss_streak=int(early_cfg.get("max_loss_streak", 18)),
            max_search_streak=int(early_cfg.get("max_search_streak", 24)),
            max_stall_streak=int(early_cfg.get("max_stall_streak", 16)),
            stall_speed_threshold=float(early_cfg.get("stall_speed_threshold", 12.0)),
        ),
        turnaround=TurnaroundConfig(
            enabled=(not args.no_turnaround) and bool(turnaround_cfg.get("enabled", True)),
            timeout_s=float(turnaround_cfg.get("timeout_s", 5.5)),
            settle_s=float(turnaround_cfg.get("settle_s", 0.25)),
            retries=int(turnaround_cfg.get("retries", 1)),
        ),
        out_dir=out_dir,
        param_specs=param_specs,
        score_weights=score_weights,
    )

    print(f"[adaptive] port={ctx.port} baud={ctx.baud} duration={ctx.duration_s:.1f}s", flush=True)
    print(
        f"[adaptive] turnaround={'on' if ctx.turnaround.enabled else 'off'} "
        f"mode=firmware-turnback timeout={ctx.turnaround.timeout_s:.1f}s retries={ctx.turnaround.retries}",
        flush=True,
    )
    print(
        "[adaptive] phases="
        + " -> ".join(
            f"{phase.name}[{','.join(phase.params)}]x{phase.repeats}/{phase.confirm_best_repeats}"
            for phase in phase_specs
        ),
        flush=True,
    )
    print(f"[adaptive] start={candidate_label(start_params, param_specs)}", flush=True)
    print(
        "[adaptive] score-weights="
        + json.dumps(score_weights, ensure_ascii=False, sort_keys=True),
        flush=True,
    )
    print(f"[adaptive] output={out_dir}", flush=True)

    best_params = dict(start_params)
    best_summary: dict[str, Any] | None = None
    history: list[dict[str, Any]] = []
    stage_offset = 0

    for phase in phase_specs:
        best_params, best_summary, phase_history, stage_offset = run_phase_coordinate_search(
            ctx,
            phase,
            best_params,
            stage_offset,
        )
        history.extend(phase_history)

    result = {
        "best_params": best_params,
        "best_summary": best_summary,
        "history": history,
    }

    ranked = build_ranked_summaries(ctx)
    write_trials_csv(out_dir / "trials.csv", ctx.all_trials)
    with (out_dir / "summary.json").open("w", encoding="utf-8") as handle:
        json.dump(
            {
                "generated_at": datetime.now().isoformat(timespec="seconds"),
                "port": ctx.port,
                "baud": ctx.baud,
                "duration_s": ctx.duration_s,
                "settle_skip_s": ctx.settle_skip_s,
                "best_params": result["best_params"],
                "best_summary": result["best_summary"],
                "top5": ranked[:5],
                "history": result["history"],
                "param_specs": [spec.__dict__ for spec in param_specs],
                "phases": [phase.__dict__ for phase in phase_specs],
                "score_weights": score_weights,
            },
            handle,
            ensure_ascii=False,
            indent=2,
        )

    print("\n[adaptive] top candidates:")
    for item in ranked[:5]:
        print(
            f"  score={item['score']:7.2f}  "
            f"center={item['center_band_ratio']:.2%}  "
            f"sc_core={item['scurve_center_core_ratio']:.2%}  "
            f"outer={item['outer_ratio']:.2%}  "
            f"sc_outer={item['scurve_outer_ratio']:.2%}  "
            f"corner={item['corner_ratio']:.2%}  "
            f"search={item['search_ratio']:.2%}  "
            f"params={item['params']}"
        )

    print(f"\n[adaptive] done -> {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
