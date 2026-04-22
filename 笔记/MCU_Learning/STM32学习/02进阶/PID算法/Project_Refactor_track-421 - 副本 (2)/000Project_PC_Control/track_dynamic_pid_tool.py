#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import time
from dataclasses import asdict, dataclass, replace
from datetime import datetime
from pathlib import Path
from typing import Iterable

import serial


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_DIR = PROJECT_ROOT / "000Data" / "track_dynamic_pid"


@dataclass
class TrackDynamicProfile:
    target_speed: float = 50.0
    dynamic_enable: int = 1
    kp_straight: float = 17.8
    kp_curve: float = 33.0
    kd_straight: float = 11.0
    kd_curve: float = 9.2
    deadband_straight: float = 0.22
    deadband_curve: float = 0.05
    load_low: float = 0.72
    load_high: float = 4.80
    center_anchor_straight: float = 0.24
    center_anchor_curve: float = 0.63
    steer_trim: float = 4.0
    curve_brake_gain: float = 0.15
    curve_speed_min_ratio: float = 0.31

    def commands(self) -> list[str]:
        return [
            "#MODE=TRACK!",
            f"#TDYN={int(self.dynamic_enable)}!",
            f"#TKP0={self.kp_straight:.4f}!",
            f"#TKP1={self.kp_curve:.4f}!",
            f"#TKD0={self.kd_straight:.4f}!",
            f"#TKD1={self.kd_curve:.4f}!",
            f"#TDB0={self.deadband_straight:.4f}!",
            f"#TDB1={self.deadband_curve:.4f}!",
            f"#TCL0={self.load_low:.4f}!",
            f"#TCL1={self.load_high:.4f}!",
            f"#TCA0={self.center_anchor_straight:.4f}!",
            f"#TCA1={self.center_anchor_curve:.4f}!",
            f"#TTR={self.steer_trim:.4f}!",
            f"#TBG={self.curve_brake_gain:.4f}!",
            f"#TSMR={self.curve_speed_min_ratio:.4f}!",
            f"#SPD={self.target_speed:.2f}!",
        ]


PRESET_PROFILES: dict[str, TrackDynamicProfile] = {
    "balanced": TrackDynamicProfile(),
    "smooth": TrackDynamicProfile(
        target_speed=44.0,
        kp_straight=18.5,
        kp_curve=26.0,
        kd_straight=12.5,
        kd_curve=9.5,
        deadband_straight=0.50,
        deadband_curve=0.16,
        center_anchor_straight=0.18,
        center_anchor_curve=0.56,
        curve_brake_gain=0.15,
        curve_speed_min_ratio=0.32,
    ),
    "fast": TrackDynamicProfile(
        target_speed=48.0,
        kp_straight=19.0,
        kp_curve=30.0,
        kd_straight=11.5,
        kd_curve=8.5,
        deadband_straight=0.42,
        deadband_curve=0.08,
        center_anchor_straight=0.20,
        center_anchor_curve=0.68,
        curve_brake_gain=0.16,
        curve_speed_min_ratio=0.36,
    ),
}


@dataclass
class HBRecord:
    t_ms: int
    mode: str
    run: int
    el: int
    er: int
    pc: int
    hd: int
    ol: int
    or_: int
    sb: int
    lp: float
    pb: int
    ga: float
    lkp: float
    lkd: float
    lca: float
    cts: float
    ltr: float
    state: str


@dataclass
class TrackMetrics:
    samples: int
    bypass_ignored_samples: int
    duration_s: float
    mean_speed_counts: float
    straight_bias_mean: float
    line_rms: float
    line_abs_p95: float
    zero_cross_rate: float
    center_flip_rate: float
    straight_center_ratio: float
    center_pair_ratio: float
    edge_dwell_ratio: float
    weak_turn_ratio: float
    wheel_clamp_ratio: float
    line_loss_ratio: float
    score: float


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = int((len(ordered) - 1) * q)
    return ordered[idx]


