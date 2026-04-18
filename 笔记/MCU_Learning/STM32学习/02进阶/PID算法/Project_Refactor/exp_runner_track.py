import argparse
import os
import re
import time
from dataclasses import dataclass
from typing import Optional, Tuple

import serial

from exp_runner import (
    allocate_experiment_label,
    build_artifact_prefix,
    drain_for,
    find_serial_port,
    read_stat_line,
    safe_write_cmd,
    sanity_check_rx,
    send_best_effort,
    wait_for_regex_lines,
    write_log_line,
)
from track_trajectory_analyzer import analyze_track_raw

DEFAULT_OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "000Data", "循迹数据")


@dataclass
class TrackExpParams:
    ts: float = 26.0
    skp: float = 10.0
    ski: float = 0.25
    skd: float = 0.0
    kp: float = 0.5
    ki: float = 0.004
    kd: float = 0.0
    pwm_max: int = 72
    cal_wait_s: float = 0.15


def wait_for_track_run(ser: serial.Serial, exp_id: int, raw_fp, print_realtime: bool, timeout_s: float = 2.5) -> bool:
    pat = re.compile(rf"^TRK\b.*\bexp_id={exp_id}\b.*\brun=1\b")
    ok, _lines = wait_for_regex_lines(
        ser,
        [pat],
        timeout_s=timeout_s,
        raw_fp=raw_fp,
        print_realtime=print_realtime,
    )
    return bool(ok)


def wait_for_track_finish(ser: serial.Serial, exp_id: int, raw_fp, print_realtime: bool, timeout_s: float) -> bool:
    patterns = [
        re.compile(rf"^EXP_END\s+id={exp_id}\b"),
        re.compile(rf"^TRK\b.*\bexp_id={exp_id}\b.*\brun=0\b"),
    ]
    ok, _lines = wait_for_regex_lines(
        ser,
        patterns,
        timeout_s=timeout_s,
        raw_fp=raw_fp,
        print_realtime=print_realtime,
    )
    return bool(ok)


def send_track_ack(
    ser: serial.Serial,
    port: str,
    baud: int,
    cmd: str,
    ack_re: str,
    raw_fp,
    print_realtime: bool,
    timeout_s: float = 1.2,
    retries: int = 2,
) -> serial.Serial:
    ack_pat = re.compile(ack_re)
    err_pat = re.compile(r"^ERR\b")
    last_lines = []

    for _ in range(max(1, retries)):
        ser = safe_write_cmd(ser, port=port, baud=baud, cmd=cmd, retries=1)
        ok, lines = wait_for_regex_lines(
            ser,
            [ack_pat],
            timeout_s=timeout_s,
            raw_fp=raw_fp,
            print_realtime=print_realtime,
        )
        last_lines = lines
        for line in reversed(lines):
            if err_pat.search(line):
                raise RuntimeError(f"MCU replied ERR for {cmd}. Last lines: {lines[-8:]}")
        if ok:
            return ser
        time.sleep(0.05)

    raise RuntimeError(f"No ACK for {cmd}, expected /{ack_re}/. Last lines: {last_lines[-8:]}")


def switch_to_track_mode(ser: serial.Serial, raw_fp, print_realtime: bool) -> None:
    send_best_effort(ser, "#STOP!", r"^(OK\s+STOP\b|ERR\b)", raw_fp, print_realtime, timeout_s=0.5, retries=1)
    ser = send_track_ack(
        ser,
        port=ser.port,
        baud=int(ser.baudrate),
        cmd="#MODE=TRACK!",
        ack_re=r"^OK\s+MODE\b",
        raw_fp=raw_fp,
        print_realtime=print_realtime,
        timeout_s=1.2,
        retries=2,
    )
    time.sleep(0.15)
    send_best_effort(ser, "#STOP!", r"^(OK\s+STOP\b|ERR\b)", raw_fp, print_realtime, timeout_s=0.5, retries=1)


