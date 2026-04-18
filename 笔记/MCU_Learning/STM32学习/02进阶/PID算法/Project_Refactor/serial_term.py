"""
serial_term.py  --  交互式串口终端

用法:
    python serial_term.py --port COM18
    python serial_term.py --port COM18 --baud 115200

启动后直接在命令行输入命令，回车发送到串口。
串口收到的数据会实时打印到屏幕。

常用命令:
    run / stop / stat / cal / hir / at_r
    ts=140 / trim=-0.0625 / so=180 / kpp=2.0 / kpi=0.2 / kpd=0.0
    hp= / hd= / hs= / db= / hi= / hil=
    pwm=150 / diff=0 / min= / kp= / km= / ramp=
    at= / at_kp= / at_ki= / at_lim=
    raw=30 / bin=3 / enc_l_sign=1 / enc_r_sign=-1
    start=117,1200 / exp=117,1200 / estop=117 / dump=117 / stream=1
    shot=117,1200      -> 自动保存 raw 到 000Data，跑完立刻分析
    rec=3.0            -> 仅录制 3 秒串口数据并保存
    log=manual         -> 开始手动录制
    logoff             -> 停止手动录制
    analyze            -> 分析最近一次保存的 raw
    q / quit           -> 退出
"""

import argparse
import os
import sys
import threading
import time
from typing import Optional, Tuple

import serial

try:
    from serial_run_analyzer import analyze_raw, print_analysis
except Exception:
    analyze_raw = None
    print_analysis = None


SHORTCUTS = {
    "run": "#RUN!",
    "stop": "#STOP!",
    "stat": "#STAT!",
    "cal": "#CAL!",
    "hir": "#HIR!",
    "at_r": "#AT_R!",
}

SHORTCUTS_PREFIX = {
    "ts=":    lambda v: f"#TS={v}!",
    "trim=":  lambda v: f"#TRIM={v}!",
    "so=":    lambda v: f"#SO={v}!",
    "hp=":    lambda v: f"#HP={v}!",
    "hd=":    lambda v: f"#HD={v}!",
    "hs=":    lambda v: f"#HS={v}!",
    "db=":    lambda v: f"#DB={v}!",
    "hi=":    lambda v: f"#HI={v}!",
    "hil=":   lambda v: f"#HIL={v}!",
    "kpp=":   lambda v: f"#KPP={v}!",
    "kpi=":   lambda v: f"#KPI={v}!",
    "kpd=":   lambda v: f"#KPD={v}!",
    "kp=":    lambda v: f"#KP={v}!",
    "km=":    lambda v: f"#KM={v}!",
    "min=":   lambda v: f"#MIN={v}!",
    "ramp=":  lambda v: f"#RAMP={v}!",
    "pwm=":   lambda v: f"#PWM_MAX={v}!",
    "diff=":  lambda v: f"#DIFF_MAX={v}!",
    "at=":    lambda v: f"#AT={v}!",
    "at_kp=": lambda v: f"#AT_KP={v}!",
    "at_ki=": lambda v: f"#AT_KI={v}!",
    "at_lim=":lambda v: f"#AT_LIM={v}!",
    "raw=":   lambda v: f"#RAW={v}!",
    "bin=":   lambda v: f"#BIN={v}!",
    "enc_l_sign=": lambda v: f"#ENC_L_SIGN={v}!",
    "enc_r_sign=": lambda v: f"#ENC_R_SIGN={v}!",
    "start=": lambda v: f"#EXP=START,{v}!",
    "estop=": lambda v: f"#EXP=STOP,{v}!",
    "dump=":  lambda v: f"#EXP=DUMP,{v}!",
    "stream=":lambda v: f"#EXP=STREAM,{v}!",
    "exp=":   lambda v: f"#EXP=RUN,{v}!",
}


def resolve_cmd(raw: str) -> str:
    s = raw.strip()
    lo = s.lower()

    if lo in ("q", "quit", "exit"):
        return "__QUIT__"

    if lo in SHORTCUTS:
        return SHORTCUTS[lo]

    for prefix, fn in SHORTCUTS_PREFIX.items():
        if lo.startswith(prefix):
            val = s[len(prefix):]
            return fn(val)

    if s.startswith("#"):
        if not s.endswith("!"):
            s = s + "!"
        return s

    return s


