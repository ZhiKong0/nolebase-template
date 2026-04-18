import os
import re
import csv
import argparse
import statistics
import math
from typing import Dict, List, Optional, Tuple

try:
    import numpy as np  # type: ignore
except Exception:
    np = None

try:
    import pandas as pd  # type: ignore
except Exception:
    pd = None


def find_latest_dump_csv(data_dir: str) -> Optional[str]:
    if not os.path.isdir(data_dir):
        return None

    latest_path: Optional[str] = None
    latest_mtime: float = -1.0
    for name in os.listdir(data_dir):
        if not name.endswith("_dump.csv"):
            continue
        p = os.path.join(data_dir, name)
        try:
            mt = os.path.getmtime(p)
        except OSError:
            continue
        if mt > latest_mtime:
            latest_mtime = mt
            latest_path = p
    return latest_path


def find_latest_raw_txt(data_dir: str) -> Optional[str]:
    if not os.path.isdir(data_dir):
        return None

    latest_path: Optional[str] = None
    latest_mtime: float = -1.0
    for name in os.listdir(data_dir):
        if not name.endswith("_raw.txt"):
            continue
        p = os.path.join(data_dir, name)
        try:
            mt = os.path.getmtime(p)
        except OSError:
            continue
        if mt > latest_mtime:
            latest_mtime = mt
            latest_path = p
    return latest_path


def read_csv(path: str) -> Tuple[List[str], List[List[str]]]:
    with open(path, "r", encoding="utf-8") as f:
        r = csv.reader(f)
        fields = next(r)
        rows = [row for row in r if row]
    return fields, rows


def get_col_idx(fields: List[str]) -> Dict[str, int]:
    return {k: i for i, k in enumerate(fields)}


def col(rows: List[List[str]], idx: Dict[str, int], name: str, cast=float) -> List:
    i = idx.get(name)
    if i is None:
        return []
    out = []
    for a in rows:
        if i >= len(a):
            continue
        try:
            out.append(cast(a[i]))
        except Exception:
            pass
    return out


def stat(v: List[float]) -> Optional[Tuple[float, float, float]]:
    if not v:
        return None
    return (min(v), float(statistics.mean(v)), max(v))


def rms(v: List[float]) -> Optional[float]:
    if not v:
        return None
    if np is not None:
        a = np.asarray(v, dtype=float)
        return float(np.sqrt(np.mean(a * a)))
    s2 = 0.0
    for x in v:
        s2 += x * x
    return (s2 / float(len(v))) ** 0.5


def std(v: List[float]) -> Optional[float]:
    if not v:
        return None
    if len(v) < 2:
        return 0.0
    if np is not None:
        return float(np.std(np.asarray(v, dtype=float)))
    return float(statistics.pstdev(v))


def count_zero_crossings(v: List[float], eps: float = 0.0) -> int:
    if not v:
        return 0
    last_s = 0
    cnt = 0
    for x in v:
        if abs(x) <= eps:
            s = 0
        elif x > 0:
            s = 1
        else:
            s = -1
        if last_s != 0 and s != 0 and s != last_s:
            cnt += 1
        if s != 0:
            last_s = s
    return cnt


def select_tail_by_time(t_ms: List[int], v: List[float], last_ms: int) -> List[float]:
    if not t_ms or not v or len(t_ms) != len(v):
        return []
    if last_ms <= 0:
        return []
    t_end = t_ms[-1]
    t_start = t_end - last_ms
    out: List[float] = []
    for t, a in zip(t_ms, v):
        if t >= t_start:
            out.append(a)
    return out


def parse_stream_s_lines(raw_path: str):
    pat = re.compile(r"^S\s+(\d+),(\d+),(-?\d+),(-?\d+(?:\.\d+)?),(-?\d+),(-?\d+(?:\.\d+)?),(-?\d+),(-?\d+),(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?),(-?\d+),(-?\d+)")
    rows = []
    with open(raw_path, "rb") as f:
        for b in f:
            try:
                ln = b.decode("utf-8", errors="ignore").strip("\r\n")
            except Exception:
                continue
            m = pat.match(ln)
            if not m:
                continue
            t_ms = int(m.group(1))
            run = int(m.group(2))
            ts = int(m.group(3))
            yaw_err = float(m.group(4))
            ed = int(m.group(5))
            corr = float(m.group(6))
            pwm_l = int(m.group(7))
            pwm_r = int(m.group(8))
            yaw_rate = float(m.group(9))
            trim = float(m.group(10))
            pmax = int(m.group(11))
            dmax = int(m.group(12))
            rows.append((t_ms, run, ts, yaw_err, ed, corr, pwm_l, pwm_r, yaw_rate, trim, pmax, dmax))

    if not rows:
        return None

    if pd is not None:
        df = pd.DataFrame(
            rows,
            columns=[
                "t_ms",
                "run",
                "ts",
                "yaw_err",
                "ed",
                "heading_corr",
                "pwm_l",
                "pwm_r",
                "yaw_rate",
                "trim",
                "pmax",
                "dmax",
            ],
        )
        return df

    return rows


