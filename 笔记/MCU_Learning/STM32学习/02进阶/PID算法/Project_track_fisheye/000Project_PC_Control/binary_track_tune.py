#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path

import serial

from experiment_score_watch import parse_hb_line


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LOGGER_SCRIPT = PROJECT_ROOT / "000Project_PC_Control" / "experiment_logger.py"
SCORE_SCRIPT = PROJECT_ROOT / "000Project_PC_Control" / "experiment_score_watch.py"
EXPERIMENT_DIR = PROJECT_ROOT / "000Data" / "serial_runs" / "experiments"
OUTPUT_ROOT = PROJECT_ROOT / "000Data" / "binary_track_tune"
EXP_FILE_RE = re.compile(r"exp_\d+_\d{8}_\d{6}_[A-Z]+_[A-Z]\.txt")


PARAMS = {
    "lkp": {
        "set_tag": "#LKP",
        "get_tag": "#LKP?!",
        "min": 8.0,
        "max": 28.0,
        "step": 0.2,
        "coarse_step": 0.6,
        "tolerance": 0.2,
    },
    "tdr": {
        "set_tag": "#TDR",
        "get_tag": "#TDR?!",
        "min": 0.20,
        "max": 0.95,
        "step": 0.01,
        "coarse_step": 0.02,
        "tolerance": 0.01,
    },
    "stf": {
        "set_tag": "#STF",
        "get_tag": "#STF?!",
        "min": 220.0,
        "max": 450.0,
        "step": 10.0,
        "coarse_step": 20.0,
        "tolerance": 10.0,
    },
    "sts": {
        "set_tag": "#STS",
        "get_tag": "#STS?!",
        "min": 120.0,
        "max": 360.0,
        "step": 10.0,
        "coarse_step": 20.0,
        "tolerance": 10.0,
    },
}


FROZEN_ALIAS = {
    "lkd": "#LKD",
    "stb": "#STB",
}

FROZEN_GET = {
    "lkd": "#LKD?!",
    "stb": "#STB?!",
}


@dataclass
class TrialSummary:
    label: str
    value: float
    repeats: int
    valid_runs: int
    invalid_runs: int
    avg_total: float
    avg_grip: float
    avg_smooth: float
    avg_center: float
    avg_search: float
    avg_loss: float
    avg_cover: float
    std_total: float
    exp_files: list[str]
    invalid_reasons: list[str]


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def quantize(key: str, value: float) -> float:
    spec = PARAMS[key]
    step = float(spec["step"])
    low = float(spec["min"])
    high = float(spec["max"])
    snapped = round(value / step) * step
    snapped = clamp(snapped, low, high)
    digits = max(0, len(str(step).split(".")[1].rstrip("0"))) if "." in str(step) else 0
    return round(snapped, digits)


def send_cmd(port: serial.Serial, cmd: str, timeout_s: float = 1.2) -> list[str]:
    port.reset_input_buffer()
    payload = cmd if cmd.endswith("!") else f"{cmd}!"
    port.write(payload.encode("ascii"))
    port.flush()
    lines: list[str] = []
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore")
        markers = ("OK:", "ERR:", "STAT:", "HB:")
        best_idx = -1
        for marker in markers:
            idx = line.find(marker)
            if idx >= 0 and (best_idx < 0 or idx < best_idx):
                best_idx = idx
        if best_idx >= 0:
            line = line[best_idx:]
        line = line.strip()
        if not line or line.startswith("HB:"):
            continue
        lines.append(line)
        if line.startswith("OK") or line.startswith("ERR") or line.startswith("STAT:"):
            break
    return lines


def set_alias(port: serial.Serial, tag: str, value: float, retries: int = 3) -> float:
    last_lines: list[str] = []
    for attempt in range(retries):
        lines = send_cmd(port, f"{tag}={value}", timeout_s=1.6 + 0.4 * attempt)
        last_lines = lines
        for line in lines:
            if line.startswith("OK:"):
                try:
                    return float(line.rsplit("=", 1)[1])
                except Exception:
                    break
        time.sleep(0.12)
    raise RuntimeError(f"写参数失败: {tag}={value} -> {last_lines}")


