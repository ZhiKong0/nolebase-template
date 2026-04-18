import argparse
import os
import statistics
from typing import Dict, List, Optional, Tuple

from trajectory_analyzer import parse_hb_rows


Event = Dict[str, float | int | str | bool | None]


def _mean(values: List[float]) -> float:
    return float(statistics.mean(values)) if values else 0.0


def _unwrap_yaw_deg(values: List[float]) -> List[float]:
    if not values:
        return []
    out = [float(values[0])]
    offset = 0.0
    prev = float(values[0])
    for raw in values[1:]:
        cur = float(raw)
        d = cur - prev
        if d > 180.0:
            offset -= 360.0
        elif d < -180.0:
            offset += 360.0
        out.append(cur + offset)
        prev = cur
    return out


def _quantile(values: List[float], q: float) -> float:
    if not values:
        return 0.0
    arr = sorted(float(v) for v in values)
    if len(arr) == 1:
        return arr[0]
    q = max(0.0, min(1.0, float(q)))
    idx = q * (len(arr) - 1)
    lo = int(idx)
    hi = min(lo + 1, len(arr) - 1)
    frac = idx - lo
    return arr[lo] * (1.0 - frac) + arr[hi] * frac


def _clip_run_rows(rows: List[Dict[str, float]], skip_s: float = 0.0, tail_s: float = 0.0) -> List[Dict[str, float]]:
    run_rows = [r for r in rows if float(r.get('run', 0.0)) > 0.5]
    if not run_rows:
        return []
    t0 = float(run_rows[0]['tick'])
    out = [r for r in run_rows if (float(r['tick']) - t0) >= skip_s * 1000.0]
    if tail_s > 0.0 and out:
        t1 = float(out[-1]['tick'])
        out = [r for r in out if (t1 - float(r['tick'])) <= tail_s * 1000.0]
    return out


def detect_events(
    rows: List[Dict[str, float]],
    start_yaw_deg: float,
    recover_yaw_deg: float,
    min_gap_s: float,
    max_recover_s: float,
    baseline_window: int,
) -> List[Event]:
    events: List[Event] = []
    if len(rows) < 3:
        return events

    yaw_unwrapped = _unwrap_yaw_deg([float(r['yaw_deg']) for r in rows])
    rel_yaws: List[float] = []
    for i, y in enumerate(yaw_unwrapped):
        j0 = max(0, i - max(1, int(baseline_window)))
        base = _mean(yaw_unwrapped[j0:i]) if i > j0 else y
        rel_yaws.append(float(y - base))

    min_gap_ms = float(min_gap_s) * 1000.0
    last_start_tick = -1e18
    i = 1
    while i < len(rows):
        prev_yaw = float(rel_yaws[i - 1])
        yaw = float(rel_yaws[i])
        sign = 0
        if prev_yaw < start_yaw_deg <= yaw:
            sign = 1
        elif prev_yaw > -start_yaw_deg >= yaw:
            sign = -1
        if sign == 0:
            i += 1
            continue

        start_tick = float(rows[i]['tick'])
        if start_tick - last_start_tick < min_gap_ms:
            i += 1
            continue

        j = i
        peak_abs_yaw = abs(yaw)
        peak_out_diff = abs(float(rows[i]['outL']) - float(rows[i]['outR']))
        peak_ed_abs = abs(float(rows[i]['ed']))
        peak_speed_gap = abs(abs(float(rows[i]['el'])) - abs(float(rows[i]['er'])))
        out_diffs: List[float] = []
        eds: List[float] = []
        speed_gaps: List[float] = []
        recovered = False
        recover_tick: Optional[float] = None

        while j < len(rows):
            row = rows[j]
            cur_yaw = float(rel_yaws[j])
            cur_out_diff = float(row['outL']) - float(row['outR'])
            cur_ed = float(row['ed'])
            cur_speed_gap = abs(abs(float(row['el'])) - abs(float(row['er'])))
            out_diffs.append(cur_out_diff)
            eds.append(cur_ed)
            speed_gaps.append(cur_speed_gap)
            peak_abs_yaw = max(peak_abs_yaw, abs(cur_yaw))
            peak_out_diff = max(peak_out_diff, abs(cur_out_diff))
            peak_ed_abs = max(peak_ed_abs, abs(cur_ed))
            peak_speed_gap = max(peak_speed_gap, cur_speed_gap)

            dt_s = (float(row['tick']) - start_tick) / 1000.0
            if sign * cur_yaw <= recover_yaw_deg:
                recovered = True
                recover_tick = float(row['tick'])
                break
            if dt_s > max_recover_s:
                break
            j += 1

        event: Event = {
            'sign': sign,
            'side': 'right_dev' if sign > 0 else 'left_dev',
            'start_tick': start_tick,
            'start_yaw': yaw,
            'start_yaw_raw': float(rows[i]['yaw_deg']),
            'peak_abs_yaw': peak_abs_yaw,
            'recovered': recovered,
            'recover_s': ((recover_tick - start_tick) / 1000.0) if (recovered and recover_tick is not None) else None,
            'mean_out_diff': _mean(out_diffs),
            'mean_out_diff_abs': _mean([abs(v) for v in out_diffs]),
            'peak_out_diff_abs': peak_out_diff,
            'mean_ed': _mean(eds),
            'mean_ed_abs': _mean([abs(v) for v in eds]),
            'peak_ed_abs': peak_ed_abs,
            'mean_speed_gap': _mean(speed_gaps),
            'peak_speed_gap': peak_speed_gap,
            'sample_n': len(out_diffs),
        }
        events.append(event)
        last_start_tick = start_tick
        i = max(j + 1, i + 1)
    return events


