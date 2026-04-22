#!/usr/bin/env python3
"""
track_adaptive_tuner.py
=======================

自适应式 TRACK 在线调参：
1. 通过串口 `#TCFG` / `#RUN!` / `#STOP!` 直接下发参数。
2. 用阶段化坐标搜索替代全排列网格，优先减少试验次数。
3. 评分重点放在“中心占比、左右摆动、丢线/找线比例”，目标是尽量把车钳在 S4/S5。
"""

from __future__ import annotations

import argparse
import csv
import json
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

LT_MASK_CENTER = 0x18
LT_MASK_OUTER = 0xC3


@dataclass
class ParamSpec:
    name: str
    key: str
    start: float
    bounds: tuple[float, float]
    steps: list[float]
    digits: int


@dataclass
class TrackRecord:
    t_ms: int
    mode: str
    run: int
    el: int
    er: int
    ol: int
    or_: int
    sb: int
    lp: float
    st: str
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
    center_band_ratio: float
    exact_center_ratio: float
    center_single_ratio: float
    outer_ratio: float
    search_ratio: float
    trim_ratio: float
    loss_ratio: float
    lp_flip_rate_hz: float
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
    out_dir: Path
    param_specs: list[ParamSpec]
    trial_index: int = 0
    trials_by_key: dict[tuple[float, ...], list[TrialResult]] = field(default_factory=dict)
    all_trials: list[TrialResult] = field(default_factory=list)


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
    valid_prefixes = ("OK", "ERR", "STAT:", "HB:")
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
            if line.startswith("OK") or line.startswith("ERR") or line.startswith("STAT:"):
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


def ensure_stop_ready(lines: list[str]) -> None:
    if any(line.startswith("OK") for line in lines):
        return
    if any(line.startswith("ERR") for line in lines):
        return
    raise RuntimeError(f"STOP no ack: {lines}")


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
            el=int(kv.get("el", "0")),
            er=int(kv.get("er", "0")),
            ol=int(kv.get("OL", "0")),
            or_=int(kv.get("OR", "0")),
            sb=int(kv.get("sb", "0")),
            lp=float(kv.get("lp", "0")),
            st=kv.get("st", "TRK"),
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


def is_search_state(state: str) -> bool:
    state = state.upper()
    return state.startswith("FND")


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