def parse_exp_pair(text: str) -> Tuple[int, int]:
    parts = [x.strip() for x in text.split(",") if x.strip()]
    if len(parts) != 2:
        raise ValueError("expected id,ms")
    return int(parts[0]), int(parts[1])


class SerialTermSession:
    def __init__(self, ser: serial.Serial, data_dir: str) -> None:
        self.ser = ser
        self.data_dir = data_dir
        self.stop_event = threading.Event()
        self.capture_lock = threading.Lock()
        self.capture_fp = None
        self.capture_path = ""
        self.last_capture_path = ""
        self.rx_buf = ""

    def build_capture_path(self, prefix: str) -> str:
        os.makedirs(self.data_dir, exist_ok=True)
        ts = time.strftime("%Y%m%d_%H%M%S")
        safe_prefix = prefix.strip().replace(" ", "_") or "serial"
        return os.path.join(self.data_dir, f"{safe_prefix}_{ts}_raw.txt")

    def write_capture_bytes(self, data: bytes) -> None:
        with self.capture_lock:
            if self.capture_fp is not None:
                self.capture_fp.write(data)
                self.capture_fp.flush()

    def write_capture_text(self, text: str) -> None:
        self.write_capture_bytes((text + "\n").encode("utf-8", errors="ignore"))

    def start_capture(self, prefix: str) -> str:
        with self.capture_lock:
            if self.capture_fp is not None:
                raise RuntimeError(f"capture already active: {self.capture_path}")
            path = self.build_capture_path(prefix)
            self.capture_fp = open(path, "wb")
            self.capture_path = path
            self.last_capture_path = path
            return path

    def stop_capture(self) -> Optional[str]:
        with self.capture_lock:
            if self.capture_fp is None:
                return None
            path = self.capture_path
            self.capture_fp.close()
            self.capture_fp = None
            self.capture_path = ""
            return path

    def send(self, cmd: str) -> None:
        self.ser.write(cmd.encode("ascii", errors="ignore"))
        self.ser.flush()
        ts = time.strftime("%H:%M:%S")
        self.write_capture_text(f"CMD {cmd}")
        print(f"  [{ts}] TX: {cmd}")

    def rx_loop(self) -> None:
        while not self.stop_event.is_set():
            try:
                n = self.ser.in_waiting
                if n <= 0:
                    time.sleep(0.01)
                    continue
                data = self.ser.read(min(n, 4096))
                self.write_capture_bytes(data)
                self.rx_buf += data.decode("utf-8", errors="replace")
                while "\n" in self.rx_buf:
                    line, self.rx_buf = self.rx_buf.split("\n", 1)
                    line = line.rstrip("\r")
                    if line:
                        ts = time.strftime("%H:%M:%S")
                        print(f"\r[{ts}] {line}")
                        sys.stdout.flush()
            except Exception:
                time.sleep(0.05)

    def sleep_with_rx(self, seconds: float) -> None:
        end_t = time.time() + max(0.0, seconds)
        while time.time() < end_t and not self.stop_event.is_set():
            time.sleep(0.02)

    def record_for(self, seconds: float, prefix: str = "serial_rec") -> Optional[str]:
        path = self.start_capture(prefix)
        print(f"REC START: {path}")
        self.sleep_with_rx(seconds)
        self.send("#STAT!")
        self.sleep_with_rx(0.35)
        path = self.stop_capture()
        if path:
            print(f"REC SAVED: {path}")
        return path

    def analyze(self, raw_path: str) -> None:
        if analyze_raw is None or print_analysis is None:
            print("ANALYZE ERROR: serial_run_analyzer import failed")
            return
        summary = analyze_raw(raw_path)
        print_analysis(summary)

    def run_shot(self, exp_id: int, exp_ms: int) -> Optional[str]:
        prefix = f"exp{exp_id:02d}_{exp_ms}ms"
        path = self.start_capture(prefix)
        print(f"SHOT START: {path}")
        self.send("#STOP!")
        self.sleep_with_rx(0.10)
        self.send("#EXP=STREAM,1!")
        self.sleep_with_rx(0.10)
        self.send(f"#EXP=RUN,{exp_id},{exp_ms}!")
        self.sleep_with_rx(max(1.2, exp_ms / 1000.0 + 0.8))
        self.send("#STAT!")
        self.sleep_with_rx(0.35)
        self.send("#EXP=STREAM,0!")
        self.sleep_with_rx(0.20)
        path = self.stop_capture()
        if path:
            print(f"SHOT SAVED: {path}")
            self.analyze(path)
        return path


