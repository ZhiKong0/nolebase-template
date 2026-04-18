import os
import re
import time
import csv
import json
import math
import argparse
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import serial
import serial.tools.list_ports
from trajectory_analyzer import build_time_windows, build_total_trajectory, parse_hb_rows, segment_rows, split_run_stretches


@dataclass
class ExpParams:
    ts: float = 20.0
    skp: float = 0.1
    ski: float = 0.012
    skd: float = 0.0
    akp: float = 0.24
    aki: float = 0.0024
    akd: float = 0.05
    cal_wait_s: float = 0.2

    @property
    def kp(self) -> float:
        return self.akp

    @property
    def ki(self) -> float:
        return self.aki

    @property
    def kd(self) -> float:
        return self.akd

    @property
    def hp(self) -> float:
        return self.akp

    @property
    def hi(self) -> float:
        return self.aki

    @property
    def hd(self) -> float:
        return self.akd


DETAIL_WINDOW_S = 0.5
DETAIL_SPEED_SCALE = 1.0
PROJECT_ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUTPUT_DIR = os.path.join(PROJECT_ROOT_DIR, "000Data")
EXP_COUNTER_FILENAME = "exp_counter.txt"
TIME_SERIES_SUFFIX = "_data.csv"
ANALYSIS_SUFFIX = "_analysis.json"
ENCODER_SPIKE_THRESHOLD = 80.0
YAW_JUMP_THRESHOLD_DEG = 8.0
YAW_JUMP_MAX_TICK_GAP_MS = 1000.0
OUTPUT_ACTIVE_THRESHOLD = 8.0
REVERSE_PULSE_THRESHOLD = 2.0

TIME_SERIES_FIELDNAMES = [
    "host_time_s",
    "host_time_ns",
    "host_monotonic_ns",
    "source",
    "tick",
    "t_ms",
    "run",
    "yaw",
    "yaw_rate",
    "left_encoder",
    "right_encoder",
    "encoder_diff",
    "left_output",
    "right_output",
    "left_core",
    "right_core",
    "raw_left_delta",
    "raw_right_delta",
    "left_clamped",
    "right_clamped",
    "anomaly_flags",
    "raw_line",
]

ACTIVE_TELEMETRY_ROWS: Optional[List[Dict[str, Any]]] = None


def get_counter_file_path(out_dir: str) -> str:
    return os.path.join(out_dir, EXP_COUNTER_FILENAME)


def allocate_experiment_label(out_dir: str) -> Tuple[int, str]:
    os.makedirs(out_dir, exist_ok=True)
    counter_path = get_counter_file_path(out_dir)
    current_value = 0
    if os.path.exists(counter_path):
        try:
            with open(counter_path, "r", encoding="utf-8") as f:
                current_value = int((f.read().strip() or "0"))
        except Exception:
            current_value = 0
    current_value += 1
    with open(counter_path, "w", encoding="utf-8") as f:
        f.write(str(current_value))
    return current_value, f"exp{current_value:03d}"


def build_artifact_prefix(raw_path: str) -> str:
    if raw_path.endswith("_raw.txt"):
        return raw_path[:-8]
    return os.path.splitext(raw_path)[0]


def make_output_paths(out_dir: str, base_name: str) -> Dict[str, str]:
    return {
        "raw": os.path.join(out_dir, base_name + "_raw.txt"),
        "dump": os.path.join(out_dir, base_name + "_dump.csv"),
        "data": os.path.join(out_dir, base_name + TIME_SERIES_SUFFIX),
        "analysis": os.path.join(out_dir, base_name + ANALYSIS_SUFFIX),
    }


def set_active_telemetry_rows(rows: Optional[List[Dict[str, Any]]]) -> None:
    global ACTIVE_TELEMETRY_ROWS
    ACTIVE_TELEMETRY_ROWS = rows


def find_serial_port(prefer_keywords: Optional[List[str]] = None) -> Optional[str]:
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None

    if prefer_keywords:
        for p in ports:
            desc = (p.description or "")
            if any(k.lower() in desc.lower() for k in prefer_keywords):
                return p.device

    return ports[0].device


def now() -> float:
    return time.time()


def read_some(ser: serial.Serial, max_bytes: int = 4096) -> bytes:
    try:
        n = ser.in_waiting
        if n <= 0:
            return b""
        return ser.read(min(n, max_bytes))
    except serial.SerialException:
        return b""


def split_lines(buf: str) -> Tuple[List[str], str]:
    if not buf:
        return [], ""
    lines = buf.split("\n")
    if not lines:
        return [], ""
    tail = lines[-1]
    out = [ln.strip("\r") for ln in lines[:-1]]
    out = [ln for ln in out if ln]
    return out, tail


def extract_rx_from_hb_line(ln: str) -> Optional[int]:
    if not ln.startswith("HB "):
        return None
    m = re.search(r"\brx=(\d+)\b", ln)
    if not m:
        return None
    try:
        return int(m.group(1))
    except Exception:
        return None


def extract_lr_from_hb_line(line: str) -> Optional[Tuple[int, int]]:
    try:
        m = re.search(r"\bL=(-?\d+)\s+R=(-?\d+)\b", line)
        if not m:
            return None
        return int(m.group(1)), int(m.group(2))
    except Exception:
        return None


def extract_pmax_dmax_from_line(line: str) -> Optional[Tuple[int, int]]:
    try:
        m = re.search(r"\bpmax=(-?\d+)\s+dmax=(-?\d+)\b", line)
        if not m:
            return None
        return int(m.group(1)), int(m.group(2))
    except Exception:
        return None


def wait_for_pmax_dmax(
    ser: serial.Serial,
    expect_pmax: Optional[int],
    expect_dmax: Optional[int],
    timeout_s: float,
    raw_fp,
) -> bool:
    if expect_pmax is None and expect_dmax is None:
        return True
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        line = safe_readline(ser)
        if not line:
            continue
        if raw_fp is not None:
            try:
                raw_fp.write(line + "\n")
            except Exception:
                pass
        got = extract_pmax_dmax_from_line(line)
        if got is None:
            continue
        pmax, dmax = got
        if expect_pmax is not None and pmax != int(expect_pmax):
            continue
        if expect_dmax is not None and dmax != int(expect_dmax):
            continue
        return True
    return False


def safe_readline(ser: serial.Serial, timeout_s: float = 0.25) -> str:
    start = now()
    text_buf = ""
    while now() - start < timeout_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue
        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        if lines:
            return lines[-1].strip()
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]
    return ""


def read_latest_rx(ser: serial.Serial, timeout_s: float, raw_fp) -> Optional[int]:
    start = now()
    text_buf = ""
    last_rx: Optional[int] = None
    while now() - start < timeout_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue
        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()
        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        for ln in lines:
            rx = extract_rx_from_hb_line(ln)
            if rx is not None:
                last_rx = rx
        if last_rx is not None:
            return last_rx
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]
    return last_rx


def wait_for_rx_increase(
    ser: serial.Serial,
    base_rx: Optional[int],
    timeout_s: float,
    raw_fp,
) -> bool:
    if base_rx is None:
        # 没有基线就无法判定增长，直接认为失败，让上层走其他判定逻辑
        return False

    start = now()
    text_buf = ""
    while now() - start < timeout_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue

        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()

        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        for ln in lines:
            rx = extract_rx_from_hb_line(ln)
            if rx is not None and rx > base_rx:
                return True
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]
    return False


def wait_for_hb_lr_nonzero(
    ser: serial.Serial,
    timeout_s: float,
    raw_fp,
    print_realtime: bool = False,
) -> bool:
    t0 = time.time()
    while (time.time() - t0) < timeout_s:
        lines = drain_for_lines(ser, 0.10, raw_fp=raw_fp, print_realtime=print_realtime)
        for ln in lines:
            lr = extract_lr_from_hb_line(ln)
            if not lr:
                continue
            l, r = lr
            if l != 0 or r != 0:
                return True
    return False


def send_cmd(ser: serial.Serial, cmd: str) -> None:
    if not cmd.endswith("!"):
        raise ValueError(f"cmd must end with '!': {cmd}")
    ser.write(cmd.encode("ascii", errors="ignore"))
    try:
        ser.flush()
    except Exception:
        pass


def safe_write_cmd(
    ser: serial.Serial,
    port: str,
    baud: int,
    cmd: str,
    retries: int = 3,
    reopen_wait_s: float = 0.35,
) -> serial.Serial:
    last_err: Optional[Exception] = None
    for k in range(retries):
        try:
            send_cmd(ser, cmd)
            return ser
        except Exception as e:
            last_err = e
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(reopen_wait_s * (1 + k))
            ser = serial.Serial(
                port=port,
                baudrate=baud,
                timeout=0.1,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )
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

    if last_err is None:
        raise RuntimeError(f"safe_write_cmd failed for {cmd}")
    raise RuntimeError(f"safe_write_cmd failed for {cmd}: {last_err}")


def drain_for(ser: serial.Serial, seconds: float, raw_fp, print_realtime: bool) -> None:
    start = now()
    text_buf = ""
    last_prog = start
    while now() - start < seconds:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue

        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()

        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        if print_realtime:
            # 每秒打印一次进度，避免刷屏
            tnow = now()
            if tnow - last_prog >= 1.0:
                last_prog = tnow
                pct = (tnow - start) / max(1e-6, seconds) * 100.0
                if pct > 100.0:
                    pct = 100.0
                print(f"PROG {pct:.0f}%")
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]


def drain_for_lines(ser: serial.Serial, seconds: float, raw_fp, print_realtime: bool) -> List[str]:
    start = now()
    text_buf = ""
    last_prog = start
    out_lines: List[str] = []
    while now() - start < seconds:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue

        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()

        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        out_lines.extend(lines)
        if print_realtime:
            tnow = now()
            if tnow - last_prog >= 1.0:
                last_prog = tnow
                pct = (tnow - start) / max(1e-6, seconds) * 100.0
                if pct > 100.0:
                    pct = 100.0
                print(f"PROG {pct:.0f}%")
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]
    return out_lines


def send_and_wait_ack(
    ser: serial.Serial,
    cmd: str,
    ack_re: str,
    raw_fp,
    print_realtime: bool,
    timeout_s: float = 2.5,
    retries: int = 3,
) -> None:
    pat = re.compile(ack_re)
    err_pat = re.compile(r"^ERR\b")
    last_lines: List[str] = []
    for _ in range(retries):
        send_cmd(ser, cmd)
        ok, lines = wait_for_regex_lines(
            ser,
            [pat, err_pat],
            timeout_s=timeout_s,
            raw_fp=raw_fp,
            print_realtime=print_realtime,
        )
        last_lines = lines

        # 如果收到了ERR，直接快速失败（避免用户误以为“卡住”）
        for ln in reversed(lines):
            if ln.startswith("ERR"):
                raise RuntimeError(f"MCU replied ERR for {cmd}. Last lines: {lines[-8:]}")

        if ok:
            return
        time.sleep(0.05)
    raise RuntimeError(f"No ACK for {cmd}, expected /{ack_re}/. Last lines: {last_lines[-5:]}")


def send_best_effort(
    ser: serial.Serial,
    cmd: str,
    ack_re: str,
    raw_fp,
    print_realtime: bool,
    timeout_s: float = 0.8,
    retries: int = 1,
) -> None:
    try:
        send_and_wait_ack(
            ser,
            cmd,
            ack_re,
            raw_fp=raw_fp,
            print_realtime=print_realtime,
            timeout_s=timeout_s,
            retries=retries,
        )
    except Exception as e:
        _ = e


def stop_vehicle_best_effort(
    ser: serial.Serial,
    port: str,
    baud: int,
    exp_id: int,
    raw_fp,
    print_realtime: bool,
) -> serial.Serial:
    send_best_effort(
        ser,
        "#EXP=STREAM,0!",
        r"^(OK\s+EXP_STREAM\b|ERR\b)",
        raw_fp,
        print_realtime,
        timeout_s=0.3,
        retries=0,
    )
    send_best_effort(
        ser,
        f"#EXP=STOP,{exp_id}!",
        r"^(OK\s+EXP_STOP\b|ERR\b)",
        raw_fp,
        print_realtime,
        timeout_s=0.5,
        retries=0,
    )
    for cmd in ("#RAW=0!", "#TS=0!", "#STOP!"):
        try:
            ser = safe_write_cmd(ser, port=port, baud=baud, cmd=cmd, retries=1)
            drain_for(ser, 0.12, raw_fp=raw_fp, print_realtime=print_realtime)
        except Exception:
            pass
    return ser


def send_run_and_wait_start(
    ser: serial.Serial,
    port: str,
    baud: int,
    cmd: str,
    expect_run_ack: bool,
    raw_fp,
    print_realtime: bool,
    timeout_s: float,
    retries: int,
) -> Tuple[serial.Serial, bool, bool]:
    run_pat = re.compile(r"^OK\s+RUN\b")
    exp_start_pat = re.compile(r"^OK\s+EXP_START\b")
    cal_pat = re.compile(r"^OK\s+CAL\b")
    err_pat = re.compile(r"^ERR\b")

    for _ in range(max(1, retries + 1)):
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        base_rx = read_latest_rx(ser, timeout_s=0.20, raw_fp=raw_fp)
        ser = safe_write_cmd(ser, port=port, baud=baud, cmd=cmd)
        rx_seen = wait_for_rx_increase(ser, base_rx=base_rx, timeout_s=0.40, raw_fp=raw_fp)
        start = now()
        text_buf = ""
        out_lines: List[str] = []
        seen_exp_start = False
        seen_cal = False

        while now() - start < timeout_s:
            b = read_some(ser)
            if not b:
                time.sleep(0.01)
                continue

            if raw_fp is not None:
                raw_fp.write(b)
                raw_fp.flush()

            text_buf += b.decode("utf-8", errors="ignore")
            lines, text_buf = split_lines(text_buf)
            capture_telemetry_lines(lines)
            for ln in lines:
                out_lines.append(ln)
                if err_pat.search(ln):
                    raise RuntimeError(f"MCU replied ERR for {cmd}. Last lines: {out_lines[-8:]}")
                if expect_run_ack and not seen_exp_start and exp_start_pat.search(ln):
                    seen_exp_start = True
                if expect_run_ack and not seen_cal and cal_pat.search(ln):
                    seen_cal = True
                if run_pat.search(ln):
                    return ser, True, rx_seen

            if len(text_buf) > 8192:
                text_buf = text_buf[-8192:]

        time.sleep(0.05)

        if rx_seen:
            return ser, False, True

    return ser, False, False


def sanity_check_rx(ser: serial.Serial, raw_fp, print_realtime: bool) -> None:
    # 只要能收到一条STAT/OK/ERR，都证明PC->MCU的RX链路是通的
    pats = [
        re.compile(r"^STAT\b"),
        re.compile(r"^OK\b"),
        re.compile(r"^ERR\b"),
    ]
    hb_rx_re = re.compile(r"\brx=(\d+)\b")

    def extract_rx(lines: List[str]) -> Optional[int]:
        for ln in reversed(lines):
            if not ln.startswith("HB "):
                continue
            m = hb_rx_re.search(ln)
            if not m:
                continue
            try:
                return int(m.group(1))
            except Exception:
                return None
        return None

    last_lines: List[str] = []
    for _ in range(3):
        # 记录发送前看到的rx计数
        ok0, pre_lines = wait_for_regex_lines(
            ser,
            [re.compile(r"^HB\b")],
            timeout_s=0.6,
            raw_fp=raw_fp,
            print_realtime=False,
        )
        _ = ok0
        base_rx = extract_rx(pre_lines)

        try:
            ser.reset_input_buffer()
        except Exception:
            pass

        try:
            send_cmd(ser, "#STAT!")
        except Exception:
            # 不要因为STAT不可用/串口瞬断而阻塞实验
            return

        # 优先等STAT/OK/ERR；如果没等到，则退化为观察HB里的rx是否增长
        ok, lines = wait_for_regex_lines(
            ser,
            pats,
            timeout_s=1.8,
            raw_fp=raw_fp,
            print_realtime=print_realtime,
        )
        last_lines = lines
        if ok:
            return

        ok2, post_lines = wait_for_regex_lines(
            ser,
            [re.compile(r"^HB\b")],
            timeout_s=1.2,
            raw_fp=raw_fp,
            print_realtime=False,
        )
        _ = ok2
        post_rx = extract_rx(post_lines)
        if base_rx is not None and post_rx is not None and post_rx > base_rx:
            return

        time.sleep(0.10)

    _ = last_lines
    return