def send_track_params(ser: serial.Serial, raw_fp, print_realtime: bool, params: TrackExpParams) -> None:
    cmds = [
        (f"#TS={params.ts}!", r"^OK\s+TS\b"),
        (f"#SKP={params.skp}!", r"^OK\s+SKP\b"),
        (f"#SKI={params.ski}!", r"^OK\s+SKI\b"),
        (f"#SKD={params.skd}!", r"^OK\s+SKD\b"),
        (f"#AKP={params.kp}!", r"^OK\s+AKP\b"),
        (f"#AKI={params.ki}!", r"^OK\s+AKI\b"),
        (f"#AKD={params.kd}!", r"^OK\s+AKD\b"),
        (f"#PWM_MAX={int(params.pwm_max)}!", r"^OK\s+PWM_MAX\b"),
    ]
    for cmd, ack_re in cmds:
        ser = send_track_ack(
            ser,
            port=ser.port,
            baud=int(ser.baudrate),
            cmd=cmd,
            ack_re=ack_re,
            raw_fp=raw_fp,
            print_realtime=print_realtime,
            timeout_s=0.8,
            retries=2,
        )
        time.sleep(0.03)


def verify_track_params(ser: serial.Serial, raw_fp, print_realtime: bool, params: TrackExpParams) -> None:
    stat_line = read_stat_line(ser, raw_fp=raw_fp, print_realtime=print_realtime, timeout_s=0.8)
    if not stat_line:
        return
    checks = [
        (r"\bts=(-?\d+(?:\.\d+)?)", float(params.ts), 0.05),
        (r"\bskp=(-?\d+(?:\.\d+)?)", float(params.skp), 0.05),
        (r"\bski=(-?\d+(?:\.\d+)?)", float(params.ski), 0.02),
        (r"\bskd=(-?\d+(?:\.\d+)?)", float(params.skd), 0.02),
        (r"\bkp=(-?\d+(?:\.\d+)?)", float(params.kp), 0.02),
        (r"\bki=(-?\d+(?:\.\d+)?)", float(params.ki), 0.01),
        (r"\bkd=(-?\d+(?:\.\d+)?)", float(params.kd), 0.01),
        (r"\bpwm_max=(-?\d+(?:\.\d+)?)", float(params.pwm_max), 0.5),
    ]
    for pattern, expect, tol in checks:
        match = re.search(pattern, stat_line)
        if match is None:
            continue
        if abs(float(match.group(1)) - expect) > tol:
            raise RuntimeError(f"Track param verify failed: {pattern} expect={expect} stat={stat_line}")