def get_alias(port: serial.Serial, tag: str, retries: int = 3) -> float:
    last_lines: list[str] = []
    for attempt in range(retries):
        lines = send_cmd(port, tag, timeout_s=1.6 + 0.4 * attempt)
        last_lines = lines
        for line in lines:
            if line.startswith("OK:"):
                try:
                    return float(line.rsplit("=", 1)[1])
                except Exception:
                    break
        time.sleep(0.12)
    raise RuntimeError(f"读参数失败: {tag} -> {last_lines}")


def run_logger_round(python_exe: str, port: str, duration_s: float) -> Path:
    before = {p.name for p in EXPERIMENT_DIR.glob("exp_*.txt")}
    cmd = [
        python_exe,
        "-u",
        str(LOGGER_SCRIPT),
        "--port",
        port,
        "--uart-test-seconds",
        f"{duration_s:.3f}",
        "--uart-mode",
        "TRACK",
        "--max-seconds",
        f"{duration_s + 6.0:.3f}",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="ignore", check=True)
    text = proc.stdout + "\n" + proc.stderr
    matches = EXP_FILE_RE.findall(text)
    if matches:
        return EXPERIMENT_DIR / matches[-1]
    after = sorted(p for p in EXPERIMENT_DIR.glob("exp_*.txt") if p.name not in before)
    if after:
        return after[-1]
    raise RuntimeError(f"未找到新实验文件。\n{text}")


def wait_experiment_ready(exp_path: Path, timeout_s: float = 4.0) -> None:
    deadline = time.time() + timeout_s
    last_size = -1
    stable_ticks = 0
    while time.time() < deadline:
        if not exp_path.exists():
            time.sleep(0.10)
            continue
        try:
            text = exp_path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            time.sleep(0.10)
            continue
        if "EVT:EXP_STOP" in text:
            return
        size = exp_path.stat().st_size
        if size == last_size:
            stable_ticks += 1
            if stable_ticks >= 3:
                return
        else:
            stable_ticks = 0
            last_size = size
        time.sleep(0.15)


def score_experiment(python_exe: str, exp_path: Path) -> dict[str, object]:
    cmd = [python_exe, str(SCORE_SCRIPT), "--once", str(exp_path), "--print-json"]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="ignore", check=True)
    return json.loads(proc.stdout)


def detect_invalid_run(exp_path: Path, summary: dict[str, object]) -> tuple[bool, str]:
    hb_records = []
    with exp_path.open("r", encoding="utf-8", errors="ignore") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            rec = parse_hb_line(line)
            if rec is None or rec.run != 1 or rec.t_ms < 400:
                continue
            hb_records.append(rec)

    if len(hb_records) < 80:
        return True, "有效样本太少"

    sbh_counts: dict[int, int] = {}
    lp_values: list[float] = []
    states: set[str] = set()
    for rec in hb_records:
        sbh_counts[rec.sbh] = sbh_counts.get(rec.sbh, 0) + 1
        lp_values.append(rec.lp)
        states.add(rec.st)

    dominant_ratio = max(sbh_counts.values()) / float(len(hb_records))
    lp_std = statistics.pstdev(lp_values) if len(lp_values) >= 2 else 0.0
    search_ratio = float(summary["search_ratio"])
    loss_ratio = float(summary["loss_ratio"])

    if dominant_ratio > 0.65 and lp_std < 18.0:
        return True, f"位型过于固定(dominant={dominant_ratio:.2f}, lp_std={lp_std:.1f})"
    if len(states) <= 2 and search_ratio == 0.0 and loss_ratio == 0.0:
        return True, "状态变化过少，疑似未真实跑线"

    return False, ""