def wait_for_regex_lines(
    ser: serial.Serial,
    patterns: List[re.Pattern],
    timeout_s: float,
    raw_fp,
    print_realtime: bool,
) -> Tuple[bool, List[str]]:
    start = now()
    text_buf = ""
    matched = [False] * len(patterns)
    out_lines: List[str] = []

    while now() - start < timeout_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue

        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()

        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        for ln in lines:
            out_lines.append(ln)
            for i, pat in enumerate(patterns):
                if not matched[i] and pat.search(ln):
                    matched[i] = True

        if all(matched):
            return True, out_lines

        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]

    return False, out_lines


def wait_for_exp_finish(
    ser: serial.Serial,
    exp_id: int,
    timeout_s: float,
    raw_fp,
    print_realtime: bool,
) -> bool:
    finish_pats = [
        re.compile(rf"^HB\b.*\bexp_id={exp_id}\b.*\brun=0\b"),
        re.compile(rf"^EXP_END\s+id={exp_id}\b"),
        re.compile(rf"^EXP_TIMEOUT\s+id={exp_id}\b"),
        re.compile(rf"^OK\s+EXP_STOP\b"),
    ]
    start = now()
    text_buf = ""
    while now() - start < timeout_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue

        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()

        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        for ln in lines:
            if print_realtime and (ln.startswith("HB ") or ln.startswith("EXP_") or ln.startswith("OK") or ln.startswith("ERR")):
                print(ln)
            for pat in finish_pats:
                if pat.search(ln):
                    return True

        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]

    return False


def wait_for_hb_run(
    ser: serial.Serial,
    exp_id: Optional[int],
    timeout_s: float,
    raw_fp,
    print_realtime: bool,
) -> bool:
    # no_dump / observe-safe 模式下不会发送 EXP=START，此时 exp_id 会沿用旧值，不能作为启动判据。
    if exp_id is None:
        pat = re.compile(r"^HB\b.*\brun=1\b")
    else:
        # 常规模式必须同时满足：exp_id=本次实验 且 run=1。
        pat = re.compile(rf"^HB\b.*\bexp_id={exp_id}\b.*\brun=1\b")
    ok, _lines = wait_for_regex_lines(
        ser,
        [pat],
        timeout_s=timeout_s,
        raw_fp=raw_fp,
        print_realtime=print_realtime,
    )
    return bool(ok)


def read_stat_line(
    ser: serial.Serial,
    raw_fp,
    print_realtime: bool,
    timeout_s: float = 0.6,
) -> str:
    send_cmd(ser, "#STAT!")
    _ok, lines = wait_for_regex_lines(
        ser,
        [re.compile(r"^STAT\b")],
        timeout_s=timeout_s,
        raw_fp=raw_fp,
        print_realtime=print_realtime,
    )
    for ln in reversed(lines):
        if ln.startswith("STAT"):
            return ln
    return ""


def wait_for_dump(
    ser: serial.Serial,
    exp_id: int,
    timeout_s: float,
    raw_fp,
    print_realtime: bool,
) -> Tuple[bool, List[str]]:
    begin_pat = re.compile(rf"^EXP_DUMP_BEGIN\s+id={exp_id}\b")
    end_pat = re.compile(rf"^EXP_DUMP_END\s+id={exp_id}\b")
    ok_pat = re.compile(r"^OK\s+EXP_DUMP\b")

    start = now()
    text_buf = ""
    out_lines: List[str] = []
    seen_begin = False
    seen_end = False
    seen_ok = False

    while now() - start < timeout_s:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue

        if raw_fp is not None:
            raw_fp.write(b)
            raw_fp.flush()

        text_buf += b.decode("utf-8", errors="ignore")
        lines, text_buf = split_lines(text_buf)
        capture_telemetry_lines(lines)
        for ln in lines:
            out_lines.append(ln)
            if not seen_begin and begin_pat.search(ln):
                seen_begin = True
            if not seen_end and end_pat.search(ln):
                seen_end = True
            if not seen_ok and ok_pat.search(ln):
                seen_ok = True

        # 成功条件：OK 直接算成功；或至少收到了BEGIN+END
        if seen_ok or (seen_begin and seen_end):
            return True, out_lines

        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]

    return False, out_lines


def parse_dump(lines: List[str]) -> Tuple[List[str], List[List[str]]]:
    fields: List[str] = []
    rows: List[List[str]] = []
    for ln in lines:
        if ln.startswith("FIELDS:"):
            fields = ln[len("FIELDS:") :].strip().split(",")
            continue
        if ln.startswith("D "):
            body = ln[2:].strip()
            rows.append([x.strip() for x in body.split(",")])
    return fields, rows


def write_csv(path: str, fields: List[str], rows: List[List[str]]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if fields:
            w.writerow(fields)
        for r in rows:
            w.writerow(r)


def parse_kv_line(line: str) -> Dict[str, float]:
    if not line.startswith("HB ") and not line.startswith("STAT "):
        return {}
    out: Dict[str, float] = {}
    for token in line.strip().split()[1:]:
        if "=" not in token:
            continue
        k, v = token.split("=", 1)
        try:
            out[k] = float(v)
        except Exception:
            continue
    return out


def capture_telemetry_lines(lines: List[str]) -> None:
    if ACTIVE_TELEMETRY_ROWS is None or not lines:
        return

    host_time_ns = time.time_ns()
    host_monotonic_ns = time.perf_counter_ns()
    for idx, line in enumerate(lines):
        kv = parse_kv_line(line)
        if not kv:
            continue
        ACTIVE_TELEMETRY_ROWS.append(
            {
                "host_time_s": (host_time_ns + idx) / 1_000_000_000.0,
                "host_time_ns": host_time_ns + idx,
                "host_monotonic_ns": host_monotonic_ns + idx,
                "source": "HB" if line.startswith("HB ") else "STAT",
                "tick": int(kv.get("tick", 0.0)),
                "t_ms": float(kv.get("t_ms", 0.0)),
                "run": int(kv.get("run", 0.0)),
                "yaw": float(kv.get("y", 0.0)),
                "yaw_rate": float(kv.get("yr", 0.0)),
                "left_encoder": float(kv.get("el", 0.0)),
                "right_encoder": float(kv.get("er", 0.0)),
                "encoder_diff": float(kv.get("ed", kv.get("el", 0.0) - kv.get("er", 0.0))),
                "left_output": float(kv.get("OL", kv.get("L", 0.0))),
                "right_output": float(kv.get("OR", kv.get("R", 0.0))),
                "left_core": float(kv.get("L", 0.0)),
                "right_core": float(kv.get("R", 0.0)),
                "raw_left_delta": float(kv.get("dl", 0.0)),
                "raw_right_delta": float(kv.get("dr", 0.0)),
                "left_clamped": int(kv.get("cl", 0.0)),
                "right_clamped": int(kv.get("cr", 0.0)),
                "raw_line": line,
            }
        )


def latest_rx_from_lines(lines: List[str]) -> Optional[int]:
    last_rx: Optional[int] = None
    for ln in lines:
        rx = extract_rx_from_hb_line(ln)
        if rx is not None:
            last_rx = rx
    return last_rx


def detect_time_series_anomalies(
    rows: List[Dict[str, Any]],
) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
    analyzed_rows: List[Dict[str, Any]] = []
    anomaly_events: List[Dict[str, Any]] = []
    prev_row: Optional[Dict[str, Any]] = None

    for row in rows:
        flags: List[str] = []
        left_encoder = float(row.get("left_encoder", 0.0))
        right_encoder = float(row.get("right_encoder", 0.0))
        left_output = float(row.get("left_output", 0.0))
        right_output = float(row.get("right_output", 0.0))
        yaw = float(row.get("yaw", 0.0))
        raw_left_delta = float(row.get("raw_left_delta", 0.0))
        raw_right_delta = float(row.get("raw_right_delta", 0.0))

        if int(row.get("left_clamped", 0)) != 0:
            flags.append("left_encoder_clamped")
        if int(row.get("right_clamped", 0)) != 0:
            flags.append("right_encoder_clamped")
        if abs(raw_left_delta) >= ENCODER_SPIKE_THRESHOLD or abs(left_encoder) >= ENCODER_SPIKE_THRESHOLD:
            flags.append("left_encoder_spike")
        if abs(raw_right_delta) >= ENCODER_SPIKE_THRESHOLD or abs(right_encoder) >= ENCODER_SPIKE_THRESHOLD:
            flags.append("right_encoder_spike")
        if abs(left_output) >= OUTPUT_ACTIVE_THRESHOLD and abs(left_encoder) <= 0.5:
            flags.append("left_output_without_encoder")
        if abs(right_output) >= OUTPUT_ACTIVE_THRESHOLD and abs(right_encoder) <= 0.5:
            flags.append("right_output_without_encoder")
        if abs(left_output) >= REVERSE_PULSE_THRESHOLD and abs(left_encoder) >= REVERSE_PULSE_THRESHOLD and left_output * left_encoder < 0.0:
            flags.append("left_reverse_pulse")
        if abs(right_output) >= REVERSE_PULSE_THRESHOLD and abs(right_encoder) >= REVERSE_PULSE_THRESHOLD and right_output * right_encoder < 0.0:
            flags.append("right_reverse_pulse")

        if prev_row is not None:
            prev_yaw = float(prev_row.get("yaw", 0.0))
            prev_run = int(float(prev_row.get("run", 0.0)))
            cur_run = int(float(row.get("run", 0.0)))
            prev_tick = float(prev_row.get("tick", 0.0))
            cur_tick = float(row.get("tick", 0.0))
            tick_gap = cur_tick - prev_tick
            if prev_run == cur_run and 0.0 < tick_gap <= YAW_JUMP_MAX_TICK_GAP_MS:
                if abs(yaw - prev_yaw) >= YAW_JUMP_THRESHOLD_DEG:
                    flags.append("yaw_jump")

        analyzed_row = dict(row)
        analyzed_row["anomaly_flags"] = "|".join(flags)
        analyzed_rows.append(analyzed_row)

        if flags:
            anomaly_events.append(
                {
                    "host_time_s": analyzed_row["host_time_s"],
                    "tick": analyzed_row["tick"],
                    "t_ms": analyzed_row["t_ms"],
                    "flags": flags,
                    "yaw": yaw,
                    "left_encoder": left_encoder,
                    "right_encoder": right_encoder,
                    "left_output": left_output,
                    "right_output": right_output,
                }
            )

        prev_row = row

    return analyzed_rows, anomaly_events


def write_time_series_csv(path: str, rows: List[Dict[str, Any]]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=TIME_SERIES_FIELDNAMES)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in TIME_SERIES_FIELDNAMES})


def write_json_file(path: str, payload: Dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)


def emit_time_series_artifacts(
    raw_path: str,
    exp_label: str,
    exp_id: int,
    exp_ms: int,
    params: ExpParams,
    rows: List[Dict[str, Any]],
) -> Tuple[str, str]:
    prefix = build_artifact_prefix(raw_path)
    data_path = prefix + TIME_SERIES_SUFFIX
    analysis_path = prefix + ANALYSIS_SUFFIX
    analyzed_rows, anomaly_events = detect_time_series_anomalies(rows)
    run_rows = [row for row in analyzed_rows if int(row.get("run", 0)) == 1]
    yaw_values = [float(row.get("yaw", 0.0)) for row in run_rows]

    write_time_series_csv(data_path, analyzed_rows)
    write_json_file(
        analysis_path,
        {
            "experiment": exp_label,
            "mcu_exp_id": exp_id,
            "exp_ms": exp_ms,
            "sample_count": len(analyzed_rows),
            "run_sample_count": len(run_rows),
            "anomaly_count": len(anomaly_events),
            "anomaly_breakdown": {
                flag: sum(1 for event in anomaly_events if flag in event["flags"])
                for flag in sorted({flag for event in anomaly_events for flag in event["flags"]})
            },
            "yaw_min": min(yaw_values) if yaw_values else 0.0,
            "yaw_max": max(yaw_values) if yaw_values else 0.0,
            "parameters": dict(params.__dict__),
            "anomalies": anomaly_events,
        },
    )
    return data_path, analysis_path


def write_log_line(raw_fp, text: str) -> None:
    if raw_fp is not None:
        raw_fp.write((text + "\n").encode("utf-8", errors="ignore"))
        raw_fp.flush()