def analyze_trial(records: list[TrackRecord], settle_skip_s: float) -> dict[str, float]:
    unique = dedupe_records(records)
    if len(unique) < 8:
        return {
            "sample_count": float(len(unique)),
            "duration_s": 0.0,
            "mean_forward_speed": 0.0,
            "mean_abs_lp": 999.0,
            "center_band_ratio": 0.0,
            "exact_center_ratio": 0.0,
            "center_single_ratio": 0.0,
            "outer_ratio": 1.0,
            "search_ratio": 1.0,
            "trim_ratio": 1.0,
            "loss_ratio": 1.0,
            "lp_flip_rate_hz": 0.0,
            "score": -9999.0,
        }

    start_ms = unique[0].t_ms
    analyzed = [
        rec for rec in unique
        if (rec.t_ms - start_ms) >= int(settle_skip_s * 1000.0)
    ]
    if len(analyzed) < 8:
        analyzed = unique

    duration_s = max(0.01, (analyzed[-1].t_ms - analyzed[0].t_ms) / 1000.0)
    forward_speeds = [max(0.0, (rec.el + rec.er) * 0.5) for rec in analyzed]
    valid = [
        rec for rec in analyzed
        if rec.sb != 0 and not is_search_state(rec.st) and rec.st.upper() != "CROSS"
    ]
    valid_lp = [abs(rec.lp) for rec in valid]
    valid_lp_signed = [rec.lp for rec in valid]

    center_band_ratio = (
        sum(1 for rec in valid if (rec.sb & LT_MASK_CENTER) and (rec.sb & LT_MASK_OUTER) == 0) / len(valid)
        if valid else 0.0
    )
    exact_center_ratio = (
        sum(1 for rec in valid if rec.sb == LT_MASK_CENTER) / len(valid)
        if valid else 0.0
    )
    center_single_ratio = (
        sum(1 for rec in valid if rec.sb in (0x08, 0x10)) / len(valid)
        if valid else 0.0
    )
    outer_ratio = (
        sum(1 for rec in valid if (rec.sb & LT_MASK_OUTER) != 0) / len(valid)
        if valid else 1.0
    )
    search_ratio = sum(1 for rec in analyzed if is_search_state(rec.st)) / len(analyzed)
    trim_ratio = sum(1 for rec in analyzed if rec.st.upper().startswith("TRM")) / len(analyzed)
    loss_ratio = sum(1 for rec in analyzed if rec.sb == 0) / len(analyzed)
    mean_forward_speed = statistics.fmean(forward_speeds)
    mean_abs_lp = statistics.fmean(valid_lp) if valid_lp else 350.0
    lp_flip_rate_hz = sign_changes(valid_lp_signed) / duration_s if valid_lp_signed else 0.0

    score = 0.0
    score += 0.85 * mean_forward_speed
    score += 70.0 * center_band_ratio
    score += 36.0 * exact_center_ratio
    score += 12.0 * center_single_ratio
    score -= 34.0 * loss_ratio
    score -= 24.0 * search_ratio
    score -= 12.0 * trim_ratio
    score -= 14.0 * outer_ratio
    score -= 0.08 * mean_abs_lp
    score -= 0.75 * lp_flip_rate_hz

    return {
        "sample_count": float(len(analyzed)),
        "duration_s": duration_s,
        "mean_forward_speed": mean_forward_speed,
        "mean_abs_lp": mean_abs_lp,
        "center_band_ratio": center_band_ratio,
        "exact_center_ratio": exact_center_ratio,
        "center_single_ratio": center_single_ratio,
        "outer_ratio": outer_ratio,
        "search_ratio": search_ratio,
        "trim_ratio": trim_ratio,
        "loss_ratio": loss_ratio,
        "lp_flip_rate_hz": lp_flip_rate_hz,
        "score": score,
    }


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

        ensure_stop_ready(send_cmd(port, "#STOP!", timeout_s=2.0))
        ensure_ok("#MODE=TRACK!", send_cmd(port, "#MODE=TRACK!", timeout_s=2.0))
        ensure_ok("#TCFG LOAD_DEFAULTS!", send_cmd(port, "#TCFG LOAD_DEFAULTS!", timeout_s=2.0))

        for spec in ctx.param_specs:
            value = params[spec.name]
            ensure_ok(
                f"#TCFG SET {spec.key}",
                send_cmd(port, f"#TCFG SET {spec.key} {value:.6f}!", timeout_s=1.6),
            )

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
                    print(f"[adaptive] early-stop: {reason} at {elapsed_s:.2f}s")
                    break

        send_cmd(port, "#STOP!", timeout_s=1.5)
        time.sleep(0.15)

    return raw_lines, records, stopped_early, stop_reason, stop_elapsed_s


