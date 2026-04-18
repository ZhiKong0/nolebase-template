import os
import re
import time
import csv
import argparse
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import serial
import serial.tools.list_ports


@dataclass
class ExpParams:
    ts: float = 100.0
    so: int = 100
    min_pwm: int = 0
    kick_pwm: int = 0
    kick_ms: int = 0
    ramp: int = 2
    hp: float = 2.0
    hd: float = 0.0050
    hs: float = 0.2
    db: float = 2.0
    hi: float = 0.0
    hil: float = 2.0
    kpp: float = 2.0
    kpi: float = 0.2
    kpd: float = 0.0
    trim: float = 0.0
    cal_wait_s: float = 1.5


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


def send_cmd(ser: serial.Serial, cmd: str) -> None:
    if not cmd.endswith("!"):
        raise ValueError(f"cmd must end with '!': {cmd}")
    ser.write(cmd.encode("ascii", errors="ignore"))


def drain_for(ser: serial.Serial, seconds: float, raw_fp, print_realtime: bool) -> None:
    start = now()
    text_buf = ""
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
        if print_realtime:
            for ln in lines:
                if ln.startswith(("OK", "ERR", "HB ", "STAT ", "EXP_")):
                    print(ln)
        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]


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
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
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

        send_cmd(ser, "#STAT!")

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

    raise RuntimeError(
        "RX sanity check failed: no response to #STAT! and no rx counter increase observed. "
        "You can still see HB (MCU->PC TX OK), but PC->MCU RX may be disconnected. "
        "Please check USB-TTL wiring: MCU PA2(TX)->TTL RX, MCU PA3(RX)<-TTL TX, and common GND. "
        f"Last lines: {last_lines[-5:]}"
    )


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
        for ln in lines:
            out_lines.append(ln)
            if print_realtime:
                if ln.startswith(("OK", "ERR", "HB ", "STAT ", "EXP_")):
                    print(ln)
            for i, pat in enumerate(patterns):
                if not matched[i] and pat.search(ln):
                    matched[i] = True

        if all(matched):
            return True, out_lines

        if len(text_buf) > 8192:
            text_buf = text_buf[-8192:]

    return False, out_lines


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
        for ln in lines:
            out_lines.append(ln)
            if print_realtime:
                if ln.startswith(("OK", "ERR", "HB ", "STAT ", "EXP_")):
                    print(ln)
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