def summarize_trials(
    label: str,
    value: float,
    repeats: int,
    results: list[tuple[Path, dict[str, object], bool, str]],
) -> TrialSummary:
    valid = [item for item in results if not item[2]]
    invalid = [item for item in results if item[2]]
    exp_files = [item[0].name for item in results]
    invalid_reasons = [item[3] for item in invalid if item[3]]

    if not valid:
        return TrialSummary(
            label=label,
            value=value,
            repeats=repeats,
            valid_runs=0,
            invalid_runs=len(invalid),
            avg_total=-1.0,
            avg_grip=0.0,
            avg_smooth=0.0,
            avg_center=0.0,
            avg_search=1.0,
            avg_loss=1.0,
            avg_cover=0.0,
            std_total=0.0,
            exp_files=exp_files,
            invalid_reasons=invalid_reasons,
        )

    totals = [float(item[1]["total_score"]) for item in valid]
    grips = [float(item[1]["grip_score"]) for item in valid]
    smooths = [float(item[1]["speed_smoothness_score"]) for item in valid]
    centers = [float(item[1]["center_score"]) for item in valid]
    searches = [float(item[1]["search_ratio"]) for item in valid]
    losses = [float(item[1]["loss_ratio"]) for item in valid]
    covers = [float(item[1]["a67_cover_ratio"]) for item in valid]

    return TrialSummary(
        label=label,
        value=value,
        repeats=repeats,
        valid_runs=len(valid),
        invalid_runs=len(invalid),
        avg_total=statistics.fmean(totals),
        avg_grip=statistics.fmean(grips),
        avg_smooth=statistics.fmean(smooths),
        avg_center=statistics.fmean(centers),
        avg_search=statistics.fmean(searches),
        avg_loss=statistics.fmean(losses),
        avg_cover=statistics.fmean(covers),
        std_total=statistics.pstdev(totals) if len(totals) >= 2 else 0.0,
        exp_files=exp_files,
        invalid_reasons=invalid_reasons,
    )


def trial_rank_key(summary: TrialSummary) -> tuple[float, float, float, float]:
    return (
        summary.avg_total,
        summary.avg_grip,
        summary.avg_cover,
        -summary.std_total,
    )


def apply_param_set(port_name: str, baud: int, focus_key: str, focus_value: float, frozen: dict[str, float]) -> None:
    with serial.Serial(port_name, baud, timeout=0.35) as ser:
        time.sleep(0.2)
        for frozen_key, frozen_value in frozen.items():
            tag = FROZEN_ALIAS[frozen_key]
            set_alias(ser, tag, frozen_value)
        focus_tag = PARAMS[focus_key]["set_tag"]
        set_alias(ser, focus_tag, focus_value)


def read_current_values(port_name: str, baud: int, focus_key: str, frozen_keys: list[str]) -> tuple[float, dict[str, float]]:
    with serial.Serial(port_name, baud, timeout=0.35) as ser:
        time.sleep(0.2)
        focus_value = get_alias(ser, PARAMS[focus_key]["get_tag"])
        frozen = {key: get_alias(ser, FROZEN_GET[key]) for key in frozen_keys}
    return focus_value, frozen


def auto_coarse_values(focus_key: str, current: float) -> list[float]:
    spec = PARAMS[focus_key]
    step = float(spec["coarse_step"])
    values = [
        current - 2 * step,
        current - step,
        current,
        current + step,
        current + 2 * step,
    ]
    return sorted({quantize(focus_key, value) for value in values})


def refine_values(focus_key: str, best_value: float, span: float) -> list[float]:
    if span <= 0.0:
        return []
    left = quantize(focus_key, best_value - span)
    right = quantize(focus_key, best_value + span)
    values = []
    if left != best_value:
        values.append(left)
    if right != best_value and right != left:
        values.append(right)
    return values


def run_trial_group(
    python_exe: str,
    port_name: str,
    baud: int,
    duration: float,
    focus_key: str,
    value: float,
    frozen: dict[str, float],
    repeats: int,
    label: str,
) -> TrialSummary:
    results: list[tuple[Path, dict[str, object], bool, str]] = []
    for _ in range(repeats):
        apply_param_set(port_name, baud, focus_key, value, frozen)
        exp_path = run_logger_round(python_exe, port_name, duration)
        wait_experiment_ready(exp_path)
        summary = score_experiment(python_exe, exp_path)
        invalid, reason = detect_invalid_run(exp_path, summary)
        results.append((exp_path, summary, invalid, reason))
    return summarize_trials(label, value, repeats, results)


