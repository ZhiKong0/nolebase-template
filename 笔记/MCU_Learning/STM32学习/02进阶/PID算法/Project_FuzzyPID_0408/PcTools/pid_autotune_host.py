#!/usr/bin/env python3
"""Serial PID autotuner for Project_FuzzyPID_0408.

This tool closes the loop from the PC side:
1. Reads JSON telemetry from the STM32 over USART2.
2. Applies host-computed heading PID gains in learning mode.
3. Scores each trial and iteratively improves the gains.
4. Saves the best result locally and optionally commits it to MCU flash.
"""

from __future__ import annotations

import argparse
from collections import deque
import json
import math
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Deque, Dict, Iterable, List, Optional

try:
    import serial  # type: ignore
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required. Install it with: pip install pyserial") from exc


GAIN_LIMITS = {
    "kp": (0.10, 6.00),
    "ki": (0.00, 2.00),
    "kd": (0.00, 2.00),
}


@dataclass
class PidGains:
    kp: float
    ki: float
    kd: float

    def clamp(self) -> "PidGains":
        return PidGains(
            kp=min(max(self.kp, GAIN_LIMITS["kp"][0]), GAIN_LIMITS["kp"][1]),
            ki=min(max(self.ki, GAIN_LIMITS["ki"][0]), GAIN_LIMITS["ki"][1]),
            kd=min(max(self.kd, GAIN_LIMITS["kd"][0]), GAIN_LIMITS["kd"][1]),
        )

    def to_payload(self) -> Dict[str, float]:
        return {"kp": self.kp, "ki": self.ki, "kd": self.kd}


@dataclass
class TrialResult:
    gains: PidGains
    score: float
    samples: int
    summary: Dict[str, float]
    log_path: Path


class McuLink:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        self._ser = serial.Serial(port=port, baudrate=baud, timeout=timeout)
        self._rx_buffer = bytearray()
        self._messages: Deque[Dict[str, object]] = deque()

    def close(self) -> None:
        self._ser.close()

    def flush(self, drain_seconds: float = 0.25) -> None:
        del drain_seconds
        self._rx_buffer.clear()
        self._messages.clear()

    def send_json(self, payload: Dict[str, object]) -> None:
        line = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
        self._ser.write((line + "\n").encode("utf-8"))
        self._ser.flush()

    def send_and_wait_ack(
        self,
        payload: Dict[str, object],
        item: str,
        timeout: float = 2.0,
        retries: int = 1,
    ) -> Optional[str]:
        for attempt in range(retries):
            self.send_json(payload)
            ack = self.wait_for_ack(item, timeout=timeout)
            if ack is not None:
                return ack
            if attempt + 1 < retries:
                time.sleep(0.2)
        return None

    @staticmethod
    def _find_next_marker(buffer: bytes, start_index: int) -> int:
        candidates = [buffer.find(marker, start_index) for marker in (b'{"time"', b'{"ack"')]
        candidates = [index for index in candidates if index >= 0]
        return min(candidates) if candidates else -1

    def _queue_objects_from_fragment(self, fragment: bytes) -> None:
        cursor = 0
        while cursor < len(fragment):
            start = self._find_next_marker(fragment, cursor)
            if start < 0:
                break

            depth = 0
            end = -1
            for idx in range(start, len(fragment)):
                byte = fragment[idx]
                if byte == 0x7B:
                    depth += 1
                elif byte == 0x7D:
                    depth -= 1
                    if depth == 0:
                        end = idx + 1
                        break

            if end < 0:
                break

            text = fragment[start:end].decode("utf-8", errors="ignore")
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                cursor = start + 1
                continue

            if isinstance(obj, dict):
                self._messages.append(obj)
            cursor = end

    def _process_rx_buffer(self) -> None:
        newline = self._rx_buffer.find(b"\n")
        while newline >= 0:
            fragment = bytes(self._rx_buffer[:newline]).rstrip(b"\r")
            del self._rx_buffer[:newline + 1]
            if fragment:
                self._queue_objects_from_fragment(fragment)
            newline = self._rx_buffer.find(b"\n")

        if len(self._rx_buffer) > 8192:
            marker = self._find_next_marker(self._rx_buffer, len(self._rx_buffer) - 2048)
            if marker >= 0:
                self._rx_buffer = self._rx_buffer[marker:]
            else:
                self._rx_buffer = self._rx_buffer[-2048:]

    def read_message(self, timeout: float) -> Optional[Dict[str, object]]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            self._process_rx_buffer()
            if self._messages:
                return self._messages.popleft()

            chunk_size = self._ser.in_waiting
            raw = self._ser.read(chunk_size if chunk_size > 0 else 256)
            if not raw:
                continue
            self._rx_buffer.extend(raw)
            self._process_rx_buffer()
            if self._messages:
                return self._messages.popleft()
        return None

    def wait_for_ack(self, item: str, timeout: float = 2.0) -> Optional[str]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            msg = self.read_message(0.2)
            if (msg is not None) and (msg.get("item") == item) and ("ack" in msg):
                ack = msg.get("ack")
                if isinstance(ack, str):
                    return ack
        return None

    def wait_for_telemetry(self, timeout: float = 3.0) -> Optional[Dict[str, object]]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            msg = self.read_message(0.2)
            if msg is not None and "time" in msg and "state" in msg:
                return msg
        return None

    def request_status(self, timeout: float = 2.0) -> Dict[str, object]:
        status = self.wait_for_telemetry(timeout=timeout)
        if status is not None:
            return status
        raise RuntimeError("No telemetry status received from MCU")

    def collect_status_messages(self, duration: float) -> List[Dict[str, object]]:
        messages: List[Dict[str, object]] = []
        deadline = time.time() + duration
        while time.time() < deadline:
            msg = self.read_message(0.2)
            if msg is not None and "time" in msg and "state" in msg:
                messages.append(msg)
        return messages