def parse_hb_line(line: str) -> HBRecord | None:
    line = line.strip()
    if not line.startswith("HB:"):
        return None

    kv: dict[str, str] = {}
    for pair in line[3:].split(","):
        if "=" not in pair:
            continue
        key, value = pair.split("=", 1)
        kv[key.strip()] = value.strip()

    try:
        return HBRecord(
            t_ms=int(kv.get("t", "0")),
            mode=kv.get("m", "S"),
            run=int(kv.get("run", "0")),
            el=int(kv.get("el", "0")),
            er=int(kv.get("er", "0")),
            pc=int(kv.get("pc", "0")),
            hd=int(kv.get("hd", "0")),
            ol=int(kv.get("OL", "0")),
            or_=int(kv.get("OR", "0")),
            sb=int(kv.get("sb", "0")),
            lp=float(kv.get("lp", "0")),
            pb=int(kv.get("pb", "0")),
            ga=float(kv.get("ga", "0")),
            lkp=float(kv.get("lkp", "0")),
            lkd=float(kv.get("lkd", "0")),
            lca=float(kv.get("lca", "0")),
            ltr=float(kv.get("ltr", "0")),
            cts=float(kv.get("cts", "0")),
            state=kv.get("st", "TRK"),
        )
    except ValueError:
        return None


def dedup_records(records: Iterable[HBRecord]) -> list[HBRecord]:
    seen: set[int] = set()
    unique: list[HBRecord] = []
    for rec in records:
        if rec.mode != "T" or rec.run != 1:
            continue
        if rec.t_ms in seen:
            continue
        seen.add(rec.t_ms)
        unique.append(rec)
    return unique


def read_serial_lines(port: serial.Serial, timeout_s: float) -> list[str]:
    lines: list[str] = []
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if line:
            lines.append(line)
    return lines


def flush_serial(port: serial.Serial, wait_ms: int = 120) -> None:
    port.reset_input_buffer()
    time.sleep(wait_ms / 1000.0)
    while port.in_waiting:
        port.read(port.in_waiting)
        time.sleep(0.02)


def send_cmd(port: serial.Serial, cmd: str, timeout_s: float = 1.5) -> list[str]:
    payload = cmd if cmd.endswith("!") else f"{cmd}!"
    flush_serial(port, wait_ms=80)
    port.write(payload.encode("utf-8"))
    port.flush()

    lines: list[str] = []
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        if line.startswith("HB:"):
            continue
        lines.append(line)
        if line == "ERR" or line.startswith("OK") or line.startswith("STAT:"):
            break
    return lines


def ensure_ok(cmd: str, lines: list[str]) -> None:
    if any(line == "ERR" or line.startswith("ERR") for line in lines):
        raise RuntimeError(f"{cmd} failed: {lines}")
    if not any(line.startswith("OK") or line.startswith("STAT:") for line in lines):
        raise RuntimeError(f"{cmd} no response: {lines}")


def send_cmd_checked(port: serial.Serial, cmd: str, timeout_s: float = 1.5,
                     retries: int = 3, settle_s: float = 0.16) -> list[str]:
    last_error: RuntimeError | None = None

    for attempt in range(1, retries + 1):
        lines = send_cmd(port, cmd, timeout_s=timeout_s)
        try:
            ensure_ok(cmd, lines)
            time.sleep(settle_s)
            return lines
        except RuntimeError as exc:
            last_error = exc
            time.sleep(0.18 * attempt)

    if last_error is None:
        raise RuntimeError(f"{cmd} failed without diagnostic")
    raise last_error


def apply_profile(port: serial.Serial, profile: TrackDynamicProfile) -> list[str]:
    responses: list[str] = []
    responses.extend(send_cmd_checked(port, "#STOP!", timeout_s=1.5, retries=2, settle_s=0.22))
    for cmd in profile.commands():
        lines = send_cmd_checked(port, cmd, timeout_s=1.5, retries=3, settle_s=0.12)
        responses.extend(lines)
    stat_lines = send_cmd_checked(port, "#STAT!", timeout_s=1.2, retries=2, settle_s=0.12)
    responses.extend(stat_lines)
    return responses


