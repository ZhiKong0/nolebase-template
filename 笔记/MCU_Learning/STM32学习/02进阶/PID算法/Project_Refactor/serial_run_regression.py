import argparse
import json
import os
import re
import time
from typing import Dict, List, Optional, Tuple

import serial


def now() -> float:
    return time.time()


def read_some(ser: serial.Serial, max_bytes: int = 4096) -> bytes:
    try:
        n = ser.in_waiting
        if n <= 0:
            return b""
        return ser.read(min(n, max_bytes))
    except Exception:
        return b""


def split_lines(buf: str) -> Tuple[List[str], str]:
    if not buf:
        return [], ""
    lines = buf.split("\n")
    tail = lines[-1]
    out = [ln.strip("\r") for ln in lines[:-1]]
    out = [ln for ln in out if ln]
    return out, tail


def send_cmd(ser: serial.Serial, cmd: str) -> None:
    if not cmd.endswith("!"):
        raise ValueError(f"cmd must end with '!': {cmd}")
    ser.write(cmd.encode("ascii", errors="ignore"))
    try:
        ser.flush()
    except Exception:
        pass


def write_log_line(fp, text: str) -> None:
    fp.write((text + "\n").encode("utf-8", errors="ignore"))
    fp.flush()
    print(text)


def parse_tick(line: str) -> Optional[int]:
    m = re.search(r"\btick=(\d+)\b", line)
    if not m:
        return None
    try:
        return int(m.group(1))
    except Exception:
        return None


def parse_run(line: str) -> Optional[int]:
    m = re.search(r"\brun=(\d+)\b", line)
    if not m:
        return None
    try:
        return int(m.group(1))
    except Exception:
        return None


def parse_rx(line: str) -> Optional[int]:
    m = re.search(r"\brx=(\d+)\b", line)
    if not m:
        return None
    try:
        return int(m.group(1))
    except Exception:
        return None


def collect_metrics(lines: List[str]) -> Dict[str, object]:
    hb_lines = [ln for ln in lines if ln.startswith("HB ")]
    trace_lines = [ln for ln in lines if ln.startswith("TRACE ")]
    boot_lines = [ln for ln in lines if ln.startswith("BOOT ")]
    ok_run = any(ln.startswith("OK RUN") for ln in lines)
    trace_run_ok = any("TRACE RUN_OK" in ln for ln in lines)
    trace_run_cmd = any("TRACE RUN_CMD" in ln for ln in lines)
    err_seen = any(ln.startswith("ERR") for ln in lines)
    run_values = [v for v in (parse_run(ln) for ln in hb_lines) if v is not None]
    run1_count = sum(1 for v in run_values if v == 1)
    ticks = [v for v in (parse_tick(ln) for ln in hb_lines) if v is not None]
    rx_values = [v for v in (parse_rx(ln) for ln in hb_lines) if v is not None]

    tick_reset_count = 0
    for i in range(1, len(ticks)):
        if ticks[i - 1] > 1000 and ticks[i] < 200:
            tick_reset_count += 1

    hb_after_run_cmd = 0
    run_cmd_idx = -1
    for i, ln in enumerate(lines):
        if "CMD #RUN!" in ln:
            run_cmd_idx = i
            break
    if run_cmd_idx >= 0:
        for ln in lines[run_cmd_idx + 1:]:
            if ln.startswith("HB "):
                hb_after_run_cmd += 1

    last_hb_tick = ticks[-1] if ticks else None
    first_hb_tick = ticks[0] if ticks else None
    last_rx = rx_values[-1] if rx_values else None

    return {
        "hb_count": len(hb_lines),
        "trace_count": len(trace_lines),
        "boot_count": len(boot_lines),
        "ok_run": ok_run,
        "trace_run_ok": trace_run_ok,
        "trace_run_cmd": trace_run_cmd,
        "err_seen": err_seen,
        "run1_count": run1_count,
        "tick_reset_count": tick_reset_count,
        "hb_after_run_cmd": hb_after_run_cmd,
        "first_hb_tick": first_hb_tick,
        "last_hb_tick": last_hb_tick,
        "last_rx": last_rx,
    }


def evaluate(metrics: Dict[str, object]) -> Dict[str, object]:
    ok_run = bool(metrics["ok_run"])
    trace_run_ok = bool(metrics["trace_run_ok"])
    run1_count = int(metrics["run1_count"])
    hb_after_run_cmd = int(metrics["hb_after_run_cmd"])
    tick_reset_count = int(metrics["tick_reset_count"])
    err_seen = bool(metrics["err_seen"])

    passed = ok_run and trace_run_ok and run1_count > 0 and hb_after_run_cmd >= 10 and tick_reset_count == 0 and not err_seen

    reasons: List[str] = []
    if not ok_run:
        reasons.append("missing_ok_run")
    if not trace_run_ok:
        reasons.append("missing_trace_run_ok")
    if run1_count <= 0:
        reasons.append("missing_run1")
    if hb_after_run_cmd < 10:
        reasons.append("insufficient_hb_after_run")
    if tick_reset_count > 0:
        reasons.append("tick_reset_detected")
    if err_seen:
        reasons.append("err_seen")

    return {
        "passed": passed,
        "reasons": reasons,
    }