def get_nested_float(obj: Dict[str, object], *path: str, default: float = 0.0) -> float:
    node: object = obj
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    if isinstance(node, (int, float)):
        return float(node)
    return default


def get_nested_str(obj: Dict[str, object], *path: str, default: str = "") -> str:
    node: object = obj
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node if isinstance(node, str) else default


def approx_equal(value: float, target: float, tolerance: float) -> bool:
    return abs(value - target) <= tolerance


def wait_for_telemetry_condition(
    link: McuLink,
    predicate,
    timeout: float,
) -> Optional[Dict[str, object]]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = link.read_message(0.2)
        if msg is None or "time" not in msg or "state" not in msg:
            continue
        if predicate(msg):
            return msg
    return None


def get_heading_gains(status: Dict[str, object], fallback: PidGains) -> PidGains:
    gains = status.get("gains")
    if not isinstance(gains, dict):
        return fallback

    heading = gains.get("heading")
    if not isinstance(heading, dict):
        return fallback

    return PidGains(
        kp=float(heading.get("kp", fallback.kp)),
        ki=float(heading.get("ki", fallback.ki)),
        kd=float(heading.get("kd", fallback.kd)),
    ).clamp()


def score_samples(samples: Iterable[Dict[str, object]], warmup_seconds: float) -> Dict[str, float]:
    data = list(samples)
    if len(data) < 3:
        return {"score": float("inf"), "ise": float("inf"), "overshoot": float("inf"), "control_effort": float("inf")}

    t0 = get_nested_float(data[0], "time", default=0.0)
    active = [msg for msg in data if (get_nested_float(msg, "time", default=t0) - t0) >= warmup_seconds]
    if len(active) < 3:
        active = data

    ise = 0.0
    rate_energy = 0.0
    control_effort = 0.0
    overshoot = 0.0
    fault_penalty = 0.0
    prev_time = get_nested_float(active[0], "time", default=t0)

    for msg in active:
        now = get_nested_float(msg, "time", default=prev_time)
        dt = max(now - prev_time, 0.05)
        err = get_nested_float(msg, "errors", "e")
        err_rate = get_nested_float(msg, "errors", "ec")
        pwm_l = get_nested_float(msg, "control", "pwm_l")
        pwm_r = get_nested_float(msg, "control", "pwm_r")
        state = get_nested_str(msg, "state")

        ise += err * err * dt
        rate_energy += err_rate * err_rate * dt
        control_effort += (abs(pwm_l) + abs(pwm_r)) * 0.5 * dt
        overshoot = max(overshoot, abs(err))
        if state == "fault":
            fault_penalty = 500.0
        prev_time = now

    duration = max(get_nested_float(active[-1], "time", default=prev_time) - get_nested_float(active[0], "time", default=t0), 0.1)
    mean_effort = control_effort / duration
    settling = get_nested_float(active[-1], "performance", "settling", default=duration)
    score = (
        0.55 * ise
        + 0.20 * rate_energy
        + 0.10 * overshoot
        + 0.10 * mean_effort
        + 0.05 * settling
        + fault_penalty
    )

    return {
        "score": score,
        "ise": ise,
        "rate_energy": rate_energy,
        "overshoot": overshoot,
        "control_effort": mean_effort,
        "settling": settling,
        "duration": duration,
    }