def capture_run(port_name: str, baudrate: int, duration_s: float,
                profile: TrackDynamicProfile, echo: bool = False) -> tuple[list[str], list[HBRecord], list[str]]:
    raw_lines: list[str] = []
    hb_records: list[HBRecord] = []

    with serial.Serial(port_name, baudrate=baudrate, timeout=0.1) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        time.sleep(0.3)

        responses = apply_profile(port, profile)
        responses.extend(send_cmd_checked(port, "#RUN!", timeout_s=1.5, retries=2, settle_s=0.18))
        start_t = time.time()

        while time.time() - start_t < duration_s:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            raw_lines.append(line)
            if echo:
                print(line)
            record = parse_hb_line(line)
            if record is not None:
                hb_records.append(record)

        try:
            send_cmd_checked(port, "#STOP!", timeout_s=1.5, retries=3, settle_s=0.12)
        except RuntimeError:
            pass
        time.sleep(0.2)

        while port.in_waiting:
            raw = port.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            raw_lines.append(line)
            record = parse_hb_line(line)
            if record is not None:
                hb_records.append(record)

    return raw_lines, dedup_records(hb_records), responses


def analyze_records(records: list[HBRecord]) -> TrackMetrics:
    if len(records) < 5:
        raise RuntimeError("有效 TRACK 遥测样本太少，无法分析")

    active_records = [rec for rec in records if rec.mode == "T" and rec.run == 1]
    pid_records = [rec for rec in active_records if rec.pb == 0]
    trk_records = [rec for rec in pid_records if rec.state == "TRK"] or pid_records or active_records
    duration_s = max((trk_records[-1].t_ms - trk_records[0].t_ms) / 1000.0, 0.01)
    line_vals = [rec.lp for rec in trk_records]
    abs_line_vals = [abs(v) for v in line_vals]
    avg_speed = [0.5 * (abs(rec.el) + abs(rec.er)) for rec in trk_records]

    zero_crosses = 0
    center_flips = 0
    last_center_sign = 0

    for prev, curr in zip(trk_records, trk_records[1:]):
        if prev.lp * curr.lp < 0.0:
            zero_crosses += 1

        if abs(curr.lp) <= 1.5 and abs(prev.lp) <= 1.5:
            curr_sign = 1 if curr.hd > 0 else (-1 if curr.hd < 0 else 0)
            if last_center_sign != 0 and curr_sign != 0 and curr_sign != last_center_sign:
                center_flips += 1
            if curr_sign != 0:
                last_center_sign = curr_sign

    curve_records = [rec for rec in trk_records if abs(rec.lp) >= 4.0]
    weak_turn_count = sum(
        1 for rec in curve_records
        if abs(rec.hd) < max(abs(rec.pc) * 0.28, 55.0)
    )
    wheel_clamp_count = sum(
        1 for rec in trk_records
        if rec.ol <= 0 or rec.or_ <= 0
    )
    edge_dwell_count = sum(1 for rec in trk_records if abs(rec.lp) >= 5.0)
    line_loss_count = sum(1 for rec in trk_records if rec.sb == 0)
    straight_records = [rec for rec in trk_records if rec.ga <= 0.25]
    straight_center_count = sum(1 for rec in straight_records if (rec.sb & 0x18) != 0)
    center_pair_count = sum(1 for rec in straight_records if (rec.sb & 0x18) == 0x18)
    straight_bias_mean = (sum(rec.lp for rec in straight_records) / len(straight_records)) if straight_records else 0.0

    line_rms = math.sqrt(sum(value * value for value in line_vals) / len(line_vals))
    zero_cross_rate = zero_crosses / duration_s
    center_flip_rate = center_flips / duration_s
    straight_center_ratio = (straight_center_count / len(straight_records)) if straight_records else 0.0
    center_pair_ratio = (center_pair_count / len(straight_records)) if straight_records else 0.0
    edge_dwell_ratio = edge_dwell_count / len(trk_records)
    weak_turn_ratio = (weak_turn_count / len(curve_records)) if curve_records else 0.0
    wheel_clamp_ratio = wheel_clamp_count / len(trk_records)
    line_loss_ratio = line_loss_count / len(trk_records)
    mean_speed_counts = sum(avg_speed) / len(avg_speed)

    score = 100.0
    score -= min(40.0, line_rms * 10.5)
    score -= min(18.0, zero_cross_rate * 6.0)
    score -= min(16.0, center_flip_rate * 5.5)
    score -= max(0.0, 0.82 - straight_center_ratio) * 35.0
    score -= max(0.0, 0.48 - center_pair_ratio) * 22.0
    score -= min(12.0, abs(straight_bias_mean) * 12.0)
    score -= edge_dwell_ratio * 45.0
    score -= weak_turn_ratio * 28.0
    score -= wheel_clamp_ratio * 38.0
    score -= line_loss_ratio * 70.0
    score += min(12.0, max(0.0, (mean_speed_counts - 120.0) * 0.05))
    score = max(score, 0.0)

    return TrackMetrics(
        samples=len(trk_records),
        bypass_ignored_samples=max(0, len(active_records) - len(trk_records)),
        duration_s=duration_s,
        mean_speed_counts=mean_speed_counts,
        straight_bias_mean=straight_bias_mean,
        line_rms=line_rms,
        line_abs_p95=percentile(abs_line_vals, 0.95),
        zero_cross_rate=zero_cross_rate,
        center_flip_rate=center_flip_rate,
        straight_center_ratio=straight_center_ratio,
        center_pair_ratio=center_pair_ratio,
        edge_dwell_ratio=edge_dwell_ratio,
        weak_turn_ratio=weak_turn_ratio,
        wheel_clamp_ratio=wheel_clamp_ratio,
        line_loss_ratio=line_loss_ratio,
        score=score,
    )


