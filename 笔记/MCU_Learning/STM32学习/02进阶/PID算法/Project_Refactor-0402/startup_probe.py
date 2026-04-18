import argparse
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
    lines = buf.replace("\r", "\n").split("\n")
    tail = lines[-1]
    out = [ln.strip() for ln in lines[:-1]]
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


def drain_lines(
    ser: serial.Serial,
    duration_s: float,
    fp=None,
    tail_state: Optional[Dict[str, str]] = None,
    echo: bool = False,
) -> List[str]:
    start = now()
    text_buf = tail_state.get("tail", "") if tail_state is not None else ""
    out: List[str] = []
    while now() - start < duration_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue
        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        for ln in lines:
            out.append(ln)
            if fp is not None:
                fp.write((ln + "\n").encode("utf-8", errors="ignore"))
            if echo:
                print(ln)
        if fp is not None and lines:
            fp.flush()
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]
    if tail_state is not None:
        tail_state["tail"] = text_buf
    return out


def extract_rx(line: str) -> Optional[int]:
    if not line.startswith("HB ") and not line.startswith("STAT "):
        return None
    m = re.search(r"\brx=(\d+)\b", line)
    if not m:
        return None
    try:
        return int(m.group(1))
    except Exception:
        return None


def extract_state(line: str) -> Optional[Tuple[Optional[int], Optional[int], Optional[float], Optional[float]]]:
    if not line.startswith("HB ") and not line.startswith("STAT "):
        return None
    def get_int(name: str) -> Optional[int]:
        m = re.search(rf"\b{name}=(-?\d+)\b", line)
        return int(m.group(1)) if m else None
    def get_float(name: str) -> Optional[float]:
        m = re.search(rf"\b{name}=(-?\d+(?:\.\d+)?)\b", line)
        return float(m.group(1)) if m else None
    return get_int("exp_id"), get_int("run"), get_float("ts"), get_float("trim")


def latest_rx(lines: List[str]) -> Optional[int]:
    val: Optional[int] = None
    for ln in lines:
        rx = extract_rx(ln)
        if rx is not None:
            val = rx
    return val


def summarize(lines: List[str]) -> str:
    ok_lines = [ln for ln in lines if ln.startswith("OK")]
    err_lines = [ln for ln in lines if ln.startswith("ERR")]
    states = [s for s in (extract_state(ln) for ln in lines) if s is not None]
    last_state = states[-1] if states else (None, None, None, None)
    run1_count = sum(1 for s in states if s[1] == 1)
    rx_last = latest_rx(lines)
    return (
        f"ok_n={len(ok_lines)} err_n={len(err_lines)} run1_n={run1_count} "
        f"last_exp_id={last_state[0]} last_run={last_state[1]} last_ts={last_state[2]} "
        f"last_trim={last_state[3]} rx_last={rx_last}"
    )


def has_run_started(lines: List[str], exp_id: Optional[int] = None) -> bool:
    for state in (extract_state(ln) for ln in lines):
        if state is None:
            continue
        line_exp_id, run, _ts, _trim = state
        if run != 1:
            continue
        if exp_id is not None and line_exp_id not in (None, exp_id):
            continue
        return True
    return False


def write_log_line(fp, text: str) -> None:
    line = text + "\n"
    fp.write(line.encode("utf-8", errors="ignore"))
    fp.flush()
    print(text)


def send_and_capture(ser: serial.Serial, fp, tail_state: Dict[str, str], cmd: str, watch_s: float, echo: bool) -> List[str]:
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    pre = drain_lines(ser, 0.25, fp=None, tail_state=tail_state, echo=False)
    pre_rx = latest_rx(pre)
    write_log_line(fp, f"CMD {cmd} pre_rx={pre_rx}")
    send_cmd(ser, cmd)
    lines = drain_lines(ser, watch_s, fp=fp, tail_state=tail_state, echo=echo)
    post_rx = latest_rx(lines)
    write_log_line(fp, f"RES {cmd} post_rx={post_rx} {summarize(lines)}")
    return lines


def start_run_and_capture(ser: serial.Serial, fp, tail_state: Dict[str, str], args: argparse.Namespace) -> None:
    confirm_watch_s = max(0.3, min(args.run_confirm_s, args.watch_s))
    total_watch_s = max(confirm_watch_s, args.watch_s)
    for attempt in range(args.run_retries + 1):
        run_lines = send_and_capture(ser, fp, tail_state, "#RUN!", watch_s=confirm_watch_s, echo=args.echo)
        if has_run_started(run_lines, args.exp_id):
            remain_s = max(0.0, total_watch_s - confirm_watch_s)
            if remain_s > 0.0:
                drain_lines(ser, remain_s, fp=fp, tail_state=tail_state, echo=args.echo)
            return
        stat_lines = send_and_capture(ser, fp, tail_state, "#STAT!", watch_s=0.8, echo=args.echo)
        if has_run_started(stat_lines, args.exp_id):
            remain_s = max(0.0, total_watch_s - confirm_watch_s - 0.8)
            if remain_s > 0.0:
                drain_lines(ser, remain_s, fp=fp, tail_state=tail_state, echo=args.echo)
            return
        if attempt < args.run_retries:
            write_log_line(fp, f"RUN_RETRY attempt={attempt + 1} reason=no_run1")
            time.sleep(0.25)
    send_and_capture(ser, fp, tail_state, "#STOP!", watch_s=0.6, echo=args.echo)
    raise RuntimeError(
        f"run did not start for exp_id={args.exp_id} after {args.run_retries + 1} attempt(s)"
    )