def write_trial_log(out_dir: Path, trial_index: int, gains: PidGains, samples: List[Dict[str, object]], summary: Dict[str, float]) -> Path:
    path = out_dir / f"trial_{trial_index:03d}.json"
    payload = {
        "trial": trial_index,
        "gains": gains.to_payload(),
        "summary": summary,
        "samples": samples,
    }
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    return path


def expect_ok(ack: Optional[str], item: str) -> None:
    if ack != "ok":
        raise RuntimeError(f"Command '{item}' failed, ack={ack!r}")


def acquire_boot_status(link: McuLink, timeout: float = 3.0, retries: int = 3) -> Dict[str, object]:
    for attempt in range(retries):
        deadline = time.time() + timeout
        boot_seen = False
        while time.time() < deadline:
            msg = link.read_message(0.2)
            if msg is None:
                continue

            if msg.get("item") == "boot" and "ack" in msg:
                boot_seen = True
                continue

            if "time" in msg and "state" in msg:
                if boot_seen:
                    return msg
                if get_nested_float(msg, "time", default=999.0) <= 1.0:
                    return msg

        if attempt + 1 < retries:
            time.sleep(0.4)

    raise RuntimeError("No telemetry status received from MCU")


def configure_vehicle(link: McuLink, args: argparse.Namespace) -> None:
    link.send_json({"cmd": "stop"})
    link.send_json({"cmd": "clear_fault"})
    wait_for_telemetry_condition(
        link,
        lambda msg: get_nested_str(msg, "state") == "idle",
        timeout=2.0,
    )

    link.send_json({"mode": args.run_mode})
    mode_applied = wait_for_telemetry_condition(
        link,
        lambda msg: get_nested_str(msg, "mode") == args.run_mode,
        timeout=4.0,
    )
    if mode_applied is None:
        raise RuntimeError("Run mode was not reflected in telemetry")

    link.send_json({"tune": "learning"})
    tune_applied = wait_for_telemetry_condition(
        link,
        lambda msg: get_nested_str(msg, "tune") == "learning",
        timeout=4.0,
    )
    if tune_applied is None:
        raise RuntimeError("Tune mode was not reflected in telemetry")

    link.send_json({"speed": args.speed})
    speed_applied = wait_for_telemetry_condition(
        link,
        lambda msg: approx_equal(get_nested_float(msg, "config", "target_speed"), float(args.speed), 0.25),
        timeout=4.0,
    )
    if speed_applied is None:
        raise RuntimeError("Target speed was not reflected in telemetry")

    link.send_json({"heading": args.trim})
    trim_applied = wait_for_telemetry_condition(
        link,
        lambda msg: approx_equal(get_nested_float(msg, "config", "trim"), float(args.trim), 0.25),
        timeout=4.0,
    )
    if trim_applied is None:
        raise RuntimeError("Heading trim was not reflected in telemetry")

    if args.speed_kp is not None:
        link.send_json({
            "cmd": "update_speed_pid",
            "kp": args.speed_kp,
            "ki": args.speed_ki,
            "kd": args.speed_kd,
        })

    configured = wait_for_telemetry_condition(
        link,
        lambda msg: (
            get_nested_str(msg, "mode") == args.run_mode
            and get_nested_str(msg, "tune") == "learning"
            and approx_equal(get_nested_float(msg, "config", "target_speed"), float(args.speed), 0.25)
            and approx_equal(get_nested_float(msg, "config", "trim"), float(args.trim), 0.25)
        ),
        timeout=3.0,
    )
    if configured is None:
        raise RuntimeError("Telemetry stream did not stabilize after configuration")