def print_help() -> None:
    print("Commands:")
    print("  run / stop / stat / cal / hir / at_r")
    print("  ts=140 trim=-0.0625 so=180 kpp=2.0 kpi=0.2 kpd=0.0")
    print("  hp= hd= hs= db= hi= hil= pwm= diff= min= kp= km= ramp=")
    print("  at= at_kp= at_ki= at_lim= raw= bin= enc_l_sign= enc_r_sign=")
    print("  start=117,1200  exp=117,1200  estop=117  dump=117  stream=1")
    print("  shot=117,1200   rec=3.0   log=manual   logoff   analyze   q")


def run_term(port: str, baud: int, data_dir: str) -> None:
    print(f"Opening {port} @ {baud}...")
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
    except Exception as e:
        print(f"ERROR: {e}")
        return

    try:
        ser.dtr = False
        ser.rts = False
    except Exception:
        pass

    ser.reset_input_buffer()
    ser.reset_output_buffer()

    session = SerialTermSession(ser, data_dir=data_dir)
    t = threading.Thread(target=session.rx_loop, daemon=True)
    t.start()

    print(f"Connected. Type a command and press Enter. 'q' to quit.")
    print_help()
    print("-" * 60)

    try:
        while True:
            try:
                raw = input("> ")
            except (EOFError, KeyboardInterrupt):
                break

            if not raw.strip():
                continue

            lo = raw.strip().lower()

            if lo in ("help", "h", "?"):
                print_help()
                continue

            if lo.startswith("shot="):
                try:
                    exp_id, exp_ms = parse_exp_pair(raw.split("=", 1)[1].strip())
                    session.run_shot(exp_id, exp_ms)
                except Exception as e:
                    print(f"SHOT ERROR: {e}")
                continue

            if lo.startswith("rec="):
                try:
                    seconds = float(raw.split("=", 1)[1].strip())
                    session.record_for(seconds)
                except Exception as e:
                    print(f"REC ERROR: {e}")
                continue

            if lo.startswith("log="):
                try:
                    prefix = raw.split("=", 1)[1].strip() or "manual"
                    path = session.start_capture(prefix)
                    print(f"LOG START: {path}")
                except Exception as e:
                    print(f"LOG ERROR: {e}")
                continue

            if lo in ("logoff", "log_stop", "logstop"):
                path = session.stop_capture()
                if path:
                    print(f"LOG SAVED: {path}")
                else:
                    print("LOG: no active capture")
                continue

            if lo.startswith("analyze"):
                try:
                    path = session.last_capture_path
                    parts = raw.strip().split(None, 1)
                    if len(parts) > 1 and parts[1].strip():
                        path = parts[1].strip()
                    if not path:
                        raise RuntimeError("no capture file available")
                    session.analyze(path)
                except Exception as e:
                    print(f"ANALYZE ERROR: {e}")
                continue

            cmd = resolve_cmd(raw)

            if cmd == "__QUIT__":
                break

            if not cmd:
                continue

            try:
                session.send(cmd)
            except Exception as e:
                print(f"  TX ERROR: {e}")

    finally:
        session.stop_event.set()
        time.sleep(0.15)
        saved = session.stop_capture()
        if saved:
            print(f"LOG SAVED: {saved}")
        ser.close()
        print("Closed.")


def main() -> None:
    p = argparse.ArgumentParser(description="Interactive serial terminal for MCU control")
    p.add_argument("--port", default="COM18", help="COM port, e.g. COM18")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--data-dir", default=os.path.join(os.path.dirname(__file__), "000Data"))
    args = p.parse_args()
    run_term(args.port, args.baud, args.data_dir)


if __name__ == "__main__":
    main()
