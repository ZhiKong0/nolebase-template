import argparse
import csv
import math
import os
import statistics
from typing import Dict, List, Optional


HEADING_CORR_LIMIT = 7.0


def _mean(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return float(sum(xs)) / float(len(xs))


def _rms(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return math.sqrt(float(sum(x * x for x in xs)) / float(len(xs)))


def _parse_value(text: str) -> Optional[float]:
    try:
        if text.lower().startswith("0x"):
            return float(int(text, 16))
        return float(text)
    except Exception:
        return None


def _unwrap_yaw_deg(yaws: List[float]) -> List[float]:
    if not yaws:
        return []
    out = [float(yaws[0])]
    for i in range(1, len(yaws)):
        prev = out[-1]
        cur = float(yaws[i])
        raw_prev = float(yaws[i - 1])
        d = cur - raw_prev
        if d > 180.0:
            d -= 360.0
        elif d < -180.0:
            d += 360.0
        out.append(prev + d)
    return out


def _linear_fit_slope(x: List[float], y: List[float]) -> float:
    if len(x) != len(y) or len(x) < 2:
        return 0.0
    mx = _mean(x)
    my = _mean(y)
    sxx = 0.0
    sxy = 0.0
    for a, b in zip(x, y):
        dx = a - mx
        sxx += dx * dx
        sxy += dx * (b - my)
    if sxx <= 1e-12:
        return 0.0
    return sxy / sxx


def _label_signed(v: float, eps: float, pos_label: str, neg_label: str, zero_label: str) -> str:
    if v > eps:
        return pos_label
    if v < -eps:
        return neg_label
    return zero_label


def _collect_segments(
    samples: List[Dict[str, object]],
    key: str,
    neutral_label: str,
    min_samples: int,
) -> List[Dict[str, float]]:
    segments: List[Dict[str, float]] = []
    if not samples:
        return segments

    start = 0
    cur_label = str(samples[0].get(key, neutral_label))
    for i in range(1, len(samples) + 1):
        next_label = cur_label
        if i < len(samples):
            next_label = str(samples[i].get(key, neutral_label))
        if i < len(samples) and next_label == cur_label:
            continue

        chunk = samples[start:i]
        if cur_label != neutral_label and len(chunk) >= min_samples:
            t0 = float(chunk[0]["t_s"])
            t1 = float(chunk[-1]["t_s"])
            vals: List[float] = []
            if key == "yaw_turn":
                vals = [float(x["yaw_delta_deg"]) for x in chunk]
            elif key == "wheel_bias" or key == "wheel_slow_side":
                vals = [float(x["speed_diff"]) for x in chunk]
            elif key == "heading_corr_state":
                vals = [float(x["heading_corr"]) for x in chunk]
            peak_abs = max(abs(v) for v in vals) if vals else 0.0
            segments.append(
                {
                    "label": cur_label,
                    "t0": t0,
                    "t1": t1,
                    "duration_s": t1 - t0,
                    "mean": _mean(vals) if vals else 0.0,
                    "peak_abs": peak_abs,
                }
            )

        if i < len(samples):
            start = i
            cur_label = next_label

    return segments


def _format_segments(title: str, segments: List[Dict[str, float]], unit: str) -> List[str]:
    lines: List[str] = []
    if not segments:
        lines.append(f"{title}: none")
        return lines
    lines.append(f"{title}:")
    for seg in segments:
        lines.append(
            f"- {seg['label']}  {seg['t0']:.3f}s -> {seg['t1']:.3f}s  dur={seg['duration_s']:.3f}s  mean={seg['mean']:.3f}{unit}  peak_abs={seg['peak_abs']:.3f}{unit}"
        )
    return lines


def _sum_segment_durations(segments: List[Dict[str, float]], label: str) -> float:
    return float(sum(float(seg.get("duration_s", 0.0)) for seg in segments if str(seg.get("label", "")) == label))


def _longest_segment(segments: List[Dict[str, float]]) -> Dict[str, float]:
    best: Dict[str, float] = {}
    best_dur = -1.0
    for seg in segments:
        dur = float(seg.get("duration_s", 0.0))
        if dur > best_dur:
            best = seg
            best_dur = dur
    return best


def find_latest_raw_txt(data_dir: str) -> Optional[str]:
    if not os.path.isdir(data_dir):
        return None
    latest_path: Optional[str] = None
    latest_mtime = -1.0
    for name in os.listdir(data_dir):
        if not name.endswith("_raw.txt"):
            continue
        path = os.path.join(data_dir, name)
        try:
            mtime = os.path.getmtime(path)
        except OSError:
            continue
        if mtime > latest_mtime:
            latest_mtime = mtime
            latest_path = path
    return latest_path


def parse_serial_rows(raw_path: str) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []
    with open(raw_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("HB ") and not line.startswith("STAT "):
                continue
            parts = line.split()
            row: Dict[str, object] = {"source": parts[0]}
            for token in parts[1:]:
                if "=" not in token:
                    continue
                k, v = token.split("=", 1)
                num = _parse_value(v)
                if num is not None:
                    row[k] = num
            if "tick" in row:
                rows.append(row)
    return rows


def parse_latest_cfg(raw_path: str) -> Dict[str, float]:
    cfg: Dict[str, float] = {}
    with open(raw_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("CFG "):
                continue
            parts = line.split()
            cur: Dict[str, float] = {}
            for token in parts[1:]:
                if "=" not in token:
                    continue
                k, v = token.split("=", 1)
                num = _parse_value(v)
                if num is not None:
                    cur[k] = num
            if cur:
                cfg = cur
    return cfg


def build_samples(rows: List[Dict[str, object]], run_only: bool = True) -> List[Dict[str, object]]:
    if not rows:
        return []
    use_rows = rows
    if run_only:
        run_rows = [r for r in rows if float(r.get("run", 0.0)) > 0.5]
        if run_rows:
            use_rows = run_rows
    if not use_rows:
        return []

    ticks = [float(r.get("tick", 0.0)) for r in use_rows]
    yaws = [float(r.get("y", 0.0)) for r in use_rows]
    yaw_unwrapped = _unwrap_yaw_deg(yaws)
    base_tick = ticks[0]
    base_yaw = yaw_unwrapped[0]

    samples: List[Dict[str, object]] = []
    for i, row in enumerate(use_rows):
        tick = float(row.get("tick", base_tick))
        t_ms = tick - base_tick
        t_s = t_ms / 1000.0
        y = float(row.get("y", 0.0))
        y_unwrap = float(yaw_unwrapped[i])
        y_delta = y_unwrap - base_yaw
        yr = float(row.get("yr", 0.0))
        el = float(row.get("el", 0.0))
        er = float(row.get("er", 0.0))
        ed = float(row.get("ed", el - er))
        target_left_pwm = float(row.get("L", 0.0))
        target_right_pwm = float(row.get("R", 0.0))
        left_pwm = float(row.get("OL", target_left_pwm))
        right_pwm = float(row.get("OR", target_right_pwm))
        heading_corr = float(row.get("hd", row.get("c", 0.0)))

        sample: Dict[str, object] = {
            "idx": i,
            "source": str(row.get("source", "")),
            "tick": tick,
            "t_ms": t_ms,
            "t_s": t_s,
            "run": float(row.get("run", 0.0)),
            "exp_id": float(row.get("exp_id", -1.0)),
            "ts": float(row.get("ts", row.get("spd", 0.0))),
            "yaw_deg": y,
            "yaw_unwrapped_deg": y_unwrap,
            "yaw_delta_deg": y_delta,
            "yaw_rate_deg_s": yr,
            "left_pwm": left_pwm,
            "right_pwm": right_pwm,
            "target_left_pwm": target_left_pwm,
            "target_right_pwm": target_right_pwm,
            "left_speed": el,
            "right_speed": er,
            "speed_diff": ed,
            "trim": float(row.get("trim", 0.0)),
            "auto_trim": float(row.get("at", 0.0)),
            "heading_corr": heading_corr,
            "heading_i": float(row.get("hi", 0.0)),
            "pwm_max": float(row.get("pmax", 0.0)),
            "diff_max": float(row.get("dmax", 0.0)),
            "pwm_command": float(row.get("pwm", 0.0)),
            "pwm_core": float(row.get("pc", 0.0)),
            "heading_diff": float(row.get("hd", 0.0)),
            "actual_angle": float(row.get("aa", 0.0)),
            "angle_error": float(row.get("ae", 0.0)),
            "angle_output": float(row.get("ao", 0.0)),
            "speed_error": float(row.get("se", 0.0)),
            "speed_output": float(row.get("so", 0.0)),
        }
        sample["wheel_bias"] = _label_signed(ed, 1.0, "left_faster", "right_faster", "balanced")
        sample["yaw_turn"] = _label_signed(y_delta, 0.8, "left_turn", "right_turn", "straight")
        sample["yaw_rate_turn"] = _label_signed(yr, 0.2, "left_turn", "right_turn", "straight")
        sample["wheel_slow_side"] = _label_signed(ed, 3.0, "right_slower", "left_slower", "balanced")
        if float(sample["heading_corr"]) >= (0.95 * HEADING_CORR_LIMIT):
            sample["heading_corr_state"] = "corr_pos_sat"
        elif float(sample["heading_corr"]) <= (-0.95 * HEADING_CORR_LIMIT):
            sample["heading_corr_state"] = "corr_neg_sat"
        else:
            sample["heading_corr_state"] = "corr_normal"
        samples.append(sample)
    return samples


def write_samples_csv(csv_path: str, samples: List[Dict[str, object]]) -> None:
    os.makedirs(os.path.dirname(csv_path) or ".", exist_ok=True)
    fields = [
        "idx",
        "source",
        "tick",
        "t_ms",
        "t_s",
        "run",
        "exp_id",
        "ts",
        "yaw_deg",
        "yaw_unwrapped_deg",
        "yaw_delta_deg",
        "yaw_rate_deg_s",
        "left_pwm",
        "right_pwm",
        "target_left_pwm",
        "target_right_pwm",
        "left_speed",
        "right_speed",
        "speed_diff",
        "trim",
        "auto_trim",
        "heading_corr",
        "heading_i",
        "pwm_max",
        "diff_max",
        "pwm_command",
        "pwm_core",
        "heading_diff",
        "actual_angle",
        "angle_error",
        "angle_output",
        "speed_error",
        "speed_output",
        "wheel_bias",
        "wheel_slow_side",
        "yaw_turn",
        "yaw_rate_turn",
        "heading_corr_state",
    ]
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for row in samples:
            w.writerow(row)


def analyze_raw(raw_path: str, export_csv_path: str = "", run_only: bool = True) -> Dict[str, object]:
    rows = parse_serial_rows(raw_path)
    cfg = parse_latest_cfg(raw_path)
    samples = build_samples(rows, run_only=run_only)
    if not samples:
        raise RuntimeError(f"No analyzable HB/STAT samples found in: {raw_path}")

    ts = [float(r["t_s"]) for r in samples]
    yaw_delta = [float(r["yaw_delta_deg"]) for r in samples]
    yaw_unwrapped = [float(r["yaw_unwrapped_deg"]) for r in samples]
    yr = [float(r["yaw_rate_deg_s"]) for r in samples]
    el = [float(r["left_speed"]) for r in samples]
    er = [float(r["right_speed"]) for r in samples]
    ed = [float(r["speed_diff"]) for r in samples]

    duration_s = ts[-1] if ts else 0.0
    yaw_slope = _linear_fit_slope(ts, yaw_unwrapped)
    ed_mean = _mean(ed)
    yr_mean = _mean(yr)

    wheel_bias = _label_signed(ed_mean, 1.0, "left_faster", "right_faster", "balanced")
    yaw_bias = _label_signed(yaw_delta[-1], 0.8, "left_turn", "right_turn", "straight")
    yaw_rate_bias = _label_signed(yr_mean, 0.2, "left_turn", "right_turn", "straight")

    left_faster_ratio = float(sum(1 for x in ed if x > 1.0)) / float(len(ed))
    right_faster_ratio = float(sum(1 for x in ed if x < -1.0)) / float(len(ed))
    left_turn_ratio = float(sum(1 for x in yr if x > 0.2)) / float(len(yr))
    right_turn_ratio = float(sum(1 for x in yr if x < -0.2)) / float(len(yr))
    drift_segments = _collect_segments(samples, "yaw_turn", "straight", min_samples=4)
    wheel_segments = _collect_segments(samples, "wheel_slow_side", "balanced", min_samples=4)
    corr_segments = _collect_segments(samples, "heading_corr_state", "corr_normal", min_samples=3)

    startup_window_s = 2.0 if duration_s >= 2.0 else duration_s
    startup_samples = [s for s in samples if float(s["t_s"]) <= startup_window_s]
    startup_end_yaw = float(startup_samples[-1]["yaw_delta_deg"]) if startup_samples else 0.0
    startup_peak_abs = max((abs(float(s["yaw_delta_deg"])) for s in startup_samples), default=0.0)
    startup_bias = _label_signed(startup_end_yaw, 2.0, "left_bias", "right_bias", "startup_straight")

    left_drift_dur = _sum_segment_durations(drift_segments, "left_turn")
    right_drift_dur = _sum_segment_durations(drift_segments, "right_turn")
    drift_hold_ratio = (max(left_drift_dur, right_drift_dur) / duration_s) if duration_s > 1e-9 else 0.0
    sustained_bias = "mixed"
    if left_drift_dur > right_drift_dur + 0.25:
        sustained_bias = "left_bias"
    elif right_drift_dur > left_drift_dur + 0.25:
        sustained_bias = "right_bias"
    elif drift_hold_ratio < 0.25:
        sustained_bias = "mostly_straight"

    longest_drift = _longest_segment(drift_segments)
    longest_drift_label = str(longest_drift.get("label", "none")) if longest_drift else "none"
    longest_drift_duration = float(longest_drift.get("duration_s", 0.0)) if longest_drift else 0.0

    corr_pos_sat_ratio = (_sum_segment_durations(corr_segments, "corr_pos_sat") / duration_s) if duration_s > 1e-9 else 0.0
    corr_neg_sat_ratio = (_sum_segment_durations(corr_segments, "corr_neg_sat") / duration_s) if duration_s > 1e-9 else 0.0
    corr_sat_ratio = corr_pos_sat_ratio + corr_neg_sat_ratio

    not_straight_forward = "no"
    if drift_hold_ratio >= 0.40 or abs(float(yaw_delta[-1])) >= 5.0 or abs(yaw_slope) >= 0.8 or corr_sat_ratio >= 0.08:
        not_straight_forward = "yes"

    csv_path = export_csv_path.strip() if export_csv_path else os.path.splitext(raw_path)[0] + "_samples.csv"
    write_samples_csv(csv_path, samples)

    return {
        "raw_path": raw_path,
        "csv_path": csv_path,
        "cfg": cfg,
        "rows_n": len(rows),
        "samples_n": len(samples),
        "duration_s": duration_s,
        "yaw_start_deg": yaw_unwrapped[0],
        "yaw_end_deg": yaw_unwrapped[-1],
        "yaw_delta_deg": yaw_delta[-1],
        "yaw_slope_deg_s": yaw_slope,
        "yaw_rate_mean_deg_s": yr_mean,
        "yaw_rate_rms_deg_s": _rms(yr),
        "left_speed_mean": _mean(el),
        "right_speed_mean": _mean(er),
        "speed_diff_mean": ed_mean,
        "speed_diff_rms": _rms(ed),
        "wheel_bias": wheel_bias,
        "yaw_bias": yaw_bias,
        "yaw_rate_bias": yaw_rate_bias,
        "left_faster_ratio": left_faster_ratio,
        "right_faster_ratio": right_faster_ratio,
        "left_turn_ratio": left_turn_ratio,
        "right_turn_ratio": right_turn_ratio,
        "drift_segments": drift_segments,
        "wheel_segments": wheel_segments,
        "corr_segments": corr_segments,
        "startup_window_s": startup_window_s,
        "startup_bias": startup_bias,
        "startup_end_yaw_deg": startup_end_yaw,
        "startup_peak_abs_yaw_deg": startup_peak_abs,
        "sustained_bias": sustained_bias,
        "bias_hold_ratio": drift_hold_ratio,
        "longest_drift_label": longest_drift_label,
        "longest_drift_duration_s": longest_drift_duration,
        "corr_pos_sat_ratio": corr_pos_sat_ratio,
        "corr_neg_sat_ratio": corr_neg_sat_ratio,
        "corr_sat_ratio": corr_sat_ratio,
        "not_straight_forward": not_straight_forward,
        "trim_last": float(samples[-1]["trim"]),
        "auto_trim_last": float(samples[-1]["auto_trim"]),
    }


def print_analysis(summary: Dict[str, object]) -> None:
    print(f"RAW: {summary['raw_path']}")
    print(f"CSV: {summary['csv_path']}")
    cfg = summary.get("cfg", {})
    if isinstance(cfg, dict) and cfg:
        parts: List[str] = []
        for key in ["exp_id", "ms", "ts", "so", "trim", "hp", "hd", "hs", "db", "hi", "hil", "kpp", "kpi", "kpd", "at", "at_kp", "at_ki", "at_lim", "pmax", "dmax", "min", "kp", "km", "ramp", "raw"]:
            if key in cfg:
                parts.append(f"{key}={cfg[key]}")
        if parts:
            print("CFG:", " ".join(parts))
    print(f"rows: {summary['rows_n']}  samples: {summary['samples_n']}  duration_s: {float(summary['duration_s']):.3f}")
    print(f"yaw start/end/delta(deg): {float(summary['yaw_start_deg']):.3f} / {float(summary['yaw_end_deg']):.3f} / {float(summary['yaw_delta_deg']):.3f}")
    print(f"yaw slope(deg/s): {float(summary['yaw_slope_deg_s']):.4f}  yaw_rate mean/rms(deg/s): {float(summary['yaw_rate_mean_deg_s']):.4f} / {float(summary['yaw_rate_rms_deg_s']):.4f}")
    print(f"left/right speed mean: {float(summary['left_speed_mean']):.3f} / {float(summary['right_speed_mean']):.3f}  ed mean/rms: {float(summary['speed_diff_mean']):.3f} / {float(summary['speed_diff_rms']):.3f}")
    print(f"wheel_bias: {summary['wheel_bias']}  yaw_bias: {summary['yaw_bias']}  yaw_rate_bias: {summary['yaw_rate_bias']}")
    print(f"left_faster_ratio: {float(summary['left_faster_ratio']):.3f}  right_faster_ratio: {float(summary['right_faster_ratio']):.3f}")
    print(f"left_turn_ratio: {float(summary['left_turn_ratio']):.3f}  right_turn_ratio: {float(summary['right_turn_ratio']):.3f}")
    print(
        f"startup bias: {summary['startup_bias']}  startup_end_yaw={float(summary['startup_end_yaw_deg']):.3f}deg  startup_peak_abs={float(summary['startup_peak_abs_yaw_deg']):.3f}deg  window={float(summary['startup_window_s']):.2f}s"
    )
    print(
        f"sustained bias: {summary['sustained_bias']}  hold_ratio={float(summary['bias_hold_ratio']):.3f}  longest={summary['longest_drift_label']}({float(summary['longest_drift_duration_s']):.3f}s)"
    )
    print(
        f"heading corr sat ratio pos/neg/all: {float(summary['corr_pos_sat_ratio']):.3f}/{float(summary['corr_neg_sat_ratio']):.3f}/{float(summary['corr_sat_ratio']):.3f}  not_straight_forward={summary['not_straight_forward']}"
    )
    print(f"trim_last: {float(summary['trim_last']):.4f}  auto_trim_last: {float(summary['auto_trim_last']):.4f}")
    for line in _format_segments("DRIFT SEGMENTS", summary.get("drift_segments", []), "deg"):
        print(line)
    for line in _format_segments("WHEEL SLOW SEGMENTS", summary.get("wheel_segments", []), ""):
        print(line)
    for line in _format_segments("HEADING CORR SAT SEGMENTS", summary.get("corr_segments", []), ""):
        print(line)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default="", help="raw txt path. empty=auto pick latest from 000Data")
    ap.add_argument("--data-dir", default=os.path.join(os.path.dirname(__file__), "000Data"))
    ap.add_argument("--csv", default="", help="output csv path. empty=derive from raw path")
    ap.add_argument("--all", action="store_true", help="analyze all HB/STAT samples, not only run=1 segment")
    args = ap.parse_args()

    raw_path = args.raw.strip() or find_latest_raw_txt(args.data_dir)
    if not raw_path:
        raise SystemExit("No raw txt found")

    summary = analyze_raw(raw_path, export_csv_path=args.csv, run_only=(not args.all))
    print_analysis(summary)


if __name__ == "__main__":
    main()