def tail_stat(v: List[float], n: int) -> Optional[Tuple[float, float, float]]:
    if not v:
        return None
    if n <= 0:
        return None
    if n >= len(v):
        return stat(v)
    return stat(v[-n:])


def tail_by_time_stat(t_ms: List[int], v: List[float], last_ms: int) -> Optional[Tuple[float, float, float]]:
    if not t_ms or not v:
        return None
    if len(t_ms) != len(v):
        return None
    if last_ms <= 0:
        return None
    t_end = t_ms[-1]
    t_start = t_end - last_ms
    sel: List[float] = []
    for t, a in zip(t_ms, v):
        if t >= t_start:
            sel.append(a)
    return stat(sel)


def slice_by_time_range(t_ms: List[int], v: List[float], t0: int, t1: int) -> List[float]:
    if not t_ms or not v or len(t_ms) != len(v):
        return []
    out: List[float] = []
    for t, a in zip(t_ms, v):
        if t0 <= t < t1:
            out.append(a)
    return out


def print_segment_stats(
    t_ms: List[int],
    e: List[float],
    c: List[float],
    ed: List[int],
    L: List[int],
    R: List[int],
    segments: int,
) -> None:
    if not t_ms or segments <= 0:
        return
    if len(t_ms) < 2:
        return

    t_start = int(t_ms[0])
    t_end = int(t_ms[-1])
    if t_end <= t_start:
        return

    print("SEGMENTS:", segments)
    for k in range(segments):
        a0 = float(k) / float(segments)
        a1 = float(k + 1) / float(segments)
        s0 = t_start + int((t_end - t_start) * a0)
        s1 = t_start + int((t_end - t_start) * a1)

        e_seg = slice_by_time_range(t_ms, e, s0, s1) if e else []
        c_seg = slice_by_time_range(t_ms, c, s0, s1) if c else []
        ed_seg = [float(x) for x in slice_by_time_range(t_ms, [float(x) for x in ed], s0, s1)] if ed else []
        L_seg = [float(x) for x in slice_by_time_range(t_ms, [float(x) for x in L], s0, s1)] if L else []
        R_seg = [float(x) for x in slice_by_time_range(t_ms, [float(x) for x in R], s0, s1)] if R else []

        label = f"{int(a0*100)}-{int(a1*100)}%"
        print("SEG", label, f"t=[{s0},{s1})ms")
        if e_seg:
            print("  yaw_err e(min,mean,max):", stat(e_seg), "rms:", rms(e_seg), "std:", std(e_seg), "zc:", count_zero_crossings(e_seg, eps=0.05))
        if c_seg:
            print("  heading_corr c(min,mean,max):", stat(c_seg), "rms:", rms(c_seg), "std:", std(c_seg))
        if ed_seg:
            print("  ed(min,mean,max):", stat(ed_seg), "rms:", rms(ed_seg), "std:", std(ed_seg))
        if L_seg:
            print("  PWM_L(min,mean,max):", stat(L_seg))
        if R_seg:
            print("  PWM_R(min,mean,max):", stat(R_seg))


