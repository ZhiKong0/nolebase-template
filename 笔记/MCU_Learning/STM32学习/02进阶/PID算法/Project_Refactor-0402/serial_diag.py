import argparse
import time
from typing import List, Tuple

import serial


def read_lines(ser: serial.Serial, seconds: float) -> Tuple[List[str], int]:
    end = time.time() + seconds
    buf = ""
    out: List[str] = []
    total_bytes = 0
    while time.time() < end:
        n = ser.in_waiting
        if n <= 0:
            time.sleep(0.01)
            continue
        b = ser.read(n)
        total_bytes += len(b)
        buf += b.decode("utf-8", errors="ignore")
        parts = buf.split("\n")
        buf = parts[-1]
        for ln in parts[:-1]:
            ln = ln.strip("\r")
            if ln:
                out.append(ln)
    return out, total_bytes


def send_cmd(ser: serial.Serial, cmd: str) -> None:
    if not cmd.endswith("!"):
        raise ValueError("Command must end with '!'")
    ser.write(cmd.encode("ascii", errors="ignore"))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument(
        "--cmd",
        action="append",
        default=[],
        help="Command to send (must end with '!'). Can be repeated.",
    )
    ap.add_argument("--wait", type=float, default=0.8, help="Wait after opening port (s)")
    ap.add_argument("--rx", type=float, default=0.6, help="Read duration after each cmd (s)")
    ap.add_argument("--gap", type=float, default=0.05, help="Gap between commands (s)")
    ap.add_argument("--show-all", action="store_true", help="Print all received lines (not only OK/ERR/HB/STAT/EXP_)")
    args = ap.parse_args()

    cmds = args.cmd or ["#STAT!", "#HIL=1.5!", "#HI=0.25!", "#HIR!", "#STAT!"]

    with serial.Serial(
        port=args.port,
        baudrate=args.baud,
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
        print(f"Opened {args.port} @ {args.baud}, wait {args.wait:.2f}s...")
        time.sleep(args.wait)
        pre, pre_bytes = read_lines(ser, 0.25)
        print(f"Pre-drain: lines={len(pre)} bytes={pre_bytes}")

        for c in cmds:
            print(f"\n>>> {c}")
            send_cmd(ser, c)
            time.sleep(args.gap)
            lines, total_bytes = read_lines(ser, args.rx)
            ok = any(ln.startswith("OK") for ln in lines)
            err = any(ln.startswith("ERR") for ln in lines)
            print(f"RX: lines={len(lines)} bytes={total_bytes} ok={ok} err={err}")
            for ln in lines:
                if args.show_all or ln.startswith(("OK", "ERR", "HB ", "STAT ", "EXP_")):
                    print(ln)


if __name__ == "__main__":
    main()