def suggest_profile(profile: TrackDynamicProfile, metrics: TrackMetrics) -> tuple[TrackDynamicProfile, list[str]]:
    next_profile = replace(profile)
    notes: list[str] = []

    if metrics.center_flip_rate > 2.2 or (metrics.zero_cross_rate > 1.5 and metrics.line_rms > 1.8):
        next_profile.kp_straight *= 0.92
        next_profile.deadband_straight = min(next_profile.deadband_straight + 0.05, 0.90)
        notes.append("中心区来回摆偏多，下调直线 KP 并放宽直线死区。")

    if metrics.straight_center_ratio < 0.78 or metrics.center_pair_ratio < 0.35:
        next_profile.center_anchor_straight = min(next_profile.center_anchor_straight + 0.04, 0.92)
        next_profile.deadband_straight = max(next_profile.deadband_straight - 0.03, 0.05)
        notes.append("直线段 S4/S5 占比不够，增强中线钳制并收紧直线死区。")

    if abs(metrics.straight_bias_mean) >= 0.18:
        next_profile.steer_trim += max(-10.0, min(10.0, metrics.straight_bias_mean * 6.0))
        notes.append("直线段仍有固定偏向，按平均偏差方向补静态差速偏置。")

    if metrics.edge_dwell_ratio > 0.10 or metrics.weak_turn_ratio > 0.18:
        next_profile.kp_curve *= 1.08
        next_profile.deadband_curve = max(next_profile.deadband_curve - 0.03, 0.04)
        next_profile.load_low = max(next_profile.load_low - 0.10, 0.40)
        next_profile.center_anchor_curve = min(next_profile.center_anchor_curve + 0.05, 0.92)
        notes.append("大弯贴边或转向发软，提升弯道 KP、缩小弯道死区并提前进入强控制区。")

    if metrics.wheel_clamp_ratio > 0.08:
        next_profile.curve_brake_gain = min(next_profile.curve_brake_gain + 0.02, 0.28)
        next_profile.target_speed = max(next_profile.target_speed - 1.0, 35.0)
        notes.append("单轮被频繁打停，增强曲率收油并略降巡航速度。")

    if metrics.line_loss_ratio > 0.03:
        next_profile.curve_brake_gain = min(next_profile.curve_brake_gain + 0.02, 0.28)
        next_profile.curve_speed_min_ratio = max(next_profile.curve_speed_min_ratio - 0.03, 0.22)
        notes.append("丢线比例偏高，进一步降低弯中保底速度。")

    if (metrics.line_loss_ratio < 0.01
            and metrics.wheel_clamp_ratio < 0.04
            and metrics.line_rms < 1.6
            and metrics.mean_speed_counts < 220.0):
        next_profile.target_speed += 2.0
        notes.append("当前稳定性还够，可以小步加速。")

    if not notes:
        notes.append("当前这组参数没有明显硬伤，可继续用同组参数复测更长时间。")

    return next_profile, notes