def compute_hard_dropout_stats(
    use: List[Dict[str, float]],
    consecutive_n: int,
) -> Tuple[int, int, float]:
    if consecutive_n <= 1:
        consecutive_n = 1

    moving_rows: List[Dict[str, float]] = []
    for r in use:
        if (r["L"] > 0.0 or r["R"] > 0.0) and r["run"] > 0.5:
            moving_rows.append(r)

    if not moving_rows:
        return 0, 0, 0.0

    hard_segments = 0
    hard_ticks = 0
    cur_run = 0
    for r in moving_rows:
        el0 = (r["el"] == 0.0)
        er0 = (r["er"] == 0.0)
        is_zero = (el0 != er0)
        if is_zero:
            cur_run += 1
        else:
            if cur_run >= consecutive_n:
                hard_segments += 1
                hard_ticks += cur_run
            cur_run = 0

    if cur_run >= consecutive_n:
        hard_segments += 1
        hard_ticks += cur_run

    hard_ratio = float(hard_ticks) / float(len(moving_rows))
    return hard_segments, hard_ticks, hard_ratio


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--csv",
        default="",
        help="dump csv path. empty=auto pick latest from 000Data",
    )
    ap.add_argument(
        "--raw",
        default="",
        help="raw txt path. empty=auto pick latest from 000Data",
    )
    ap.add_argument(
        "--data-dir",
        default=os.path.join(os.path.dirname(__file__), "000Data"),
        help="where to auto-find latest *_dump.csv",
    )
    ap.add_argument(
        "--max-run-s",
        type=float,
        default=0.0,
        help="only analyze first N seconds after run=1 starts (0=disabled)",
    )
    ap.add_argument(
        "--dropout-n",
        type=int,
        default=3,
        help="hard dropout consecutive threshold N (count segments where el==0 or er==0 repeats >=N while moving)",
    )
    ap.add_argument("--segments", type=int, default=5, help="segment count by time percent, default=5 (20%% each)")
    args = ap.parse_args()

    raw_path = args.raw.strip() or find_latest_raw_txt(args.data_dir)
    csv_path = args.csv.strip() or find_latest_dump_csv(args.data_dir)

    if raw_path:
        analyze_raw(
            raw_path,
            segments=max(1, int(args.segments)),
            max_run_s=float(args.max_run_s),
            dropout_n=int(args.dropout_n),
        )
    elif csv_path:
        analyze_csv(csv_path, segments=max(1, int(args.segments)))
    else:
        raise SystemExit("No dump csv or raw txt found")


def _median(xs: List[float]) -> float:
    if not xs:
        return 0.0
    ys = sorted(xs)
    n = len(ys)
    mid = n // 2
    if (n % 2) == 1:
        return float(ys[mid])
    return 0.5 * (float(ys[mid - 1]) + float(ys[mid]))