def run_probe(args: argparse.Namespace) -> str:
    out_dir = os.path.join(os.path.dirname(__file__), "000Data")
    os.makedirs(out_dir, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(out_dir, f"startup_probe_{args.mode}_{args.exp_id}_{ts}.txt")

    with serial.Serial(
        port=args.port,
        baudrate=args.baud,
        timeout=0.1,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as ser, open(log_path, "wb") as fp:
        tail_state: Dict[str, str] = {"tail": ""}
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

        drain_lines(ser, 0.6, fp=None, tail_state=tail_state, echo=False)
        cmd_list = [
            "#STOP!",
            f"#PWM_MAX={args.pwm_max}!",
            f"#DIFF_MAX={args.diff_max}!",
            f"#SO={args.so}!",
            f"#TRIM={args.trim}!",
            f"#TS={args.ts}!",
        ]
        if args.at is not None:
            cmd_list.append(f"#AT={args.at}!")
        if args.at_kp is not None:
            cmd_list.append(f"#AT_KP={args.at_kp}!")
        if args.at_ki is not None:
            cmd_list.append(f"#AT_KI={args.at_ki}!")
        if args.at_lim is not None:
            cmd_list.append(f"#AT_LIM={args.at_lim}!")
        if args.hp is not None:
            cmd_list.append(f"#HP={args.hp}!")
        if args.hd is not None:
            cmd_list.append(f"#HD={args.hd}!")
        if args.hs is not None:
            cmd_list.append(f"#HS={args.hs}!")
        if args.db is not None:
            cmd_list.append(f"#DB={args.db}!")
        if args.hi is not None:
            cmd_list.append(f"#HI={args.hi}!")
        if args.hil is not None:
            cmd_list.append(f"#HIL={args.hil}!")
        for cmd in cmd_list:
            send_and_capture(ser, fp, tail_state, cmd, watch_s=0.45, echo=args.echo)

        if args.mode == "exp_run":
            exp_lines = send_and_capture(
                ser,
                fp,
                tail_state,
                f"#EXP=RUN,{args.exp_id},{args.ms}!",
                watch_s=max(0.3, min(args.run_confirm_s, args.watch_s)),
                echo=args.echo,
            )
            if not has_run_started(exp_lines, args.exp_id):
                send_and_capture(ser, fp, tail_state, "#STAT!", watch_s=0.8, echo=args.echo)
                raise RuntimeError(f"exp_run did not enter run=1 for exp_id={args.exp_id}")
            remain_s = max(0.0, args.watch_s - max(0.3, min(args.run_confirm_s, args.watch_s)))
            if remain_s > 0.0:
                drain_lines(ser, remain_s, fp=fp, tail_state=tail_state, echo=args.echo)
        else:
            send_and_capture(ser, fp, tail_state, f"#EXP=START,{args.exp_id},{args.ms}!", watch_s=1.2, echo=args.echo)
            start_run_and_capture(ser, fp, tail_state, args)

        send_and_capture(ser, fp, tail_state, "#STAT!", watch_s=0.8, echo=args.echo)
        drain_lines(ser, 0.8, fp=fp, tail_state=tail_state, echo=args.echo)

    return log_path


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--mode", choices=["exp_run", "exp_start_run"], required=True)
    p.add_argument("--exp-id", type=int, required=True)
    p.add_argument("--ms", type=int, default=1200)
    p.add_argument("--ts", type=float, default=140.0)
    p.add_argument("--so", type=int, default=180)
    p.add_argument("--trim", type=float, default=-0.0625)
    p.add_argument("--pwm-max", type=int, default=150)
    p.add_argument("--diff-max", type=int, default=0)
    p.add_argument("--at", type=int, default=None)
    p.add_argument("--at-kp", dest="at_kp", type=float, default=None)
    p.add_argument("--at-ki", dest="at_ki", type=float, default=None)
    p.add_argument("--at-lim", dest="at_lim", type=float, default=None)
    p.add_argument("--hp", type=float, default=None)
    p.add_argument("--hd", type=float, default=None)
    p.add_argument("--hs", type=float, default=None)
    p.add_argument("--db", type=float, default=None)
    p.add_argument("--hi", type=float, default=None)
    p.add_argument("--hil", type=float, default=None)
    p.add_argument("--watch-s", type=float, default=2.5)
    p.add_argument("--run-confirm-s", type=float, default=1.8)
    p.add_argument("--run-retries", type=int, default=1)
    p.add_argument("--echo", action="store_true")
    return p


def main() -> None:
    args = build_parser().parse_args()
    path = run_probe(args)
    print(path)


if __name__ == "__main__":
    main()