def save_run(output_dir: Path, profile: TrackDynamicProfile, raw_lines: list[str],
             metrics: TrackMetrics, next_profile: TrackDynamicProfile, notes: list[str],
             stat_lines: list[str], label: str | None = None) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    prefix = f"track_dynamic_{label}_" if label else "track_dynamic_"
    raw_path = output_dir / f"{prefix}{stamp}.txt"
    json_path = output_dir / f"{prefix}{stamp}.json"

    raw_path.write_text("\n".join(raw_lines) + ("\n" if raw_lines else ""), encoding="utf-8")
    json_path.write_text(
        json.dumps(
            {
                "profile": asdict(profile),
                "metrics": asdict(metrics),
                "suggested_profile": asdict(next_profile),
                "notes": notes,
                "stat": stat_lines,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    return raw_path, json_path


def build_profile_from_args(args: argparse.Namespace) -> TrackDynamicProfile:
    profile = replace(PRESET_PROFILES[args.profile])
    for key in (
        "target_speed",
        "kp_straight", "kp_curve",
        "kd_straight", "kd_curve",
        "deadband_straight", "deadband_curve",
        "load_low", "load_high",
        "center_anchor_straight", "center_anchor_curve",
        "steer_trim",
        "curve_brake_gain", "curve_speed_min_ratio",
    ):
        value = getattr(args, key, None)
        if value is not None:
            setattr(profile, key, value)
    if getattr(args, "dynamic_enable", None) is not None:
        profile.dynamic_enable = 1 if args.dynamic_enable else 0
    return profile


def print_profile(title: str, profile: TrackDynamicProfile) -> None:
    print(title)
    for key, value in asdict(profile).items():
        print(f"  {key}: {value}")


def print_metrics(metrics: TrackMetrics) -> None:
    print("分析结果")
    print(f"  samples: {metrics.samples}")
    print(f"  duration_s: {metrics.duration_s:.2f}")
    print(f"  mean_speed_counts: {metrics.mean_speed_counts:.1f}")
    print(f"  straight_bias_mean: {metrics.straight_bias_mean:.3f}")
    print(f"  line_rms: {metrics.line_rms:.3f}")
    print(f"  line_abs_p95: {metrics.line_abs_p95:.3f}")
    print(f"  bypass_ignored_samples: {metrics.bypass_ignored_samples}")
    print(f"  zero_cross_rate: {metrics.zero_cross_rate:.3f}/s")
    print(f"  center_flip_rate: {metrics.center_flip_rate:.3f}/s")
    print(f"  straight_center_ratio: {metrics.straight_center_ratio:.3%}")
    print(f"  center_pair_ratio: {metrics.center_pair_ratio:.3%}")
    print(f"  edge_dwell_ratio: {metrics.edge_dwell_ratio:.3%}")
    print(f"  weak_turn_ratio: {metrics.weak_turn_ratio:.3%}")
    print(f"  wheel_clamp_ratio: {metrics.wheel_clamp_ratio:.3%}")
    print(f"  line_loss_ratio: {metrics.line_loss_ratio:.3%}")
    print(f"  score: {metrics.score:.2f}")


def print_suggested_commands(profile: TrackDynamicProfile) -> None:
    print("建议下一轮下发命令")
    for cmd in profile.commands():
        print(f"  {cmd}")


def load_records_from_file(path: Path) -> list[HBRecord]:
    records = [parse_hb_line(line) for line in path.read_text(encoding="utf-8").splitlines()]
    return dedup_records([record for record in records if record is not None])


def generate_candidate_profiles(base: TrackDynamicProfile) -> list[tuple[str, TrackDynamicProfile]]:
    return [
        ("base", replace(base)),
        ("center_lock", replace(
            base,
            center_anchor_straight=min(base.center_anchor_straight + 0.05, 0.92),
            deadband_straight=max(base.deadband_straight - 0.04, 0.05),
            kp_straight=base.kp_straight * 0.96,
        )),
        ("trim_left", replace(
            base,
            steer_trim=base.steer_trim - 4.0,
        )),
        ("trim_right", replace(
            base,
            steer_trim=base.steer_trim + 4.0,
        )),
        ("center_fast", replace(
            base,
            target_speed=base.target_speed + 1.0,
            center_anchor_straight=min(base.center_anchor_straight + 0.04, 0.92),
            curve_brake_gain=min(base.curve_brake_gain + 0.01, 0.28),
        )),
        ("curve_hold", replace(
            base,
            kp_curve=base.kp_curve * 1.08,
            deadband_curve=max(base.deadband_curve - 0.03, 0.04),
            load_low=max(base.load_low - 0.08, 0.40),
            center_anchor_curve=min(base.center_anchor_curve + 0.05, 0.92),
        )),
        ("smooth_damp", replace(
            base,
            kp_straight=base.kp_straight * 0.94,
            kd_straight=base.kd_straight * 1.08,
            deadband_straight=min(base.deadband_straight + 0.05, 0.90),
            center_anchor_straight=min(base.center_anchor_straight + 0.02, 0.92),
        )),
    ]


def run_profile_once(port: str, baud: int, duration: float, out_dir: Path,
                     label: str, profile: TrackDynamicProfile,
                     echo: bool = False) -> tuple[TrackMetrics, TrackDynamicProfile, list[str], Path, Path]:
    raw_lines, records, stat_lines = capture_run(port, baud, duration, profile, echo=echo)
    metrics = analyze_records(records)
    next_profile, notes = suggest_profile(profile, metrics)
    raw_path, json_path = save_run(out_dir, profile, raw_lines, metrics, next_profile, notes, stat_lines, label=label)
    return metrics, next_profile, notes, raw_path, json_path


def add_profile_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--profile", choices=sorted(PRESET_PROFILES), default="balanced")
    parser.add_argument("--target-speed", type=float)
    parser.add_argument("--dynamic-enable", type=int, choices=(0, 1))
    parser.add_argument("--kp-straight", type=float)
    parser.add_argument("--kp-curve", type=float)
    parser.add_argument("--kd-straight", type=float)
    parser.add_argument("--kd-curve", type=float)
    parser.add_argument("--deadband-straight", type=float)
    parser.add_argument("--deadband-curve", type=float)
    parser.add_argument("--load-low", type=float)
    parser.add_argument("--load-high", type=float)
    parser.add_argument("--center-anchor-straight", type=float)
    parser.add_argument("--center-anchor-curve", type=float)
    parser.add_argument("--steer-trim", type=float)
    parser.add_argument("--curve-brake-gain", type=float)
    parser.add_argument("--curve-speed-min-ratio", type=float)


def main() -> int:
    parser = argparse.ArgumentParser(description="TRACK 动态 PID 调参与采集工具")
    subparsers = parser.add_subparsers(dest="command", required=True)

    stat_parser = subparsers.add_parser("stat", help="查询当前板端参数")
    stat_parser.add_argument("--port", default="COM18")
    stat_parser.add_argument("--baud", type=int, default=115200)

    apply_parser = subparsers.add_parser("apply", help="下发一组动态 TRACK 参数")
    apply_parser.add_argument("--port", default="COM18")
    apply_parser.add_argument("--baud", type=int, default=115200)
    add_profile_args(apply_parser)

    run_parser = subparsers.add_parser("run", help="下发参数、运行、采集并给出建议")
    run_parser.add_argument("--port", default="COM18")
    run_parser.add_argument("--baud", type=int, default=115200)
    run_parser.add_argument("--duration", type=float, default=8.0)
    run_parser.add_argument("--out", type=Path, default=DEFAULT_OUT_DIR)
    run_parser.add_argument("--echo", action="store_true")
    add_profile_args(run_parser)

    autotune_parser = subparsers.add_parser("autotune", help="连续跑多轮候选参数并选出最稳的一轮")
    autotune_parser.add_argument("--port", default="COM18")
    autotune_parser.add_argument("--baud", type=int, default=115200)
    autotune_parser.add_argument("--duration", type=float, default=7.0)
    autotune_parser.add_argument("--rounds", type=int, default=3)
    autotune_parser.add_argument("--out", type=Path, default=DEFAULT_OUT_DIR)
    autotune_parser.add_argument("--echo", action="store_true")
    add_profile_args(autotune_parser)

    analyze_parser = subparsers.add_parser("analyze", help="分析已有 HB 日志")
    analyze_parser.add_argument("file", type=Path)
    add_profile_args(analyze_parser)

    args = parser.parse_args()

    if args.command == "stat":
        with serial.Serial(args.port, baudrate=args.baud, timeout=0.15) as port:
            port.reset_input_buffer()
            port.reset_output_buffer()
            time.sleep(0.2)
            lines = send_cmd(port, "#STAT!", timeout_s=1.2)
            if not lines:
                raise RuntimeError("没有拿到 STAT 返回")
            for line in lines:
                print(line)
        return 0

    if args.command == "apply":
        profile = build_profile_from_args(args)
        print_profile("准备下发的 TRACK 动态参数", profile)
        with serial.Serial(args.port, baudrate=args.baud, timeout=0.15) as port:
            port.reset_input_buffer()
            port.reset_output_buffer()
            time.sleep(0.2)
            responses = apply_profile(port, profile)
            for line in responses:
                print(line)
        return 0

    if args.command == "run":
        profile = build_profile_from_args(args)
        print_profile("本轮运行参数", profile)
        raw_lines, records, stat_lines = capture_run(args.port, args.baud, args.duration, profile, echo=args.echo)
        metrics = analyze_records(records)
        next_profile, notes = suggest_profile(profile, metrics)
        raw_path, json_path = save_run(args.out, profile, raw_lines, metrics, next_profile, notes, stat_lines)
        print_metrics(metrics)
        print(f"原始日志: {raw_path}")
        print(f"分析摘要: {json_path}")
        print("建议")
        for note in notes:
            print(f"  - {note}")
        print_suggested_commands(next_profile)
        return 0

    if args.command == "autotune":
        base_profile = build_profile_from_args(args)
        print_profile("自动调参起始参数", base_profile)

        best_profile = replace(base_profile)
        best_metrics: TrackMetrics | None = None
        best_round = 0
        best_label = "base"

        for round_idx in range(1, max(1, args.rounds) + 1):
            print(f"\n=== Round {round_idx} ===")
            candidates = generate_candidate_profiles(base_profile)
            round_best_profile = None
            round_best_metrics = None
            round_best_suggested = None
            round_best_label = "base"

            for label, profile in candidates:
                print(f"\n--- {label} ---")
                print_profile("候选参数", profile)
                try:
                    metrics, suggested_profile, notes, raw_path, json_path = run_profile_once(
                        args.port, args.baud, args.duration, args.out,
                        f"r{round_idx:02d}_{label}", profile, echo=args.echo
                    )
                    print_metrics(metrics)
                    print(f"原始日志: {raw_path}")
                    print(f"分析摘要: {json_path}")
                    for note in notes:
                        print(f"  - {note}")
                except Exception as exc:
                    print(f"  候选失败: {exc}")
                    continue

                if round_best_metrics is None or metrics.score > round_best_metrics.score:
                    round_best_profile = replace(profile)
                    round_best_metrics = metrics
                    round_best_suggested = replace(suggested_profile)
                    round_best_label = label

                if best_metrics is None or metrics.score > best_metrics.score:
                    best_profile = replace(profile)
                    best_metrics = metrics
                    best_round = round_idx
                    best_label = label

            if round_best_metrics is None or round_best_profile is None or round_best_suggested is None:
                break

            print(f"\nRound {round_idx} 最优: {round_best_label}")
            print_profile("本轮最优参数", round_best_profile)
            print_metrics(round_best_metrics)
            base_profile = round_best_suggested

            if best_metrics is not None and round_best_metrics.score + 0.8 < best_metrics.score and round_idx > 1:
                print("后续候选已经明显不如当前全局最优，提前结束。")
                break

        if best_metrics is None:
            raise RuntimeError("自动调参没有得到有效结果")

        print("\n=== Best Overall ===")
        print(f"best_round: {best_round}")
        print(f"best_label: {best_label}")
        print_profile("全局最优参数", best_profile)
        print_metrics(best_metrics)
        print_suggested_commands(best_profile)
        return 0

    if args.command == "analyze":
        profile = build_profile_from_args(args)
        records = load_records_from_file(args.file)
        metrics = analyze_records(records)
        next_profile, notes = suggest_profile(profile, metrics)
        print_metrics(metrics)
        print("建议")
        for note in notes:
            print(f"  - {note}")
        print_suggested_commands(next_profile)
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