def run_one_exp(
    port: str,
    baud: int,
    exp_id: int,
    exp_ms: int,
    params: ExpParams,
    out_dir: str,
    print_realtime: bool,
    pause_after: bool,
) -> Tuple[str, str]:
    ts = time.strftime("%Y%m%d_%H%M%S")
    base = f"exp{exp_id:02d}_{exp_ms}ms_{ts}"
    raw_path = os.path.join(out_dir, base + "_raw.txt")
    csv_path = os.path.join(out_dir, base + "_dump.csv")

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
        # 某些USB-TTL打开串口会触发MCU复位/串口重初始化，先等一下并把启动期HB排干净
        time.sleep(0.8)

        with open(raw_path, "wb") as raw_fp:
            drain_for(ser, 0.6, raw_fp=raw_fp, print_realtime=print_realtime)
            sanity_check_rx(ser, raw_fp=raw_fp, print_realtime=print_realtime)

            seq = [
                (f"#TS={params.ts}!", r"^OK\s+TS\b"),
                (f"#SO={params.so}!", r"^OK\s+SO\b"),
                (f"#RAMP={params.ramp}!", r"^OK\s+RAMP\b"),
                (f"#HP={params.hp}!", r"^OK\s+HP\b"),
                (f"#HD={params.hd}!", r"^OK\s+HD\b"),
                (f"#HS={params.hs}!", r"^OK\s+HS\b"),
                (f"#DB={params.db}!", r"^OK\s+DB\b"),
                (f"#HI={params.hi}!", r"^OK\s+HI\b"),
                (f"#HIL={params.hil}!", r"^OK\s+HIL\b"),
                (f"#KPP={params.kpp}!", r"^OK\s+KPP\b"),
                (f"#KPI={params.kpi}!", r"^OK\s+KPI\b"),
                (f"#KPD={params.kpd}!", r"^OK\s+KPD\b"),
                (f"#EXP=START,{exp_id},{exp_ms}!", r"^OK\s+EXP_START\b"),
                ("#RUN!", r"^OK\s+RUN\b"),
            ]

            # 开启实时流：部分固件版本可能不回OK（但仍可能生效）。这里尽量开启，失败不致命。
            try:
                send_and_wait_ack(
                    ser,
                    "#EXP=STREAM,1!",
                    r"^OK\s+EXP_STREAM\b",
                    raw_fp=raw_fp,
                    print_realtime=print_realtime,
                    timeout_s=1.2,
                    retries=2,
                )
            except Exception as e:
                if print_realtime:
                    print(f"WARN: no ACK for #EXP=STREAM,1! (ignored): {e}")

            for cmd, ack in seq:
                send_and_wait_ack(
                    ser,
                    cmd,
                    ack,
                    raw_fp=raw_fp,
                    print_realtime=print_realtime,
                    timeout_s=2.5,
                    retries=3,
                )

                time.sleep(0.03)

                if cmd == "#STOP!" and params.cal_wait_s > 0.0:
                    time.sleep(params.cal_wait_s)
                    send_and_wait_ack(
                        ser,
                        "#CAL!",
                        r"^OK\s+CAL\b",
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                        timeout_s=2.5,
                        retries=3,
                    )
                    time.sleep(0.2)

                if cmd.startswith("#EXP=START"):
                    send_and_wait_ack(
                        ser,
                        "#CAL!",
                        r"^OK\s+CAL\b",
                        raw_fp=raw_fp,
                        print_realtime=print_realtime,
                        timeout_s=2.5,
                        retries=3,
                    )

            # Wait for experiment end (timeout or stop)
            end_pats = [
                re.compile(rf"^EXP_(END|TIMEOUT)\s+id={exp_id}\b"),
            ]
            ok, end_lines = wait_for_regex_lines(
                ser,
                end_pats,
                timeout_s=(exp_ms / 1000.0) + 2.5,
                raw_fp=raw_fp,
                print_realtime=print_realtime,
            )
            if not ok:
                send_cmd(ser, "#STOP!")
                # 兜底：有些情况下串口会丢行/乱码导致漏读EXP_END/EXP_TIMEOUT，
                # 但实验实际已结束并且固件可能已经准备好dump。这里继续尝试DUMP。

            # 给固件一个tick时间把dumpReady置位，避免EXP_END刚打印就立刻DUMP导致拒绝
            time.sleep(0.20)

            # Dump
            dump_timeout_s = max(12.0, (exp_ms / 1000.0) + 12.0)
            ok = False
            dump_lines: List[str] = []
            for attempt in range(3):
                if attempt > 0:
                    time.sleep(0.5 * attempt)
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
                raise RuntimeError("EXP dump failed (missing OK or BEGIN+END)")

            # Stop streaming after experiment
            try:
                send_and_wait_ack(
                    ser,
                    "#EXP=STREAM,0!",
                    r"^OK\s+EXP_STREAM\b",
                    raw_fp=raw_fp,
                    print_realtime=print_realtime,
                    timeout_s=1.2,
                    retries=2,
                )
            except Exception as e:
                if print_realtime:
                    print(f"WARN: no ACK for #EXP=STREAM,0! (ignored): {e}")

            fields, rows = parse_dump(dump_lines)
            write_csv(csv_path, fields, rows)

            if print_realtime:
                print(f"DUMP rows: {len(rows)}")

            if pause_after:
                try:
                    input("[PAUSE] \u5b9e\u9a8c\u5df2\u7ed3\u675f\u5e76\u5df2\u751f\u6210CSV\u3002\u8bf7\u89c2\u5bdf\u5c0f\u8f66\u73b0\u8c61\u5e76\u63cf\u8ff0\u7ed9\u6211\uff0c\u7136\u540e\u6309\u56de\u8f66\u7ee7\u7eed... ")
                except KeyboardInterrupt:
                    # 允许你用Ctrl+C结束暂停，不把异常堆栈刷屏
                    return raw_path, csv_path

    return raw_path, csv_path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8", help="COM port, e.g. COM8. Empty=auto")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--id", type=int, default=1)
    ap.add_argument("--ms", type=int, default=10000)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "000Data"))
    ap.add_argument("--realtime", action="store_true")
    ap.add_argument("--pause", action="store_true")

    ap.add_argument("--ts", type=float, default=100.0)
    ap.add_argument("--so", type=int, default=100)
    ap.add_argument("--min", dest="min_pwm", type=int, default=0)
    ap.add_argument("--kp", dest="kick_pwm", type=int, default=0)
    ap.add_argument("--km", dest="kick_ms", type=int, default=0)
    ap.add_argument("--ramp", type=int, default=2)
    ap.add_argument("--hp", type=float, default=2.0)
    ap.add_argument("--hd", type=float, default=0.0050)
    ap.add_argument("--hs", type=float, default=0.2)
    ap.add_argument("--db", type=float, default=2.0)
    ap.add_argument("--hi", type=float, default=0.0)
    ap.add_argument("--hil", type=float, default=2.0)
    ap.add_argument("--kpp", type=float, default=2.0)
    ap.add_argument("--kpi", type=float, default=0.2)
    ap.add_argument("--kpd", type=float, default=0.0)
    ap.add_argument("--trim", type=float, default=0.0)
    ap.add_argument("--cal-wait", dest="cal_wait_s", type=float, default=1.5)

    args = ap.parse_args()

    port = args.port.strip() or find_serial_port(prefer_keywords=["CH340", "USB-SERIAL", "USB Serial", "CP210"])
    if not port:
        raise RuntimeError("No serial ports found")

    params = ExpParams(
        ts=args.ts,
        so=args.so,
        min_pwm=args.min_pwm,
        kick_pwm=args.kick_pwm,
        kick_ms=args.kick_ms,
        ramp=args.ramp,
        hp=args.hp,
        hd=args.hd,
        hs=args.hs,
        db=args.db,
        hi=args.hi,
        hil=args.hil,
        kpp=args.kpp,
        kpi=args.kpi,
        kpd=args.kpd,
        trim=args.trim,
        cal_wait_s=args.cal_wait_s,
    )

    print(f"PORT={port} BAUD={args.baud} EXP id={args.id} ms={args.ms}")
    print(
        "PARAMS: "
        f"ts={params.ts} so={params.so} min={params.min_pwm} kp={params.kick_pwm} km={params.kick_ms} ramp={params.ramp} "
        f"hp={params.hp} hd={params.hd} hs={params.hs} db={params.db} hi={params.hi} hil={params.hil} "
        f"kpp={params.kpp} kpi={params.kpi} kpd={params.kpd} trim={params.trim} cal_wait={params.cal_wait_s}"
    )
    raw_path, csv_path = run_one_exp(
        port=port,
        baud=args.baud,
        exp_id=args.id,
        exp_ms=args.ms,
        params=params,
        out_dir=args.out,
        print_realtime=args.realtime,
        pause_after=args.pause,
    )
    print(f"RAW: {raw_path}")
    print(f"CSV: {csv_path}")


if __name__ == "__main__":
    main()