def mean_or_zero(xs: List[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


def median_or_zero(xs: List[float]) -> float:
    if not xs:
        return 0.0
    ys = sorted(float(x) for x in xs)
    mid = len(ys) // 2
    if len(ys) % 2 == 1:
        return ys[mid]
    return 0.5 * (ys[mid - 1] + ys[mid])


def _detail_side_from_value(value: float, eps: float) -> str:
    if value > eps:
        return "right"
    if value < -eps:
        return "left"
    return "balanced"


def _detail_deviation_severity(abs_dev: float, ref_len: float) -> str:
    ratio = abs(float(abs_dev)) / max(1.0, abs(float(ref_len)))
    if ratio <= 0.01:
        return "low"
    if ratio <= 0.03:
        return "mild"
    if ratio <= 0.06:
        return "moderate"
    return "high"


def _window_progress_rate(win: Dict[str, object]) -> float:
    duration_s = float(win.get("duration_s", 0.0))
    if duration_s <= 1e-9:
        return 0.0
    return float(win.get("path_len", 0.0)) / duration_s


def _window_drive_mean(win: Dict[str, object]) -> float:
    return 0.5 * (abs(float(win.get("outL_mean", 0.0))) + abs(float(win.get("outR_mean", 0.0))))


def _window_drive_min(win: Dict[str, object]) -> float:
    return min(abs(float(win.get("outL_mean", 0.0))), abs(float(win.get("outR_mean", 0.0))))


def _window_mid_run_pause_flags(
    win: Dict[str, object],
    cruise_rate: float,
    cruise_pc: float,
    cruise_out: float,
    cruise_so: float,
    cruise_ts: float,
) -> Dict[str, float | bool]:
    duration_s = float(win.get("duration_s", 0.0))
    path_len = float(win.get("path_len", 0.0))
    path_rate = _window_progress_rate(win)
    pc_mean = abs(float(win.get("pc_mean", 0.0)))
    so_mean = abs(float(win.get("so_mean", 0.0)))
    ts_mean = abs(float(win.get("ts_mean", 0.0)))
    drive_mean = _window_drive_mean(win)
    drive_min = _window_drive_min(win)
    motion_level = str(win.get("motion_level", "unknown"))
    state = str(win.get("state", "unknown"))
    segment_kind = str(win.get("segment_kind", "unknown"))

    paused_like = (
        ("pause" in state)
        or segment_kind in ("pause", "stalled", "stuck", "restart_after_pause")
        or motion_level == "no_motion"
    )
    path_stop = (
        path_rate <= max(1.0, 0.22 * max(cruise_rate, 0.0))
        and path_len <= max(0.20, 0.12 * max(cruise_rate, 1.0) * max(duration_s, 0.1))
    )
    pc_drop = cruise_pc > 0.0 and pc_mean <= max(6.0, 0.72 * cruise_pc)
    out_drop = (
        cruise_out > 0.0
        and drive_mean <= max(6.0, 0.72 * cruise_out)
        and drive_min <= max(5.0, 0.75 * cruise_out)
    )
    so_drop = cruise_so > 0.0 and so_mean <= max(6.0, 0.75 * cruise_so)
    ts_drop = cruise_ts > 0.0 and ts_mean <= max(8.0, 0.75 * cruise_ts)
    hard = (
        path_stop and (pc_drop or out_drop or so_drop or ts_drop or paused_like)
    ) or (
        paused_like
        and path_rate <= max(2.0, 0.35 * max(cruise_rate, 0.0))
        and (pc_drop or out_drop)
    )
    return {
        "hard": hard,
        "paused_like": paused_like,
        "path_stop": path_stop,
        "pc_drop": pc_drop,
        "out_drop": out_drop,
        "so_drop": so_drop,
        "ts_drop": ts_drop,
        "path_rate": path_rate,
    }


def _detail_zero_crossings(values: List[float], eps: float = 0.0) -> int:
    last_sign = 0
    count = 0
    for value in values:
        sign = 0
        if value > eps:
            sign = 1
        elif value < -eps:
            sign = -1
        if sign != 0 and last_sign != 0 and sign != last_sign:
            count += 1
        if sign != 0:
            last_sign = sign
    return count


def _classify_detailed_window(
    win: Dict[str, object],
    yaw_switch_n: int,
    line_dev_end: float,
    line_dev_abs_max: float,
    curve_abs_mean: float,
) -> str:
    motion_level = str(win.get("motion_level", "unknown"))
    state = str(win.get("state", "unknown"))
    turn_state = str(win.get("turn_state", "straight"))
    path_len = float(win.get("path_len", 0.0))
    enc_nonzero_ratio = float(win.get("enc_nonzero_ratio", 0.0))
    out_nonzero_ratio = float(win.get("out_nonzero_ratio", 0.0))
    out_strong_ratio = float(win.get("out_strong_ratio", 0.0))
    output_only_ratio = float(win.get("output_only_ratio", 0.0))
    idle_streak_s = float(win.get("idle_streak_s", 0.0))
    output_idle_streak_s = float(win.get("output_idle_streak_s", 0.0))
    pause_suspected = float(win.get("pause_suspected", 0.0))
    out_diff_abs_mean = abs(float(win.get("out_diff_mean", 0.0)))
    yaw_delta_abs = abs(float(win.get("yaw_delta", 0.0)))
    stiction = float(win.get("stiction", 0.0))
    drift_eps = max(0.8, min(3.0, 0.04 * max(path_len, 1.0)))

    if "restart_after_pause" in state:
        return "restart_after_pause"
    if "pause_recovering" in state or pause_suspected >= 0.5:
        return "pause_recovering"
    if motion_level == "no_motion":
        if out_nonzero_ratio >= 0.2 or output_idle_streak_s >= 0.12:
            return "stalled"
        return "pause"
    if ((path_len <= 0.2 and out_diff_abs_mean >= 4.0) or
            (stiction >= 0.5 and enc_nonzero_ratio < 0.2) or
            (enc_nonzero_ratio < 0.1 and out_diff_abs_mean >= 6.0) or
            (output_only_ratio >= 0.25 and out_strong_ratio >= 0.25) or
            (idle_streak_s >= 0.18 and out_strong_ratio >= 0.25)):
        return "stuck"
    if yaw_switch_n >= 2 and yaw_delta_abs >= 6.0:
        return "wobble"
    if turn_state == "left_arc":
        return "left_curve"
    if turn_state == "right_arc":
        return "right_curve"
    if line_dev_end > drift_eps or line_dev_abs_max > (drift_eps * 1.5):
        return "drift_right"
    if line_dev_end < -drift_eps or line_dev_abs_max > (drift_eps * 1.5):
        if line_dev_end < 0.0:
            return "drift_left"
    if curve_abs_mean >= 0.08 and path_len >= 1.0:
        return "curve"
    return "straight"


def build_detailed_trajectory_points(
    run_segments: List[List[Dict[str, float]]],
    speed_scale: float,
) -> List[Dict[str, object]]:
    if not run_segments:
        return []

    tick0 = float(run_segments[0][0].get("tick", 0.0))
    points: List[Dict[str, object]] = []
    x = 0.0
    y = 0.0
    s = 0.0
    point_idx = 0

    for seg_index, run_seg in enumerate(run_segments, start=1):
        if not run_seg:
            continue
        first = run_seg[0]
        prev_theta = float(first.get("yaw_rad", 0.0))
        prev_yaw_unwrapped = float(first.get("yaw_deg", 0.0))
        points.append(
            {
                "idx": point_idx,
                "seg_index": seg_index,
                "seg_row_index": 0,
                "tick": float(first.get("tick", 0.0)),
                "t_s": (float(first.get("tick", 0.0)) - tick0) / 1000.0,
                "run": float(first.get("run", 0.0)),
                "yaw_deg": float(first.get("yaw_deg", 0.0)),
                "yaw_unwrapped_deg": float(first.get("yaw_deg", 0.0)),
                "yr": float(first.get("yr", 0.0)),
                "el": float(first.get("el", 0.0)),
                "er": float(first.get("er", 0.0)),
                "ed": float(first.get("ed", 0.0)),
                "L": float(first.get("L", 0.0)),
                "R": float(first.get("R", 0.0)),
                "outL": float(first.get("outL", first.get("L", 0.0))),
                "outR": float(first.get("outR", first.get("R", 0.0))),
                "out_diff": float(first.get("outL", first.get("L", 0.0))) - float(first.get("outR", first.get("R", 0.0))),
                "pwm": float(first.get("pwm", 0.0)),
                "pc": float(first.get("pc", 0.0)),
                "hd": float(first.get("hd", 0.0)),
                "aa": float(first.get("aa", 0.0)),
                "ae": float(first.get("ae", 0.0)),
                "ao": float(first.get("ao", 0.0)),
                "se": float(first.get("se", 0.0)),
                "so": float(first.get("so", 0.0)),
                "x": x,
                "y": y,
                "s": s,
                "dx": 0.0,
                "dy": 0.0,
                "ds": 0.0,
                "kappa": 0.0,
            }
        )
        point_idx += 1

        for row_index in range(1, len(run_seg)):
            prev = run_seg[row_index - 1]
            cur = run_seg[row_index]
            dt_ms = float(cur.get("tick", 0.0) - prev.get("tick", 0.0))
            theta = float(cur.get("yaw_rad", 0.0))
            yaw_unwrapped = float(cur.get("yaw_deg", 0.0))
            if dt_ms > 0.0:
                dyaw = yaw_unwrapped - float(prev.get("yaw_deg", 0.0))
                if dyaw > 180.0:
                    dyaw -= 360.0
                elif dyaw < -180.0:
                    dyaw += 360.0
                yaw_unwrapped = prev_yaw_unwrapped + dyaw
                v = 0.5 * (float(cur.get("el", 0.0)) + float(cur.get("er", 0.0)))
                ds_signed = v * (dt_ms / 1000.0) * float(speed_scale)
                dx = ds_signed * math.cos(theta)
                dy = ds_signed * math.sin(theta)
                x += dx
                y += dy
                s += abs(ds_signed)
                dtheta = theta - prev_theta
                if dtheta > math.pi:
                    dtheta -= 2.0 * math.pi
                elif dtheta < -math.pi:
                    dtheta += 2.0 * math.pi
                kappa = (dtheta / ds_signed) if abs(ds_signed) > 1e-9 else 0.0
            else:
                dx = 0.0
                dy = 0.0
                ds_signed = 0.0
                kappa = 0.0

            points.append(
                {
                    "idx": point_idx,
                    "seg_index": seg_index,
                    "seg_row_index": row_index,
                    "tick": float(cur.get("tick", 0.0)),
                    "t_s": (float(cur.get("tick", 0.0)) - tick0) / 1000.0,
                    "run": float(cur.get("run", 0.0)),
                    "yaw_deg": float(cur.get("yaw_deg", 0.0)),
                    "yaw_unwrapped_deg": yaw_unwrapped,
                    "yr": float(cur.get("yr", 0.0)),
                    "el": float(cur.get("el", 0.0)),
                    "er": float(cur.get("er", 0.0)),
                    "ed": float(cur.get("ed", 0.0)),
                    "L": float(cur.get("L", 0.0)),
                    "R": float(cur.get("R", 0.0)),
                    "outL": float(cur.get("outL", cur.get("L", 0.0))),
                    "outR": float(cur.get("outR", cur.get("R", 0.0))),
                    "out_diff": float(cur.get("outL", cur.get("L", 0.0))) - float(cur.get("outR", cur.get("R", 0.0))),
                    "pwm": float(cur.get("pwm", 0.0)),
                    "pc": float(cur.get("pc", 0.0)),
                    "hd": float(cur.get("hd", 0.0)),
                    "aa": float(cur.get("aa", 0.0)),
                    "ae": float(cur.get("ae", 0.0)),
                    "ao": float(cur.get("ao", 0.0)),
                    "se": float(cur.get("se", 0.0)),
                    "so": float(cur.get("so", 0.0)),
                    "x": x,
                    "y": y,
                    "s": s,
                    "dx": dx,
                    "dy": dy,
                    "ds": ds_signed,
                    "kappa": kappa,
                }
            )
            point_idx += 1
            prev_theta = theta
            prev_yaw_unwrapped = yaw_unwrapped

    if not points:
        return points

    final_x = float(points[-1].get("x", 0.0))
    final_y = float(points[-1].get("y", 0.0))
    direct_len = math.sqrt(final_x * final_x + final_y * final_y)
    total_s = float(points[-1].get("s", 0.0))
    line_eps = max(0.8, min(3.0, 0.02 * max(total_s, direct_len, 1.0)))

    for point in points:
        px = float(point.get("x", 0.0))
        py = float(point.get("y", 0.0))
        if direct_len > 1e-9:
            line_progress = (px * final_x + py * final_y) / direct_len
            line_dev = ((-final_y) * px + final_x * py) / direct_len
            line_progress_ratio = line_progress / direct_len
        else:
            line_progress = px
            line_dev = py
            line_progress_ratio = 0.0
        point["line_progress"] = line_progress
        point["line_progress_ratio"] = line_progress_ratio
        point["line_dev"] = line_dev
        point["line_dev_abs"] = abs(line_dev)
        point["line_dev_side"] = _detail_side_from_value(line_dev, line_eps)

    return points


def enrich_windows_with_trajectory_details(
    windows: List[Dict[str, object]],
    points: List[Dict[str, object]],
) -> List[Dict[str, object]]:
    if not windows:
        return []
    if not points:
        return [dict(win) for win in windows]

    out: List[Dict[str, object]] = []
    point_cursor = 0
    for win in windows:
        t0 = float(win.get("t0_s", 0.0))
        t1 = float(win.get("t1_s", 0.0))
        while point_cursor < len(points) and float(points[point_cursor].get("t_s", 0.0)) < t0:
            point_cursor += 1

        use: List[Dict[str, object]] = []
        probe = point_cursor
        while probe < len(points) and float(points[probe].get("t_s", 0.0)) <= t1:
            use.append(points[probe])
            probe += 1
        if not use and point_cursor < len(points):
            use = [points[point_cursor]]

        line_devs = [float(p.get("line_dev", 0.0)) for p in use]
        kappas = [float(p.get("kappa", 0.0)) for p in use]
        yaw_unwrapped = [float(p.get("yaw_unwrapped_deg", 0.0)) for p in use]
        yaw_steps = [yaw_unwrapped[i] - yaw_unwrapped[i - 1] for i in range(1, len(yaw_unwrapped))]
        yaw_switch_n = _detail_zero_crossings(yaw_steps, eps=1.5)
        line_dev_start = line_devs[0] if line_devs else 0.0
        line_dev_end = line_devs[-1] if line_devs else 0.0
        line_dev_mean = mean_or_zero(line_devs)
        line_dev_abs_max = max((abs(v) for v in line_devs), default=0.0)
        curve_rms = math.sqrt(sum(v * v for v in kappas) / len(kappas)) if kappas else 0.0
        curve_abs_mean = mean_or_zero([abs(v) for v in kappas]) if kappas else 0.0
        path_len = float(win.get("path_len", 0.0))
        total_x_end = float(win.get("x_total_end", 0.0))
        total_y_end = float(win.get("y_total_end", 0.0))
        cum_lat_ref = max(abs(total_x_end), path_len, 1.0)
        cum_lat_eps = max(0.08, min(0.35, 0.01 * cum_lat_ref + 0.05))
        micro_cum_lat_eps = max(0.03, min(0.18, 0.005 * cum_lat_ref + 0.02))
        cum_lat_side = _detail_side_from_value(total_y_end, cum_lat_eps)
        micro_cum_lat_side = _detail_side_from_value(total_y_end, micro_cum_lat_eps)
        cum_lat_severity = _detail_deviation_severity(abs(total_y_end), cum_lat_ref)
        dev_eps = max(0.08, min(1.20, 0.02 * max(path_len, 1.0)))
        micro_dev_eps = max(0.03, min(0.40, 0.008 * max(path_len, 1.0) + 0.02))
        deviation_side = _detail_side_from_value(line_dev_end, dev_eps)
        micro_deviation_side = _detail_side_from_value(line_dev_end, micro_dev_eps)
        deviation_severity = _detail_deviation_severity(line_dev_abs_max, path_len)
        segment_kind = _classify_detailed_window(win, yaw_switch_n, line_dev_end, line_dev_abs_max, curve_abs_mean)

        enriched = dict(win)
        enriched.update(
            {
                "line_dev_start": line_dev_start,
                "line_dev_end": line_dev_end,
                "line_dev_mean": line_dev_mean,
                "line_dev_abs_max": line_dev_abs_max,
                "line_dev_delta": line_dev_end - line_dev_start,
                "curve_rms": curve_rms,
                "curve_abs_mean": curve_abs_mean,
                "yaw_switch_n": float(yaw_switch_n),
                "deviation_eps": dev_eps,
                "deviation_side": deviation_side,
                "micro_deviation_eps": micro_dev_eps,
                "micro_deviation_side": micro_deviation_side,
                "deviation_severity": deviation_severity,
                "cum_lat_end": total_y_end,
                "cum_lat_abs": abs(total_y_end),
                "cum_lat_eps": cum_lat_eps,
                "cum_lat_side": cum_lat_side,
                "micro_cum_lat_eps": micro_cum_lat_eps,
                "micro_cum_lat_side": micro_cum_lat_side,
                "cum_lat_severity": cum_lat_severity,
                "segment_kind": segment_kind,
            }
        )
        out.append(enriched)

    return out


def build_detailed_run_report(
    raw_path: str,
    window_s: float = DETAIL_WINDOW_S,
    speed_scale: float = DETAIL_SPEED_SCALE,
) -> Tuple[List[Dict[str, object]], Dict[str, object], List[Dict[str, object]], Dict[str, object]]:
    rows = parse_hb_rows(raw_path)
    run_segments = split_run_stretches(rows)
    if not run_segments:
        return [], build_total_trajectory([], speed_scale), [], {"segment_n": 0, "segments": []}

    base_windows = build_time_windows(run_segments, speed_scale, window_s)
    detailed_windows: List[Dict[str, object]] = []
    base_idx = 0

    for run_seg in run_segments:
        for win in segment_rows(run_seg, window_s):
            if len(win) < 2:
                continue
            if base_idx >= len(base_windows):
                break

            base = dict(base_windows[base_idx])
            base_idx += 1

            yaws = [float(r.get("yaw_deg", 0.0)) for r in win]
            els = [float(r.get("el", 0.0)) for r in win]
            ers = [float(r.get("er", 0.0)) for r in win]
            eds = [float(r.get("ed", float(r.get("el", 0.0)) - float(r.get("er", 0.0)))) for r in win]
            out_ls = [float(r.get("outL", r.get("L", 0.0))) for r in win]
            out_rs = [float(r.get("outR", r.get("R", 0.0))) for r in win]

            base.update(
                {
                    "sample_n": float(len(win)),
                    "yaw_mean": mean_or_zero(yaws),
                    "el_end": els[-1] if els else 0.0,
                    "er_end": ers[-1] if ers else 0.0,
                    "ed_end": eds[-1] if eds else 0.0,
                    "outL_mean": mean_or_zero(out_ls),
                    "outR_mean": mean_or_zero(out_rs),
                    "out_diff_mean": mean_or_zero([a - b for a, b in zip(out_ls, out_rs)]),
                    "outL_end": out_ls[-1] if out_ls else 0.0,
                    "outR_end": out_rs[-1] if out_rs else 0.0,
                }
            )
            detailed_windows.append(base)

    trajectory = build_total_trajectory(run_segments, speed_scale)
    trajectory_points = build_detailed_trajectory_points(run_segments, speed_scale)
    detailed_windows = enrich_windows_with_trajectory_details(detailed_windows, trajectory_points)
    segment_report = {
        "segment_n": len(detailed_windows),
        "segments": detailed_windows,
    }
    return detailed_windows, trajectory, trajectory_points, segment_report


def _pattern_sign(value: float, eps: float = 2.0) -> int:
    if value > eps:
        return 1
    if value < -eps:
        return -1
    return 0


def _window_bias_label(win: Dict[str, object]) -> str:
    out_diff = float(win.get("out_diff_mean", 0.0))
    ed_mean = float(win.get("ed_mean", 0.0))
    score = 0.0
    if out_diff >= 2.0:
        score += 1.0
    elif out_diff <= -2.0:
        score -= 1.0
    if ed_mean >= 2.0:
        score += 1.0
    elif ed_mean <= -2.0:
        score -= 1.0
    if score >= 1.0:
        return "left_biased"
    if score <= -1.0:
        return "right_biased"
    return "balanced"


def _window_spin_candidate(win: Dict[str, object]) -> bool:
    bias = _window_bias_label(win)
    yaw_mag = max(
        abs(float(win.get("yaw_start", 0.0))),
        abs(float(win.get("yaw_end", 0.0))),
        abs(float(win.get("yaw_mean", 0.0))),
    )
    yaw_delta = abs(float(win.get("yaw_delta", 0.0)))
    yr_rms = abs(float(win.get("yr_rms", 0.0)))
    out_diff = abs(float(win.get("out_diff_mean", 0.0)))
    ed_mean = abs(float(win.get("ed_mean", 0.0)))
    return bias != "balanced" and (yaw_mag >= 8.0 or yaw_delta >= 8.0 or yr_rms >= 6.0 or out_diff >= 6.0 or ed_mean >= 6.0)


def _wobbling_straight_stats(windows: List[Dict[str, object]]) -> Dict[str, float]:
    if not windows:
        return {
            "yaw_switches": 0.0,
            "forward_progress": 0.0,
            "lateral_span": 0.0,
            "path_len": 0.0,
            "line_dev_abs_max": 0.0,
            "yaw_delta_abs_mean": 0.0,
            "yaw_delta_abs_max": 0.0,
            "curve_abs_mean": 0.0,
            "non_straight_ratio": 0.0,
            "wobble_score": 0.0,
        }

    yaw_signs = [_pattern_sign(float(w.get("yaw_delta", 0.0))) for w in windows]
    yaw_signs = [s for s in yaw_signs if s != 0]
    yaw_switches = sum(1 for a, b in zip(yaw_signs, yaw_signs[1:]) if a != b)
    ys = [float(w.get("y_total_end", 0.0)) for w in windows]
    x0 = float(windows[0].get("x_total_start", 0.0))
    x1 = float(windows[-1].get("x_total_end", 0.0))
    forward_progress = x1 - x0
    lateral_span = (max(ys) - min(ys)) if ys else 0.0
    path_len = sum(float(w.get("path_len", 0.0)) for w in windows)
    line_dev_abs_max = max((abs(float(w.get("line_dev_abs_max", 0.0))) for w in windows), default=0.0)
    yaw_delta_abs = [abs(float(w.get("yaw_delta", 0.0))) for w in windows]
    curve_abs = [abs(float(w.get("curve_abs_mean", 0.0))) for w in windows]
    non_straight_n = sum(
        1
        for w in windows
        if str(w.get("segment_kind", "straight")) not in ("straight", "drift_left", "drift_right")
    )
    non_straight_ratio = (float(non_straight_n) / float(len(windows))) if windows else 0.0

    wobble_score = 0.0
    if yaw_switches >= 2:
        wobble_score += 1.0
    if lateral_span >= max(1.8, 0.03 * max(path_len, 1.0)):
        wobble_score += 1.0
    if line_dev_abs_max >= max(0.8, 0.02 * max(path_len, 1.0)):
        wobble_score += 1.0
    if max(yaw_delta_abs, default=0.0) >= 2.5:
        wobble_score += 1.0
    if mean_or_zero(curve_abs) >= 0.025:
        wobble_score += 1.0
    if non_straight_ratio >= 0.20:
        wobble_score += 1.0

    return {
        "yaw_switches": float(yaw_switches),
        "forward_progress": forward_progress,
        "lateral_span": lateral_span,
        "path_len": path_len,
        "line_dev_abs_max": line_dev_abs_max,
        "yaw_delta_abs_mean": mean_or_zero(yaw_delta_abs),
        "yaw_delta_abs_max": max(yaw_delta_abs, default=0.0),
        "curve_abs_mean": mean_or_zero(curve_abs),
        "non_straight_ratio": non_straight_ratio,
        "wobble_score": wobble_score,
    }


def _is_wobbling_straight(stats: Dict[str, float]) -> bool:
    yaw_switches = int(stats.get("yaw_switches", 0.0))
    forward_progress = float(stats.get("forward_progress", 0.0))
    lateral_span = float(stats.get("lateral_span", 0.0))
    path_len = float(stats.get("path_len", 0.0))
    line_dev_abs_max = float(stats.get("line_dev_abs_max", 0.0))
    yaw_delta_abs_max = float(stats.get("yaw_delta_abs_max", 0.0))
    curve_abs_mean = float(stats.get("curve_abs_mean", 0.0))
    non_straight_ratio = float(stats.get("non_straight_ratio", 0.0))
    wobble_score = float(stats.get("wobble_score", 0.0))

    if yaw_switches < 2:
        return False
    if forward_progress <= max(6.0, 0.5 * max(path_len, 1.0)):
        return False
    if wobble_score < 3.0:
        return False
    if lateral_span < max(1.8, 0.03 * max(path_len, 1.0)) and line_dev_abs_max < max(0.8, 0.02 * max(path_len, 1.0)):
        return False
    if yaw_delta_abs_max < 2.5 and curve_abs_mean < 0.025 and non_straight_ratio < 0.20:
        return False
    return True


def _phase_side_to_pattern_label(side: str) -> str:
    if side == "left":
        return "left_drift"
    if side == "right":
        return "right_drift"
    return "straight"


def summarize_phase_pattern(windows: List[Dict[str, object]]) -> Dict[str, object]:
    if not windows:
        return {}

    best_start = -1
    best_end = -1
    best_len = 0
    best_bias = "balanced"
    i = 0
    while i < len(windows):
        if not _window_spin_candidate(windows[i]):
            i += 1
            continue
        bias = _window_bias_label(windows[i])
        j = i
        while j + 1 < len(windows) and _window_spin_candidate(windows[j + 1]) and _window_bias_label(windows[j + 1]) == bias:
            j += 1
        run_len = j - i + 1
        if run_len > best_len:
            best_start = i
            best_end = j
            best_len = run_len
            best_bias = bias
        i = j + 1

    phases: List[Dict[str, object]] = []
    pattern = "unclassified"
    confidence = 0.0
    onset_summary = summarize_window_onsets(windows)
    launch_hesitation_window_count = int(onset_summary.get("launch_hesitation_window_count", 0))
    persistent_bias_window = int(onset_summary.get("persistent_bias_window", 0))
    persistent_bias_side = str(onset_summary.get("persistent_bias_side", "balanced"))
    max_bias_cum_lat = abs(float(onset_summary.get("max_bias_cum_lat", 0.0)))
    first_bias_window = int(onset_summary.get("first_bias_window", 0))
    mid_run_pause_window = int(onset_summary.get("mid_run_pause_window", 0))
    mid_run_pause_window_count = int(onset_summary.get("mid_run_pause_window_count", 0))

    if launch_hesitation_window_count > 0:
        phases.append(
            {
                "kind": "launch_hesitation",
                "t0_s": float(windows[0].get("t0_s", 0.0)),
                "t1_s": float(onset_summary.get("launch_hesitation_t1_s", 0.0)),
                "window_count": launch_hesitation_window_count,
                "slow_window_count": int(onset_summary.get("slow_window_count", 0)),
            }
        )

    if mid_run_pause_window > 0 and mid_run_pause_window_count > 0:
        phases.append(
            {
                "kind": "mid_run_pause",
                "t0_s": float(onset_summary.get("mid_run_pause_t0_s", 0.0)),
                "t1_s": float(onset_summary.get("mid_run_pause_t1_s", 0.0)),
                "window_count": mid_run_pause_window_count,
                "start_window": mid_run_pause_window,
                "max_idle_streak_s": float(onset_summary.get("mid_run_pause_max_idle_streak_s", 0.0)),
                "max_output_idle_streak_s": float(onset_summary.get("mid_run_pause_max_output_idle_streak_s", 0.0)),
            }
        )

    if best_len >= 2:
        prefix = windows[:best_start]
        spin = windows[best_start:best_end + 1]

        spin_yaw_abs_mean = mean_or_zero([
            abs(float(w.get("yaw_mean", 0.0))) for w in spin
        ])
        spin_out_diff_abs_mean = mean_or_zero([
            abs(float(w.get("out_diff_mean", 0.0))) for w in spin
        ])
        spin_ed_abs_mean = mean_or_zero([
            abs(float(w.get("ed_mean", 0.0))) for w in spin
        ])

        phases.append(
            {
                "kind": "spin_turn",
                "t0_s": float(spin[0].get("t0_s", 0.0)),
                "t1_s": float(spin[-1].get("t1_s", 0.0)),
                "window_count": len(spin),
                "bias": best_bias,
                "yaw_abs_mean": spin_yaw_abs_mean,
                "out_diff_abs_mean": spin_out_diff_abs_mean,
                "ed_abs_mean": spin_ed_abs_mean,
            }
        )

        if len(prefix) >= 2:
            prefix_stats = _wobbling_straight_stats(prefix)
            yaw_switches = int(prefix_stats.get("yaw_switches", 0.0))
            prefix_forward = float(prefix_stats.get("forward_progress", 0.0))
            prefix_lat_span = float(prefix_stats.get("lateral_span", 0.0))
            prefix_path = float(prefix_stats.get("path_len", 0.0))
            wobbling_straight = _is_wobbling_straight(prefix_stats)
            if wobbling_straight:
                phases.insert(
                    0,
                    {
                        "kind": "wobbling_straight",
                        "t0_s": float(prefix[0].get("t0_s", 0.0)),
                        "t1_s": float(prefix[-1].get("t1_s", 0.0)),
                        "window_count": len(prefix),
                        "yaw_switches": yaw_switches,
                        "forward_progress": prefix_forward,
                        "lateral_span": prefix_lat_span,
                        "path_len": prefix_path,
                        "line_dev_abs_max": float(prefix_stats.get("line_dev_abs_max", 0.0)),
                        "yaw_delta_abs_mean": float(prefix_stats.get("yaw_delta_abs_mean", 0.0)),
                        "curve_abs_mean": float(prefix_stats.get("curve_abs_mean", 0.0)),
                        "non_straight_ratio": float(prefix_stats.get("non_straight_ratio", 0.0)),
                        "wobble_score": float(prefix_stats.get("wobble_score", 0.0)),
                    },
                )
                pattern = "wobbling_straight_then_spin_turn"
                confidence = min(0.95, 0.45 + 0.10 * float(best_len) + 0.08 * float(yaw_switches))
            else:
                if persistent_bias_side in ("left", "right") and persistent_bias_window > 0:
                    drift_label = _phase_side_to_pattern_label(persistent_bias_side)
                    if launch_hesitation_window_count > 0:
                        pattern = f"launch_hesitation_then_{drift_label}_then_spin_turn"
                        confidence = min(
                            0.92,
                            0.48
                            + 0.07
                            * float(min(launch_hesitation_window_count, 6))
                            + 0.03
                            * float(min(mid_run_pause_window_count, 4)),
                        )
                    else:
                        pattern = f"{drift_label}_then_spin_turn"
                        confidence = min(0.88, 0.45 + 0.08 * float(best_len))
                    phases.append(
                        {
                            "kind": drift_label,
                            "t0_s": float(onset_summary.get("persistent_bias_t0_s", 0.0)),
                            "t1_s": float(spin[0].get("t0_s", 0.0)),
                            "window_count": max(
                                0, best_start - (persistent_bias_window - 1)
                            ),
                            "side": persistent_bias_side,
                            "start_window": persistent_bias_window,
                            "max_cum_lat": max_bias_cum_lat,
                        },
                    )
                else:
                    pattern = "straight_to_spin_turn"
                    confidence = min(0.85, 0.40 + 0.10 * float(best_len))
        else:
            pattern = "spin_turn"
            confidence = min(0.80, 0.35 + 0.12 * float(best_len))
    else:
        stats = _wobbling_straight_stats(windows)
        yaw_switches = int(stats.get("yaw_switches", 0.0))
        if _is_wobbling_straight(stats):
            pattern = "wobbling_straight"
            confidence = min(
                0.70,
                0.30
                + 0.06
                * float(yaw_switches)
                + 0.05
                * float(stats.get("wobble_score", 0.0)),
            )
            phases.append(
                {
                    "kind": "wobbling_straight",
                    "t0_s": float(windows[0].get("t0_s", 0.0)),
                    "t1_s": float(windows[-1].get("t1_s", 0.0)),
                    "window_count": len(windows),
                    "yaw_switches": yaw_switches,
                    "forward_progress": float(stats.get("forward_progress", 0.0)),
                    "lateral_span": float(stats.get("lateral_span", 0.0)),
                    "path_len": float(stats.get("path_len", 0.0)),
                    "line_dev_abs_max": float(stats.get("line_dev_abs_max", 0.0)),
                    "yaw_delta_abs_mean": float(stats.get("yaw_delta_abs_mean", 0.0)),
                    "curve_abs_mean": float(stats.get("curve_abs_mean", 0.0)),
                    "non_straight_ratio": float(stats.get("non_straight_ratio", 0.0)),
                    "wobble_score": float(stats.get("wobble_score", 0.0)),
                }
            )
        elif persistent_bias_side in ("left", "right") and persistent_bias_window > 0 and max_bias_cum_lat >= 0.08:
            drift_label = _phase_side_to_pattern_label(persistent_bias_side)
            if launch_hesitation_window_count > 0:
                pattern = f"launch_hesitation_then_{drift_label}"
                confidence = min(
                    0.88,
                    0.38
                    + 0.05
                    * float(min(launch_hesitation_window_count, 6))
                    + 0.10
                    * min(1.0, max_bias_cum_lat / 1.0),
                )
            else:
                pattern = drift_label
                confidence = min(0.78, 0.34 + 0.10 * min(1.0, max_bias_cum_lat / 1.0))

            phases.append(
                {
                    "kind": drift_label,
                    "t0_s": float(onset_summary.get("persistent_bias_t0_s", onset_summary.get("first_bias_t0_s", 0.0))),
                    "t1_s": float(windows[-1].get("t1_s", 0.0)),
                    "window_count": max(0, len(windows) - persistent_bias_window + 1),
                    "side": persistent_bias_side,
                    "start_window": persistent_bias_window,
                    "first_window": first_bias_window,
                    "max_cum_lat": max_bias_cum_lat,
                }
            )
        elif launch_hesitation_window_count > 0:
            pattern = "launch_hesitation_then_straight"
            confidence = min(0.72, 0.34 + 0.05 * float(min(launch_hesitation_window_count, 6)))

    if mid_run_pause_window > 0 and mid_run_pause_window_count > 0 and "mid_run_pause" not in pattern:
        if pattern == "unclassified":
            pattern = "mid_run_pause_then_straight"
            confidence = min(0.72, 0.38 + 0.06 * float(min(mid_run_pause_window_count, 4)))
        elif pattern.endswith("_straight"):
            pattern = pattern.replace("_straight", "_mid_run_pause_then_straight")

    return {
        "pattern": pattern,
        "confidence": confidence,
        "phase_count": len(phases),
        "phases": phases,
    }


def summarize_window_onsets(windows: List[Dict[str, object]]) -> Dict[str, object]:
    if not windows:
        return {}

    rates = [_window_progress_rate(win) for win in windows]
    valid_rates = [r for r in rates if r > 0.0]
    cruise_pool = sorted(valid_rates)
    if len(cruise_pool) >= 2:
        cruise_pool = cruise_pool[len(cruise_pool) // 2:]
    cruise_rate = median_or_zero(cruise_pool)
    hesitation_rate_threshold = max(0.5, 0.65 * cruise_rate) if cruise_rate > 0.0 else 0.0

    hesitation_window_count = 0
    for rate in rates:
        if hesitation_rate_threshold > 0.0 and rate < hesitation_rate_threshold:
            hesitation_window_count += 1
        else:
            break

    slow_rate_threshold = max(0.5, 0.60 * cruise_rate) if cruise_rate > 0.0 else 0.0
    slow_window_count = sum(1 for rate in rates if slow_rate_threshold > 0.0 and rate < slow_rate_threshold)

    slowest_idx = min(range(len(windows)), key=lambda i: rates[i]) if windows else -1
    sides = [str(win.get("cum_lat_side", "balanced")) for win in windows]
    micro_sides = []
    for win in windows:
        micro_cum_side = str(win.get("micro_cum_lat_side", "balanced"))
        micro_dev_side = str(win.get("micro_deviation_side", "balanced"))
        if micro_cum_side != "balanced":
            micro_sides.append(micro_cum_side)
        elif micro_dev_side != "balanced":
            micro_sides.append(micro_dev_side)
        else:
            micro_sides.append("balanced")
    side_counts = {
        "left": sum(1 for side in sides if side == "left"),
        "right": sum(1 for side in sides if side == "right"),
        "balanced": sum(1 for side in sides if side == "balanced"),
    }
    micro_side_counts = {
        "left": sum(1 for side in micro_sides if side == "left"),
        "right": sum(1 for side in micro_sides if side == "right"),
        "balanced": sum(1 for side in micro_sides if side == "balanced"),
    }

    micro_bias_idx = -1
    micro_bias_side = "balanced"
    for idx, side in enumerate(micro_sides):
        if side != "balanced":
            micro_bias_idx = idx
            micro_bias_side = side
            break

    micro_persistent_bias_idx = -1
    micro_persistent_bias_side = "balanced"
    for idx, side in enumerate(micro_sides):
        if side == "balanced":
            continue
        tail = micro_sides[idx:min(len(micro_sides), idx + 3)]
        if sum(1 for item in tail if item == side) >= 2:
            micro_persistent_bias_idx = idx
            micro_persistent_bias_side = side
            break

    first_bias_idx = -1
    first_bias_side = "balanced"
    for idx, side in enumerate(sides):
        if side != "balanced":
            first_bias_idx = idx
            first_bias_side = side
            break

    persistent_bias_idx = -1
    persistent_bias_side = "balanced"
    for idx, side in enumerate(sides):
        if side == "balanced":
            continue
        tail = sides[idx:min(len(sides), idx + 3)]
        if sum(1 for item in tail if item == side) >= 2:
            persistent_bias_idx = idx
            persistent_bias_side = side
            break

    max_bias_idx = max(range(len(windows)), key=lambda i: abs(float(windows[i].get("cum_lat_end", 0.0)))) if windows else -1

    cruise_ref_idxs = [
        idx for idx, rate in enumerate(rates)
        if idx >= hesitation_window_count and rate >= max(4.0, 0.70 * cruise_rate)
    ]
    if len(cruise_ref_idxs) < 3:
        cruise_ref_idxs = [idx for idx, rate in enumerate(rates) if idx >= hesitation_window_count and rate > 0.0]
    cruise_pc = median_or_zero([abs(float(windows[idx].get("pc_mean", 0.0))) for idx in cruise_ref_idxs])
    cruise_out = median_or_zero([_window_drive_mean(windows[idx]) for idx in cruise_ref_idxs])
    cruise_so = median_or_zero([abs(float(windows[idx].get("so_mean", 0.0))) for idx in cruise_ref_idxs])
    cruise_ts = median_or_zero([abs(float(windows[idx].get("ts_mean", 0.0))) for idx in cruise_ref_idxs])

    pause_flags = [
        _window_mid_run_pause_flags(win, cruise_rate, cruise_pc, cruise_out, cruise_so, cruise_ts)
        for win in windows
    ]

    pause_candidate_idxs = [
        idx
        for idx, win in enumerate(windows)
        if idx >= hesitation_window_count
        and bool(pause_flags[idx].get("hard", False))
    ]
    if len(pause_candidate_idxs) < 2:
        pause_candidate_idxs = []
    mid_run_pause_idx = pause_candidate_idxs[0] if pause_candidate_idxs else -1
    mid_run_pause_last_idx = pause_candidate_idxs[-1] if pause_candidate_idxs else -1

    summary: Dict[str, object] = {
        "window_count": len(windows),
        "cruise_rate": cruise_rate,
        "cruise_pc": cruise_pc,
        "cruise_out": cruise_out,
        "cruise_so": cruise_so,
        "cruise_ts": cruise_ts,
        "hesitation_rate_threshold": hesitation_rate_threshold,
        "launch_hesitation_window_count": hesitation_window_count,
        "launch_hesitation_t1_s": float(windows[hesitation_window_count - 1].get("t1_s", 0.0)) if hesitation_window_count > 0 else 0.0,
        "slow_window_count": slow_window_count,
        "side_counts": side_counts,
        "micro_side_counts": micro_side_counts,
    }

    if slowest_idx >= 0:
        slowest = windows[slowest_idx]
        summary.update(
            {
                "slowest_window": slowest_idx + 1,
                "slowest_t0_s": float(slowest.get("t0_s", 0.0)),
                "slowest_t1_s": float(slowest.get("t1_s", 0.0)),
                "slowest_path_rate": rates[slowest_idx],
                "slowest_state": str(slowest.get("state", "unknown")),
            }
        )

    if mid_run_pause_idx >= 0:
        first_pause = windows[mid_run_pause_idx]
        last_pause = windows[mid_run_pause_last_idx]
        summary.update(
            {
                "mid_run_pause_window": mid_run_pause_idx + 1,
                "mid_run_pause_window_count": len(pause_candidate_idxs),
                "mid_run_pause_t0_s": float(first_pause.get("t0_s", 0.0)),
                "mid_run_pause_t1_s": float(last_pause.get("t1_s", 0.0)),
                "mid_run_pause_state": str(first_pause.get("state", "unknown")),
                "mid_run_pause_path_rate": rates[mid_run_pause_idx],
                "mid_run_pause_max_idle_streak_s": max(float(windows[i].get("idle_streak_s", 0.0)) for i in pause_candidate_idxs),
                "mid_run_pause_max_output_idle_streak_s": max(float(windows[i].get("output_idle_streak_s", 0.0)) for i in pause_candidate_idxs),
                "mid_run_pause_pc_drop_count": sum(1 for i in pause_candidate_idxs if bool(pause_flags[i].get("pc_drop", False))),
                "mid_run_pause_out_drop_count": sum(1 for i in pause_candidate_idxs if bool(pause_flags[i].get("out_drop", False))),
                "mid_run_pause_path_stop_count": sum(1 for i in pause_candidate_idxs if bool(pause_flags[i].get("path_stop", False))),
                "mid_run_pause_so_drop_count": sum(1 for i in pause_candidate_idxs if bool(pause_flags[i].get("so_drop", False))),
                "mid_run_pause_ts_drop_count": sum(1 for i in pause_candidate_idxs if bool(pause_flags[i].get("ts_drop", False))),
            }
        )

    if first_bias_idx >= 0:
        first_bias = windows[first_bias_idx]
        summary.update(
            {
                "first_bias_window": first_bias_idx + 1,
                "first_bias_side": first_bias_side,
                "first_bias_t0_s": float(first_bias.get("t0_s", 0.0)),
                "first_bias_t1_s": float(first_bias.get("t1_s", 0.0)),
                "first_bias_cum_lat": float(first_bias.get("cum_lat_end", 0.0)),
                "first_bias_cum_lat_eps": float(first_bias.get("cum_lat_eps", 0.0)),
                "first_bias_state": str(first_bias.get("state", "unknown")),
            }
        )

    if micro_bias_idx >= 0:
        micro_bias = windows[micro_bias_idx]
        summary.update(
            {
                "micro_bias_window": micro_bias_idx + 1,
                "micro_bias_side": micro_bias_side,
                "micro_bias_t0_s": float(micro_bias.get("t0_s", 0.0)),
                "micro_bias_t1_s": float(micro_bias.get("t1_s", 0.0)),
                "micro_bias_cum_lat": float(micro_bias.get("cum_lat_end", 0.0)),
                "micro_bias_cum_lat_eps": float(micro_bias.get("micro_cum_lat_eps", 0.0)),
                "micro_bias_line_dev": float(micro_bias.get("line_dev_end", 0.0)),
                "micro_bias_line_dev_eps": float(micro_bias.get("micro_deviation_eps", 0.0)),
                "micro_bias_state": str(micro_bias.get("state", "unknown")),
            }
        )

    if micro_persistent_bias_idx >= 0:
        micro_persistent_bias = windows[micro_persistent_bias_idx]
        summary.update(
            {
                "micro_persistent_bias_window": micro_persistent_bias_idx + 1,
                "micro_persistent_bias_side": micro_persistent_bias_side,
                "micro_persistent_bias_t0_s": float(micro_persistent_bias.get("t0_s", 0.0)),
                "micro_persistent_bias_t1_s": float(micro_persistent_bias.get("t1_s", 0.0)),
                "micro_persistent_bias_cum_lat": float(micro_persistent_bias.get("cum_lat_end", 0.0)),
                "micro_persistent_bias_cum_lat_eps": float(micro_persistent_bias.get("micro_cum_lat_eps", 0.0)),
            }
        )

    if persistent_bias_idx >= 0:
        persistent_bias = windows[persistent_bias_idx]
        summary.update(
            {
                "persistent_bias_window": persistent_bias_idx + 1,
                "persistent_bias_side": persistent_bias_side,
                "persistent_bias_t0_s": float(persistent_bias.get("t0_s", 0.0)),
                "persistent_bias_t1_s": float(persistent_bias.get("t1_s", 0.0)),
                "persistent_bias_cum_lat": float(persistent_bias.get("cum_lat_end", 0.0)),
                "persistent_bias_cum_lat_eps": float(persistent_bias.get("cum_lat_eps", 0.0)),
            }
        )

    if max_bias_idx >= 0:
        max_bias = windows[max_bias_idx]
        summary.update(
            {
                "max_bias_window": max_bias_idx + 1,
                "max_bias_side": str(max_bias.get("cum_lat_side", "balanced")),
                "max_bias_t0_s": float(max_bias.get("t0_s", 0.0)),
                "max_bias_t1_s": float(max_bias.get("t1_s", 0.0)),
                "max_bias_cum_lat": float(max_bias.get("cum_lat_end", 0.0)),
                "max_bias_cum_lat_abs": abs(float(max_bias.get("cum_lat_end", 0.0))),
                "max_bias_cum_lat_eps": float(max_bias.get("cum_lat_eps", 0.0)),
            }
        )

    return summary


def print_detailed_window_report(windows: List[Dict[str, object]]) -> None:
    if not windows:
        print("DETAIL: no run windows reconstructed")
        return

    print(f"DETAIL window_s={DETAIL_WINDOW_S:.3f} count={len(windows)} traj_unit=encoder_rel")
    for idx, win in enumerate(windows, start=1):
        print(
            f"WIN {idx:02d} t={float(win.get('t0_s', 0.0)):.2f}-{float(win.get('t1_s', 0.0)):.2f}s "
            f"yaw={float(win.get('yaw_start', 0.0)):.3f}->{float(win.get('yaw_end', 0.0)):.3f} "
            f"dy={float(win.get('yaw_delta', 0.0)):.3f} ymean={float(win.get('yaw_mean', 0.0)):.3f} "
            f"enc_mean L/R={float(win.get('el_mean', 0.0)):.3f}/{float(win.get('er_mean', 0.0)):.3f} "
            f"enc_end L/R={float(win.get('el_end', 0.0)):.3f}/{float(win.get('er_end', 0.0)):.3f} "
            f"out_mean L/R={float(win.get('outL_mean', 0.0)):.3f}/{float(win.get('outR_mean', 0.0)):.3f} "
            f"out_end L/R={float(win.get('outL_end', 0.0)):.3f}/{float(win.get('outR_end', 0.0)):.3f} "
            f"pc/hd/ao={float(win.get('pc_mean', 0.0)):.3f}/{float(win.get('hd_mean', 0.0)):.3f}/{float(win.get('ao_mean', 0.0)):.3f} "
            f"dev_end/max={float(win.get('line_dev_end', 0.0)):.3f}/{float(win.get('line_dev_abs_max', 0.0)):.3f} "
            f"dev_side={str(win.get('deviation_side', 'balanced'))} cum_y={float(win.get('cum_lat_end', 0.0)):.3f} cum_side={str(win.get('cum_lat_side', 'balanced'))} "
            f"kind={str(win.get('segment_kind', 'unknown'))} "
            f"xy_end=({float(win.get('x_total_end', 0.0)):.3f},{float(win.get('y_total_end', 0.0)):.3f}) "
            f"path={float(win.get('path_len', 0.0)):.3f} state={str(win.get('state', 'unknown'))}"
        )


def print_window_onset_report(summary: Dict[str, object]) -> None:
    if not summary:
        return
    print(
        "ONSET "
        f"launch_hesitation_w={int(summary.get('launch_hesitation_window_count', 0))} "
        f"launch_hesitation_t1_s={float(summary.get('launch_hesitation_t1_s', 0.0)):.2f} "
        f"cruise_rate={float(summary.get('cruise_rate', 0.0)):.3f} "
        f"slow_w={int(summary.get('slow_window_count', 0))} "
        f"slowest_win={int(summary.get('slowest_window', 0))} "
        f"slowest_rate={float(summary.get('slowest_path_rate', 0.0)):.3f}"
    )
    if int(summary.get("micro_bias_window", 0)) > 0:
        print(
            "MICRO_BIAS "
            f"first_win={int(summary.get('micro_bias_window', 0))} "
            f"side={summary.get('micro_bias_side', 'balanced')} "
            f"t={float(summary.get('micro_bias_t0_s', 0.0)):.2f}-{float(summary.get('micro_bias_t1_s', 0.0)):.2f}s "
            f"cum_y={float(summary.get('micro_bias_cum_lat', 0.0)):.3f}/{float(summary.get('micro_bias_cum_lat_eps', 0.0)):.3f} "
            f"line_y={float(summary.get('micro_bias_line_dev', 0.0)):.3f}/{float(summary.get('micro_bias_line_dev_eps', 0.0)):.3f}"
        )
    if int(summary.get("micro_persistent_bias_window", 0)) > 0:
        print(
            "MICRO_BIAS_PERSIST "
            f"win={int(summary.get('micro_persistent_bias_window', 0))} "
            f"side={summary.get('micro_persistent_bias_side', 'balanced')} "
            f"t={float(summary.get('micro_persistent_bias_t0_s', 0.0)):.2f}-{float(summary.get('micro_persistent_bias_t1_s', 0.0)):.2f}s "
            f"cum_y={float(summary.get('micro_persistent_bias_cum_lat', 0.0)):.3f}"
        )
    if int(summary.get("first_bias_window", 0)) > 0:
        print(
            "BIAS "
            f"first_win={int(summary.get('first_bias_window', 0))} "
            f"side={summary.get('first_bias_side', 'balanced')} "
            f"t={float(summary.get('first_bias_t0_s', 0.0)):.2f}-{float(summary.get('first_bias_t1_s', 0.0)):.2f}s "
            f"cum_y={float(summary.get('first_bias_cum_lat', 0.0)):.3f} "
            f"eps={float(summary.get('first_bias_cum_lat_eps', 0.0)):.3f}"
        )
    if int(summary.get("persistent_bias_window", 0)) > 0:
        print(
            "BIAS_PERSIST "
            f"win={int(summary.get('persistent_bias_window', 0))} "
            f"side={summary.get('persistent_bias_side', 'balanced')} "
            f"t={float(summary.get('persistent_bias_t0_s', 0.0)):.2f}-{float(summary.get('persistent_bias_t1_s', 0.0)):.2f}s "
            f"cum_y={float(summary.get('persistent_bias_cum_lat', 0.0)):.3f}"
        )
    if int(summary.get("max_bias_window", 0)) > 0:
        print(
            "BIAS_MAX "
            f"win={int(summary.get('max_bias_window', 0))} "
            f"side={summary.get('max_bias_side', 'balanced')} "
            f"t={float(summary.get('max_bias_t0_s', 0.0)):.2f}-{float(summary.get('max_bias_t1_s', 0.0)):.2f}s "
            f"cum_y={float(summary.get('max_bias_cum_lat', 0.0)):.3f}"
        )
    if int(summary.get("mid_run_pause_window", 0)) > 0:
        print(
            "MID_PAUSE "
            f"win={int(summary.get('mid_run_pause_window', 0))} "
            f"count={int(summary.get('mid_run_pause_window_count', 0))} "
            f"t={float(summary.get('mid_run_pause_t0_s', 0.0)):.2f}-{float(summary.get('mid_run_pause_t1_s', 0.0)):.2f}s "
            f"state={summary.get('mid_run_pause_state', 'unknown')} "
            f"idle={float(summary.get('mid_run_pause_max_idle_streak_s', 0.0)):.3f} "
            f"out_idle={float(summary.get('mid_run_pause_max_output_idle_streak_s', 0.0)):.3f}"
        )


def print_phase_pattern_report(summary: Dict[str, object]) -> None:
    if not summary:
        return
    print(
        "PATTERN "
        f"pattern={summary.get('pattern', 'unclassified')} "
        f"conf={float(summary.get('confidence', 0.0)):.3f} "
        f"phase_n={int(summary.get('phase_count', 0))}"
    )
    for idx, phase in enumerate(summary.get("phases", []), start=1):
        if str(phase.get("kind", "")) == "wobbling_straight":
            print(
                f"PHASE {idx} kind=wobbling_straight "
                f"t={float(phase.get('t0_s', 0.0)):.2f}-{float(phase.get('t1_s', 0.0)):.2f}s "
                f"windows={int(phase.get('window_count', 0))} "
                f"yaw_switches={int(phase.get('yaw_switches', 0))} "
                f"forward={float(phase.get('forward_progress', 0.0)):.3f} "
                f"lat_span={float(phase.get('lateral_span', 0.0)):.3f}"
            )
        elif str(phase.get("kind", "")) == "spin_turn":
            print(
                f"PHASE {idx} kind=spin_turn "
                f"t={float(phase.get('t0_s', 0.0)):.2f}-{float(phase.get('t1_s', 0.0)):.2f}s "
                f"windows={int(phase.get('window_count', 0))} "
                f"bias={phase.get('bias', 'balanced')} "
                f"yaw_abs_mean={float(phase.get('yaw_abs_mean', 0.0)):.3f} "
                f"out_diff_abs_mean={float(phase.get('out_diff_abs_mean', 0.0)):.3f} "
                f"ed_abs_mean={float(phase.get('ed_abs_mean', 0.0)):.3f}"
            )
        else:
            print(
                f"PHASE {idx} kind={phase.get('kind', 'unknown')} "
                f"t={float(phase.get('t0_s', 0.0)):.2f}-{float(phase.get('t1_s', 0.0)):.2f}s "
                f"windows={int(phase.get('window_count', 0))}"
            )


def print_trajectory_report(trajectory: Dict[str, object]) -> None:
    print(
        "TRAJ "
        f"final=({float(trajectory.get('final_x', 0.0)):.3f},{float(trajectory.get('final_y', 0.0)):.3f}) "
        f"path={float(trajectory.get('path_len', 0.0)):.3f} direct={float(trajectory.get('direct_len', 0.0)):.3f} "
        f"sinuosity={float(trajectory.get('sinuosity', 1.0)):.3f} lat_abs_max={float(trajectory.get('lateral_abs_max', 0.0)):.3f} "
        f"mean_y={float(trajectory.get('mean_y', 0.0)):.3f} final_y_per_path={float(trajectory.get('final_y_per_path', 0.0)):.6f} "
        f"y_pos_ratio={float(trajectory.get('y_pos_ratio', 0.0)):.3f} y_neg_ratio={float(trajectory.get('y_neg_ratio', 0.0)):.3f} y_zero_ratio={float(trajectory.get('y_zero_ratio', 0.0)):.3f} "
        f"heading_change={float(trajectory.get('heading_change', 0.0)):.3f} heading_variation={float(trajectory.get('heading_variation', 0.0)):.3f} "
        f"dominant_side={trajectory.get('dominant_side', 'balanced')} conf={float(trajectory.get('dominant_conf', 0.0)):.3f} "
        f"side_eps={float(trajectory.get('trajectory_side_eps', 0.0)):.3f} "
        f"bias_severity={trajectory.get('bias_severity', 'unknown')} stability={trajectory.get('stability', 'unknown')} "
        f"reuse={trajectory.get('reuse_recommendation', 'unknown')} "
        f"point_n={int(trajectory.get('point_n', 0))}"
    )


def write_window_csv(path: str, windows: List[Dict[str, object]]) -> None:
    fieldnames = [
        "seg_index",
        "sample_n",
        "t0_s",
        "t1_s",
        "duration_s",
        "yaw_start",
        "yaw_end",
        "yaw_delta",
        "yaw_mean",
        "yr_mean",
        "yr_rms",
        "el_mean",
        "er_mean",
        "ed_mean",
        "el_end",
        "er_end",
        "ed_end",
        "outL_mean",
        "outR_mean",
        "out_diff_mean",
        "outL_end",
        "outR_end",
        "pwm_mean",
        "pwm_end",
        "pc_mean",
        "pc_end",
        "hd_mean",
        "hd_end",
        "ts_mean",
        "ts_end",
        "as_mean",
        "as_end",
        "aa_mean",
        "aa_end",
        "ae_mean",
        "ae_end",
        "ao_mean",
        "ao_end",
        "se_mean",
        "se_end",
        "so_mean",
        "so_end",
        "curve_abs_mean",
        "line_dev_start",
        "line_dev_end",
        "line_dev_mean",
        "line_dev_abs_max",
        "line_dev_delta",
        "yaw_switch_n",
        "deviation_eps",
        "deviation_side",
        "deviation_severity",
        "micro_deviation_eps",
        "micro_deviation_side",
        "cum_lat_end",
        "cum_lat_abs",
        "cum_lat_eps",
        "cum_lat_side",
        "micro_cum_lat_eps",
        "micro_cum_lat_side",
        "cum_lat_severity",
        "segment_kind",
        "x_local",
        "y_local",
        "path_len",
        "x_total_start",
        "y_total_start",
        "x_total_end",
        "y_total_end",
        "motion_level",
        "wheel_bias",
        "turn_state",
        "turn_conf",
        "turn_yaw_eps",
        "turn_y_eps",
        "state",
        "enc_active_ratio",
        "enc_nonzero_ratio",
        "out_nonzero_ratio",
        "out_strong_ratio",
        "output_only_ratio",
        "speed_proxy",
        "idle_streak_s",
        "output_idle_streak_s",
        "pause_suspected",
        "stiction",
    ]
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for win in windows:
            writer.writerow({k: win.get(k, "") for k in fieldnames})


def write_trajectory_csv(path: str, trajectory: Dict[str, object]) -> None:
    points = trajectory.get("points", [])
    with open(path, "w", newline="", encoding="utf-8") as f:
        fieldnames = [
            "idx",
            "seg_index",
            "seg_row_index",
            "tick",
            "t_s",
            "yaw_deg",
            "yaw_unwrapped_deg",
            "yr",
            "el",
            "er",
            "ed",
            "L",
            "R",
            "outL",
            "outR",
            "out_diff",
            "pwm",
            "pc",
            "hd",
            "aa",
            "ae",
            "ao",
            "se",
            "so",
            "x",
            "y",
            "s",
            "dx",
            "dy",
            "ds",
            "kappa",
            "line_progress",
            "line_progress_ratio",
            "line_dev",
            "line_dev_abs",
            "line_dev_side",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for point in points:
            writer.writerow({k: point.get(k, "") for k in fieldnames})


def write_segments_json(path: str, payload: Dict[str, object]) -> None:
    write_json_file(path, payload)


def emit_detailed_analysis(raw_path: str) -> Tuple[str, str]:
    windows, trajectory, trajectory_points, segment_report = build_detailed_run_report(raw_path)
    phase_summary = summarize_phase_pattern(windows)
    onset_summary = summarize_window_onsets(windows)
    print_detailed_window_report(windows)
    print_trajectory_report(trajectory)
    print_window_onset_report(onset_summary)
    print_phase_pattern_report(phase_summary)

    prefix = raw_path[:-8] if raw_path.endswith("_raw.txt") else os.path.splitext(raw_path)[0]
    windows_csv = prefix + "_windows.csv"
    trajectory_csv = prefix + "_trajectory.csv"
    phases_json = prefix + "_phases.json"
    segments_json = prefix + "_segments.json"
    analysis_json = prefix + ANALYSIS_SUFFIX
    write_window_csv(windows_csv, windows)
    write_trajectory_csv(trajectory_csv, {"points": trajectory_points})
    write_json_file(phases_json, phase_summary)
    write_segments_json(
        segments_json,
        {
            "segment_n": int(segment_report.get("segment_n", 0)),
            "segments": segment_report.get("segments", []),
            "phase_summary": phase_summary,
            "window_onset_summary": onset_summary,
            "trajectory_summary": {
                "final_x": float(trajectory.get("final_x", 0.0)),
                "final_y": float(trajectory.get("final_y", 0.0)),
                "path_len": float(trajectory.get("path_len", 0.0)),
                "direct_len": float(trajectory.get("direct_len", 0.0)),
                "lateral_abs_max": float(trajectory.get("lateral_abs_max", 0.0)),
                "heading_change": float(trajectory.get("heading_change", 0.0)),
                "heading_variation": float(trajectory.get("heading_variation", 0.0)),
            },
        },
    )
    if os.path.exists(analysis_json):
        with open(analysis_json, "r", encoding="utf-8") as f:
            analysis_payload = json.load(f)
        max_line_dev_abs = max((abs(float(w.get("line_dev_abs_max", 0.0))) for w in windows), default=0.0)
        segment_kind_counts: Dict[str, int] = {}
        for win in windows:
            kind = str(win.get("segment_kind", "unknown"))
            segment_kind_counts[kind] = segment_kind_counts.get(kind, 0) + 1
        analysis_payload.update(
            {
                "final_x": float(trajectory.get("final_x", 0.0)),
                "final_y": float(trajectory.get("final_y", 0.0)),
                "path": float(trajectory.get("path_len", 0.0)),
                "direct": float(trajectory.get("direct_len", 0.0)),
                "sinuosity": float(trajectory.get("sinuosity", 1.0)),
                "lat_abs_max": float(trajectory.get("lateral_abs_max", 0.0)),
                "mean_y": float(trajectory.get("mean_y", 0.0)),
                "mean_abs_y": float(trajectory.get("mean_abs_y", 0.0)),
                "final_y_per_path": float(trajectory.get("final_y_per_path", 0.0)),
                "y_pos_ratio": float(trajectory.get("y_pos_ratio", 0.0)),
                "y_neg_ratio": float(trajectory.get("y_neg_ratio", 0.0)),
                "y_zero_ratio": float(trajectory.get("y_zero_ratio", 0.0)),
                "bias_ratio": float(trajectory.get("bias_ratio", 0.0)),
                "bias_severity": str(trajectory.get("bias_severity", "unknown")),
                "trajectory_side_eps": float(trajectory.get("trajectory_side_eps", 0.0)),
                "heading_change": float(trajectory.get("heading_change", 0.0)),
                "heading_variation": float(trajectory.get("heading_variation", 0.0)),
                "stability": str(trajectory.get("stability", "unknown")),
                "reuse_recommendation": str(trajectory.get("reuse_recommendation", "unknown")),
                "dominant_side": str(trajectory.get("dominant_side", "balanced")),
                "dominant_conf": float(trajectory.get("dominant_conf", 0.0)),
                "point_n": int(trajectory.get("point_n", 0)),
                "trajectory_point_n": int(len(trajectory_points)),
                "segment_n": int(segment_report.get("segment_n", 0)),
                "max_line_dev_abs": max_line_dev_abs,
                "segment_kind_counts": segment_kind_counts,
                "pattern": str(phase_summary.get("pattern", "unknown")),
                "pattern_conf": float(phase_summary.get("confidence", 0.0)),
                "phase_n": int(phase_summary.get("phase_count", 0)),
                "window_onset_summary": onset_summary,
                "launch_hesitation_window_count": int(onset_summary.get("launch_hesitation_window_count", 0)),
                "launch_hesitation_t1_s": float(onset_summary.get("launch_hesitation_t1_s", 0.0)),
                "slow_window_count": int(onset_summary.get("slow_window_count", 0)),
                "slowest_window": int(onset_summary.get("slowest_window", 0)),
                "slowest_path_rate": float(onset_summary.get("slowest_path_rate", 0.0)),
                "mid_run_pause_window": int(onset_summary.get("mid_run_pause_window", 0)),
                "mid_run_pause_window_count": int(onset_summary.get("mid_run_pause_window_count", 0)),
                "mid_run_pause_t0_s": float(onset_summary.get("mid_run_pause_t0_s", 0.0)),
                "mid_run_pause_t1_s": float(onset_summary.get("mid_run_pause_t1_s", 0.0)),
                "mid_run_pause_state": str(onset_summary.get("mid_run_pause_state", "unknown")),
                "mid_run_pause_max_idle_streak_s": float(onset_summary.get("mid_run_pause_max_idle_streak_s", 0.0)),
                "mid_run_pause_max_output_idle_streak_s": float(onset_summary.get("mid_run_pause_max_output_idle_streak_s", 0.0)),
                "micro_bias_window": int(onset_summary.get("micro_bias_window", 0)),
                "micro_bias_t0_s": float(onset_summary.get("micro_bias_t0_s", 0.0)),
                "micro_bias_side": str(onset_summary.get("micro_bias_side", "balanced")),
                "micro_bias_cum_lat": float(onset_summary.get("micro_bias_cum_lat", 0.0)),
                "micro_persistent_bias_window": int(onset_summary.get("micro_persistent_bias_window", 0)),
                "micro_persistent_bias_t0_s": float(onset_summary.get("micro_persistent_bias_t0_s", 0.0)),
                "micro_persistent_bias_side": str(onset_summary.get("micro_persistent_bias_side", "balanced")),
                "micro_persistent_bias_cum_lat": float(onset_summary.get("micro_persistent_bias_cum_lat", 0.0)),
                "first_bias_window": int(onset_summary.get("first_bias_window", 0)),
                "first_bias_t0_s": float(onset_summary.get("first_bias_t0_s", 0.0)),
                "first_bias_side": str(onset_summary.get("first_bias_side", "balanced")),
                "first_bias_cum_lat": float(onset_summary.get("first_bias_cum_lat", 0.0)),
                "persistent_bias_window": int(onset_summary.get("persistent_bias_window", 0)),
                "persistent_bias_t0_s": float(onset_summary.get("persistent_bias_t0_s", 0.0)),
                "persistent_bias_side": str(onset_summary.get("persistent_bias_side", "balanced")),
                "persistent_bias_cum_lat": float(onset_summary.get("persistent_bias_cum_lat", 0.0)),
                "max_bias_window": int(onset_summary.get("max_bias_window", 0)),
                "max_bias_t1_s": float(onset_summary.get("max_bias_t1_s", 0.0)),
                "max_bias_side": str(onset_summary.get("max_bias_side", "balanced")),
                "max_bias_cum_lat": float(onset_summary.get("max_bias_cum_lat", 0.0)),
            }
        )
        write_json_file(analysis_json, analysis_payload)
    print(f"DETAIL windows_csv={windows_csv}")
    print(f"DETAIL trajectory_csv={trajectory_csv}")
    print(f"DETAIL phases_json={phases_json}")
    print(f"DETAIL segments_json={segments_json}")
    return windows_csv, trajectory_csv


def summarize_direct_run(lines: List[str], exp_id: Optional[int]) -> Dict[str, float]:
    samples: List[Dict[str, float]] = []
    for ln in lines:
        kv = parse_kv_line(ln)
        if not kv:
            continue
        if exp_id is not None and int(kv.get("exp_id", -1)) != int(exp_id):
            continue
        if int(kv.get("run", 0)) != 1:
            continue
        samples.append(kv)

    if not samples:
        return {}

    yaw0 = samples[0].get("y", 0.0)
    yaw1 = samples[-1].get("y", yaw0)
    t0 = samples[0].get("t_ms", 0.0)
    t1 = samples[-1].get("t_ms", t0)
    dt_s = max(1e-3, (t1 - t0) / 1000.0)
    yaw_delta = yaw1 - yaw0
    ed_vals = [s.get("ed", 0.0) for s in samples]
    el_vals = [s.get("el", 0.0) for s in samples]
    er_vals = [s.get("er", 0.0) for s in samples]
    l_vals = [s.get("L", 0.0) for s in samples]
    r_vals = [s.get("R", 0.0) for s in samples]
    ol_vals = [s.get("OL", s.get("L", 0.0)) for s in samples]
    or_vals = [s.get("OR", s.get("R", 0.0)) for s in samples]
    yr_vals = [s.get("yr", 0.0) for s in samples]

    def mean(xs: List[float]) -> float:
        return sum(xs) / len(xs) if xs else 0.0

    def mean_abs(xs: List[float]) -> float:
        return sum(abs(x) for x in xs) / len(xs) if xs else 0.0

    def max_abs(xs: List[float]) -> float:
        return max((abs(x) for x in xs), default=0.0)

    def ratio_nonzero(xs: List[float], eps: float = 0.5) -> float:
        if not xs:
            return 0.0
        return sum(1 for x in xs if abs(x) > eps) / float(len(xs))

    out_sum_vals = [abs(a) + abs(b) for a, b in zip(ol_vals, or_vals)]
    enc_sum_vals = [abs(a) + abs(b) for a, b in zip(el_vals, er_vals)]
    out_nonzero_ratio = ratio_nonzero(out_sum_vals, eps=0.5)
    enc_nonzero_ratio = ratio_nonzero(enc_sum_vals, eps=0.5)
    enc_active_ratio = ratio_nonzero(enc_sum_vals, eps=2.0)
    out_strong_ratio = ratio_nonzero(out_sum_vals, eps=8.0)
    yaw_active_ratio = ratio_nonzero(yr_vals, eps=0.05)
    movement_score = 0.55 * enc_nonzero_ratio + 0.30 * out_nonzero_ratio + 0.15 * yaw_active_ratio

    dist_proxy = sum(0.5 * (abs(a) + abs(b)) for a, b in zip(el_vals, er_vals)) * 0.01
    speed_proxy = dist_proxy / dt_s if dt_s > 1e-9 else 0.0
    output_only_ratio = 0.0
    if out_sum_vals:
        output_only_ratio = sum(1 for o, e in zip(out_sum_vals, enc_sum_vals) if o >= 8.0 and e <= 1.0) / float(len(out_sum_vals))

    has_output = out_nonzero_ratio >= 0.20 or mean_abs(ol_vals) + mean_abs(or_vals) >= 2.0
    has_encoder = enc_nonzero_ratio >= 0.12 or mean_abs(el_vals) + mean_abs(er_vals) >= 1.0
    has_yaw_activity = abs(yaw_delta) >= 0.30 or mean_abs(yr_vals) >= 0.08

    motion_state = "unknown"
    if has_output and has_encoder:
        if dist_proxy < 8.0 or speed_proxy < 1.5:
            motion_state = "barely_moving"
        elif dist_proxy < 20.0 or speed_proxy < 3.0 or enc_active_ratio < 0.35:
            motion_state = "slow_crawl"
        else:
            motion_state = "moving"
    elif has_output and (not has_encoder):
        motion_state = "output_present_but_encoder_weak"
    elif (not has_output) and has_encoder:
        motion_state = "encoder_active_but_output_weak"
    else:
        motion_state = "barely_started"

    stiction_suspected = has_output and (dist_proxy < 12.0 or speed_proxy < 2.0) and output_only_ratio >= 0.20

    left_stronger_ratio = 0.0
    right_stronger_ratio = 0.0
    if ol_vals and or_vals:
        left_stronger_ratio = sum(1 for a, b in zip(ol_vals, or_vals) if a > b + 0.5) / float(len(ol_vals))
        right_stronger_ratio = sum(1 for a, b in zip(ol_vals, or_vals) if b > a + 0.5) / float(len(ol_vals))

    enc_left_stronger_ratio = 0.0
    enc_right_stronger_ratio = 0.0
    if el_vals and er_vals:
        enc_left_stronger_ratio = sum(1 for a, b in zip(el_vals, er_vals) if a > b + 0.5) / float(len(el_vals))
        enc_right_stronger_ratio = sum(1 for a, b in zip(el_vals, er_vals) if b > a + 0.5) / float(len(er_vals))

    return {
        "n": float(len(samples)),
        "yaw_start": yaw0,
        "yaw_end": yaw1,
        "yaw_delta": yaw_delta,
        "yaw_rate": yaw_delta / dt_s,
        "ed_mean": mean(ed_vals),
        "el_mean": mean(el_vals),
        "er_mean": mean(er_vals),
        "L_mean": mean(l_vals),
        "R_mean": mean(r_vals),
        "OL_mean": mean(ol_vals),
        "OR_mean": mean(or_vals),
        "OL_abs_mean": mean_abs(ol_vals),
        "OR_abs_mean": mean_abs(or_vals),
        "el_abs_mean": mean_abs(el_vals),
        "er_abs_mean": mean_abs(er_vals),
        "out_nonzero_ratio": out_nonzero_ratio,
        "enc_nonzero_ratio": enc_nonzero_ratio,
        "yaw_active_ratio": yaw_active_ratio,
        "movement_score": movement_score,
        "motion_state": 1.0 if motion_state == "moving" else (0.75 if motion_state == "slow_crawl" else (0.5 if motion_state == "output_present_but_encoder_weak" else (0.35 if motion_state == "barely_moving" else (0.25 if motion_state == "encoder_active_but_output_weak" else 0.0)))),
        "dist_proxy": dist_proxy,
        "speed_proxy": speed_proxy,
        "enc_active_ratio": enc_active_ratio,
        "out_strong_ratio": out_strong_ratio,
        "output_only_ratio": output_only_ratio,
        "stiction_suspected": 1.0 if stiction_suspected else 0.0,
        "max_out_sum": max_abs(out_sum_vals),
        "max_enc_sum": max_abs(enc_sum_vals),
        "left_out_stronger_ratio": left_stronger_ratio,
        "right_out_stronger_ratio": right_stronger_ratio,
        "left_enc_stronger_ratio": enc_left_stronger_ratio,
        "right_enc_stronger_ratio": enc_right_stronger_ratio,
        "has_output": 1.0 if has_output else 0.0,
        "has_encoder": 1.0 if has_encoder else 0.0,
        "has_yaw_activity": 1.0 if has_yaw_activity else 0.0,
    }


def print_direct_summary(summary: Dict[str, float]) -> None:
    if not summary:
        print("SUMMARY: no run=1 HB samples captured")
        return

    yaw_delta = summary["yaw_delta"]
    yaw_rate = summary["yaw_rate"]
    ed_mean = summary["ed_mean"]
    el_mean = summary["el_mean"]
    er_mean = summary["er_mean"]
    out_nonzero_ratio = summary.get("out_nonzero_ratio", 0.0)
    enc_active_ratio = summary.get("enc_active_ratio", 0.0)
    enc_nonzero_ratio = summary.get("enc_nonzero_ratio", 0.0)
    yaw_active_ratio = summary.get("yaw_active_ratio", 0.0)
    movement_score = summary.get("movement_score", 0.0)
    dist_proxy = summary.get("dist_proxy", 0.0)
    speed_proxy = summary.get("speed_proxy", 0.0)
    output_only_ratio = summary.get("output_only_ratio", 0.0)
    stiction_suspected = summary.get("stiction_suspected", 0.0) > 0.5
    has_output = summary.get("has_output", 0.0) > 0.5
    has_encoder = summary.get("has_encoder", 0.0) > 0.5
    has_yaw_activity = summary.get("has_yaw_activity", 0.0) > 0.5

    if yaw_delta > 3.0:
        turn_hint = "left"
    elif yaw_delta < -3.0:
        turn_hint = "right"
    else:
        turn_hint = "straight-ish"

    wheel_hint = "balanced"
    if ed_mean > 3.0:
        wheel_hint = "left_encoder_faster_(el-er_>0)"
    elif ed_mean < -3.0:
        wheel_hint = "right_encoder_faster_(el-er_<0)"

    motion_hint = "barely-started"
    if has_output and has_encoder:
        if dist_proxy < 8.0 or speed_proxy < 1.5:
            motion_hint = "barely_moving"
        elif dist_proxy < 20.0 or speed_proxy < 3.0 or enc_active_ratio < 0.35:
            motion_hint = "slow_crawl"
        else:
            motion_hint = "moving"
    elif has_output and (not has_encoder):
        motion_hint = "output_present_but_encoder_weak"
    elif (not has_output) and has_encoder:
        motion_hint = "encoder_active_but_output_weak"
    else:
        motion_hint = "barely_started"

    if stiction_suspected:
        motion_hint += "+stiction_suspected"

    asym_hint = "balanced"
    if summary.get("right_out_stronger_ratio", 0.0) >= 0.60:
        asym_hint = "right_output_stronger"
    elif summary.get("left_out_stronger_ratio", 0.0) >= 0.60:
        asym_hint = "left_output_stronger"

    enc_asym_hint = "balanced"
    if summary.get("right_enc_stronger_ratio", 0.0) >= 0.60:
        enc_asym_hint = "right_encoder_stronger"
    elif summary.get("left_enc_stronger_ratio", 0.0) >= 0.60:
        enc_asym_hint = "left_encoder_stronger"

    print(f"SUMMARY run_samples={int(summary['n'])}")
    print(f"SUMMARY yaw_start={summary['yaw_start']:.3f} yaw_end={summary['yaw_end']:.3f} yaw_delta={yaw_delta:.3f} yaw_rate={yaw_rate:.3f}")
    print(f"SUMMARY ed_mean={ed_mean:.3f} el_mean={el_mean:.3f} er_mean={er_mean:.3f}")
    print(
        f"SUMMARY out_mean L/R={summary.get('OL_mean', 0.0):.3f}/{summary.get('OR_mean', 0.0):.3f} enc_abs_mean L/R={summary.get('el_abs_mean', 0.0):.3f}/{summary.get('er_abs_mean', 0.0):.3f}"
    )
    print(
        f"SUMMARY active_ratio out={out_nonzero_ratio:.3f} enc={enc_nonzero_ratio:.3f} yaw={yaw_active_ratio:.3f} movement_score={movement_score:.3f}"
    )
    print(
        f"SUMMARY motion_proxy dist={dist_proxy:.3f} speed={speed_proxy:.3f} enc_active={enc_active_ratio:.3f} output_only_ratio={output_only_ratio:.3f}"
    )
    print(
        f"SUMMARY motion_hint={motion_hint} asym_hint={asym_hint} enc_asym_hint={enc_asym_hint}"
    )
    print(f"SUMMARY turn_hint={turn_hint}")
    print(f"SUMMARY wheel_hint={wheel_hint}")


def build_param_commands(params: ExpParams) -> List[Tuple[str, str]]:
    cmds: List[Tuple[str, str]] = [
        (f"#TS={params.ts}!", r"^OK\s+TS\b"),
        (f"#SKP={params.skp}!", r"^OK\s+SKP\b"),
        (f"#SKI={params.ski}!", r"^OK\s+SKI\b"),
        (f"#SKD={params.skd}!", r"^OK\s+SKD\b"),
        (f"#AKP={params.akp}!", r"^OK\s+AKP\b"),
        (f"#AKI={params.aki}!", r"^OK\s+AKI\b"),
        (f"#AKD={params.akd}!", r"^OK\s+AKD\b"),
    ]
    return cmds


def send_param_commands(
    ser: serial.Serial,
    raw_fp,
    print_realtime: bool,
    params: ExpParams,
    pwm_max: Optional[int],
    diff_max: Optional[int],
) -> None:
    cmds = build_param_commands(params)

    for cmd, ack_re in cmds:
        send_best_effort(
            ser,
            cmd,
            ack_re,
            raw_fp=raw_fp,
            print_realtime=print_realtime,
            timeout_s=0.6,
            retries=1,
        )
        time.sleep(0.02)


def send_cmd_and_capture(
    ser: serial.Serial,
    cmd: str,
    watch_s: float,
    raw_fp,
    print_realtime: bool,
) -> List[str]:
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    pre = drain_for_lines(ser, 0.25, raw_fp=raw_fp, print_realtime=False)
    write_log_line(raw_fp, f"CMD {cmd} pre_rx={latest_rx_from_lines(pre)}")
    send_cmd(ser, cmd)
    lines = drain_for_lines(ser, watch_s, raw_fp=raw_fp, print_realtime=print_realtime)
    write_log_line(raw_fp, f"RES {cmd} post_rx={latest_rx_from_lines(lines)} n={len(lines)}")
    return lines


def run_direct_serial_experiment(
    port: str,
    baud: int,
    exp_id: int,
    exp_ms: int,
    params: ExpParams,
    pwm_max: Optional[int],
    diff_max: Optional[int],
    out_dir: str,
    print_realtime: bool,
    enc_l_sign: Optional[int] = None,
    enc_r_sign: Optional[int] = None,
    bin_mode: Optional[int] = None,
    pause_after: bool = False,
    quick: bool = False,
    fast_start: bool = False,
    no_verify: bool = False,
    no_cal: bool = False,
    no_dump: bool = False,
    raw_pwm: Optional[int] = None,
    exp_label: Optional[str] = None,
) -> Tuple[str, Optional[str]]:
    ts = time.strftime("%Y%m%d_%H%M%S")
    base = exp_label if exp_label else f"exp{exp_id:02d}_{exp_ms}ms_{ts}"
    output_paths = make_output_paths(out_dir, base)
    raw_path = output_paths["raw"]
    csv_path = output_paths["dump"]
    telemetry_rows: List[Dict[str, Any]] = []

    os.makedirs(out_dir, exist_ok=True)

    set_active_telemetry_rows(telemetry_rows)
    try:
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
            startup_settle_s = 0.08 if (quick and no_dump) else (0.2 if quick else 0.8)
            time.sleep(startup_settle_s)

            with open(raw_path, "wb") as raw_fp:
                drain_for(ser, 0.3, raw_fp=raw_fp, print_realtime=False)
                if not (quick and no_dump):
                    try:
                        sanity_check_rx(ser, raw_fp=raw_fp, print_realtime=print_realtime)
                    except Exception:
                        pass

                if fast_start:
                    be_timeout = 0.25
                    be_retries = 0
                else:
                    be_timeout = 0.25 if quick else 1.0
                    be_retries = 0 if quick else 2

                if enc_l_sign is not None:
                    send_best_effort(
                        ser,
                        f"#ENC_L_SIGN={enc_l_sign}!",
                        r"^OK\s+ENC_L_SIGN\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=be_timeout,
                        retries=be_retries,
                    )
                if enc_r_sign is not None:
                    send_best_effort(
                        ser,
                        f"#ENC_R_SIGN={enc_r_sign}!",
                        r"^OK\s+ENC_R_SIGN\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=be_timeout,
                        retries=be_retries,
                    )

                if pwm_max is not None:
                    send_best_effort(
                        ser,
                        f"#PWM_MAX={pwm_max}!",
                        r"^OK\s+PWM_MAX\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=be_timeout,
                        retries=be_retries,
                    )
                if diff_max is not None:
                    send_best_effort(
                        ser,
                        f"#DIFF_MAX={diff_max}!",
                        r"^OK\s+DIFF_MAX\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=be_timeout,
                        retries=be_retries,
                    )

                if bin_mode is not None:
                    send_best_effort(
                        ser,
                        f"#BIN={bin_mode}!",
                        r"^OK\s+BIN\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=be_timeout,
                        retries=be_retries,
                    )

                if not wait_for_pmax_dmax(
                    ser,
                    expect_pmax=pwm_max,
                    expect_dmax=diff_max,
                    timeout_s=(0.2 if (quick and no_dump) else (0.5 if quick else 1.2)),
                    raw_fp=raw_fp,
                ):
                    if pwm_max is not None:
                        send_best_effort(
                            ser,
                            f"#PWM_MAX={pwm_max}!",
                            r"^OK\s+PWM_MAX\b",
                            raw_fp,
                            print_realtime,
                            timeout_s=(0.3 if (quick and no_dump) else (0.6 if quick else 1.2)),
                            retries=(1 if (quick and no_dump) else (2 if quick else 3)),
                        )
                    if diff_max is not None:
                        send_best_effort(
                            ser,
                            f"#DIFF_MAX={diff_max}!",
                            r"^OK\s+DIFF_MAX\b",
                            raw_fp,
                            print_realtime,
                            timeout_s=(0.3 if (quick and no_dump) else (0.6 if quick else 1.2)),
                            retries=(1 if (quick and no_dump) else (2 if quick else 3)),
                        )
                    _ = wait_for_pmax_dmax(
                        ser,
                        expect_pmax=pwm_max,
                        expect_dmax=diff_max,
                        timeout_s=(0.4 if (quick and no_dump) else (0.8 if quick else 1.5)),
                        raw_fp=raw_fp,
                    )

                send_best_effort(ser, "#STOP!", r"^(OK\s+STOP\b|ERR\b)", raw_fp, print_realtime)

                if no_dump and raw_pwm is not None:
                    base_rx = read_latest_rx(ser, timeout_s=0.25, raw_fp=raw_fp)
                    raw_ok = False
                    for _ in range(3):
                        base_rx2 = read_latest_rx(ser, timeout_s=0.25, raw_fp=raw_fp)
                        if base_rx2 is not None:
                            base_rx = base_rx2
                        send_cmd(ser, f"#RAW={raw_pwm}!")
                        if wait_for_rx_increase(ser, base_rx=base_rx, timeout_s=0.35, raw_fp=raw_fp):
                            raw_ok = True
                            break
                        if wait_for_hb_lr_nonzero(ser, timeout_s=0.35, raw_fp=raw_fp, print_realtime=print_realtime):
                            raw_ok = True
                            break
                        time.sleep(0.03)

                    _ = raw_ok
                    drain_for(ser, exp_ms / 1000.0, raw_fp=raw_fp, print_realtime=print_realtime)
                    send_cmd(ser, "#RAW=0!")
                    ser = safe_write_cmd(ser, port=port, baud=baud, cmd="#STOP!")
                    drain_for(ser, 0.3, raw_fp=raw_fp, print_realtime=print_realtime)
                    return raw_path, None

                if not no_cal:
                    if params.cal_wait_s > 0.0 and not quick:
                        time.sleep(params.cal_wait_s)
                    send_best_effort(
                        ser,
                        "#CAL!",
                        r"^OK\s+CAL\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=(0.6 if quick else 1.2),
                        retries=(0 if quick else 2),
                    )

                send_param_commands(
                    ser,
                    raw_fp=raw_fp,
                    print_realtime=print_realtime,
                    params=params,
                    pwm_max=None,
                    diff_max=None,
                )

                if not no_verify:
                    try:
                        ok_ts = False
                        ok_skp = False
                        ok_ski = False
                        ok_skd = False
                        ok_akp = False
                        ok_aki = False
                        ok_akd = False
                        stat_timeout = 0.35 if quick else 0.6
                        for _ in range(3):
                            stat_line = read_stat_line(
                                ser,
                                raw_fp=raw_fp,
                                print_realtime=print_realtime,
                                timeout_s=stat_timeout,
                            )
                            if stat_line:
                                m_ts = re.search(r"\bts=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_ts is not None:
                                    ok_ts = abs(float(m_ts.group(1)) - float(params.ts)) <= 0.05
                                m_skp = re.search(r"\bskp=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_skp is not None:
                                    ok_skp = abs(float(m_skp.group(1)) - float(params.skp)) <= 0.02
                                m_ski = re.search(r"\bski=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_ski is not None:
                                    ok_ski = abs(float(m_ski.group(1)) - float(params.ski)) <= 0.005
                                m_skd = re.search(r"\bskd=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_skd is not None:
                                    ok_skd = abs(float(m_skd.group(1)) - float(params.skd)) <= 0.005
                                m_akp = re.search(r"\bakp=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_akp is not None:
                                    ok_akp = abs(float(m_akp.group(1)) - float(params.akp)) <= 0.02
                                m_aki = re.search(r"\baki=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_aki is not None:
                                    ok_aki = abs(float(m_aki.group(1)) - float(params.aki)) <= 0.005
                                m_akd = re.search(r"\bakd=(-?\d+(?:\.\d+)?)", stat_line)
                                if m_akd is not None:
                                    ok_akd = abs(float(m_akd.group(1)) - float(params.akd)) <= 0.005
                            if ok_ts and ok_skp and ok_ski and ok_skd and ok_akp and ok_aki and ok_akd:
                                break
                            if not ok_ts:
                                send_best_effort(ser, f"#TS={params.ts}!", r"^OK\s+TS\b", raw_fp, print_realtime, timeout_s=0.55, retries=2)
                                time.sleep(0.03)
                            if not ok_skp:
                                send_best_effort(ser, f"#SKP={params.skp}!", r"^OK\s+SKP\b", raw_fp, print_realtime, timeout_s=0.45, retries=2)
                                time.sleep(0.03)
                            if not ok_ski:
                                send_best_effort(ser, f"#SKI={params.ski}!", r"^OK\s+SKI\b", raw_fp, print_realtime, timeout_s=0.45, retries=2)
                            if not ok_skd:
                                send_best_effort(ser, f"#SKD={params.skd}!", r"^OK\s+SKD\b", raw_fp, print_realtime, timeout_s=0.45, retries=2)
                            if not ok_akp:
                                send_best_effort(ser, f"#AKP={params.akp}!", r"^OK\s+AKP\b", raw_fp, print_realtime, timeout_s=0.45, retries=2)
                            if not ok_aki:
                                send_best_effort(ser, f"#AKI={params.aki}!", r"^OK\s+AKI\b", raw_fp, print_realtime, timeout_s=0.45, retries=2)
                            if not ok_akd:
                                send_best_effort(ser, f"#AKD={params.akd}!", r"^OK\s+AKD\b", raw_fp, print_realtime, timeout_s=0.45, retries=2)
                    except Exception:
                        pass

                run_ok = False
                run_tries = 1 if (no_dump or fast_start) else 2
                hb_wait_s = 2.2 if (no_dump or fast_start) else 1.2
                expected_exp_id = None if no_dump else exp_id
                run_cmd = "#RUN!" if no_dump else f"#EXP=RUN,{exp_id},{exp_ms}!"
                run_ack_timeout_s = 2.6 if no_dump else (1.2 if quick else 3.2)

                for _ in range(run_tries):
                    base_rx = read_latest_rx(ser, timeout_s=0.35, raw_fp=raw_fp)
                    _ = base_rx
                    ser, run_ack_ok, run_rx_ok = send_run_and_wait_start(
                        ser,
                        port,
                        baud,
                        run_cmd,
                        expect_run_ack=(not no_dump),
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                        timeout_s=run_ack_timeout_s,
                        retries=0,
                    )
                    time.sleep(0.05)
                    if wait_for_hb_run(
                        ser,
                        exp_id=expected_exp_id,
                        timeout_s=hb_wait_s,
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                    ):
                        run_ok = True
                        break
                    if (run_ack_ok or run_rx_ok) and not no_dump:
                        run_ok = True
                        break
                    if no_dump and wait_for_hb_lr_nonzero(
                        ser,
                        timeout_s=0.6,
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                    ):
                        run_ok = True
                        break
                    time.sleep(0.05)

                if (not run_ok) and (not no_dump):
                    send_best_effort(
                        ser,
                        f"#EXP=START,{exp_id},{exp_ms}!",
                        r"^OK\s+EXP_START\b",
                        raw_fp,
                        print_realtime,
                        timeout_s=(0.8 if quick else 1.8),
                        retries=(1 if quick else 2),
                    )
                    ser, run_ack_ok, run_rx_ok = send_run_and_wait_start(
                        ser,
                        port,
                        baud,
                        "#RUN!",
                        expect_run_ack=False,
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                        timeout_s=(1.2 if quick else 2.0),
                        retries=1,
                    )
                    if wait_for_hb_run(
                        ser,
                        exp_id=exp_id,
                        timeout_s=max(1.2, hb_wait_s),
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                    ):
                        run_ok = True
                    elif run_ack_ok or run_rx_ok:
                        run_ok = True

                if run_ok:
                    try:
                        if not no_dump:
                            send_best_effort(
                                ser,
                                "#EXP=STREAM,1!",
                                r"^OK\s+EXP_STREAM\b",
                                raw_fp,
                                print_realtime,
                                timeout_s=(0.4 if quick else 1.2),
                                retries=(0 if quick else 2),
                            )
                        _ = read_stat_line(
                            ser,
                            raw_fp=raw_fp,
                            print_realtime=print_realtime,
                            timeout_s=0.6,
                        )
                    except Exception:
                        pass

                _ = run_ok
                drain_for(ser, exp_ms / 1000.0, raw_fp=raw_fp, print_realtime=print_realtime)
                if no_dump:
                    ser = safe_write_cmd(ser, port=port, baud=baud, cmd="#STOP!")
                    drain_for(ser, 0.4, raw_fp=raw_fp, print_realtime=print_realtime)
                else:
                    send_best_effort(
                        ser,
                        f"#EXP=STOP,{exp_id}!",
                        r"^(OK\s+EXP_STOP\b|ERR\b)",
                        raw_fp,
                        print_realtime,
                        timeout_s=(0.4 if quick else 1.0),
                        retries=(0 if quick else 1),
                    )
                    finished = wait_for_exp_finish(
                        ser,
                        exp_id=exp_id,
                        timeout_s=max(1.0, min(4.0, exp_ms / 1000.0 + 1.0)),
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                    )
                    if not finished:
                        drain_for(ser, 0.4, raw_fp=raw_fp, print_realtime=print_realtime)

                if not no_dump:
                    time.sleep(0.10 if quick else 0.20)

                    try:
                        send_and_wait_ack(
                            ser,
                            "#EXP=STREAM,0!",
                            r"^OK\s+EXP_STREAM\b",
                            raw_fp=raw_fp,
                            print_realtime=print_realtime,
                            timeout_s=(0.4 if quick else 1.2),
                            retries=(0 if quick else 2),
                        )
                    except Exception as e:
                        if print_realtime:
                            print(f"WARN: no ACK for #EXP=STREAM,0! before dump (ignored): {e}")

                    dump_timeout_s = 1.2 if quick else 3.0
                    ok = False
                    dump_lines: List[str] = []
                    for attempt in range(1):
                        if attempt > 0:
                            time.sleep(0.2 * attempt if quick else 0.5 * attempt)
                        send_cmd(ser, f"#EXP=DUMP,{exp_id}!")
                        ok, dump_lines = wait_for_dump(
                            ser,
                            exp_id=exp_id,
                            timeout_s=dump_timeout_s,
                            raw_fp=raw_fp,
                            print_realtime=print_realtime,
                        )
                        if ok:
                            break

                    if not ok:
                        if print_realtime:
                            print("WARN: EXP dump failed quickly, keep RAW only")
                        return raw_path, None

                    fields, rows = parse_dump(dump_lines)
                    write_csv(csv_path, fields, rows)

                    if print_realtime:
                        print(f"DUMP rows: {len(rows)}")

                    if pause_after:
                        try:
                            input(
                                "[PAUSE] 实验已结束并已生成CSV。请观察小车现象并描述给我，然后按回车继续... "
                            )
                        except KeyboardInterrupt:
                            ser = stop_vehicle_best_effort(
                                ser,
                                port=port,
                                baud=baud,
                                exp_id=exp_id,
                                raw_fp=raw_fp,
                                print_realtime=print_realtime,
                            )
                            return raw_path, csv_path

                try:
                    ser = stop_vehicle_best_effort(
                        ser,
                        port=port,
                        baud=baud,
                        exp_id=exp_id,
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                    )
                except Exception:
                    pass

                return raw_path, csv_path
    finally:
        set_active_telemetry_rows(None)
        try:
            emit_time_series_artifacts(
                raw_path=raw_path,
                exp_label=base,
                exp_id=exp_id,
                exp_ms=exp_ms,
                params=params,
                rows=telemetry_rows,
            )
        except Exception as exc:
            print(f"WARN: failed to write time series artifacts: {exc}")

    return raw_path, csv_path


def run_one_exp(
    port: str,
    baud: int,
    exp_id: int,
    exp_ms: int,
    params: ExpParams,
    enc_l_sign: Optional[int],
    enc_r_sign: Optional[int],
    pwm_max: Optional[int],
    diff_max: Optional[int],
    bin_mode: Optional[int],
    out_dir: str,
    print_realtime: bool,
    pause_after: bool,
    quick: bool,
    fast_start: bool,
    no_verify: bool,
    no_cal: bool,
    no_dump: bool,
    raw_pwm: Optional[int] = None,
) -> Tuple[str, Optional[str]]:
    return run_direct_serial_experiment(
        port=port,
        baud=baud,
        exp_id=exp_id,
        exp_ms=exp_ms,
        params=params,
        pwm_max=pwm_max,
        diff_max=diff_max,
        out_dir=out_dir,
        print_realtime=print_realtime,
        enc_l_sign=enc_l_sign,
        enc_r_sign=enc_r_sign,
        bin_mode=bin_mode,
        pause_after=pause_after,
        quick=quick,
        fast_start=fast_start,
        no_verify=no_verify,
        no_cal=no_cal,
        no_dump=no_dump,
        raw_pwm=raw_pwm,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM18", help="COM port, e.g. COM18. Empty=auto")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--id", type=int, default=0, help="MCU exp_id. <=0 means auto sync with experiment counter")
    ap.add_argument("--ms", type=int, default=10000)
    ap.add_argument("--out", default=DEFAULT_OUTPUT_DIR)
    ap.add_argument("--realtime", action="store_true")
    ap.add_argument("--ts", type=float, default=20.0)
    ap.add_argument("--skp", type=float, default=0.1)
    ap.add_argument("--ski", type=float, default=0.012)
    ap.add_argument("--skd", type=float, default=0.0)
    ap.add_argument("--akp", "--kp", dest="akp", type=float, default=0.24)
    ap.add_argument("--aki", "--ki", dest="aki", type=float, default=0.0024)
    ap.add_argument("--akd", "--kd", dest="akd", type=float, default=0.05)

    args = ap.parse_args()

    port = args.port.strip() or find_serial_port(prefer_keywords=["CH340", "USB-SERIAL", "USB Serial", "CP210"])
    if not port:
        raise RuntimeError("No serial ports found")

    exp_seq, exp_label = allocate_experiment_label(args.out)
    exp_id = args.id if args.id > 0 else exp_seq
    print(f"EXPERIMENT={exp_label}")
    print(f"MCU_EXP_ID={exp_id}")

    params = ExpParams(
        ts=args.ts,
        skp=args.skp,
        ski=args.ski,
        skd=args.skd,
        akp=args.akp,
        aki=args.aki,
        akd=args.akd,
    )

    raw_path, csv_path = run_direct_serial_experiment(
        port=port,
        baud=args.baud,
        exp_id=exp_id,
        exp_ms=args.ms,
        params=params,
        out_dir=args.out,
        print_realtime=args.realtime,
        pwm_max=None,
        diff_max=None,
        enc_l_sign=None,
        enc_r_sign=None,
        bin_mode=None,
        pause_after=False,
        quick=False,
        fast_start=False,
        no_verify=False,
        no_cal=False,
        no_dump=False,
        raw_pwm=None,
        exp_label=exp_label,
    )
    prefix = build_artifact_prefix(raw_path)
    data_csv = prefix + TIME_SERIES_SUFFIX
    analysis_json = prefix + ANALYSIS_SUFFIX
    windows_csv, trajectory_csv = emit_detailed_analysis(raw_path)
    print(f"EXPERIMENT={exp_label}")
    print(f"MCU_EXP_ID={exp_id}")
    print(f"RAW={raw_path}")
    print(f"CSV={csv_path}")
    print(f"DATA={data_csv}")
    print(f"ANALYSIS={analysis_json}")
    print(f"WINDOWS={windows_csv}")
    print(f"TRAJECTORY={trajectory_csv}")


if __name__ == "__main__":
    main()