def run_trial(link: McuLink, out_dir: Path, trial_index: int, gains: PidGains, args: argparse.Namespace) -> TrialResult:
    link.flush()
    link.send_json({"cmd": "stop"})
    link.send_json({"cmd": "clear_fault"})
    link.send_json({"tune": "learning"})
    link.send_json({
        "cmd": "update_heading_pid",
        "kp": gains.kp,
        "ki": gains.ki,
        "kd": gains.kd,
    })

    applied = wait_for_telemetry_condition(
        link,
        lambda msg: (
            get_nested_str(msg, "tune") == "learning"
            and approx_equal(get_nested_float(msg, "gains", "heading", "kp"), gains.kp, 0.02)
            and approx_equal(get_nested_float(msg, "gains", "heading", "ki"), gains.ki, 0.02)
            and approx_equal(get_nested_float(msg, "gains", "heading", "kd"), gains.kd, 0.02)
        ),
        timeout=6.0,
    )
    if applied is None:
        raise RuntimeError("Heading PID update was not reflected in telemetry")

    link.send_json({"cmd": "start"})
    running = wait_for_telemetry_condition(
        link,
        lambda msg: get_nested_str(msg, "state") == "running",
        timeout=4.0,
    )
    if running is None:
        summary = {"score": float("inf"), "ise": float("inf"), "overshoot": float("inf"), "control_effort": float("inf")}
        log_path = write_trial_log(out_dir, trial_index, gains, [], summary)
        return TrialResult(gains=gains, score=float("inf"), samples=0, summary=summary, log_path=log_path)

    samples = [running]
    remaining = max(args.trial_seconds - 0.1, 0.0)
    samples.extend(link.collect_status_messages(remaining))
    link.send_json({"cmd": "stop"})
    wait_for_telemetry_condition(
        link,
        lambda msg: get_nested_str(msg, "state") == "idle",
        timeout=3.0,
    )

    summary = score_samples(samples, args.warmup_seconds)
    log_path = write_trial_log(out_dir, trial_index, gains, samples, summary)
    return TrialResult(
        gains=gains,
        score=summary["score"],
        samples=len(samples),
        summary=summary,
        log_path=log_path,
    )


def optimize_heading_pid(link: McuLink, out_dir: Path, initial: PidGains, args: argparse.Namespace) -> Dict[str, object]:
    deltas = {"kp": args.kp_step, "ki": args.ki_step, "kd": args.kd_step}
    best = run_trial(link, out_dir, 0, initial.clamp(), args)
    trial_counter = 1
    history: List[Dict[str, object]] = [{
        "trial": 0,
        "gains": best.gains.to_payload(),
        "score": best.score,
        "samples": best.samples,
        "summary": best.summary,
        "log_path": str(best.log_path),
    }]

    current = best.gains
    for round_index in range(args.rounds):
        improved = False
        for key in ("kp", "ki", "kd"):
            for direction in (1.0, -1.0):
                candidate = PidGains(current.kp, current.ki, current.kd)
                setattr(candidate, key, getattr(candidate, key) + deltas[key] * direction)
                candidate = candidate.clamp()

                result = run_trial(link, out_dir, trial_counter, candidate, args)
                trial_counter += 1
                history.append({
                    "trial": trial_counter - 1,
                    "gains": result.gains.to_payload(),
                    "score": result.score,
                    "samples": result.samples,
                    "summary": result.summary,
                    "log_path": str(result.log_path),
                })

                if result.score < best.score:
                    best = result
                    current = result.gains
                    deltas[key] *= 1.15
                    improved = True
                    break
            else:
                deltas[key] *= 0.5
                continue
            break

        if (not improved) and (sum(deltas.values()) < args.stop_delta_sum):
            break

        if not improved:
            continue

        history.append({
            "round": round_index + 1,
            "best_gains": best.gains.to_payload(),
            "best_score": best.score,
            "deltas": deltas.copy(),
        })

    return {"best": best, "history": history}


