#!/usr/bin/env python3
"""
diag_30s.py — 30 秒直线诊断采集 + 自动统计 + 数据保存
=======================================================

用法:
    python diag_30s.py                          # 默认 COM18, 30s
    python diag_30s.py --port COM5 --dur 10     # 指定串口和时长
    python diag_30s.py --no-run                 # 仅采集不发 RUN (手动启动)
    python diag_30s.py --tag "baseline_v3"      # 给本次数据打标签

输出:
    000Data/diag/<timestamp>_<tag>/
        ├── raw.txt          原始 HB 行
        ├── summary.txt      统计摘要 (供 AI 审查)
        └── parsed.csv       解析后 CSV (供后续分析)
"""

import argparse
import csv
import os
import serial
import statistics
import sys
import time
from datetime import datetime


def parse_hb_line(line: str) -> dict:
    """Parse 'HB:k1=v1,k2=v2,...' into dict."""
    kv = {}
    if not line.startswith("HB:"):
        return kv
    for pair in line[3:].split(","):
        if "=" in pair:
            k, v = pair.split("=", 1)
            kv[k.strip()] = v.strip()
    return kv


def run_capture(port: str, baud: int, duration: float, send_run: bool) -> list:
    """Open serial, optionally send #RUN!, capture HB lines, send #STOP!."""
    s = serial.Serial(port, baud, timeout=0.1)
    time.sleep(0.5)
    s.reset_input_buffer()

    if send_run:
        s.write(b"#RUN!\r\n")
        time.sleep(0.05)

    samples = []
    t0 = time.time()
    while time.time() - t0 < duration + 2:  # +2s margin
        line = s.readline().decode(errors="replace").strip()
        if not line or not line.startswith("HB:"):
            continue
        samples.append(line)

    if send_run:
        s.write(b"#STOP!\r\n")
        time.sleep(0.5)
    s.close()
    return samples


def compute_stats(parsed: list) -> dict:
    """Compute summary statistics from parsed HB dicts."""
    pcs, yaws, hds = [], [], []
    neg_core = 0
    neg_ol = 0
    min_ol = 9999
    min_or = 9999

    for kv in parsed:
        # core
        pc = kv.get("pc", "")
        if pc.lstrip("-").isdigit():
            pci = int(pc)
            pcs.append(pci)
            if pci < 0:
                neg_core += 1
        # hd
        hd = kv.get("hd", "")
        if hd.lstrip("-").isdigit():
            hds.append(int(hd))
        # yaw
        yaw = kv.get("yaw", "")
        try:
            yaws.append(float(yaw))
        except ValueError:
            pass
        # OL/OR
        ol = kv.get("OL", "")
        orr = kv.get("OR", "")
        if ol.lstrip("-").isdigit():
            oli = int(ol)
            if oli < 0:
                neg_ol += 1
            min_ol = min(min_ol, oli)
        if orr.lstrip("-").isdigit():
            min_or = min(min_or, int(orr))

    st = {
        "total_samples": len(parsed),
        "neg_core": neg_core,
        "neg_core_pct": f"{100*neg_core/max(len(parsed),1):.1f}%",
        "neg_ol_or": neg_ol,
    }
    if pcs:
        st["core_min"] = min(pcs)
        st["core_max"] = max(pcs)
        st["core_mean"] = f"{sum(pcs)/len(pcs):.1f}"
    if yaws:
        st["yaw_min"] = f"{min(yaws):.1f}"
        st["yaw_max"] = f"{max(yaws):.1f}"
        st["yaw_final"] = f"{yaws[-1]:.1f}"
        if len(yaws) > 1:
            st["yaw_std"] = f"{statistics.stdev(yaws):.2f}"
    if hds:
        st["max_abs_hd"] = max(abs(h) for h in hds)
    st["min_OL"] = min_ol if min_ol < 9999 else "?"
    st["min_OR"] = min_or if min_or < 9999 else "?"
    return st