def summarize_events(events: List[Event], sign: int) -> Dict[str, float | int]:
    group = [e for e in events if int(e['sign']) == int(sign)]
    rec = [float(e['recover_s']) for e in group if e['recover_s'] is not None]
    out_abs = [float(e['mean_out_diff_abs']) for e in group]
    peak_out_abs = [float(e['peak_out_diff_abs']) for e in group]
    ed_abs = [float(e['mean_ed_abs']) for e in group]
    peak_ed_abs = [float(e['peak_ed_abs']) for e in group]
    spd_gap = [float(e['mean_speed_gap']) for e in group]
    peak_spd_gap = [float(e['peak_speed_gap']) for e in group]
    peaks_yaw = [float(e['peak_abs_yaw']) for e in group]
    return {
        'n': len(group),
        'recover_n': len(rec),
        'recover_mean_s': _mean(rec),
        'recover_p95_s': _quantile(rec, 0.95),
        'peak_yaw_mean_deg': _mean(peaks_yaw),
        'out_abs_mean': _mean(out_abs),
        'out_abs_p95': _quantile(peak_out_abs, 0.95),
        'ed_abs_mean': _mean(ed_abs),
        'ed_abs_p95': _quantile(peak_ed_abs, 0.95),
        'speed_gap_mean': _mean(spd_gap),
        'speed_gap_p95': _quantile(peak_spd_gap, 0.95),
    }


def print_summary(label: str, summary: Dict[str, float | int]) -> None:
    print(label)
    print(f"- events: {int(summary['n'])}")
    print(f"- recovered: {int(summary['recover_n'])}")
    print(f"- recover_mean_s: {float(summary['recover_mean_s']):.3f}")
    print(f"- recover_p95_s: {float(summary['recover_p95_s']):.3f}")
    print(f"- peak_yaw_mean_deg: {float(summary['peak_yaw_mean_deg']):.3f}")
    print(f"- out_abs_mean: {float(summary['out_abs_mean']):.3f}")
    print(f"- out_abs_p95: {float(summary['out_abs_p95']):.3f}")
    print(f"- ed_abs_mean: {float(summary['ed_abs_mean']):.3f}")
    print(f"- ed_abs_p95: {float(summary['ed_abs_p95']):.3f}")
    print(f"- speed_gap_mean: {float(summary['speed_gap_mean']):.3f}")
    print(f"- speed_gap_p95: {float(summary['speed_gap_p95']):.3f}")


def print_event_table(events: List[Event]) -> None:
    if not events:
        print('EVENTS: none')
        return
    print('EVENTS:')
    for idx, e in enumerate(events, start=1):
        recover_s = 'NA' if e['recover_s'] is None else f"{float(e['recover_s']):.3f}"
        print(
            f"- e{idx:02d} side={e['side']} start_yaw_rel={float(e['start_yaw']):.3f} raw={float(e['start_yaw_raw']):.3f} peak|yaw|={float(e['peak_abs_yaw']):.3f} "
            f"recover_s={recover_s} out_abs={float(e['mean_out_diff_abs']):.3f}/{float(e['peak_out_diff_abs']):.3f} "
            f"ed_abs={float(e['mean_ed_abs']):.3f}/{float(e['peak_ed_abs']):.3f} "
            f"spd_gap={float(e['mean_speed_gap']):.3f}/{float(e['peak_speed_gap']):.3f} n={int(e['sample_n'])}"
        )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', required=True, help='raw txt path')
    ap.add_argument('--start-yaw', type=float, default=1.0, help='event start threshold in deg')
    ap.add_argument('--recover-yaw', type=float, default=0.25, help='event recovery threshold in deg')
    ap.add_argument('--min-gap-s', type=float, default=0.8, help='minimum gap between two events')
    ap.add_argument('--max-recover-s', type=float, default=2.0, help='maximum time window for one recovery event')
    ap.add_argument('--baseline-window', type=int, default=8, help='number of previous samples used as local yaw baseline')
    ap.add_argument('--skip-s', type=float, default=0.5, help='skip first N seconds after run starts')
    ap.add_argument('--tail-s', type=float, default=0.0, help='only keep last N seconds of run')
    ap.add_argument('--show-events', action='store_true', help='print all detected events')
    args = ap.parse_args()

    raw_path = args.raw
    rows = parse_hb_rows(raw_path)
    if not rows:
        raise SystemExit(f'No HB rows parsed from: {raw_path}')

    run_rows = _clip_run_rows(rows, skip_s=float(args.skip_s), tail_s=float(args.tail_s))
    if not run_rows:
        raise SystemExit('No run rows after clipping')

    events = detect_events(
        run_rows,
        start_yaw_deg=float(args.start_yaw),
        recover_yaw_deg=float(args.recover_yaw),
        min_gap_s=float(args.min_gap_s),
        max_recover_s=float(args.max_recover_s),
        baseline_window=int(args.baseline_window),
    )

    print(f'RAW: {raw_path}')
    print(f'FILE: {os.path.basename(raw_path)}')
    print(f'RUN_ROWS: {len(run_rows)}')
    print(f'EVENT_TOTAL: {len(events)}')
    print_summary('RIGHT_DEVIATION', summarize_events(events, 1))
    print_summary('LEFT_DEVIATION', summarize_events(events, -1))

    if args.show_events:
        print_event_table(events)


if __name__ == '__main__':
    main()