def _mean(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return float(sum(xs)) / float(len(xs))


def _rms(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return math.sqrt(sum((x * x) for x in xs) / float(len(xs)))


def _std(xs: List[float]) -> float:
    if len(xs) < 2:
        return 0.0
    m = _mean(xs)
    return math.sqrt(sum(((x - m) * (x - m)) for x in xs) / float(len(xs)))


def analyze_raw(raw_path: str, segments: int = 5, max_run_s: float = 0.0, dropout_n: int = 3) -> None:
    hb_re = re.compile(
        r"^HB\s+tick=(\d+)\s+exp_id=(\d+)\s+t_ms=(\d+)\s+run=(\d+)\s+ts=([-\d\.]+)\s+"
        r"y=([-\d\.]+)\s+ty=([-\d\.]+)\s+e=([-\d\.]+)\s+c=([-\d\.]+)\s+hi=([-\d\.]+)\s+"
        r"L=([-\d]+)\s+R=([-\d]+)\s+el=([-\d]+)\s+er=([-\d]+)\s+ed=([-\d]+)\s+"
        r"trim=([-\d\.]+)"
    )

    rows: List[Dict[str, float]] = []
    with open(raw_path, "r", encoding="utf-8", errors="ignore") as f:
        for ln in f:
            ln = ln.strip()
            if not ln.startswith("HB "):
                continue
            m = hb_re.match(ln)
            if not m:
                continue
            tick = int(m.group(1))
            t_ms = int(m.group(3))
            run = int(m.group(4))
            y = float(m.group(6))
            ty = float(m.group(7))
            e = float(m.group(8))
            c = float(m.group(9))
            hi = float(m.group(10))
            L = int(m.group(11))
            R = int(m.group(12))
            el = int(m.group(13))
            er = int(m.group(14))
            ed = int(m.group(15))
            trim = float(m.group(16))
            rows.append(
                {
                    "tick": float(tick),
                    "t_ms": float(t_ms),
                    "run": float(run),
                    "y": y,
                    "ty": ty,
                    "e": e,
                    "c": c,
                    "hi": hi,
                    "L": float(L),
                    "R": float(R),
                    "el": float(el),
                    "er": float(er),
                    "ed": float(ed),
                    "trim": trim,
                }
            )

    if not rows:
        print(f"RAW: {raw_path}\n(no HB rows parsed)")
        return

    run_rows = [r for r in rows if r["run"] > 0.5]
    use = run_rows if run_rows else rows

    if max_run_s > 0.0 and run_rows:
        t0 = run_rows[0]["tick"]
        t1 = t0 + (max_run_s * 1000.0)
        clipped = [r for r in run_rows if (r["tick"] >= t0 and r["tick"] <= t1)]
        if clipped:
            use = clipped

    ys = [r["y"] for r in use]
    es = [r["e"] for r in use]
    cs = [r["c"] for r in use]
    els = [r["el"] for r in use]
    ers = [r["er"] for r in use]
    eds = [r["ed"] for r in use]
    Ls = [r["L"] for r in use]
    Rs = [r["R"] for r in use]

    dt_ticks = [use[i + 1]["tick"] - use[i]["tick"] for i in range(len(use) - 1)]
    dt_tick_median = _median(dt_ticks)

    dropout = 0
    both_zero = 0
    moving = 0
    for r in use:
        if (r["L"] > 0.0 or r["R"] > 0.0) and r["run"] > 0.5:
            moving += 1
            el0 = (r["el"] == 0.0)
            er0 = (r["er"] == 0.0)
            if el0 and er0:
                both_zero += 1
            elif el0 != er0:
                dropout += 1
    dropout_ratio = (float(dropout) / float(moving)) if moving > 0 else 0.0
    both_zero_ratio = (float(both_zero) / float(moving)) if moving > 0 else 0.0

    hard_segments, hard_ticks, hard_ratio = compute_hard_dropout_stats(use, int(dropout_n))

    print(f"RAW: {raw_path}")
    print(f"HB rows: {len(rows)}  RUN rows: {len(run_rows)}")
    if max_run_s > 0.0 and run_rows:
        print(f"CLIP: first {max_run_s:.2f}s after run=1 => rows: {len(use)}")
    print(f"tick step median: {dt_tick_median}")
    print(f"yaw y(min,mean,max): ({min(ys):.2f}, {_mean(ys):.2f}, {max(ys):.2f})")
    print(f"yaw_err e(min,mean,max): ({min(es):.2f}, {_mean(es):.2f}, {max(es):.2f}) rms: {_rms(es):.3f} std: {_std(es):.3f}")
    print(f"heading_corr c(min,mean,max): ({min(cs):.2f}, {_mean(cs):.2f}, {max(cs):.2f}) rms: {_rms(cs):.3f}")
    print(f"PWM_L(min,mean,max): ({min(Ls):.1f}, {_mean(Ls):.3f}, {max(Ls):.1f})")
    print(f"PWM_R(min,mean,max): ({min(Rs):.1f}, {_mean(Rs):.3f}, {max(Rs):.1f})")
    print(f"encL(min,mean,max): ({min(els):.1f}, {_mean(els):.3f}, {max(els):.1f})")
    print(f"encR(min,mean,max): ({min(ers):.1f}, {_mean(ers):.3f}, {max(ers):.1f})")
    print(f"ed(min,mean,max): ({min(eds):.1f}, {_mean(eds):.3f}, {max(eds):.1f}) mean_signed: {_mean(eds):.3f}")
    print(f"dropout_ratio(el==0 xor er==0 while moving): {dropout_ratio:.3f}")
    print(f"both_zero_ratio(el==0 and er==0 while moving): {both_zero_ratio:.3f}")
    print(
        f"dropout_hard_ratio(consecutive>={int(dropout_n)} while moving): {hard_ratio:.3f}  segments: {hard_segments}  ticks: {hard_ticks}"
    )

    seg_n = max(1, int(segments))
    if len(use) >= seg_n:
        print(f"SEGMENTS: {seg_n}")
        for i in range(seg_n):
            a = (len(use) * i) // seg_n
            b = (len(use) * (i + 1)) // seg_n
            chunk = use[a:b]
            if not chunk:
                continue
            ces = [r["e"] for r in chunk]
            ceds = [r["ed"] for r in chunk]
            print(
                f"SEG {i} rows=[{a},{b}) yaw_err(mean,rms): ({_mean(ces):.3f}, {_rms(ces):.3f}) ed_mean_signed: {_mean(ceds):.3f} ed_rms: {_rms(ceds):.3f}"
            )


def analyze_csv(csv_path: str, segments: int = 5) -> None:
    fields, rows = read_csv(csv_path)
    idx = get_col_idx(fields)

    L = col(rows, idx, "L", int)
    R = col(rows, idx, "R", int)
    el = col(rows, idx, "el", int)
    er = col(rows, idx, "er", int)
    ed = col(rows, idx, "ed", int)
    trimQ = col(rows, idx, "trimQ", int)
    trim16 = col(rows, idx, "trim16", int)
    trim8 = col(rows, idx, "trim8", int)
    trim4 = []
    trim2 = []
    if (not trimQ) and (not trim16) and (not trim8):
        trim4 = col(rows, idx, "trim4", int)
    if (not trimQ) and (not trim16) and (not trim8) and (not trim4):
        trim2 = col(rows, idx, "trim2", int)

    y = col(rows, idx, "y", float)
    ty = col(rows, idx, "ty", float)
    e = col(rows, idx, "e", float)
    c = col(rows, idx, "c", float)
    hi = col(rows, idx, "hi", float)
    t_ms = col(rows, idx, "t_ms", int)

    if not y:
        y10 = col(rows, idx, "y10", int)
        y = [v / 10.0 for v in y10]
    if not ty:
        ty10 = col(rows, idx, "ty10", int)
        ty = [v / 10.0 for v in ty10]
    if not e:
        e10 = col(rows, idx, "e10", int)
        e = [v / 10.0 for v in e10]
    if not c:
        c10 = col(rows, idx, "c10", int)
        c = [v / 10.0 for v in c10]

    if not hi:
        hi100 = col(rows, idx, "hi100", int)
        if hi100:
            hi = [v / 100.0 for v in hi100]

    if not hi:
        hi10 = col(rows, idx, "hi10", int)
        hi = [v / 10.0 for v in hi10]

    trim = [v / 1024.0 for v in trimQ]
    if not trim and trim16:
        trim = [v * 0.0625 for v in trim16]
    if not trim and trim8:
        trim = [v * 0.125 for v in trim8]
    if not trim and trim4:
        trim = [v * 0.25 for v in trim4]
    if not trim and trim2:
        trim = [v * 0.5 for v in trim2]

    abs_el = [abs(x) for x in el]
    abs_er = [abs(x) for x in er]
    mean_abs_speed = 0.0
    if abs_el and abs_er:
        mean_abs_speed = (float(statistics.mean(abs_el)) + float(statistics.mean(abs_er))) / 2.0

    stall_like = False
    if mean_abs_speed < 50:
        stall_like = True

    print("CSV:", csv_path)
    print("ROWS:", len(rows))
    print("PWM_L(min,mean,max):", stat([float(x) for x in L]))
    print("PWM_R(min,mean,max):", stat([float(x) for x in R]))
    print("encL(min,mean,max):", stat([float(x) for x in el]))
    print("encR(min,mean,max):", stat([float(x) for x in er]))
    print("encDiff ed(min,mean,max):", stat([float(x) for x in ed]))
    if trim:
        print("trim(min,mean,max):", stat([float(x) for x in trim]))
    print("yaw y(min,mean,max):", stat(y))
    print("target_yaw ty(min,mean,max):", stat(ty))
    print("yaw_err e(min,mean,max):", stat(e))
    print("heading_corr c(min,mean,max):", stat(c))
    if hi:
        print("heading_I hi(min,mean,max):", stat(hi))
    print("mean_abs_speed:", mean_abs_speed)
    print("stall_like:", int(stall_like))

    # Tail stats for bias trend
    if y and t_ms and len(y) == len(t_ms):
        n20 = max(1, int(len(y) * 0.2))
        print("TAIL(last20%) yaw_err e(min,mean,max):", tail_stat(e, n20))
        print("TAIL(last20%) heading_corr c(min,mean,max):", tail_stat(c, n20))
        if hi:
            print("TAIL(last20%) heading_I hi(min,mean,max):", tail_stat(hi, n20))

        t5 = tail_by_time_stat(t_ms, e, 5000)
        if t5 is not None:
            print("TAIL(last5s) yaw_err e(min,mean,max):", t5)
            print("TAIL(last5s) heading_corr c(min,mean,max):", tail_by_time_stat(t_ms, c, 5000))
            if hi:
                print("TAIL(last5s) heading_I hi(min,mean,max):", tail_by_time_stat(t_ms, hi, 5000))

    e_rms = rms(e) if e else None
    c_rms = rms(c) if c else None
    ed_mean = float(statistics.mean(ed)) if ed else 0.0
    ed_rms = rms([float(x) for x in ed]) if ed else None
    e_zc = count_zero_crossings(e, eps=0.05) if e else 0
    e_std = std(e) if e else None

    if t_ms and len(t_ms) >= 2:
        dt = [t_ms[i] - t_ms[i - 1] for i in range(1, len(t_ms))]
        dt_med = float(statistics.median(dt)) if dt else 0.0
    else:
        dt_med = 0.0

    print("dt_ms(median):", dt_med)
    print("yaw_err_rms:", e_rms)
    print("yaw_err_std:", e_std)
    print("yaw_err_zero_crossings:", e_zc)
    print("heading_corr_rms:", c_rms)
    print("ed_mean_signed:", ed_mean)
    print("ed_rms:", ed_rms)

    if L and R:
        pwm_hi = 95
        sat = 0
        for a, b in zip(L, R):
            if a >= pwm_hi or b >= pwm_hi:
                sat += 1
        sat_ratio = float(sat) / float(max(1, min(len(L), len(R))))
        print("pwm_sat_ratio(>=95):", sat_ratio)
    else:
        sat_ratio = 0.0

    tail_e = select_tail_by_time(t_ms, e, 5000) if (t_ms and e) else []
    tail_ed = select_tail_by_time(t_ms, [float(x) for x in ed], 5000) if (t_ms and ed) else []
    tail_e_rms = rms(tail_e) if tail_e else None
    tail_ed_mean = float(statistics.mean(tail_ed)) if tail_ed else 0.0
    print("TAIL(last5s) yaw_err_rms:", tail_e_rms)
    print("TAIL(last5s) ed_mean_signed:", tail_ed_mean)

    drift = "UNKNOWN"
    if abs(tail_ed_mean) < 5:
        drift = "OK"
    elif tail_ed_mean > 0:
        drift = "LEFT_FASTER"
    else:
        drift = "RIGHT_FASTER"
    print("DRIFT_BY_ED:", drift)

    if segments > 1:
        try:
            print_segment_stats(t_ms, e, c, ed, L, R, segments)
        except Exception:
            pass

    sugg: List[str] = []
    if abs(tail_ed_mean) > 10:
        sugg.append("TRIM: 先把 ed 的长期均值推回接近 0（优先调 TRIM，再开大外环）。")
    if sat_ratio > 0.10:
        sugg.append("LIMIT: 饱和占比偏高，先降 TS/HP 或降低 PWM_MAX/DIFF_MAX，避免暴力打满。")
    if e_rms is not None:
        if e_zc >= 8 and (e_std is not None and e_std > 1.0):
            sugg.append("HEADING: 摆动/蛇形明显，优先增大 HD 或减小 HP，必要时减小 HS(更平滑)。")
        elif e_rms > 2.0 and e_zc < 4:
            sugg.append("HEADING: 误差偏大但不太摆动，可逐步增大 HP 或减小 DB(别太小)。")

    if raw_p:
        s_data = parse_stream_s_lines(raw_p)
        if s_data is not None:
            print("RAW:", raw_p)
            if pd is not None and hasattr(s_data, "__class__") and s_data.__class__.__name__ == "DataFrame":
                df = s_data
                try:
                    yr = df["yaw_rate"].astype(float).to_numpy()
                    yr_rms = float(np.sqrt(np.mean(yr * yr))) if (np is not None and len(yr) > 0) else None
                except Exception:
                    yr_rms = None
                print("stream_rows:", int(len(df)))
                print("yaw_rate_rms:", yr_rms)
                try:
                    if yr_rms is not None and yr_rms > 8.0:
                        sugg.append("IMU: yawRate 抖动偏大，优先增大 HD 或降低 HS/HP；也可能是 IMU 噪声/安装震动。")
                except Exception:
                    pass
            else:
                ys = [float(r[8]) for r in s_data]
                yr_rms = rms(ys)
                print("stream_rows:", len(s_data))
                print("yaw_rate_rms:", yr_rms)
                if yr_rms is not None and yr_rms > 8.0:
                    sugg.append("IMU: yawRate 抖动偏大，优先增大 HD 或降低 HS/HP；也可能是 IMU 噪声/安装震动。")

    if sugg:
        print("SUGGEST:")
        for s in sugg:
            print("-", s)


if __name__ == "__main__":
    main()