def append_trial(ctx: SearchContext, params: dict[str, float], stage: int, source: str) -> TrialResult:
    ctx.trial_index += 1
    label = candidate_label(params, ctx.param_specs)
    if ctx.manual_reset:
        input(f"\n[adaptive] trial {ctx.trial_index}: {label}. 放回起点后回车...")

    print(f"\n[adaptive] run {ctx.trial_index}: stage={stage} source={source} {label}")
    raw_lines, records, stopped_early, stop_reason, stop_elapsed_s = run_trial(ctx, params)
    metrics = analyze_trial(records, ctx.settle_skip_s)
    score = metrics["score"] - (60.0 if stopped_early else 0.0)

    raw_path = ctx.out_dir / f"trial_{ctx.trial_index:03d}.txt"
    with raw_path.open("w", encoding="utf-8") as handle:
        for line in raw_lines:
            handle.write(line + "\n")

    result = TrialResult(
        trial_index=ctx.trial_index,
        stage=stage,
        source=source,
        params_json=serialize_params(params, ctx.param_specs),
        raw_path=str(raw_path),
        sample_count=int(metrics["sample_count"]),
        duration_s=metrics["duration_s"],
        mean_forward_speed=metrics["mean_forward_speed"],
        mean_abs_lp=metrics["mean_abs_lp"],
        center_band_ratio=metrics["center_band_ratio"],
        exact_center_ratio=metrics["exact_center_ratio"],
        center_single_ratio=metrics["center_single_ratio"],
        outer_ratio=metrics["outer_ratio"],
        search_ratio=metrics["search_ratio"],
        trim_ratio=metrics["trim_ratio"],
        loss_ratio=metrics["loss_ratio"],
        lp_flip_rate_hz=metrics["lp_flip_rate_hz"],
        score=score,
        stopped_early=1 if stopped_early else 0,
        stop_reason=stop_reason,
        stop_elapsed_s=stop_elapsed_s,
    )

    key = make_candidate_key(params, ctx.param_specs)
    ctx.trials_by_key.setdefault(key, []).append(result)
    ctx.all_trials.append(result)
    print(
        "[adaptive] "
        f"score={result.score:.2f} center={result.center_band_ratio:.2%} "
        f"exact={result.exact_center_ratio:.2%} outer={result.outer_ratio:.2%} "
        f"search={result.search_ratio:.2%} |lp|={result.mean_abs_lp:.1f}"
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
        "score": statistics.fmean(item.score for item in trials),
        "center_band_ratio": statistics.fmean(item.center_band_ratio for item in trials),
        "exact_center_ratio": statistics.fmean(item.exact_center_ratio for item in trials),
        "outer_ratio": statistics.fmean(item.outer_ratio for item in trials),
        "search_ratio": statistics.fmean(item.search_ratio for item in trials),
        "loss_ratio": statistics.fmean(item.loss_ratio for item in trials),
        "mean_abs_lp": statistics.fmean(item.mean_abs_lp for item in trials),
        "lp_flip_rate_hz": statistics.fmean(item.lp_flip_rate_hz for item in trials),
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


def run_coordinate_search(ctx: SearchContext,
                          start_params: dict[str, float],
                          base_runs: int,
                          confirm_best_runs: int) -> dict[str, Any]:
    best_params = dict(start_params)
    best_summary = ensure_candidate_runs(ctx, best_params, base_runs, 0, "seed")
    history: list[dict[str, Any]] = []
    stage_count = max(len(spec.steps) for spec in ctx.param_specs)

    for stage_idx in range(stage_count):
        print(f"\n[adaptive] stage {stage_idx + 1}/{stage_count}")
        improved_any = True
        while improved_any:
            improved_any = False
            for spec in ctx.param_specs:
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
                    ensure_candidate_runs(ctx, candidate, base_runs, stage_idx + 1, spec.name)
                    for candidate in candidates
                ]
                stage_best = max(summaries, key=lambda item: item["score"])
                if stage_best["score"] > best_summary["score"] + 0.2:
                    best_params = dict(stage_best["params"])
                    best_summary = stage_best
                    improved_any = True
                    history.append({
                        "stage": stage_idx + 1,
                        "param": spec.name,
                        "step": step,
                        "best": stage_best,
                    })
                    print(f"[adaptive] improve {spec.name}: score -> {best_summary['score']:.2f}")

        print(
            f"[adaptive] stage {stage_idx + 1} best: "
            f"{candidate_label(best_params, ctx.param_specs)} "
            f"score={best_summary['score']:.2f}"
        )

    ensure_candidate_runs(ctx, best_params, confirm_best_runs, stage_count + 1, "confirm")
    best_summary = summarize_candidate(ctx, best_params)
    return {"best_params": best_params, "best_summary": best_summary, "history": history}