def drain_lines(ser: serial.Serial, duration_s: float, fp, echo: bool, sink: List[str]) -> None:
    start = now()
    text_buf = ""
    while now() - start < duration_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue
        fp.write(b)
        fp.flush()
        text_buf += b.decode("utf-8", errors="replace")
        lines, text_buf = split_lines(text_buf)
        for ln in lines:
            sink.append(ln)
            if echo:
                print(ln)
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]


def send_and_watch(ser: serial.Serial, fp, sink: List[str], cmd: str, watch_s: float, echo: bool) -> None:
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    marker = f"CMD {cmd}"
    sink.append(marker)
    write_log_line(fp, marker)
    send_cmd(ser, cmd)
    drain_lines(ser, watch_s, fp=fp, echo=echo, sink=sink)


def build_paths(base_dir: str, name: str) -> Tuple[str, str]:
    os.makedirs(base_dir, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    raw_path = os.path.join(base_dir, f"{name}_{ts}.txt")
    json_path = os.path.join(base_dir, f"{name}_{ts}.json")
    return raw_path, json_path


def run_regression(args: argparse.Namespace) -> Tuple[str, str, Dict[str, object]]:
    base_dir = os.path.join(os.path.dirname(__file__), "000Data")
    raw_path, json_path = build_paths(base_dir, f"run_regression_{args.mode}")
    lines: List[str] = []

    with serial.Serial(
        port=args.port,
        baudrate=args.baud,
        timeout=0.1,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as ser, open(raw_path, "wb") as fp:
        try:
            ser.dtr = False
            ser.rts = False
        except Exception:
            pass
        try:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
        except Exception:
            pass

        drain_lines(ser, args.pre_idle_s, fp=fp, echo=args.echo, sink=lines)

        setup_cmds = [
            "#STOP!",
            f"#PWM_MAX={args.pwm_max}!",
            f"#DIFF_MAX={args.diff_max}!",
            f"#SO={args.so}!",
            f"#TRIM={args.trim}!",
            f"#TS={args.ts}!",
        ]
        for cmd in setup_cmds:
            send_and_watch(ser, fp, lines, cmd, args.cmd_watch_s, args.echo)

        if args.mode == "exp_start_run":
            send_and_watch(ser, fp, lines, f"#EXP=START,{args.exp_id},{args.ms}!", args.exp_watch_s, args.echo)
            send_and_watch(ser, fp, lines, "#RUN!", args.run_watch_s, args.echo)
        else:
            send_and_watch(ser, fp, lines, "#RUN!", args.run_watch_s, args.echo)

        send_and_watch(ser, fp, lines, "#STAT!", args.stat_watch_s, args.echo)
        drain_lines(ser, args.tail_watch_s, fp=fp, echo=args.echo, sink=lines)

    metrics = collect_metrics(lines)
    verdict = evaluate(metrics)
    result = {
        "port": args.port,
        "baud": args.baud,
        "mode": args.mode,
        "exp_id": args.exp_id,
        "ms": args.ms,
        "ts": args.ts,
        "so": args.so,
        "trim": args.trim,
        "pwm_max": args.pwm_max,
        "diff_max": args.diff_max,
        "raw_log": raw_path,
        "metrics": metrics,
        "verdict": verdict,
    }

    with open(json_path, "w", encoding="utf-8") as fp:
        json.dump(result, fp, ensure_ascii=False, indent=2)

    return raw_path, json_path, result


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM18")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--mode", choices=["run", "exp_start_run"], default="exp_start_run")
    p.add_argument("--exp-id", type=int, default=2001)
    p.add_argument("--ms", type=int, default=1200)
    p.add_argument("--ts", type=float, default=100.0)
    p.add_argument("--so", type=int, default=90)
    p.add_argument("--trim", type=float, default=0.0)
    p.add_argument("--pwm-max", type=int, default=60)
    p.add_argument("--diff-max", type=int, default=20)
    p.add_argument("--pre-idle-s", type=float, default=0.8)
    p.add_argument("--cmd-watch-s", type=float, default=0.45)
    p.add_argument("--exp-watch-s", type=float, default=1.2)
    p.add_argument("--run-watch-s", type=float, default=3.0)
    p.add_argument("--stat-watch-s", type=float, default=0.8)
    p.add_argument("--tail-watch-s", type=float, default=0.6)
    p.add_argument("--echo", action="store_true")
    return p


def main() -> None:
    args = build_parser().parse_args()
    raw_path, json_path, result = run_regression(args)
    print(json.dumps(result["verdict"], ensure_ascii=False))
    print(raw_path)
    print(json_path)


if __name__ == "__main__":
    main()
