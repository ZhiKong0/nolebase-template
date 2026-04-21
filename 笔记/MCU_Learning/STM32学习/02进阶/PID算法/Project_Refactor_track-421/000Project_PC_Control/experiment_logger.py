#!/usr/bin/env python3
# Windows PowerShell:
#   py -3 .\experiment_logger.py --port COM18
# or
#   .\experiment_logger.ps1 --port COM18
from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import serial


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "000Data" / "serial_runs" / "experiments"
EXP_FILE_RE = re.compile(r"^exp_(\d+)_")
HOST_KEEPALIVE_INTERVAL_S = 1.5
RECONNECT_RETRY_INTERVAL_S = 1.0


def parse_kv_line(line: str, prefix: str) -> dict[str, str] | None:
    if not line.startswith(prefix):
        return None
    data: dict[str, str] = {}
    for part in line[len(prefix):].split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        data[key.strip()] = value.strip()
    return data


def experiment_path(output_dir: Path, experiment_id: int, source: str, mode: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return output_dir / f"exp_{experiment_id:04d}_{stamp}_{source}_{mode}.txt"


def open_session(output_dir: Path, experiment_id: int, source: str, mode: str):
    output_dir.mkdir(parents=True, exist_ok=True)
    path = experiment_path(output_dir, experiment_id, source, mode)
    handle = path.open("w", encoding="utf-8", newline="")
    return path, handle


def send_serial_command(port: serial.Serial, command: str) -> None:
    payload = command if command.endswith("!") else f"{command}!"
    port.write(payload.encode("ascii"))
    port.flush()


def send_command_and_collect(port: serial.Serial, command: str, ok_prefix: str,
                             timeout_s: float = 1.5) -> tuple[bool, list[str]]:
    lines: list[str] = []
    deadline = time.time() + timeout_s

    send_serial_command(port, command)
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        lines.append(line)
        if ok_prefix in line:
            return True, lines
    return False, lines


def infer_ok_prefix(command: str) -> str | None:
    if command.startswith("#MODE="):
        return "OK:MODE"
    if command.startswith("#SPD="):
        return "OK:SPD"
    if command.startswith("#TCFG="):
        return "OK:TCFG"
    if command == "#TCFG?!":
        return "TCFG:"
    if command == "#STAT!":
        return "STAT:"
    if command == "#RUN!":
        return "OK:RUN"
    if command == "#EXP?!":
        return "OK:EXP="
    return None


def load_uart_pre_commands(path: Path | None) -> list[str]:
    if path is None:
        return []

    lines: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("# ") or line.startswith("//"):
            continue
        lines.append(line)
    return lines


def line_indicates_stopped(line: str) -> bool:
    if "OK:STOP" in line:
        return True
    if parse_kv_line(line, "EVT:EXP_STOP,") is not None:
        return True

    hb = parse_kv_line(line, "HB:")
    if hb is None:
        return False

    return hb.get("run", "1") == "0"


def send_stop_and_collect(port: serial.Serial, timeout_s: float = 2.5) -> tuple[bool, list[str]]:
    lines: list[str] = []
    deadline = time.time() + timeout_s

    send_serial_command(port, "#STOP!")
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        lines.append(line)
        if line_indicates_stopped(line):
            return True, lines

    return False, lines


def parse_exp_ok(lines: list[str]) -> int | None:
    for line in reversed(lines):
        marker = "OK:EXP="
        if marker not in line:
            continue
        try:
            return int(line.split(marker, 1)[1].strip())
        except ValueError:
            return None
    return None


def find_latest_experiment_id(output_dir: Path) -> int:
    latest = 0
    if not output_dir.exists():
        return latest
    for path in output_dir.iterdir():
        match = EXP_FILE_RE.match(path.name)
        if not match:
            continue
        try:
            latest = max(latest, int(match.group(1)))
        except ValueError:
            continue
    return latest


def sync_experiment_base(port: serial.Serial, output_dir: Path) -> tuple[int | None, list[str]]:
    prefetched: list[str] = []
    disk_id = find_latest_experiment_id(output_dir)
    ok, lines = send_command_and_collect(port, "#EXP?!", "OK:EXP=")
    prefetched.extend(lines)
    board_id = parse_exp_ok(lines) if ok else None

    if board_id is None:
        print("[logger] exp sync warning: board did not return OK:EXP", file=sys.stderr)
        return None, prefetched

    target_id = max(board_id, disk_id)
    if board_id < target_id:
        ok, lines = send_command_and_collect(port, f"#EXP={target_id}!", "OK:EXP=")
        prefetched.extend(lines)
        synced_id = parse_exp_ok(lines) if ok else None
        if synced_id is None:
            print(f"[logger] exp sync warning: failed to set board exp to {target_id}", file=sys.stderr)
        else:
            board_id = synced_id

    print(f"[logger] exp sync: disk={disk_id} board={board_id} using={max(disk_id, board_id)}")
    return board_id, prefetched


def send_host_keepalive(port: serial.Serial, experiment_id: int) -> None:
    send_serial_command(port, f"#EXPHOST={max(0, experiment_id)}!")


def send_host_release(port: serial.Serial) -> None:
    send_serial_command(port, "#EXPHOST=OFF!")


def open_serial_port(port_name: str, baud: int) -> serial.Serial:
    port = serial.Serial(port_name, baudrate=baud, timeout=0.1)
    time.sleep(0.2)
    port.reset_input_buffer()
    return port


def bootstrap_connection(port: serial.Serial, output_dir: Path,
                         host_sync_id: int) -> tuple[int, list[str]]:
    prefetched_lines: list[str] = []
    synced_id, sync_lines = sync_experiment_base(port, output_dir)
    prefetched_lines.extend(sync_lines)
    if synced_id is None:
        synced_id = max(host_sync_id, find_latest_experiment_id(output_dir))
    else:
        synced_id = max(host_sync_id, synced_id)
    send_host_keepalive(port, synced_id)
    return synced_id, prefetched_lines


def close_current_session(current_file, current_path: Path | None,
                          current_id: int | None, current_mode: str,
                          reason: str):
    if current_file is None:
        return None, None, "-", "UNKNOWN"
    label = current_path.name if current_path is not None else "-"
    current_file.write(f"# {reason}\n")
    current_file.flush()
    current_file.close()
    if current_id is not None:
        print(f"[logger] close exp={current_id} mode={current_mode} -> {label} ({reason})")
    else:
        print(f"[logger] close unknown exp -> {label} ({reason})")
    return None, None, "-", "UNKNOWN"


def main() -> int:
    ap = argparse.ArgumentParser(description="Save each MCU experiment interval by shared experiment id.")
    ap.add_argument("--port", default="COM18", help="Serial port (default: COM18)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUTPUT_DIR,
                    help=f"Output directory (default: {DEFAULT_OUTPUT_DIR})")
    ap.add_argument("--echo", action="store_true", help="Print all incoming serial lines")
    ap.add_argument("--max-seconds", type=float, default=0.0,
                    help="Exit after N seconds; 0 means run forever")
    ap.add_argument("--uart-test-seconds", type=float, default=0.0,
                    help="Run a built-in UART start/stop test for N seconds")
    ap.add_argument("--uart-mode", choices=("TRACK", "STRAIGHT"), default="TRACK",
                    help="Mode used by --uart-test-seconds (default: TRACK)")
    ap.add_argument("--uart-pre-cmd-file", type=Path, default=None,
                    help="Optional file with one serial command per line, applied after #MODE and before #RUN")
    args = ap.parse_args()

    current_id: int | None = None
    current_path: Path | None = None
    current_file = None
    current_mode = "-"
    current_start_src = "UNKNOWN"
    host_sync_id = 0
    test_started = False
    stop_sent = False
    prefetched_lines: list[str] = []
    ser: serial.Serial | None = None
    uart_pre_commands = load_uart_pre_commands(args.uart_pre_cmd_file)

    print(f"[logger] port={args.port} baud={args.baud}")
    print(f"[logger] output={args.out}")
    print("[logger] waiting for EVT:EXP_START / EVT:EXP_STOP ...")

    try:
        launch_time = time.time()
        uart_stop_at = 0.0
        next_host_keepalive = 0.0

        while True:
            now = time.time()
            if ser is None:
                try:
                    print(f"[logger] opening {args.port} ...")
                    ser = open_serial_port(args.port, args.baud)
                    host_sync_id, sync_lines = bootstrap_connection(ser, args.out, host_sync_id)
                    prefetched_lines.extend(sync_lines)
                    next_host_keepalive = now + HOST_KEEPALIVE_INTERVAL_S
                    print(f"[logger] connected: {args.port}, exp-base={host_sync_id}")

                    if args.uart_test_seconds > 0.0 and not test_started:
                        ok, lines = send_stop_and_collect(ser)
                        prefetched_lines.extend(lines)
                        if not ok:
                            print("[logger] uart test warning: no STOP confirmation before timeout", file=sys.stderr)

                        ok, lines = send_command_and_collect(ser, f"#MODE={args.uart_mode}!", "OK:MODE")
                        prefetched_lines.extend(lines)
                        if not ok:
                            print("[logger] uart test warning: no OK:MODE before timeout", file=sys.stderr)

                        for pre_cmd in uart_pre_commands:
                            ok_prefix = infer_ok_prefix(pre_cmd)
                            if ok_prefix is None:
                                print(f"[logger] uart pre-cmd skip: unsupported ack rule for {pre_cmd}", file=sys.stderr)
                                continue
                            ok, lines = send_command_and_collect(ser, pre_cmd, ok_prefix)
                            prefetched_lines.extend(lines)
                            if not ok:
                                print(f"[logger] uart pre-cmd warning: no {ok_prefix} before timeout for {pre_cmd}",
                                      file=sys.stderr)

                        ok, lines = send_command_and_collect(ser, "#RUN!", "OK:RUN")
                        prefetched_lines.extend(lines)
                        if not ok:
                            print("[logger] uart test warning: no OK:RUN before timeout", file=sys.stderr)
                        test_started = True
                        uart_stop_at = time.time() + args.uart_test_seconds
                        print(f"[logger] uart test armed: mode={args.uart_mode} dur={args.uart_test_seconds:.2f}s")
                except (serial.SerialException, OSError) as exc:
                    print(f"[logger] open failed: {exc}", file=sys.stderr)
                    if args.max_seconds > 0.0 and now - launch_time >= args.max_seconds:
                        print("[logger] max-seconds reached")
                        break
                    time.sleep(RECONNECT_RETRY_INTERVAL_S)
                    continue

            if now >= next_host_keepalive:
                try:
                    send_host_keepalive(ser, host_sync_id)
                    next_host_keepalive = now + HOST_KEEPALIVE_INTERVAL_S
                except (serial.SerialException, OSError) as exc:
                    print(f"[logger] keepalive failed: {exc}", file=sys.stderr)
                    if ser is not None:
                        try:
                            ser.close()
                        except Exception:
                            pass
                    ser = None
                    prefetched_lines.clear()
                    next_host_keepalive = 0.0
                    (current_file,
                     current_path,
                     current_mode,
                     current_start_src) = close_current_session(
                        current_file, current_path, current_id, current_mode,
                        "logger lost serial before stop event"
                    )
                    current_id = None
                    continue
            if test_started and not stop_sent and now >= uart_stop_at:
                try:
                    send_serial_command(ser, "#STOP!")
                    stop_sent = True
                    print("[logger] uart test sent #STOP!")
                except (serial.SerialException, OSError) as exc:
                    print(f"[logger] uart test stop failed: {exc}", file=sys.stderr)

            if prefetched_lines:
                line = prefetched_lines.pop(0)
            else:
                try:
                    raw = ser.readline()
                except (serial.SerialException, OSError) as exc:
                    print(f"[logger] serial read failed: {exc}", file=sys.stderr)
                    if ser is not None:
                        try:
                            ser.close()
                        except Exception:
                            pass
                    ser = None
                    prefetched_lines.clear()
                    next_host_keepalive = 0.0
                    (current_file,
                     current_path,
                     current_mode,
                     current_start_src) = close_current_session(
                        current_file, current_path, current_id, current_mode,
                        "logger lost serial before stop event"
                    )
                    current_id = None
                    time.sleep(RECONNECT_RETRY_INTERVAL_S)
                    continue
                if not raw:
                    if args.max_seconds > 0.0 and now - launch_time >= args.max_seconds:
                        print("[logger] max-seconds reached")
                        break
                    continue

                line = raw.decode("utf-8", errors="ignore").strip()
                if not line:
                    if args.max_seconds > 0.0 and now - launch_time >= args.max_seconds:
                        print("[logger] max-seconds reached")
                        break
                    continue

            if args.echo:
                print(line)

            start_evt = parse_kv_line(line, "EVT:EXP_START,")
            stop_evt = parse_kv_line(line, "EVT:EXP_STOP,")
            hb = parse_kv_line(line, "HB:")

            if start_evt:
                experiment_id = int(start_evt.get("id", "0") or "0")
                source = start_evt.get("src", "UNKNOWN")
                mode = start_evt.get("mode", "-")
                host_sync_id = max(host_sync_id, experiment_id)

                if current_file is not None:
                    current_file.write("# WARNING: previous experiment was still open\n")
                    current_file.flush()
                    current_file.close()

                current_path, current_file = open_session(args.out, experiment_id, source, mode)
                current_id = experiment_id
                current_mode = mode
                current_start_src = source
                print(f"[logger] start exp={experiment_id} src={source} mode={mode} -> {current_path.name}")

            if hb and current_file is None:
                run = hb.get("run", "0")
                exp_raw = hb.get("exp", "0")
                try:
                    experiment_id = int(exp_raw or "0")
                except ValueError:
                    experiment_id = 0
                if run == "1" and experiment_id > 0:
                    mode = hb.get("m", "-")
                    host_sync_id = max(host_sync_id, experiment_id)
                    current_path, current_file = open_session(args.out, experiment_id, "HB", mode)
                    current_id = experiment_id
                    current_mode = mode
                    current_start_src = "HB"
                    print(f"[logger] late-open exp={experiment_id} mode={mode} -> {current_path.name}")

            if current_file is not None:
                current_file.write(line + "\n")
                current_file.flush()

            if stop_evt:
                stop_id = int(stop_evt.get("id", "0") or "0")
                stop_src = stop_evt.get("src", "UNKNOWN")
                if current_file is not None and current_id == stop_id:
                    print(f"[logger] stop  exp={stop_id} src={stop_src} mode={current_mode} -> {current_path.name}")
                    current_file.close()
                    current_file = None
                    current_id = None
                    current_path = None
                    current_mode = "-"
                    current_start_src = "UNKNOWN"
                else:
                    print(f"[logger] stop event without open session: exp={stop_id} src={stop_src}", file=sys.stderr)
            elif hb and current_file is not None:
                run = hb.get("run", "0")
                exp_raw = hb.get("exp", "0")
                try:
                    hb_id = int(exp_raw or "0")
                except ValueError:
                    hb_id = -1
                if run == "0" and current_id == hb_id:
                    print(f"[logger] fallback close exp={hb_id} mode={current_mode} -> {current_path.name}")
                    current_file.close()
                    current_file = None
                    current_id = None
                    current_path = None
                    current_mode = "-"
                    current_start_src = "UNKNOWN"

            if args.max_seconds > 0.0 and now - launch_time >= args.max_seconds and current_file is None:
                print("[logger] max-seconds reached")
                break
    except KeyboardInterrupt:
        print("\n[logger] stopped by user")
    finally:
        if ser is not None:
            try:
                send_host_release(ser)
            except Exception:
                pass
            try:
                ser.close()
            except Exception:
                pass
        if current_file is not None:
            close_current_session(current_file, current_path, current_id,
                                  current_mode, "logger interrupted before stop event")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