def print_table(parsed: list, first_n=20, last_n=20):
    """Print first/last samples in a readable table."""
    header = "t_ms  pc   hd   OL   OR   yaw      yr     el    er"
    sep = "-" * 85

    def row(kv):
        t = kv.get("t", "?")
        pc = kv.get("pc", "?")
        hd = kv.get("hd", "?")
        ol = kv.get("OL", "?")
        orr = kv.get("OR", "?")
        yaw = kv.get("yaw", "?")
        yr = kv.get("yr", "?")
        el = kv.get("el", "?")
        er = kv.get("er", "?")
        return f"{t:>6s} {pc:>5s} {hd:>5s} {ol:>5s} {orr:>5s} {yaw:>8s} {yr:>8s} {el:>5s} {er:>5s}"

    lines = []
    lines.append(f"\n--- FIRST {first_n} ---")
    lines.append(header)
    lines.append(sep)
    for kv in parsed[:first_n]:
        lines.append(row(kv))

    if len(parsed) > first_n + last_n:
        lines.append(f"\n  ... ({len(parsed) - first_n - last_n} samples omitted) ...\n")

    lines.append(f"\n--- LAST {last_n} ---")
    lines.append(header)
    lines.append(sep)
    for kv in parsed[-last_n:]:
        lines.append(row(kv))

    return "\n".join(lines)


def save_results(out_dir: str, raw_lines: list, parsed: list, stats: dict, table_str: str):
    """Save raw.txt, parsed.csv, summary.txt."""
    os.makedirs(out_dir, exist_ok=True)

    # raw.txt
    with open(os.path.join(out_dir, "raw.txt"), "w", encoding="utf-8") as f:
        for line in raw_lines:
            f.write(line + "\n")

    # parsed.csv
    all_keys = set()
    for kv in parsed:
        all_keys.update(kv.keys())
    all_keys = sorted(all_keys)
    with open(os.path.join(out_dir, "parsed.csv"), "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=all_keys)
        writer.writeheader()
        for kv in parsed:
            writer.writerow(kv)

    # summary.txt
    with open(os.path.join(out_dir, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("=" * 60 + "\n")
        f.write(f"  Diagnostic Capture Summary\n")
        f.write(f"  Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("=" * 60 + "\n\n")

        f.write("=== STATISTICS ===\n")
        for k, v in stats.items():
            f.write(f"  {k:20s} : {v}\n")

        f.write("\n")
        f.write(table_str)
        f.write("\n")

    return out_dir


def main():
    parser = argparse.ArgumentParser(description="30s straight-line diagnostic capture")
    parser.add_argument("--port", default="COM18", help="Serial port (default: COM18)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--dur", type=float, default=30, help="Capture duration in seconds")
    parser.add_argument("--no-run", action="store_true", help="Don't send #RUN! (manual start)")
    parser.add_argument("--tag", default="", help="Tag for output folder name")
    parser.add_argument("--out-base", default=None,
                        help="Base output directory (default: ../000Data/diag/)")
    args = parser.parse_args()

    # Determine output directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    if args.out_base:
        base_dir = args.out_base
    else:
        base_dir = os.path.join(script_dir, "..", "000Data", "diag")

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    folder_name = f"{ts}_{args.tag}" if args.tag else ts
    out_dir = os.path.join(base_dir, folder_name)

    # Capture
    print(f"Capturing on {args.port} for {args.dur}s ...")
    raw_lines = run_capture(args.port, args.baud, args.dur, send_run=not args.no_run)
    print(f"Captured {len(raw_lines)} HB samples")

    if not raw_lines:
        print("ERROR: No samples captured! Check serial port and device.", file=sys.stderr)
        sys.exit(1)

    # Parse
    parsed = [parse_hb_line(line) for line in raw_lines]

    # Stats
    stats = compute_stats(parsed)
    table_str = print_table(parsed)

    # Print to console
    print(table_str)
    print(f"\n=== SUMMARY ===")
    for k, v in stats.items():
        print(f"  {k:20s} : {v}")

    # Save
    save_results(out_dir, raw_lines, parsed, stats, table_str)
    print(f"\nData saved to: {out_dir}")


if __name__ == "__main__":
    main()