def write_trials_csv(path: Path, rows: list[TrialResult]) -> None:
    if not rows:
        return
    fieldnames = list(rows[0].__dataclass_fields__.keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def build_default_specs(cfg: dict[str, Any]) -> list[ParamSpec]:
    track_cfg = cfg_get(cfg, "commands", "track", default={}) or {}
    line_pid = track_cfg.get("line_pid", [12.5, 9.8]) or [12.5, 9.8]
    adaptive_cfg = cfg_get(cfg, "autotune", "track_adaptive", default={}) or {}
    param_cfg = adaptive_cfg.get("params", {}) or {}

    defaults = {
        "speed_target": ParamSpec("speed_target", "track.speed_target", float(track_cfg.get("target_speed", 25.0)), (22.0, 32.0), [2.0, 1.0, 0.5], 2),
        "lkp": ParamSpec("lkp", "track.lkp", float(line_pid[0]), (8.0, 24.0), [2.0, 1.0, 0.5], 3),
        "lkd": ParamSpec("lkd", "track.lkd", float(line_pid[1]), (4.0, 16.0), [1.6, 0.8, 0.4], 3),
        "center_small_ratio": ParamSpec("center_small_ratio", "track.center_small_ratio", 0.16, (0.08, 0.40), [0.05, 0.03, 0.015], 3),
        "center_mid_ratio": ParamSpec("center_mid_ratio", "track.center_mid_ratio", 0.40, (0.20, 0.70), [0.08, 0.04, 0.02], 3),
        "edge_ratio": ParamSpec("edge_ratio", "track.edge_ratio", 0.60, (0.30, 0.90), [0.10, 0.05, 0.025], 3),
        "center_deadband": ParamSpec("center_deadband", "track.center_deadband", 45.0, (20.0, 90.0), [10.0, 5.0, 2.0], 1),
    }

    result: list[ParamSpec] = []
    for name in ("speed_target", "lkp", "lkd", "center_small_ratio", "center_mid_ratio", "edge_ratio", "center_deadband"):
        spec = defaults[name]
        override = param_cfg.get(name, {}) or {}
        spec = ParamSpec(
            name=spec.name,
            key=str(override.get("key", spec.key)),
            start=float(override.get("start", spec.start)),
            bounds=tuple(float(v) for v in override.get("bounds", spec.bounds)),
            steps=[float(v) for v in override.get("steps", spec.steps)],
            digits=int(override.get("digits", spec.digits)),
        )
        result.append(spec)
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

    ap = argparse.ArgumentParser(description="Adaptive TRACK tuning via serial telemetry")
    ap.add_argument("--port", default=default_port)
    ap.add_argument("--baud", type=int, default=default_baud)
    ap.add_argument("--duration", type=float, default=duration)
    ap.add_argument("--settle-skip", type=float, default=settle_skip)
    ap.add_argument("--repeats", type=int, default=repeats)
    ap.add_argument("--confirm-best-repeats", type=int, default=confirm_best_repeats)
    ap.add_argument("--manual-reset", action="store_true")
    ap.add_argument("--no-early-stop", action="store_true")
    args = ap.parse_args()

    param_specs = build_default_specs(cfg)
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
        out_dir=out_dir,
        param_specs=param_specs,
    )

    print(f"[adaptive] port={ctx.port} baud={ctx.baud} duration={ctx.duration_s:.1f}s")
    print(f"[adaptive] start={candidate_label(start_params, param_specs)}")
    print(f"[adaptive] output={out_dir}")

    result = run_coordinate_search(
        ctx,
        start_params,
        max(1, args.repeats),
        max(args.repeats, args.confirm_best_repeats),
    )

    ranked = sorted(
        (summarize_candidate(ctx, json.loads(rows[0].params_json)) for rows in ctx.trials_by_key.values()),
        key=lambda item: item["score"],
        reverse=True,
    )
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
            f"exact={item['exact_center_ratio']:.2%}  "
            f"outer={item['outer_ratio']:.2%}  "
            f"search={item['search_ratio']:.2%}  "
            f"params={item['params']}"
        )

    print(f"\n[adaptive] done -> {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