def main() -> int:
    parser = argparse.ArgumentParser(description="Coarse tiers + binary-style halving tune loop for track parameters.")
    parser.add_argument("--port", default="COM18")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--focus", choices=sorted(PARAMS.keys()), default="lkp")
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--refine-rounds", type=int, default=3)
    parser.add_argument("--coarse-values", default="", help="Comma-separated explicit coarse values. Leave empty for auto tiers.")
    parser.add_argument("--frozen-lkd", type=float, default=None)
    parser.add_argument("--frozen-stb", type=float, default=None)
    args = parser.parse_args()

    out_dir = OUTPUT_ROOT / datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)

    current_focus, current_frozen = read_current_values(args.port, args.baud, args.focus, ["lkd", "stb"])
    if args.frozen_lkd is not None:
        current_frozen["lkd"] = args.frozen_lkd
    if args.frozen_stb is not None:
        current_frozen["stb"] = args.frozen_stb

    if args.coarse_values.strip():
        coarse_values = sorted({
            quantize(args.focus, float(part.strip()))
            for part in args.coarse_values.split(",")
            if part.strip()
        })
    else:
        coarse_values = auto_coarse_values(args.focus, current_focus)

    report: dict[str, object] = {
        "focus": args.focus,
        "current_focus": current_focus,
        "frozen": current_frozen,
        "coarse_values": coarse_values,
        "coarse_results": [],
        "refine_results": [],
    }

    print(f"[binary] focus={args.focus} current={current_focus} frozen={json.dumps(current_frozen, ensure_ascii=False)}")
    print(f"[binary] coarse_values={coarse_values}")

    coarse_results: list[TrialSummary] = []
    for value in coarse_values:
        summary = run_trial_group(
            args.python, args.port, args.baud, args.duration,
            args.focus, value, current_frozen, args.repeats, f"coarse_{value:g}"
        )
        coarse_results.append(summary)
        report["coarse_results"].append(asdict(summary))
        print(
            f"[binary] coarse value={value} avg={summary.avg_total:.2f} "
            f"grip={summary.avg_grip:.2f} smooth={summary.avg_smooth:.2f} "
            f"cover={summary.avg_cover:.2%} invalid={summary.invalid_runs}"
        )

    valid_coarse = [item for item in coarse_results if item.valid_runs > 0]
    if not valid_coarse:
        (out_dir / "binary_summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print("[binary] no valid coarse trials")
        return 1

    best = max(valid_coarse, key=trial_rank_key)
    spec = PARAMS[args.focus]
    span = float(spec["coarse_step"])

    for refine_round in range(1, args.refine_rounds + 1):
        span *= 0.5
        if span < float(spec["tolerance"]):
            break
        candidates = refine_values(args.focus, best.value, span)
        if not candidates:
            break
        print(f"[binary] refine_round={refine_round} around={best.value} span={span} candidates={candidates}")
        round_results: list[TrialSummary] = []
        for value in candidates:
            summary = run_trial_group(
                args.python, args.port, args.baud, args.duration,
                args.focus, value, current_frozen, args.repeats, f"refine{refine_round}_{value:g}"
            )
            round_results.append(summary)
            report["refine_results"].append(asdict(summary))
            print(
                f"[binary] refine value={value} avg={summary.avg_total:.2f} "
                f"grip={summary.avg_grip:.2f} smooth={summary.avg_smooth:.2f} "
                f"cover={summary.avg_cover:.2%} invalid={summary.invalid_runs}"
            )
        valid_round = [item for item in round_results if item.valid_runs > 0]
        if valid_round:
            round_best = max(valid_round, key=trial_rank_key)
            if trial_rank_key(round_best) > trial_rank_key(best):
                best = round_best

    report["best"] = asdict(best)
    (out_dir / "binary_summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    apply_param_set(args.port, args.baud, args.focus, best.value, current_frozen)
    print(f"[binary] best value={best.value} avg={best.avg_total:.2f} files={best.exp_files}")
    print(f"[binary] summary={out_dir / 'binary_summary.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