def run_track_experiment(
    port: str,
    baud: int,
    exp_id: int,
    exp_ms: int,
    params: TrackExpParams,
    out_dir: str,
    print_realtime: bool,
    exp_label: Optional[str] = None,
) -> Tuple[str, str, str, str]:
    ts = time.strftime("%Y%m%d_%H%M%S")
    base = exp_label if exp_label else f"track_exp{exp_id:03d}_{exp_ms}ms_{ts}"
    raw_path = os.path.join(out_dir, base + "_raw.txt")
    os.makedirs(out_dir, exist_ok=True)

    with serial.Serial(
        port=port,
        baudrate=baud,
        timeout=0.1,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as ser:
        try:
            ser.dtr = False
            ser.rts = False
        except Exception:
            pass
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.30)

        with open(raw_path, "wb") as raw_fp:
            drain_for(ser, 0.30, raw_fp=raw_fp, print_realtime=False)
            try:
                sanity_check_rx(ser, raw_fp=raw_fp, print_realtime=print_realtime)
            except Exception:
                pass

            switch_to_track_mode(ser, raw_fp=raw_fp, print_realtime=print_realtime)
            if params.cal_wait_s > 0.0:
                time.sleep(float(params.cal_wait_s))
            send_best_effort(ser, "#CAL!", r"^(OK\s+CAL\b|ERR\b)", raw_fp, print_realtime, timeout_s=0.8, retries=1)
            send_track_params(ser, raw_fp=raw_fp, print_realtime=print_realtime, params=params)
            verify_track_params(ser, raw_fp=raw_fp, print_realtime=print_realtime, params=params)

            ser = send_track_ack(
                ser,
                port=port,
                baud=baud,
                cmd="#EXP=STREAM,1!",
                ack_re=r"^OK\s+EXP_STREAM\b",
                raw_fp=raw_fp,
                print_realtime=print_realtime,
                timeout_s=0.8,
                retries=2,
            )
            write_log_line(raw_fp, f"CMD #EXP=RUN,{exp_id},{exp_ms}!")
            ser = send_track_ack(
                ser,
                port=port,
                baud=baud,
                cmd=f"#EXP=RUN,{exp_id},{exp_ms}!",
                ack_re=r"^OK\s+EXP_START\b",
                raw_fp=raw_fp,
                print_realtime=print_realtime,
                timeout_s=2.0,
                retries=2,
            )
            if not wait_for_track_run(ser, exp_id=exp_id, raw_fp=raw_fp, print_realtime=print_realtime, timeout_s=2.5):
                raise RuntimeError("Track experiment did not enter run state")

            drain_for(ser, max(0.0, exp_ms / 1000.0), raw_fp=raw_fp, print_realtime=print_realtime)
            _ = wait_for_track_finish(
                ser,
                exp_id=exp_id,
                raw_fp=raw_fp,
                print_realtime=print_realtime,
                timeout_s=max(1.0, min(4.0, exp_ms / 1000.0 + 1.0)),
            )
            send_best_effort(ser, f"#EXP=STOP,{exp_id}!", r"^(OK\s+EXP_STOP\b|ERR\b)", raw_fp, print_realtime, timeout_s=0.8, retries=1)
            send_best_effort(ser, "#EXP=STREAM,0!", r"^(OK\s+EXP_STREAM\b|ERR\b)", raw_fp, print_realtime, timeout_s=0.8, retries=1)
            ser = safe_write_cmd(ser, port=port, baud=baud, cmd="#STOP!", retries=1)
            drain_for(ser, 0.30, raw_fp=raw_fp, print_realtime=print_realtime)

    data_path, analysis_path, trajectory_path, _analysis = analyze_track_raw(raw_path, speed_scale=1.0, write_outputs=True)
    return raw_path, data_path, analysis_path, trajectory_path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM18", help="串口号，空则自动查找")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--id", type=int, default=0, help="MCU exp_id，<=0 则自动与实验编号同步")
    ap.add_argument("--ms", type=int, default=10000)
    ap.add_argument("--out", default=DEFAULT_OUTPUT_DIR)
    ap.add_argument("--realtime", action="store_true")
    ap.add_argument("--ts", type=float, default=26.0)
    ap.add_argument("--skp", type=float, default=10.0)
    ap.add_argument("--ski", type=float, default=0.25)
    ap.add_argument("--skd", type=float, default=0.0)
    ap.add_argument("--kp", type=float, default=0.5)
    ap.add_argument("--ki", type=float, default=0.004)
    ap.add_argument("--kd", type=float, default=0.0)
    ap.add_argument("--pwm-max", type=int, default=72)
    args = ap.parse_args()

    port = args.port.strip() or find_serial_port(prefer_keywords=["CH340", "USB-SERIAL", "USB Serial", "CP210"])
    if not port:
        raise RuntimeError("No serial ports found")

    exp_seq, exp_label = allocate_experiment_label(args.out)
    exp_id = args.id if args.id > 0 else exp_seq
    params = TrackExpParams(
        ts=args.ts,
        skp=args.skp,
        ski=args.ski,
        skd=args.skd,
        kp=args.kp,
        ki=args.ki,
        kd=args.kd,
        pwm_max=int(args.pwm_max),
    )

    raw_path, data_path, analysis_path, trajectory_path = run_track_experiment(
        port=port,
        baud=args.baud,
        exp_id=exp_id,
        exp_ms=args.ms,
        params=params,
        out_dir=args.out,
        print_realtime=args.realtime,
        exp_label=exp_label,
    )

    prefix = build_artifact_prefix(raw_path)
    print(f"EXPERIMENT={exp_label}")
    print(f"MCU_EXP_ID={exp_id}")
    print(f"RAW={raw_path}")
    print(f"DATA={data_path}")
    print(f"ANALYSIS={analysis_path}")
    print(f"TRAJECTORY={trajectory_path}")
    print(f"PREFIX={prefix}")


if __name__ == "__main__":
    main()