def persist_best(link: McuLink, best: PidGains, args: argparse.Namespace) -> None:
    link.send_json({"cmd": "stop"})
    link.send_json({"tune": args.persist_mode})
    link.send_json({
        "cmd": "update_heading_pid",
        "kp": best.kp,
        "ki": best.ki,
        "kd": best.kd,
    })

    applied = wait_for_telemetry_condition(
        link,
        lambda msg: (
            get_nested_str(msg, "tune") == args.persist_mode
            and approx_equal(get_nested_float(msg, "gains", "heading", "kp"), best.kp, 0.02)
            and approx_equal(get_nested_float(msg, "gains", "heading", "ki"), best.ki, 0.02)
            and approx_equal(get_nested_float(msg, "gains", "heading", "kd"), best.kd, 0.02)
        ),
        timeout=6.0,
    )
    if applied is None:
        raise RuntimeError("Persist phase did not reach expected heading gains")

    if args.save_to_mcu:
        expect_ok(link.send_and_wait_ack({"cmd": "save_profile"}, "profile_save", timeout=3.0, retries=2), "profile_save")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Host-side heading PID autotuner for Project_FuzzyPID_0408")
    parser.add_argument("--port", required=True, help="Serial port, for example COM18")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate")
    parser.add_argument("--run-mode", choices=("straight", "track"), default="straight", help="Vehicle run mode during tuning")
    parser.add_argument("--speed", type=float, default=5.0, help="Configured target speed in RPM during tuning")
    parser.add_argument("--trim", type=float, default=0.0, help="Heading trim command sent to MCU")
    parser.add_argument("--heading-kp", type=float, default=1.20, help="Initial heading Kp")
    parser.add_argument("--heading-ki", type=float, default=0.08, help="Initial heading Ki")
    parser.add_argument("--heading-kd", type=float, default=0.22, help="Initial heading Kd")
    parser.add_argument("--speed-kp", type=float, default=None, help="Optional speed-loop Kp override")
    parser.add_argument("--speed-ki", type=float, default=0.35, help="Optional speed-loop Ki override")
    parser.add_argument("--speed-kd", type=float, default=0.02, help="Optional speed-loop Kd override")
    parser.add_argument("--kp-step", type=float, default=0.20, help="Twiddle step for heading Kp")
    parser.add_argument("--ki-step", type=float, default=0.02, help="Twiddle step for heading Ki")
    parser.add_argument("--kd-step", type=float, default=0.03, help="Twiddle step for heading Kd")
    parser.add_argument("--rounds", type=int, default=4, help="Maximum optimization rounds")
    parser.add_argument("--trial-seconds", type=float, default=5.0, help="Telemetry capture time per trial")
    parser.add_argument("--warmup-seconds", type=float, default=0.8, help="Warmup section ignored in score")
    parser.add_argument("--stop-delta-sum", type=float, default=0.03, help="Stop when sum of steps becomes smaller than this value")
    parser.add_argument("--persist-mode", choices=("fixed", "learning"), default="fixed", help="Tune mode stored into MCU after autotune")
    parser.add_argument("--save-to-mcu", action="store_true", default=True, help="Save best gains into MCU flash")
    parser.add_argument("--no-save-to-mcu", dest="save_to_mcu", action="store_false", help="Only save result locally")
    parser.add_argument("--out-dir", default=str(Path("PcTools") / "results"), help="Directory for trial logs and summary")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path(args.out_dir) / f"pid_autotune_{timestamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    initial = PidGains(args.heading_kp, args.heading_ki, args.heading_kd).clamp()
    link = McuLink(args.port, args.baud, timeout=0.2)

    try:
        time.sleep(4.2)
        try:
            boot_status = acquire_boot_status(link, timeout=6.0, retries=1)
        except RuntimeError:
            boot_status = None
        live_initial = get_heading_gains(boot_status, initial) if boot_status is not None else initial
        configure_vehicle(link, args)
        result = optimize_heading_pid(link, out_dir, live_initial, args)
        best: TrialResult = result["best"]
        persist_best(link, best.gains, args)
        final_status = link.request_status(timeout=2.0)

        summary = {
            "timestamp": timestamp,
            "port": args.port,
            "baud": args.baud,
            "run_mode": args.run_mode,
            "persist_mode": args.persist_mode,
            "save_to_mcu": args.save_to_mcu,
            "initial_gains": initial.to_payload(),
            "live_initial_gains": live_initial.to_payload(),
            "best_gains": best.gains.to_payload(),
            "best_score": best.score,
            "best_summary": best.summary,
            "best_log_path": str(best.log_path),
            "history": result["history"],
            "final_status": final_status,
        }
        summary_path = out_dir / "autotune_summary.json"
        summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

        print(json.dumps({
            "best_gains": best.gains.to_payload(),
            "best_score": best.score,
            "summary_path": str(summary_path),
        }, ensure_ascii=False, indent=2))
        return 0
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
