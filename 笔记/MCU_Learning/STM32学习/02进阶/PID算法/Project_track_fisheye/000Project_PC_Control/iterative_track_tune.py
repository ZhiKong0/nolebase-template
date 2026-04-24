#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import serial


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LOGGER_SCRIPT = PROJECT_ROOT / "000Project_PC_Control" / "experiment_logger.py"
SCORE_SCRIPT = PROJECT_ROOT / "000Project_PC_Control" / "experiment_score_watch.py"
EXPERIMENT_DIR = PROJECT_ROOT / "000Data" / "serial_runs" / "experiments"
OUTPUT_ROOT = PROJECT_ROOT / "000Data" / "iterative_track_tune"

PARAM_SPECS = {
    "track.lkp": (8.0, 28.0, 3),
    "track.lkd": (0.0, 18.0, 3),
    "track.outer_gain": (0.20, 0.90, 3),
    "track.loss_hold_gain": (0.20, 0.90, 3),
    "track.error_scale": (30.0, 120.0, 2),
    "track.dev_ratio": (0.20, 0.95, 3),
    "track.dev_step_limit": (12.0, 120.0, 1),
    "track.search_turn_fast": (220.0, 450.0, 1),
    "track.search_turn_slow": (120.0, 360.0, 1),
    "track.search_timeout": (10.0, 120.0, 0),
    "track.search_side_exit_ticks": (1.0, 8.0, 0),
}

PRIMARY_KEYS = [
    "track.lkp",
    "track.lkd",
    "track.outer_gain",
    "track.loss_hold_gain",
    "track.error_scale",
    "track.dev_ratio",
    "track.dev_step_limit",
    "track.search_turn_fast",
    "track.search_turn_slow",
]

PARAM_LINE_RE = re.compile(r"OK:TCFG GET ([^=]+)=([-+]?\d+(?:\.\d+)?)")
EXP_FILE_RE = re.compile(r"exp_\d+_\d{8}_\d{6}_[A-Z]+_[A-Z]\.txt")


@dataclass
class RoundResult:
    round_index: int
    exp_file: str
    total_score: float
    grip_score: float
    speed_smoothness_score: float
    center_score: float
    a67_cover_ratio: float
    a67_exact_ratio: float
    search_ratio: float
    loss_ratio: float
    primary_param: str
    primary_action: str
    primary_reason: str
    secondary_param: str
    secondary_action: str
    secondary_reason: str
    params: dict[str, float]


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def round_to_digits(value: float, digits: int) -> float:
    return round(value, digits)


def send_cmd(port: serial.Serial, cmd: str, timeout_s: float = 1.2) -> list[str]:
    port.reset_input_buffer()
    port.write((cmd if cmd.endswith("!") else f"{cmd}!").encode("ascii"))
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
        if line.startswith("OK") or line.startswith("ERR") or line == "ERR":
            break
    return lines


def get_param(port: serial.Serial, key: str) -> float:
    lines = send_cmd(port, f"#TCFG GET {key}", timeout_s=2.5)
    for line in lines:
        match = PARAM_LINE_RE.match(line)
        if match and match.group(1) == key:
            return float(match.group(2))
    raise RuntimeError(f"读取参数失败: {key} -> {lines}")


def set_param(port: serial.Serial, key: str, value: float, digits: int) -> float:
    lines = send_cmd(port, f"#TCFG SET {key} {value:.{digits}f}", timeout_s=2.5)
    for line in lines:
        match = PARAM_LINE_RE.match(line.replace("SET ", "GET "))
        if line.startswith("OK:TCFG SET "):
            try:
                return float(line.rsplit("=", 1)[1])
            except Exception:
                break
    raise RuntimeError(f"写参数失败: {key}={value} -> {lines}")


def snapshot_params(port: serial.Serial, keys: list[str]) -> dict[str, float]:
    values: dict[str, float] = {}
    for key in keys:
        values[key] = get_param(port, key)
    return values


def parse_action_delta(action: str) -> float | None:
    action = action.strip()
    if action.startswith("+") or action.startswith("-"):
        try:
            return float(action)
        except ValueError:
            return None
    return None


def adjust_value(key: str, current: float, action: str) -> float | None:
    delta = parse_action_delta(action)
    if delta is None:
        return None
    low, high, digits = PARAM_SPECS[key]
    return round_to_digits(clamp(current + delta, low, high), digits)


def run_logger_round(python_exe: str, port: str, duration_s: float, mode: str) -> Path:
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
        mode,
        "--max-seconds",
        f"{duration_s + 6.0:.3f}",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="ignore", check=True)
    stdout = proc.stdout + "\n" + proc.stderr
    matches = EXP_FILE_RE.findall(stdout)
    if matches:
        return EXPERIMENT_DIR / matches[-1]
    after = sorted(p for p in EXPERIMENT_DIR.glob("exp_*.txt") if p.name not in before)
    if after:
        return after[-1]
    raise RuntimeError(f"未找到新实验文件。\n{stdout}")


def score_experiment(python_exe: str, exp_path: Path) -> dict[str, object]:
    cmd = [python_exe, str(SCORE_SCRIPT), "--once", str(exp_path), "--print-json"]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="ignore", check=True)
    return json.loads(proc.stdout)


def should_stop(summary: dict[str, object], rounds_without_improve: int) -> bool:
    total = float(summary["total_score"])
    cover = float(summary["a67_cover_ratio"])
    exact = float(summary["a67_exact_ratio"])
    search_ratio = float(summary["search_ratio"])
    loss_ratio = float(summary["loss_ratio"])
    if total >= 82.0 and cover >= 0.65 and exact >= 0.18 and search_ratio <= 0.06 and loss_ratio <= 0.04:
        return True
    if rounds_without_improve >= 3 and total >= 72.0:
        return True
    return False


def format_round(summary: dict[str, object]) -> str:
    return (
        f"score={float(summary['total_score']):.2f} "
        f"grip={float(summary['grip_score']):.2f} "
        f"smooth={float(summary['speed_smoothness_score']):.2f} "
        f"a67={float(summary['a67_cover_ratio']):.2%} "
        f"exact={float(summary['a67_exact_ratio']):.2%} "
        f"search={float(summary['search_ratio']):.2%} "
        f"loss={float(summary['loss_ratio']):.2%}"
    )


def save_round_log(out_dir: Path, rounds: list[RoundResult], best: RoundResult | None) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    data = {
        "best": None if best is None else {
            "round_index": best.round_index,
            "exp_file": best.exp_file,
            "total_score": best.total_score,
            "params": best.params,
        },
        "rounds": [r.__dict__ for r in rounds],
    }
    (out_dir / "iterative_summary.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def with_serial(port_name: str, baud: int):
    return serial.Serial(port_name, baud, timeout=0.35)


def main() -> int:
    parser = argparse.ArgumentParser(description="Iterative 8-second track tuning loop without lowering speed target.")
    parser.add_argument("--port", default="COM18")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--mode", default="TRACK", choices=["TRACK"])
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = OUTPUT_ROOT / timestamp
    out_dir.mkdir(parents=True, exist_ok=True)

    rounds: list[RoundResult] = []
    best_round: RoundResult | None = None
    rounds_without_improve = 0

    with with_serial(args.port, args.baud) as port:
        time.sleep(0.2)
        current_params = snapshot_params(port, PRIMARY_KEYS)

    (out_dir / "initial_params.json").write_text(
        json.dumps(current_params, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    for round_index in range(1, args.rounds + 1):
        print(f"[iter] round {round_index} params={json.dumps(current_params, ensure_ascii=False)}")
        with with_serial(args.port, args.baud) as port:
            time.sleep(0.15)
            for key, value in current_params.items():
                _, _, digits = PARAM_SPECS[key]
                applied = set_param(port, key, value, digits)
                current_params[key] = applied

        exp_path = run_logger_round(args.python, args.port, args.duration, args.mode)
        summary = score_experiment(args.python, exp_path)
        print(f"[iter] {exp_path.name} {format_round(summary)}")

        round_result = RoundResult(
            round_index=round_index,
            exp_file=exp_path.name,
            total_score=float(summary["total_score"]),
            grip_score=float(summary["grip_score"]),
            speed_smoothness_score=float(summary["speed_smoothness_score"]),
            center_score=float(summary["center_score"]),
            a67_cover_ratio=float(summary["a67_cover_ratio"]),
            a67_exact_ratio=float(summary["a67_exact_ratio"]),
            search_ratio=float(summary["search_ratio"]),
            loss_ratio=float(summary["loss_ratio"]),
            primary_param=str(summary["primary_param"]),
            primary_action=str(summary["primary_action"]),
            primary_reason=str(summary["primary_reason"]),
            secondary_param=str(summary["secondary_param"]),
            secondary_action=str(summary["secondary_action"]),
            secondary_reason=str(summary["secondary_reason"]),
            params=dict(current_params),
        )
        rounds.append(round_result)

        improved = False
        if best_round is None or round_result.total_score > best_round.total_score:
            best_round = round_result
            improved = True
            rounds_without_improve = 0
        else:
            rounds_without_improve += 1

        save_round_log(out_dir, rounds, best_round)

        if should_stop(summary, rounds_without_improve):
            print(f"[iter] stop condition met at round {round_index}")
            break

        next_params = dict(current_params)
        primary_key = str(summary["primary_param"])
        secondary_key = str(summary["secondary_param"])
        primary_action = str(summary["primary_action"])
        secondary_action = str(summary["secondary_action"])

        if primary_key in PARAM_SPECS:
            updated = adjust_value(primary_key, next_params[primary_key], primary_action)
            if updated is not None:
                next_params[primary_key] = updated

        if not improved and secondary_key in PARAM_SPECS:
            updated = adjust_value(secondary_key, next_params[secondary_key], secondary_action)
            if updated is not None:
                next_params[secondary_key] = updated

        current_params = next_params

    if best_round is not None:
        print(
            f"[iter] best round={best_round.round_index} file={best_round.exp_file} "
            f"score={best_round.total_score:.2f} params={json.dumps(best_round.params, ensure_ascii=False)}"
        )
    else:
        print("[iter] no valid round")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
