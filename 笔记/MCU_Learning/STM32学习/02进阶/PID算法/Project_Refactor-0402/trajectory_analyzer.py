import os
import argparse
import math
import re
import statistics
from typing import Dict, List, Optional, Tuple

try:
    import matplotlib.pyplot as plt  # type: ignore
except Exception:
    plt = None


def _parse_value(text: str) -> Optional[float]:
    try:
        if text.lower().startswith("0x"):
            return float(int(text, 16))
        return float(text)
    except Exception:
        return None


def _is_clean_hb_stat_line(line: str) -> bool:
    s = line.strip()
    if s.startswith("HB "):
        prefix = "HB "
    elif s.startswith("STAT "):
        prefix = "STAT "
    else:
        return False
    if not s.startswith(f"{prefix}tick="):
        return False
    tail = s[len(prefix):]
    if "HB " in tail or "STAT " in tail:
        return False
    if "CMD " in s or "RES " in s or "TRACE " in s:
        return False
    if s.count("=") < 12:
        return False
    return True


def _parse_kv_line(line: str) -> Dict[str, float]:
    if not _is_clean_hb_stat_line(line):
        return {}
    out: Dict[str, float] = {}
    for token in line.strip().split()[1:]:
        if "=" not in token:
            continue
        k, v = token.split("=", 1)
        num = _parse_value(v)
        if num is not None:
            out[k] = num
    return out


def _extract_hb_stat_candidates(line: str) -> List[str]:
    s = line.strip()
    if not s:
        return []
    matches = list(re.finditer(r"(?:HB|STAT)\s+tick=", s))
    if not matches:
        return [s]
    out: List[str] = []
    for i, m in enumerate(matches):
        start = m.start()
        end = matches[i + 1].start() if (i + 1) < len(matches) else len(s)
        chunk = s[start:end].strip()
        if chunk:
            out.append(chunk)
    return out


def _mean_abs(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return float(sum(abs(float(x)) for x in xs)) / float(len(xs))


def _yaw_delta_deg(yaws: List[float]) -> float:
    if len(yaws) < 2:
        return 0.0
    yaws_unwrapped = _unwrap_yaw_deg([float(y) for y in yaws])
    if len(yaws_unwrapped) < 2:
        return 0.0
    return float(yaws_unwrapped[-1] - yaws_unwrapped[0])


def _lateral_side_eps(path_len: float) -> float:
    return max(0.10, min(0.80, 0.02 * abs(float(path_len))))


def _trajectory_side_eps(path_len: float, lateral_abs_max: float) -> float:
    return max(0.10, min(1.00, 0.01 * abs(float(path_len)) + 0.05 * abs(float(lateral_abs_max))))


def _side_label_from_value(value: float, eps: float) -> str:
    if float(value) > float(eps):
        return "right"
    if float(value) < -float(eps):
        return "left"
    return "balanced"


def _turn_state_from_motion(
    yaw_delta: float,
    local_y: float,
    path_len: float,
    motion_level: str,
) -> Tuple[str, float, float, float]:
    yaw_eps = 3.0
    y_eps = _lateral_side_eps(path_len)
    if motion_level == "no_motion":
        return "straight", 0.0, yaw_eps, y_eps
    if motion_level == "barely_moving":
        yaw_eps *= 1.5
        y_eps *= 1.5
    elif motion_level == "slow_crawl":
        yaw_eps *= 1.2
        y_eps *= 1.2

    path_ref = max(abs(float(path_len)), 1e-9)
    y_ratio = abs(float(local_y)) / path_ref
    right_by_yaw = float(yaw_delta) > yaw_eps and float(local_y) >= -y_eps
    left_by_yaw = float(yaw_delta) < -yaw_eps and float(local_y) <= y_eps
    right_by_lateral = float(local_y) > y_eps and y_ratio > 0.02
    left_by_lateral = float(local_y) < -y_eps and y_ratio > 0.02

    turn_conf = 0.0
    if right_by_yaw or right_by_lateral:
        turn_conf = min(1.0, max(abs(float(yaw_delta)) / yaw_eps, abs(float(local_y)) / y_eps, y_ratio / 0.02) - 1.0)
        return "right_arc", turn_conf, yaw_eps, y_eps
    if left_by_yaw or left_by_lateral:
        turn_conf = min(1.0, max(abs(float(yaw_delta)) / yaw_eps, abs(float(local_y)) / y_eps, y_ratio / 0.02) - 1.0)
        return "left_arc", turn_conf, yaw_eps, y_eps
    return "straight", 0.0, yaw_eps, y_eps


def reconstruct_path_from_outputs(
    rows: List[Dict[str, float]],
    speed_scale: float,
) -> Tuple[List[float], List[float], List[float], List[float]]:
    if len(rows) < 2:
        return [0.0], [0.0], [0.0], [0.0]

    xs: List[float] = [0.0]
    ys: List[float] = [0.0]
    ss: List[float] = [0.0]
    kappas: List[float] = [0.0]

    x = 0.0
    y = 0.0
    s = 0.0
    prev_theta = float(rows[0]["yaw_rad"])
    for i in range(1, len(rows)):
        a = rows[i - 1]
        b = rows[i]
        dt_ms = float(b["tick"] - a["tick"])
        if dt_ms <= 0:
            xs.append(x)
            ys.append(y)
            ss.append(s)
            kappas.append(0.0)
            continue

        theta = float(b["yaw_rad"])
        v_rel = 0.5 * (float(b["outL"]) + float(b["outR"]))
        ds = v_rel * (dt_ms / 1000.0) * float(speed_scale)
        dtheta = theta - prev_theta
        if dtheta > math.pi:
            dtheta -= 2.0 * math.pi
        elif dtheta < -math.pi:
            dtheta += 2.0 * math.pi
        x += ds * math.cos(theta)
        y += ds * math.sin(theta)
        s += abs(ds)
        kappa = (dtheta / ds) if abs(ds) > 1e-9 else 0.0

        xs.append(x)
        ys.append(y)
        ss.append(s)
        kappas.append(kappa)
        prev_theta = theta

    return xs, ys, ss, kappas


def segment_rows(rows: List[Dict[str, float]], segment_s: float) -> List[List[Dict[str, float]]]:
    if not rows:
        return []
    if segment_s <= 0.0:
        return [rows]

    out: List[List[Dict[str, float]]] = []
    cur: List[Dict[str, float]] = [rows[0]]
    t0 = float(rows[0]["tick"])
    for r in rows[1:]:
        if (float(r["tick"]) - t0) >= segment_s * 1000.0 and len(cur) >= 2:
            out.append(cur)
            cur = [r]
            t0 = float(r["tick"])
        else:
            cur.append(r)
    if cur:
        out.append(cur)
    return out


def summarize_segment(seg: List[Dict[str, float]], speed_scale: float) -> Dict[str, float]:
    ticks = [float(r["tick"]) for r in seg]
    yaws = [float(r["yaw_deg"]) for r in seg]
    yrs = [float(r["yr"]) for r in seg]
    eds = [float(r["ed"]) for r in seg]
    out_diffs = [float(r["outL"] - r["outR"]) for r in seg]
    xs, ys, ss, kappas = reconstruct_path_from_outputs(seg, speed_scale)
    yaw_unwrapped = _unwrap_yaw_deg(yaws)
    t_s = [(t - ticks[0]) / 1000.0 for t in ticks] if ticks else []

    return {
        "t0_s": 0.0,
        "t1_s": (ticks[-1] - ticks[0]) / 1000.0 if len(ticks) >= 2 else 0.0,
        "yaw_span_deg": (max(yaws) - min(yaws)) if yaws else 0.0,
        "yaw_drift_deg_s": _linear_fit_slope(t_s, yaw_unwrapped) if t_s and yaw_unwrapped else 0.0,
        "yr_rms": _rms(yrs),
        "ed_mean": _mean(eds),
        "ed_rms": _rms(eds),
        "out_diff_mean": _mean(out_diffs),
        "out_diff_abs_mean": _mean_abs(out_diffs),
        "lateral_final": ys[-1] if ys else 0.0,
        "forward_final": xs[-1] if xs else 0.0,
        "path_len": ss[-1] if ss else 0.0,
        "curvature_rms": _rms(kappas),
    }


def summarize_second_segments(seg: List[Dict[str, float]], speed_scale: float) -> Dict[str, float | str]:
    ticks = [float(r["tick"]) for r in seg]
    yaws = [float(r["yaw_deg"]) for r in seg]
    yrs = [float(r["yr"]) for r in seg]
    els = [float(r["el"]) for r in seg]
    ers = [float(r["er"]) for r in seg]
    out_ls = [float(r["outL"]) for r in seg]
    out_rs = [float(r["outR"]) for r in seg]
    pwms = [float(r.get("pwm", 0.0)) for r in seg]
    pcs = [float(r.get("pc", 0.0)) for r in seg]
    hds = [float(r.get("hd", 0.0)) for r in seg]
    aas = [float(r.get("aa", 0.0)) for r in seg]
    aes = [float(r.get("ae", 0.0)) for r in seg]
    aos = [float(r.get("ao", 0.0)) for r in seg]
    ses = [float(r.get("se", 0.0)) for r in seg]
    sos = [float(r.get("so", 0.0)) for r in seg]
    xs, ys, _ = reconstruct_path(seg, speed_scale)
    ss = reconstruct_s_along_path(seg, speed_scale)
    motion = summarize_motion_state(seg, (ticks[-1] - ticks[0]) / 1000.0 if len(ticks) >= 2 else 0.0)
    eds = [float(r["ed"]) for r in seg]
    out_diffs = [a - b for a, b in zip(out_ls, out_rs)]
    yaw_delta = _yaw_delta_deg(yaws)
    path_len = ss[-1] if ss else 0.0
    motion_level = str(motion["motion_level"])
    side_eps = _lateral_side_eps(path_len)

    side = _side_label_from_value(ys[-1] if ys else 0.0, side_eps)

    return {
        "t0_s": 0.0,
        "t1_s": (ticks[-1] - ticks[0]) / 1000.0 if len(ticks) >= 2 else 0.0,
        "x_final": xs[-1] if xs else 0.0,
        "y_final": ys[-1] if ys else 0.0,
        "path_len": path_len,
        "yaw_start": yaws[0] if yaws else 0.0,
        "yaw_end": yaws[-1] if yaws else 0.0,
        "yaw_delta": yaw_delta,
        "yr_rms": _rms(yrs),
        "el_mean": _mean(els),
        "er_mean": _mean(ers),
        "el_abs_mean": _mean_abs(els),
        "er_abs_mean": _mean_abs(ers),
        "ed_mean": _mean(eds),
        "ed_rms": _rms(eds),
        "outL_mean": _mean(out_ls),
        "outR_mean": _mean(out_rs),
        "out_diff_mean": _mean(out_diffs),
        "pwm_mean": _mean(pwms),
        "pwm_end": pwms[-1] if pwms else 0.0,
        "pc_mean": _mean(pcs),
        "pc_end": pcs[-1] if pcs else 0.0,
        "hd_mean": _mean(hds),
        "hd_end": hds[-1] if hds else 0.0,
        "ts_mean": _mean(tss),
        "ts_end": tss[-1] if tss else 0.0,
        "as_mean": _mean(ass),
        "as_end": ass[-1] if ass else 0.0,
        "aa_mean": _mean(aas),
        "aa_end": aas[-1] if aas else 0.0,
        "ae_mean": _mean(aes),
        "ae_end": aes[-1] if aes else 0.0,
        "ao_mean": _mean(aos),
        "ao_end": aos[-1] if aos else 0.0,
        "se_mean": _mean(ses),
        "se_end": ses[-1] if ses else 0.0,
        "so_mean": _mean(sos),
        "so_end": sos[-1] if sos else 0.0,
        "enc_active_ratio": float(motion["enc_active_ratio"]),
        "enc_nonzero_ratio": float(motion["enc_nonzero_ratio"]),
        "speed_proxy": float(motion["speed_proxy"]),
        "motion_level": motion_level,
        "stiction": float(motion["stiction_suspected"]),
        "side_eps": side_eps,
        "side": side,
    }


def summarize_time_window(
    seg: List[Dict[str, float]],
    speed_scale: float,
    seg_index: int,
    global_tick0: float,
    x_offset: float,
    y_offset: float,
) -> Dict[str, float | str]:
    ticks = [float(r["tick"]) for r in seg]
    yaws = [float(r["yaw_deg"]) for r in seg]
    yrs = [float(r["yr"]) for r in seg]
    els = [float(r["el"]) for r in seg]
    ers = [float(r["er"]) for r in seg]
    eds = [float(r["ed"]) for r in seg]
    out_ls = [float(r.get("outL", r.get("L", 0.0))) for r in seg]
    out_rs = [float(r.get("outR", r.get("R", 0.0))) for r in seg]
    out_diffs = [a - b for a, b in zip(out_ls, out_rs)]
    pwms = [float(r.get("pwm", 0.0)) for r in seg]
    pcs = [float(r.get("pc", 0.0)) for r in seg]
    hds = [float(r.get("hd", 0.0)) for r in seg]
    tss = [float(r.get("ts", 0.0)) for r in seg]
    ass = [float(r.get("as", 0.0)) for r in seg]
    aas = [float(r.get("aa", 0.0)) for r in seg]
    aes = [float(r.get("ae", 0.0)) for r in seg]
    aos = [float(r.get("ao", 0.0)) for r in seg]
    ses = [float(r.get("se", 0.0)) for r in seg]
    sos = [float(r.get("so", 0.0)) for r in seg]
    xs, ys, _ = reconstruct_path(seg, speed_scale)
    local_x = xs[-1] if xs else 0.0
    local_y = ys[-1] if ys else 0.0
    path_len = _path_arc_len(xs, ys)
    duration_s = (ticks[-1] - ticks[0]) / 1000.0 if len(ticks) >= 2 else 0.0
    motion = summarize_motion_state(seg, duration_s)
    motion_level = str(motion.get("motion_level", "unknown"))
    yaw_delta = _yaw_delta_deg(yaws)
    ed_mean = _mean(eds)
    ed_rms = _rms(eds)
    yr_mean = _mean(yrs)
    yr_rms = _rms(yrs)
    wheel_bias = "balanced"
    if ed_mean > 1.0:
        wheel_bias = "left_faster"
    elif ed_mean < -1.0:
        wheel_bias = "right_faster"
    turn_state, turn_conf, turn_yaw_eps, turn_y_eps = _turn_state_from_motion(yaw_delta, local_y, path_len, motion_level)
    state = turn_state
    pause_suspected = float(motion.get("pause_suspected", 0.0))
    output_idle_streak_s = float(motion.get("output_idle_streak_s", 0.0))
    if motion_level == "no_motion":
        if float(motion.get("stiction_suspected", 0.0)) >= 0.5 or output_idle_streak_s >= 0.12:
            state = "stalled"
        else:
            state = "paused"
    elif motion_level == "barely_moving":
        if pause_suspected >= 0.5 or output_idle_streak_s >= 0.12:
            state = f"restart_after_pause_{turn_state}"
        else:
            state = f"hesitating_{turn_state}"
    elif motion_level == "slow_crawl":
        state = f"slow_{turn_state}"
    elif pause_suspected >= 0.5:
        state = f"pause_recovering_{turn_state}"
    return {
        "seg_index": float(seg_index),
        "t0_s": (ticks[0] - global_tick0) / 1000.0 if ticks else 0.0,
        "t1_s": (ticks[-1] - global_tick0) / 1000.0 if ticks else 0.0,
        "duration_s": duration_s,
        "yaw_start": yaws[0] if yaws else 0.0,
        "yaw_end": yaws[-1] if yaws else 0.0,
        "yaw_delta": yaw_delta,
        "yr_mean": yr_mean,
        "yr_rms": yr_rms,
        "el_mean": _mean(els),
        "er_mean": _mean(ers),
        "el_abs_mean": _mean_abs(els),
        "er_abs_mean": _mean_abs(ers),
        "ed_mean": ed_mean,
        "ed_rms": ed_rms,
        "el_end": els[-1] if els else 0.0,
        "er_end": ers[-1] if ers else 0.0,
        "ed_end": eds[-1] if eds else 0.0,
        "outL_mean": _mean(out_ls),
        "outR_mean": _mean(out_rs),
        "out_diff_mean": _mean(out_diffs),
        "outL_end": out_ls[-1] if out_ls else 0.0,
        "outR_end": out_rs[-1] if out_rs else 0.0,
        "pwm_mean": _mean(pwms),
        "pwm_end": pwms[-1] if pwms else 0.0,
        "pc_mean": _mean(pcs),
        "pc_end": pcs[-1] if pcs else 0.0,
        "hd_mean": _mean(hds),
        "hd_end": hds[-1] if hds else 0.0,
        "aa_mean": _mean(aas),
        "aa_end": aas[-1] if aas else 0.0,
        "ae_mean": _mean(aes),
        "ae_end": aes[-1] if aes else 0.0,
        "ao_mean": _mean(aos),
        "ao_end": aos[-1] if aos else 0.0,
        "se_mean": _mean(ses),
        "se_end": ses[-1] if ses else 0.0,
        "so_mean": _mean(sos),
        "so_end": sos[-1] if sos else 0.0,
        "x_local": local_x,
        "y_local": local_y,
        "path_len": path_len,
        "x_total_start": x_offset,
        "y_total_start": y_offset,
        "x_total_end": x_offset + local_x,
        "y_total_end": y_offset + local_y,
        "motion_level": motion_level,
        "wheel_bias": wheel_bias,
        "turn_state": turn_state,
        "turn_conf": turn_conf,
        "turn_yaw_eps": turn_yaw_eps,
        "turn_y_eps": turn_y_eps,
        "state": state,
        "enc_active_ratio": float(motion.get("enc_active_ratio", 0.0)),
        "enc_nonzero_ratio": float(motion.get("enc_nonzero_ratio", 0.0)),
        "out_nonzero_ratio": float(motion.get("out_nonzero_ratio", 0.0)),
        "out_strong_ratio": float(motion.get("out_strong_ratio", 0.0)),
        "output_only_ratio": float(motion.get("output_only_ratio", 0.0)),
        "speed_proxy": float(motion.get("speed_proxy", 0.0)),
        "idle_streak_s": float(motion.get("idle_streak_s", 0.0)),
        "output_idle_streak_s": float(motion.get("output_idle_streak_s", 0.0)),
        "pause_suspected": float(motion.get("pause_suspected", 0.0)),
        "stiction": float(motion.get("stiction_suspected", 0.0)),
    }


def build_time_windows(
    run_segments: List[List[Dict[str, float]]],
    speed_scale: float,
    window_s: float,
) -> List[Dict[str, float | str]]:
    if not run_segments:
        return []
    global_tick0 = float(run_segments[0][0]["tick"])
    x_offset = 0.0
    y_offset = 0.0
    out: List[Dict[str, float | str]] = []
    for seg_index, run_seg in enumerate(run_segments, start=1):
        for win in segment_rows(run_seg, window_s):
            if len(win) < 2:
                continue
            summary = summarize_time_window(win, speed_scale, seg_index, global_tick0, x_offset, y_offset)
            out.append(summary)
            x_offset = float(summary["x_total_end"])
            y_offset = float(summary["y_total_end"])
    return out


def build_total_trajectory(run_segments: List[List[Dict[str, float]]], speed_scale: float) -> Dict[str, object]:
    if not run_segments:
        return {
            "run_segment_n": 0,
            "point_n": 1,
            "xs": [0.0],
            "ys": [0.0],
            "final_x": 0.0,
            "final_y": 0.0,
            "path_len": 0.0,
            "direct_len": 0.0,
            "sinuosity": 1.0,
            "lateral_abs_max": 0.0,
            "mean_y": 0.0,
            "mean_abs_y": 0.0,
            "final_y_per_path": 0.0,
            "y_pos_ratio": 0.0,
            "y_neg_ratio": 0.0,
            "bbox_w": 0.0,
            "bbox_h": 0.0,
            "heading_change": 0.0,
            "heading_variation": 0.0,
            "dominant_side": "balanced",
            "dominant_conf": 0.0,
            "segments": [],
        }
    xs_all: List[float] = [0.0]
    ys_all: List[float] = [0.0]
    x_offset = 0.0
    y_offset = 0.0
    path_len = 0.0
    heading_change = 0.0
    heading_variation = 0.0
    seg_infos: List[Dict[str, float]] = []
    tick0 = float(run_segments[0][0]["tick"])
    for seg_index, run_seg in enumerate(run_segments, start=1):
        xs, ys, _ = reconstruct_path(run_seg, speed_scale)
        local_len = _path_arc_len(xs, ys)
        yaws = _unwrap_yaw_deg([float(r["yaw_deg"]) for r in run_seg])
        heading_change += (yaws[-1] - yaws[0]) if len(yaws) >= 2 else 0.0
        heading_variation += float(sum(abs(yaws[i] - yaws[i - 1]) for i in range(1, len(yaws)))) if len(yaws) >= 2 else 0.0
        for i in range(1, len(xs)):
            xs_all.append(x_offset + float(xs[i]))
            ys_all.append(y_offset + float(ys[i]))
        x_offset += float(xs[-1]) if xs else 0.0
        y_offset += float(ys[-1]) if ys else 0.0
        path_len += local_len
        seg_infos.append(
            {
                "seg_index": float(seg_index),
                "t0_s": (float(run_seg[0]["tick"]) - tick0) / 1000.0,
                "t1_s": (float(run_seg[-1]["tick"]) - tick0) / 1000.0,
                "x_end": x_offset,
                "y_end": y_offset,
                "path_len": local_len,
            }
        )
    direct_len = math.sqrt(x_offset * x_offset + y_offset * y_offset)
    sinuosity = (path_len / max(1e-9, direct_len)) if path_len > 1e-9 else 1.0
    lateral_abs_max = max((abs(float(y)) for y in ys_all), default=0.0)
    x_min = min(xs_all) if xs_all else 0.0
    x_max = max(xs_all) if xs_all else 0.0
    y_min = min(ys_all) if ys_all else 0.0
    y_max = max(ys_all) if ys_all else 0.0
    side_eps = _trajectory_side_eps(path_len, lateral_abs_max)
    pos_y, neg_y, zero_y = _sign_vote(ys_all, eps=side_eps)
    mean_y = _mean(ys_all) if ys_all else 0.0
    mean_abs_y = _mean([abs(float(y)) for y in ys_all]) if ys_all else 0.0
    final_y_per_path = (y_offset / path_len) if path_len > 1e-9 else 0.0
    y_tot = pos_y + neg_y + zero_y
    y_pos_ratio = (float(pos_y) / float(y_tot)) if y_tot > 0 else 0.0
    y_neg_ratio = (float(neg_y) / float(y_tot)) if y_tot > 0 else 0.0
    y_zero_ratio = (float(zero_y) / float(y_tot)) if y_tot > 0 else 0.0
    dominant_side = _dominant_side_label(pos_y, neg_y)
    dominant_conf = _dominant_confidence(pos_y, neg_y)
    bias_ratio = abs(final_y_per_path)
    bias_severity = _bias_severity_label(bias_ratio)
    stability = _stability_label(sinuosity, heading_variation, lateral_abs_max)
    reuse_recommendation = _reuse_recommendation(stability, bias_severity)
    return {
        "run_segment_n": len(run_segments),
        "point_n": len(xs_all),
        "xs": xs_all,
        "ys": ys_all,
        "final_x": x_offset,
        "final_y": y_offset,
        "path_len": path_len,
        "direct_len": direct_len,
        "sinuosity": sinuosity,
        "lateral_abs_max": lateral_abs_max,
        "mean_y": mean_y,
        "mean_abs_y": mean_abs_y,
        "final_y_per_path": final_y_per_path,
        "y_pos_ratio": y_pos_ratio,
        "y_neg_ratio": y_neg_ratio,
        "y_zero_ratio": y_zero_ratio,
        "bias_ratio": bias_ratio,
        "bias_severity": bias_severity,
        "trajectory_side_eps": side_eps,
        "bbox_w": x_max - x_min,
        "bbox_h": y_max - y_min,
        "heading_change": heading_change,
        "heading_variation": heading_variation,
        "stability": stability,
        "reuse_recommendation": reuse_recommendation,
        "dominant_side": dominant_side,
        "dominant_conf": dominant_conf,
        "segments": seg_infos,
    }


def _mean(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return float(sum(xs)) / float(len(xs))


def _quantile(xs: List[float], q: float) -> float:
    if not xs:
        return 0.0
    ys = sorted(float(x) for x in xs)
    if q <= 0.0:
        return ys[0]
    if q >= 1.0:
        return ys[-1]
    pos = q * float(len(ys) - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return ys[lo]
    w = pos - float(lo)
    return ys[lo] * (1.0 - w) + ys[hi] * w


def _corr(xs: List[float], ys: List[float]) -> float:
    if not xs or not ys or len(xs) != len(ys) or len(xs) < 2:
        return 0.0
    mx = _mean(xs)
    my = _mean(ys)
    sx = 0.0
    sy = 0.0
    sxy = 0.0
    for a, b in zip(xs, ys):
        dx = float(a) - mx
        dy = float(b) - my
        sx += dx * dx
        sy += dy * dy
        sxy += dx * dy
    if sx <= 1e-12 or sy <= 1e-12:
        return 0.0
    return sxy / math.sqrt(sx * sy)


def _window_endpoint_slopes(x: List[float], y: List[float], win_s: float) -> List[float]:
    if len(x) < 3 or len(y) < 3:
        return []
    out: List[float] = []
    j = 0
    for i in range(len(x)):
        while j + 1 < len(x) and (float(x[j + 1]) - float(x[i])) <= float(win_s):
            j += 1
        if j > i:
            dx = float(x[j]) - float(x[i])
            if dx > 1e-9:
                out.append((float(y[j]) - float(y[i])) / dx)
    return out


def _window_endpoint_pairs(x: List[float], y: List[float], win_s: float) -> List[Tuple[float, float]]:
    if len(x) < 3 or len(y) < 3:
        return []
    out: List[Tuple[float, float]] = []
    j = 0
    for i in range(len(x)):
        while j + 1 < len(x) and (float(x[j + 1]) - float(x[i])) <= float(win_s):
            j += 1
        if j > i:
            dx = float(x[j]) - float(x[i])
            if dx > 1e-9:
                dy = float(y[j]) - float(y[i])
                out.append((dx, dy))
    return out


def _first_stuck_index(rows: List[Dict[str, float]], ys_out: List[float], s_out: List[float]) -> int:
    if len(rows) < 20 or len(ys_out) != len(rows) or len(s_out) != len(rows):
        return len(rows)
    for i in range(12, len(rows) - 6):
        window = rows[i:i + 6]
        ds = float(s_out[i + 6] - s_out[i])
        dyaw = abs(float(rows[i + 6]["yaw_deg"] - rows[i]["yaw_deg"]))
        out_abs = _mean_abs([float(r["outL"] - r["outR"]) for r in window])
        zero_out_count = sum(1 for r in window if abs(float(r["outL"])) < 0.5 and abs(float(r["outR"])) < 0.5)
        trim_zero_count = sum(1 for r in window if abs(float(r.get("trim", 0.0))) < 1e-6)
        enc_low_count = sum(1 for r in window if (abs(float(r["el"])) + abs(float(r["er"]))) < 20.0)
        abnormal_row_count = sum(
            1
            for r in window
            if (
                (abs(float(r["outL"])) < 0.5 and abs(float(r["outR"])) < 0.5)
                or abs(float(r.get("trim", 0.0))) < 1e-6
                or (abs(float(r["el"])) + abs(float(r["er"]))) < 20.0
            )
        )
        severe_signature = abnormal_row_count >= 2 and (
            zero_out_count >= 1
            or trim_zero_count >= 2
            or enc_low_count >= 3
        )
        if ds < 0.035 and dyaw > 8.0 and severe_signature:
            return i
        if ds < 0.050 and out_abs > 8.0 and severe_signature:
            return i
    return len(rows)


def _empty_side_bias() -> Dict[str, float | str]:
    return {
        "y25": 0.0,
        "y50": 0.0,
        "y75": 0.0,
        "y100": 0.0,
        "y_side": "balanced",
        "slope_side": "balanced",
        "y_conf": 0.0,
        "slope_conf": 0.0,
        "y_pos": 0.0,
        "y_neg": 0.0,
        "y_zero": 0.0,
        "s_pos": 0.0,
        "s_neg": 0.0,
        "s_zero": 0.0,
    }


def _last_nonzero_value(rows: List[Dict[str, float]], key: str) -> float:
    if not rows:
        return 0.0
    for row in reversed(rows):
        v = float(row.get(key, 0.0))
        if abs(v) > 1e-9:
            return v
    return float(rows[-1].get(key, 0.0))


def _find_first_yaw_jump(rows: List[Dict[str, float]], jump_deg: float = 90.0) -> Dict[str, float | bool | int]:
    if len(rows) < 2:
        return {"idx": len(rows), "delta_deg": 0.0, "detected": False}
    for i in range(1, len(rows)):
        prev = rows[i - 1]
        cur = rows[i]
        dy = float(cur["yaw_deg"] - prev["yaw_deg"])
        if abs(dy) >= float(jump_deg):
            return {
                "idx": i,
                "detected": True,
                "delta_deg": dy,
                "prev_yaw": float(prev["yaw_deg"]),
                "cur_yaw": float(cur["yaw_deg"]),
                "prev_yr": float(prev["yr"]),
                "cur_yr": float(cur["yr"]),
                "prev_outL": float(prev["outL"]),
                "prev_outR": float(prev["outR"]),
                "cur_outL": float(cur["outL"]),
                "cur_outR": float(cur["outR"]),
                "prev_pwm": float(prev.get("pwm", 0.0)),
                "cur_pwm": float(cur.get("pwm", 0.0)),
                "prev_pc": float(prev.get("pc", 0.0)),
                "cur_pc": float(cur.get("pc", 0.0)),
                "prev_hd": float(prev.get("hd", 0.0)),
                "cur_hd": float(cur.get("hd", 0.0)),
                "prev_aa": float(prev.get("aa", 0.0)),
                "cur_aa": float(cur.get("aa", 0.0)),
                "prev_ae": float(prev.get("ae", 0.0)),
                "cur_ae": float(cur.get("ae", 0.0)),
                "prev_ao": float(prev.get("ao", 0.0)),
                "cur_ao": float(cur.get("ao", 0.0)),
                "prev_ed": float(prev["ed"]),
                "cur_ed": float(cur["ed"]),
                "prev_trim": float(prev["trim"]),
                "cur_trim": float(cur["trim"]),
                "prev_zero_out": bool(abs(float(prev["outL"])) < 0.5 and abs(float(prev["outR"])) < 0.5),
                "cur_zero_out": bool(abs(float(cur["outL"])) < 0.5 and abs(float(cur["outR"])) < 0.5),
            }
    return {"idx": len(rows), "delta_deg": 0.0, "detected": False}


def _micro_drift_signature(run_t_s: List[float], ys_out: List[float]) -> Dict[str, float | str]:
    pairs = _window_endpoint_pairs(run_t_s, ys_out, 0.20) if run_t_s and len(run_t_s) == len(ys_out) else []
    micro_slopes = [(dy / dx) for dx, dy in pairs if dx > 1e-9]
    micro_deltas = [dy for _dx, dy in pairs]
    pos_s, neg_s, _ = _sign_vote(micro_slopes, eps=0.002)
    pos_d, neg_d, _ = _sign_vote(micro_deltas, eps=0.0008)
    return {
        "micro_slope_mean": _mean(micro_slopes) if micro_slopes else 0.0,
        "micro_slope_abs_p95": _quantile([abs(v) for v in micro_slopes], 0.95) if micro_slopes else 0.0,
        "micro_delta_mean": _mean(micro_deltas) if micro_deltas else 0.0,
        "micro_delta_abs_p95": _quantile([abs(v) for v in micro_deltas], 0.95) if micro_deltas else 0.0,
        "micro_side": _dominant_side_label(pos_d, neg_d),
        "micro_conf": _dominant_confidence(pos_d, neg_d),
        "micro_slope_side": _dominant_side_label(pos_s, neg_s),
        "micro_slope_conf": _dominant_confidence(pos_s, neg_s),
    }


def summarize_phase_drift(rows: List[Dict[str, float]], speed_scale: float) -> Dict[str, object]:
    if len(rows) < 6:
        return {
            "phases": [],
            "trend_side": "balanced",
            "trend_conf": 0.0,
            "phase_shift": "none",
            "late_drift_strength": 0.0,
        }

    n = len(rows)
    i1 = max(2, n // 3)
    i2 = max(i1 + 2, (2 * n) // 3)
    if i2 >= n:
        i2 = n - 1

    phase_specs = [
        ("early", rows[:i1]),
        ("mid", rows[i1:i2]),
        ("late", rows[i2:]),
    ]
    phases: List[Dict[str, float | str]] = []
    y_ends: List[float] = []

    for name, seg in phase_specs:
        if len(seg) < 2:
            continue
        ticks = [float(r["tick"]) for r in seg]
        yaws = [float(r["yaw_deg"]) for r in seg]
        els = [float(r["el"]) for r in seg]
        ers = [float(r["er"]) for r in seg]
        out_ls = [float(r["outL"]) for r in seg]
        out_rs = [float(r["outR"]) for r in seg]
        eds = [float(r["ed"]) for r in seg]
        xs, ys, ss, _ = reconstruct_path_from_outputs(seg, speed_scale)
        phase_path_len = ss[-1] if ss else 0.0
        y_end = ys[-1] if ys else 0.0
        y_ends.append(y_end)
        side_eps = _lateral_side_eps(phase_path_len)
        side = _side_label_from_value(y_end, side_eps)
        phases.append({
            "name": name,
            "t0_s": (ticks[0] - ticks[0]) / 1000.0,
            "t1_s": (ticks[-1] - ticks[0]) / 1000.0,
            "x_final": xs[-1] if xs else 0.0,
            "y_final": y_end,
            "path_len": phase_path_len,
            "yaw_delta": _yaw_delta_deg(yaws),
            "ed_mean": _mean(eds),
            "el_mean": _mean_abs(els),
            "er_mean": _mean_abs(ers),
            "out_diff_mean": _mean([a - b for a, b in zip(out_ls, out_rs)]),
            "side_eps": side_eps,
            "side": side,
        })

    if len(y_ends) < 3:
        return {
            "phases": phases,
            "trend_side": "balanced",
            "trend_conf": 0.0,
            "phase_shift": "none",
            "late_drift_strength": 0.0,
        }

    dy_early_mid = y_ends[1] - y_ends[0]
    dy_mid_late = y_ends[2] - y_ends[1]
    late_drift_strength = abs(dy_mid_late)
    trend_eps = max(0.10, 0.5 * (float(phases[0].get("side_eps", 0.10)) + float(phases[2].get("side_eps", 0.10))))
    trend_side = _side_label_from_value(y_ends[2] - y_ends[0], trend_eps)

    denom = abs(y_ends[0]) + abs(y_ends[1]) + abs(y_ends[2]) + 1e-6
    trend_conf = min(1.0, abs(y_ends[2] - y_ends[0]) / denom)

    phase_shift = "none"
    signs = [1 if str(p.get("side", "balanced")) == "right" else (-1 if str(p.get("side", "balanced")) == "left" else 0) for p in phases]
    if signs[0] == 0 and signs[1] == 0 and signs[2] != 0:
        phase_shift = f"steady_to_{trend_side}"
    elif signs[0] != 0 and signs[2] != 0 and signs[0] != signs[2]:
        phase_shift = "cross_over"
    elif abs(dy_mid_late) > abs(dy_early_mid) * 1.5 and abs(dy_mid_late) > trend_eps:
        phase_shift = f"late_{trend_side}_drift"

    return {
        "phases": phases,
        "trend_side": trend_side,
        "trend_conf": trend_conf,
        "phase_shift": phase_shift,
        "late_drift_strength": late_drift_strength,
    }


def _run_metrics(rows: List[Dict[str, float]], speed_scale: float) -> Dict[str, object]:
    if not rows:
        return {
            "run_ticks": [],
            "run_t_s": [],
            "run_yaw_unwrapped": [],
            "s_along": [],
            "xs_out": [0.0],
            "ys_out": [0.0],
            "s_out": [0.0],
            "kappas_out": [0.0],
            "yaw_drift_deg_s": 0.0,
            "yaw_drift_deg_per_s": 0.0,
            "forward_final": 0.0,
            "lateral_final": 0.0,
            "path_len": 0.0,
            "curvature_rms": 0.0,
            "curvature_abs_mean": 0.0,
            "curvature_p95": 0.0,
            "net_heading_change": 0.0,
            "total_heading_variation": 0.0,
            "sinuosity": 1.0,
            "lateral_per_path": 0.0,
            "lateral_abs_per_path": 0.0,
            "out_ls": [],
            "out_rs": [],
            "out_diffs": [],
            "out_diff_mean": 0.0,
            "out_diff_abs_mean": 0.0,
            "out_l_mean": 0.0,
            "out_r_mean": 0.0,
            "out_asym_ratio": 0.0,
            "yrs": [],
            "yr_rms": 0.0,
            "corr_out_yawstep": 0.0,
            "lateral_window_slopes": [],
            "lateral_drift_mean": 0.0,
            "lateral_drift_abs_p95": 0.0,
            "side_bias": _empty_side_bias(),
            "micro_sig": _micro_drift_signature([], []),
            "phase_drift": summarize_phase_drift([], speed_scale),
        }

    run_ticks = [r["tick"] for r in rows]
    run_t_s = [(t - run_ticks[0]) / 1000.0 for t in run_ticks] if run_ticks else []
    run_yaw_unwrapped = _unwrap_yaw_deg([r["yaw_deg"] for r in rows])
    s_along = reconstruct_s_along_path(rows, float(speed_scale)) if rows else []
    xs_out, ys_out, s_out, kappas_out = reconstruct_path_from_outputs(rows, float(speed_scale)) if rows else ([0.0], [0.0], [0.0], [0.0])
    out_ls = [r["outL"] for r in rows]
    out_rs = [r["outR"] for r in rows]
    out_diffs = [float(a - b) for a, b in zip(out_ls, out_rs)] if out_ls and out_rs else []
    yrs = [r["yr"] for r in rows]

    yaw_drift_deg_s = _linear_fit_slope(run_t_s, run_yaw_unwrapped) if (run_t_s and run_yaw_unwrapped) else 0.0
    yaw_drift_deg_per_s = _linear_fit_slope(s_along, run_yaw_unwrapped) if (s_along and run_yaw_unwrapped and (max(s_along) > 1e-6)) else 0.0
    forward_final = ys_out and float(xs_out[-1]) or 0.0
    lateral_final = ys_out and float(ys_out[-1]) or 0.0
    path_len = s_out and float(s_out[-1]) or 0.0
    curvature_rms = _rms(kappas_out) if kappas_out else 0.0
    curvature_abs_mean = _mean_abs(kappas_out) if kappas_out else 0.0
    curvature_p95 = _quantile([abs(k) for k in kappas_out], 0.95) if kappas_out else 0.0
    net_heading_change = (run_yaw_unwrapped[-1] - run_yaw_unwrapped[0]) if len(run_yaw_unwrapped) >= 2 else 0.0
    total_heading_variation = float(sum(abs(run_yaw_unwrapped[i] - run_yaw_unwrapped[i - 1]) for i in range(1, len(run_yaw_unwrapped)))) if len(run_yaw_unwrapped) >= 2 else 0.0
    sinuosity = (path_len / max(1e-9, math.sqrt(forward_final * forward_final + lateral_final * lateral_final))) if path_len > 1e-9 else 1.0
    lateral_per_path = (lateral_final / path_len) if path_len > 1e-9 else 0.0
    lateral_abs_per_path = (abs(lateral_final) / path_len) if path_len > 1e-9 else 0.0
    motion_state = summarize_motion_state(rows, run_t_s[-1] if run_t_s else 0.0)
    out_diff_mean = _mean(out_diffs) if out_diffs else 0.0
    out_diff_abs_mean = _mean_abs(out_diffs) if out_diffs else 0.0
    out_l_mean = _mean(out_ls) if out_ls else 0.0
    out_r_mean = _mean(out_rs) if out_rs else 0.0
    out_asym_ratio = (out_r_mean / out_l_mean) if abs(out_l_mean) > 1e-9 else 0.0
    yaw_step = [run_yaw_unwrapped[i] - run_yaw_unwrapped[i - 1] for i in range(1, len(run_yaw_unwrapped))] if len(run_yaw_unwrapped) >= 2 else []
    out_diff_step = out_diffs[1:1 + len(yaw_step)] if len(out_diffs) >= len(yaw_step) + 1 else out_diffs[:len(yaw_step)]
    corr_out_yawstep = _corr(out_diff_step, yaw_step[:len(out_diff_step)]) if out_diff_step and yaw_step else 0.0
    lateral_window_slopes = _window_endpoint_slopes(run_t_s, ys_out, 0.5) if (run_t_s and len(ys_out) == len(run_t_s)) else []
    lateral_drift_mean = _mean(lateral_window_slopes) if lateral_window_slopes else 0.0
    lateral_drift_abs_p95 = _quantile([abs(v) for v in lateral_window_slopes], 0.95) if lateral_window_slopes else 0.0
    side_bias = summarize_side_bias(ys_out, lateral_window_slopes) if ys_out else _empty_side_bias()
    micro_sig = _micro_drift_signature(run_t_s, ys_out)
    phase_drift = summarize_phase_drift(rows, speed_scale)
    yr_rms = _rms(yrs) if yrs else 0.0

    return {
        "run_ticks": run_ticks,
        "run_t_s": run_t_s,
        "run_yaw_unwrapped": run_yaw_unwrapped,
        "s_along": s_along,
        "xs_out": xs_out,
        "ys_out": ys_out,
        "s_out": s_out,
        "kappas_out": kappas_out,
        "yaw_drift_deg_s": yaw_drift_deg_s,
        "yaw_drift_deg_per_s": yaw_drift_deg_per_s,
        "forward_final": forward_final,
        "lateral_final": lateral_final,
        "path_len": path_len,
        "curvature_rms": curvature_rms,
        "curvature_abs_mean": curvature_abs_mean,
        "curvature_p95": curvature_p95,
        "net_heading_change": net_heading_change,
        "total_heading_variation": total_heading_variation,
        "sinuosity": sinuosity,
        "lateral_per_path": lateral_per_path,
        "lateral_abs_per_path": lateral_abs_per_path,
        "out_ls": out_ls,
        "out_rs": out_rs,
        "out_diffs": out_diffs,
        "out_diff_mean": out_diff_mean,
        "out_diff_abs_mean": out_diff_abs_mean,
        "out_l_mean": out_l_mean,
        "out_r_mean": out_r_mean,
        "out_asym_ratio": out_asym_ratio,
        "yrs": yrs,
        "yr_rms": yr_rms,
        "corr_out_yawstep": corr_out_yawstep,
        "lateral_window_slopes": lateral_window_slopes,
        "lateral_drift_mean": lateral_drift_mean,
        "lateral_drift_abs_p95": lateral_drift_abs_p95,
        "side_bias": side_bias,
        "micro_sig": micro_sig,
        "phase_drift": phase_drift,
    }


def _sign_vote(xs: List[float], eps: float = 0.0) -> Tuple[int, int, int]:
    pos = 0
    neg = 0
    zero = 0
    for x in xs:
        v = float(x)
        if v > eps:
            pos += 1
        elif v < -eps:
            neg += 1
        else:
            zero += 1
    return pos, neg, zero


def _dominant_side_label(pos: int, neg: int) -> str:
    if pos > neg:
        return "right"
    if neg > pos:
        return "left"
    return "balanced"


def _dominant_confidence(pos: int, neg: int) -> float:
    tot = pos + neg
    if tot <= 0:
        return 0.0
    return abs(float(pos - neg)) / float(tot)


def _bias_severity_label(bias_ratio: float) -> str:
    if bias_ratio <= 0.005:
        return "low"
    if bias_ratio <= 0.020:
        return "mild"
    if bias_ratio <= 0.040:
        return "moderate"
    return "high"


def _stability_label(sinuosity: float, heading_variation: float, lateral_abs_max: float) -> str:
    if sinuosity <= 1.002 and heading_variation <= 100.0 and lateral_abs_max <= 15.0:
        return "stable"
    if sinuosity <= 1.004 and heading_variation <= 160.0 and lateral_abs_max <= 20.0:
        return "mostly_stable"
    return "unstable_or_wobbling"


def _reuse_recommendation(stability: str, bias_severity: str) -> str:
    if stability == "stable":
        if bias_severity in ("low", "mild"):
            return "reusable_candidate"
        return "stable_but_biased"
    if stability == "mostly_stable":
        if bias_severity == "low":
            return "candidate_needs_repeat"
        if bias_severity == "mild":
            return "candidate_with_bias"
    return "needs_tuning"


def _sample_series_at_fraction(xs: List[float], frac: float) -> float:
    if not xs:
        return 0.0
    if frac <= 0.0:
        return float(xs[0])
    if frac >= 1.0:
        return float(xs[-1])
    idx = int(round(frac * float(len(xs) - 1)))
    if idx < 0:
        idx = 0
    if idx >= len(xs):
        idx = len(xs) - 1
    return float(xs[idx])


def _rms(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return math.sqrt(float(sum(x * x for x in xs)) / float(len(xs)))


def _std(xs: List[float]) -> float:
    if len(xs) < 2:
        return 0.0
    m = _mean(xs)
    return math.sqrt(float(sum((x - m) * (x - m) for x in xs)) / float(len(xs)))


def _count_zero_crossings(xs: List[float], eps: float = 0.0) -> int:
    if not xs:
        return 0
    last_s = 0
    cnt = 0
    for x in xs:
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


def _unwrap_yaw_deg(yaws: List[float]) -> List[float]:
    if not yaws:
        return []
    out = [float(yaws[0])]
    for i in range(1, len(yaws)):
        prev = out[-1]
        cur = float(yaws[i])
        d = cur - float(yaws[i - 1])
        if d > 180.0:
            d -= 360.0
        elif d < -180.0:
            d += 360.0
        out.append(prev + d)
    return out


def _linear_fit_slope(x: List[float], y: List[float]) -> float:
    if not x or not y or len(x) != len(y) or len(x) < 2:
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


def _median(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return float(statistics.median(xs))


def _rolling_median(xs: List[float], win: int) -> List[float]:
    if not xs:
        return []
    if win <= 1:
        return [float(x) for x in xs]
    if win % 2 == 0:
        win += 1
    k = win // 2
    out: List[float] = []
    n = len(xs)
    for i in range(n):
        a = i - k
        b = i + k + 1
        if a < 0:
            a = 0
        if b > n:
            b = n
        out.append(float(statistics.median(xs[a:b])))
    return out


def _mad(xs: List[float]) -> float:
    if not xs:
        return 0.0
    med = float(statistics.median(xs))
    dev = [abs(float(x) - med) for x in xs]
    return float(statistics.median(dev))


def _robust_keep_mask(residuals: List[float], k: float) -> List[bool]:
    if not residuals:
        return []
    mad = _mad(residuals)
    if mad <= 1e-9:
        return [True] * len(residuals)
    thresh = float(k) * 1.4826 * mad
    return [abs(float(r)) <= thresh for r in residuals]


def _window_slopes(x: List[float], y: List[float], win_s: float) -> List[float]:
    if not x or not y or len(x) != len(y) or len(x) < 4:
        return []
    if win_s <= 0:
        return []

    slopes: List[float] = []
    n = len(x)
    j = 0
    for i in range(n):
        while j < n and (x[j] - x[i]) < win_s:
            j += 1
        if j - i >= 4:
            slopes.append(_linear_fit_slope(x[i:j], y[i:j]))
    return slopes


def _row_quality_score(row: Dict[str, float]) -> float:
    score = 0.0
    if abs(float(row.get("outL", 0.0))) + abs(float(row.get("outR", 0.0))) > 0.5:
        score += 4.0
    if abs(float(row.get("el", 0.0))) + abs(float(row.get("er", 0.0))) > 0.5:
        score += 3.0
    if abs(float(row.get("yr", 0.0))) > 0.05:
        score += 1.0
    if abs(float(row.get("trim", 0.0))) > 1e-9 or abs(float(row.get("at", 0.0))) > 1e-9:
        score += 1.0
    if abs(float(row.get("gx", 0.0))) + abs(float(row.get("gy", 0.0))) + abs(float(row.get("gz", 0.0))) > 0.5:
        score += 1.0
    return score


def _dedupe_rows_by_tick(rows: List[Dict[str, float]]) -> List[Dict[str, float]]:
    if not rows:
        return []
    best_by_tick: Dict[int, Dict[str, float]] = {}
    score_by_tick: Dict[int, float] = {}
    order: List[int] = []
    for row in rows:
        key = int(round(float(row.get("tick", 0.0))))
        score = _row_quality_score(row)
        if key not in best_by_tick:
            best_by_tick[key] = row
            score_by_tick[key] = score
            order.append(key)
            continue
        if score >= score_by_tick[key]:
            best_by_tick[key] = row
            score_by_tick[key] = score
    out = [best_by_tick[k] for k in sorted(order)]
    out.sort(key=lambda r: float(r.get("tick", 0.0)))
    return out


def parse_hb_rows(raw_path: str) -> List[Dict[str, float]]:
    rows: List[Dict[str, float]] = []
    bno_re = re.compile(
        r"^BNO\s+tick=(?P<tick>[-0-9.]+)\s+run=(?P<run>[-0-9.]+)\s+y=(?P<y>[-0-9.]+)\s+yr=(?P<yr>[-0-9.]+)"
    )
    sample_re = re.compile(r"^\s*([-+]?\d+(?:\.\d+)?),([-+]?\d+(?:\.\d+)?),([-+]?\d+(?:\.\d+)?)\s*$")
    pending_bno: Optional[Dict[str, float]] = None

    with open(raw_path, "r", encoding="utf-8", errors="ignore") as f:
        for ln in f:
            line = ln.strip()
            for chunk in _extract_hb_stat_candidates(ln):
                kv = _parse_kv_line(chunk)
                if not kv or "tick" not in kv:
                    continue
                tick = float(kv.get("tick", 0.0))
                run = float(kv.get("run", 0.0))
                yaw_deg = float(kv.get("y", 0.0))
                yr = float(kv.get("yr", 0.0))
                L = float(kv.get("L", 0.0))
                R = float(kv.get("R", 0.0))
                outL = float(kv.get("OL", L))
                outR = float(kv.get("OR", R))
                el = float(kv.get("el", 0.0))
                er = float(kv.get("er", 0.0))
                ed = float(kv.get("ed", el - er))
                ts = float(kv.get("ts", 0.0))
                actual_speed = float(kv.get("as", 0.0))
                pwm = float(kv.get("pwm", 0.0))
                pc = float(kv.get("pc", 0.0))
                hd = float(kv.get("hd", 0.0))
                ta = float(kv.get("ta", 0.0))
                aa = float(kv.get("aa", 0.0))
                se = float(kv.get("se", 0.0))
                so = float(kv.get("so", 0.0))
                ae = float(kv.get("ae", 0.0))
                ao = float(kv.get("ao", 0.0))
                trim = float(kv.get("trim", 0.0))
                at = float(kv.get("at", 0.0))
                gx = float(kv.get("gx", 0.0))
                gy = float(kv.get("gy", 0.0))
                gz = float(kv.get("gz", 0.0))

                rows.append(
                    {
                        "tick": tick,
                        "run": run,
                        "yaw_deg": yaw_deg,
                        "yaw_rad": yaw_deg * math.pi / 180.0,
                        "yr": yr,
                        "gx": gx,
                        "gy": gy,
                        "gz": gz,
                        "L": L,
                        "R": R,
                        "outL": outL,
                        "outR": outR,
                        "ts": ts,
                        "as": actual_speed,
                        "pwm": pwm,
                        "pc": pc,
                        "hd": hd,
                        "ta": ta,
                        "aa": aa,
                        "se": se,
                        "so": so,
                        "ae": ae,
                        "ao": ao,
                        "el": el,
                        "er": er,
                        "ed": ed,
                        "trim": trim,
                        "at": at,
                    }
                )

            m_bno = bno_re.match(line)
            if m_bno:
                if pending_bno is not None:
                    rows.append(pending_bno)
                tick = float(m_bno.group("tick"))
                run = float(m_bno.group("run"))
                yaw_deg = float(m_bno.group("y"))
                yr = float(m_bno.group("yr"))
                pending_bno = {
                    "tick": tick,
                    "run": run,
                    "yaw_deg": yaw_deg,
                    "yaw_rad": yaw_deg * math.pi / 180.0,
                    "yr": yr,
                    "gx": 0.0,
                    "gy": 0.0,
                    "gz": 0.0,
                    "L": 0.0,
                    "R": 0.0,
                    "outL": 0.0,
                    "outR": 0.0,
                    "pwm": 0.0,
                    "pc": 0.0,
                    "hd": 0.0,
                    "ta": 0.0,
                    "aa": 0.0,
                    "se": 0.0,
                    "so": 0.0,
                    "ae": 0.0,
                    "ao": 0.0,
                    "el": 0.0,
                    "er": 0.0,
                    "ed": 0.0,
                    "trim": 0.0,
                    "at": 0.0,
                }
                continue

            m_sample = sample_re.match(line)
            if m_sample and pending_bno is not None:
                left = float(m_sample.group(2))
                right = float(m_sample.group(3))
                pending_bno["el"] = left
                pending_bno["er"] = right
                pending_bno["ed"] = left - right
                rows.append(pending_bno)
                pending_bno = None

    if pending_bno is not None:
        rows.append(pending_bno)

    return _dedupe_rows_by_tick(rows)


def clip_run_segment(rows: List[Dict[str, float]], max_run_s: float) -> List[Dict[str, float]]:
    run_rows = [r for r in rows if r["run"] > 0.5]
    if not run_rows:
        return rows

    use = run_rows
    if max_run_s > 0.0:
        t0 = run_rows[0]["tick"]
        t1 = t0 + max_run_s * 1000.0
        clipped = [r for r in run_rows if t0 <= r["tick"] <= t1]
        if clipped:
            use = clipped
    return use


def clip_steady_segment(rows: List[Dict[str, float]], skip_s: float, tail_s: float) -> List[Dict[str, float]]:
    if not rows:
        return rows

    use = rows
    if skip_s > 0.0:
        t0 = float(use[0]["tick"])
        t1 = t0 + skip_s * 1000.0
        clipped = [r for r in use if r["tick"] >= t1]
        if len(clipped) >= 5:
            use = clipped

    if tail_s > 0.0 and use:
        t_end = float(use[-1]["tick"])
        t0 = t_end - tail_s * 1000.0
        clipped = [r for r in use if r["tick"] >= t0]
        if len(clipped) >= 5:
            use = clipped

    return use


def split_run_stretches(rows: List[Dict[str, float]], gap_ms: float = 600.0) -> List[List[Dict[str, float]]]:
    run_rows = [r for r in rows if float(r.get("run", 0.0)) > 0.5]
    if not run_rows:
        return []
    out: List[List[Dict[str, float]]] = []
    cur: List[Dict[str, float]] = [run_rows[0]]
    for r in run_rows[1:]:
        prev = cur[-1]
        dt = float(r["tick"] - prev["tick"])
        if dt <= 0.0 or dt > float(gap_ms):
            if len(cur) >= 2:
                out.append(cur)
            cur = [r]
        else:
            cur.append(r)
    if len(cur) >= 2:
        out.append(cur)
    return out


def reconstruct_path(
    rows: List[Dict[str, float]],
    speed_scale: float,
) -> Tuple[List[float], List[float], List[float]]:
    if len(rows) < 2:
        return [0.0], [0.0], [0.0]

    xs: List[float] = [0.0]
    ys: List[float] = [0.0]
    ts: List[float] = [rows[0]["tick"]]

    x = 0.0
    y = 0.0
    for i in range(1, len(rows)):
        a = rows[i - 1]
        b = rows[i]
        dt_ms = float(b["tick"] - a["tick"])
        if dt_ms <= 0:
            continue

        theta = float(b["yaw_rad"])
        v = 0.5 * (float(b["el"]) + float(b["er"]))
        ds = v * (dt_ms / 1000.0) * float(speed_scale)
        x += ds * math.cos(theta)
        y += ds * math.sin(theta)

        xs.append(x)
        ys.append(y)
        ts.append(float(b["tick"]))

    return xs, ys, ts


def _path_arc_len(xs: List[float], ys: List[float]) -> float:
    if len(xs) < 2 or len(ys) < 2:
        return 0.0
    total = 0.0
    for i in range(1, min(len(xs), len(ys))):
        dx = float(xs[i] - xs[i - 1])
        dy = float(ys[i] - ys[i - 1])
        total += math.sqrt(dx * dx + dy * dy)
    return total


def reconstruct_s_along_path(rows: List[Dict[str, float]], speed_scale: float) -> List[float]:
    if len(rows) < 2:
        return [0.0] * max(1, len(rows))
    out: List[float] = [0.0]
    s = 0.0
    for i in range(1, len(rows)):
        a = rows[i - 1]
        b = rows[i]
        dt_ms = float(b["tick"] - a["tick"])
        if dt_ms <= 0:
            out.append(s)
            continue
        v = 0.5 * (float(b["el"]) + float(b["er"]))
        ds = v * (dt_ms / 1000.0) * float(speed_scale)
        s += ds
        out.append(s)
    return out


def suggest_trim(ed_mean: float, current_trim: float) -> Tuple[float, str]:
    if abs(ed_mean) < 10:
        return current_trim, "ed_mean 已接近 0，TRIM 可保持不变"

    step = 0.25
    if abs(ed_mean) > 40:
        step = 0.5

    if ed_mean > 0:
        return current_trim - step, f"ed_mean={ed_mean:.1f}>0（左轮更快），建议 TRIM 往负方向调 {step}"
    return current_trim + step, f"ed_mean={ed_mean:.1f}<0（右轮更快），建议 TRIM 往正方向调 {step}"


def suggest_at_gains(
    yr: List[float],
    at: List[float],
    at_lim: float,
) -> List[str]:
    sugg: List[str] = []
    if not yr:
        return sugg

    zc = _count_zero_crossings(yr, eps=0.2)
    yr_rms = _rms(yr)
    yr_std = _std(yr)

    sat_hits = 0
    if at and at_lim > 0:
        for u in at:
            if abs(u) >= (0.95 * at_lim):
                sat_hits += 1
        sat_ratio = float(sat_hits) / float(len(at))
    else:
        sat_ratio = 0.0

    if sat_ratio > 0.10:
        sugg.append(f"AT: auto-trim 触顶占比 {sat_ratio:.2f}，建议增大手动 TRIM 或增大 AT_LIM（先别冲太大）。")

    if zc >= 10 and yr_rms > 6.0:
        sugg.append("AT: 偏航角速度来回摆动明显，建议降低 AT_KP/AT_KI（例如各减半）以减小过冲。")
    elif yr_std > 5.0 and zc >= 6:
        sugg.append("AT: 有一定摆动，建议小幅降低 AT_KP（例如 *0.7），AT_KI 保持或略降。")

    return sugg


def summarize_motion_state(
    run_use: List[Dict[str, float]],
    dt_s: float,
) -> Dict[str, float | str]:
    if not run_use or dt_s <= 1e-9:
        return {
            "dist_proxy": 0.0,
            "speed_proxy": 0.0,
            "left_dist_proxy": 0.0,
            "right_dist_proxy": 0.0,
            "enc_nonzero_ratio": 0.0,
            "enc_active_ratio": 0.0,
            "out_nonzero_ratio": 0.0,
            "out_strong_ratio": 0.0,
            "output_only_ratio": 0.0,
            "idle_streak_s": 0.0,
            "output_idle_streak_s": 0.0,
            "pause_suspected": 0.0,
            "motion_level": "no_motion",
            "stiction_suspected": 0.0,
        }

    left_abs = [abs(float(r["el"])) for r in run_use]
    right_abs = [abs(float(r["er"])) for r in run_use]
    out_sum = [abs(float(r["outL"])) + abs(float(r["outR"])) for r in run_use]
    enc_sum = [a + b for a, b in zip(left_abs, right_abs)]

    left_dist_proxy = 0.0
    right_dist_proxy = 0.0
    idle_streak_s = 0.0
    output_idle_streak_s = 0.0
    max_idle_streak_s = 0.0
    max_output_idle_streak_s = 0.0
    for i in range(1, len(run_use)):
        prev = run_use[i - 1]
        cur = run_use[i]
        dt_step_s = max(0.0, (float(cur["tick"]) - float(prev["tick"])) / 1000.0)
        if dt_step_s <= 1e-9:
            continue
        left_step = left_abs[i] * dt_step_s
        right_step = right_abs[i] * dt_step_s
        left_dist_proxy += left_step
        right_dist_proxy += right_step
        if enc_sum[i] <= 1.0:
            idle_streak_s += dt_step_s
            if idle_streak_s > max_idle_streak_s:
                max_idle_streak_s = idle_streak_s
        else:
            idle_streak_s = 0.0
        if enc_sum[i] <= 1.0 and out_sum[i] >= 8.0:
            output_idle_streak_s += dt_step_s
            if output_idle_streak_s > max_output_idle_streak_s:
                max_output_idle_streak_s = output_idle_streak_s
        else:
            output_idle_streak_s = 0.0

    if len(run_use) == 1:
        left_dist_proxy = left_abs[0] * dt_s
        right_dist_proxy = right_abs[0] * dt_s

    dist_proxy = 0.5 * (left_dist_proxy + right_dist_proxy)
    speed_proxy = dist_proxy / dt_s if dt_s > 1e-9 else 0.0

    enc_nonzero_ratio = float(sum(1 for v in enc_sum if v > 0.5)) / float(len(enc_sum)) if enc_sum else 0.0
    enc_active_ratio = float(sum(1 for v in enc_sum if v > 2.0)) / float(len(enc_sum)) if enc_sum else 0.0
    out_nonzero_ratio = float(sum(1 for v in out_sum if v > 0.5)) / float(len(out_sum)) if out_sum else 0.0
    out_strong_ratio = float(sum(1 for v in out_sum if v >= 8.0)) / float(len(out_sum)) if out_sum else 0.0
    output_only_ratio = float(sum(1 for o, e in zip(out_sum, enc_sum) if o >= 8.0 and e <= 1.0)) / float(len(out_sum)) if out_sum else 0.0

    motion_level = "moving"
    if dist_proxy < 0.35 or (speed_proxy < 1.0 and enc_nonzero_ratio < 0.2):
        motion_level = "no_motion"
    elif dist_proxy < 2.0 or speed_proxy < 4.5 or enc_active_ratio < 0.25:
        motion_level = "barely_moving"
    elif dist_proxy < 4.0 or speed_proxy < 8.0 or enc_active_ratio < 0.5:
        motion_level = "slow_crawl"

    pause_suspected = 1.0 if (max_idle_streak_s >= min(0.18, max(0.12, 0.35 * dt_s)) and dist_proxy >= 1.0) else 0.0
    stiction_suspected = 1.0 if (out_nonzero_ratio >= 0.2 and (dist_proxy < 2.0 or speed_proxy < 4.0 or max_output_idle_streak_s >= 0.12) and output_only_ratio >= 0.2) else 0.0

    return {
        "dist_proxy": dist_proxy,
        "speed_proxy": speed_proxy,
        "left_dist_proxy": left_dist_proxy,
        "right_dist_proxy": right_dist_proxy,
        "enc_nonzero_ratio": enc_nonzero_ratio,
        "enc_active_ratio": enc_active_ratio,
        "out_nonzero_ratio": out_nonzero_ratio,
        "out_strong_ratio": out_strong_ratio,
        "output_only_ratio": output_only_ratio,
        "idle_streak_s": max_idle_streak_s,
        "output_idle_streak_s": max_output_idle_streak_s,
        "pause_suspected": pause_suspected,
        "motion_level": motion_level,
        "stiction_suspected": stiction_suspected,
    }


def summarize_side_bias(ys_out: List[float], lateral_window_slopes: List[float]) -> Dict[str, float | str]:
    y25 = _sample_series_at_fraction(ys_out, 0.25)
    y50 = _sample_series_at_fraction(ys_out, 0.50)
    y75 = _sample_series_at_fraction(ys_out, 0.75)
    y100 = _sample_series_at_fraction(ys_out, 1.00)
    y_abs_max = max((abs(float(y)) for y in ys_out), default=0.0)
    y_eps = max(0.10, min(1.00, 0.05 * y_abs_max))
    slope_eps = 0.002
    pos_y, neg_y, zero_y = _sign_vote(ys_out, eps=y_eps)
    pos_s, neg_s, zero_s = _sign_vote(lateral_window_slopes, eps=slope_eps)
    y_tot = pos_y + neg_y + zero_y
    s_tot = pos_s + neg_s + zero_s
    return {
        "y25": y25,
        "y50": y50,
        "y75": y75,
        "y100": y100,
        "y_side": _dominant_side_label(pos_y, neg_y),
        "slope_side": _dominant_side_label(pos_s, neg_s),
        "y_conf": _dominant_confidence(pos_y, neg_y),
        "slope_conf": _dominant_confidence(pos_s, neg_s),
        "y_eps": y_eps,
        "slope_eps": slope_eps,
        "y_zero_ratio": (float(zero_y) / float(y_tot)) if y_tot > 0 else 0.0,
        "s_zero_ratio": (float(zero_s) / float(s_tot)) if s_tot > 0 else 0.0,
        "y_pos": float(pos_y),
        "y_neg": float(neg_y),
        "y_zero": float(zero_y),
        "s_pos": float(pos_s),
        "s_neg": float(neg_s),
        "s_zero": float(zero_s),
    }


def _side_flip_count(sides: List[str]) -> int:
    flips = 0
    prev = ""
    for side in sides:
        s = str(side)
        if s == "balanced":
            continue
        if prev and s != prev:
            flips += 1
        prev = s
    return flips


def _label_flip_count(labels: List[str], neutral_label: str) -> int:
    flips = 0
    prev = ""
    for label in labels:
        s = str(label)
        if s == neutral_label:
            continue
        if prev and s != prev:
            flips += 1
        prev = s
    return flips


def summarize_startup_behavior(rows: List[Dict[str, float]], speed_scale: float, startup_s: float, segment_s: float) -> Dict[str, object]:
    if len(rows) < 4:
        return {
            "segments": [],
            "window_s": float(startup_s),
            "segment_s": float(segment_s),
            "side_seq": "none",
            "side_flips": 0,
            "early_yaw_abs_mean": 0.0,
            "late_yaw_abs_mean": 0.0,
            "yaw_abs_ratio": 0.0,
            "early_yr_rms": 0.0,
            "late_yr_rms": 0.0,
            "early_out_diff_abs_mean": 0.0,
            "late_out_diff_abs_mean": 0.0,
            "convergence": "insufficient",
            "residual_jitter": "unknown",
            "diagnosis": "insufficient_data",
        }

    win_s = max(0.5, float(startup_s))
    seg_s = max(0.2, float(segment_s))
    t0 = float(rows[0]["tick"])
    use = [r for r in rows if (float(r["tick"]) - t0) <= (win_s * 1000.0)]
    segs = [summarize_second_segments(seg, speed_scale) for seg in segment_rows(use, seg_s) if len(seg) >= 2]
    if not segs:
        return {
            "segments": [],
            "window_s": win_s,
            "segment_s": seg_s,
            "side_seq": "none",
            "side_flips": 0,
            "early_yaw_abs_mean": 0.0,
            "late_yaw_abs_mean": 0.0,
            "yaw_abs_ratio": 0.0,
            "early_yr_rms": 0.0,
            "late_yr_rms": 0.0,
            "early_out_diff_abs_mean": 0.0,
            "late_out_diff_abs_mean": 0.0,
            "convergence": "insufficient",
            "residual_jitter": "unknown",
            "diagnosis": "insufficient_data",
        }

    split = max(1, len(segs) // 2)
    early = segs[:split]
    late = segs[split:] if segs[split:] else [segs[-1]]
    sides = [str(s.get("side", "balanced")) for s in segs]
    yaw_turns: List[str] = []
    for s in segs:
        yaw_delta = float(s.get("yaw_delta", 0.0))
        if yaw_delta > 1.0:
            yaw_turns.append("right_turn")
        elif yaw_delta < -1.0:
            yaw_turns.append("left_turn")
        else:
            yaw_turns.append("straight")

    early_yaw = _mean([abs(float(s.get("yaw_delta", 0.0))) for s in early])
    late_yaw = _mean([abs(float(s.get("yaw_delta", 0.0))) for s in late])
    early_yr = _mean([float(s.get("yr_rms", 0.0)) for s in early])
    late_yr = _mean([float(s.get("yr_rms", 0.0)) for s in late])
    early_od = _mean([abs(float(s.get("out_diff_mean", 0.0))) for s in early])
    late_od = _mean([abs(float(s.get("out_diff_mean", 0.0))) for s in late])

    if early_yaw > 1e-6:
        yaw_ratio = late_yaw / early_yaw
    else:
        yaw_ratio = 0.0 if late_yaw <= 1e-6 else 9.0

    convergence = "flat"
    if yaw_ratio < 0.75 and late_yr <= (early_yr * 1.10 + 1e-6):
        convergence = "improving"
    elif yaw_ratio > 1.25 or late_yr > (early_yr * 1.25 + 1e-6):
        convergence = "worsening"

    residual_jitter = "low"
    if late_yr > 4.0 or late_yaw > 6.0 or late_od > 6.0:
        residual_jitter = "high"
    elif late_yr > 2.0 or late_yaw > 2.5 or late_od > 3.0:
        residual_jitter = "medium"

    side_flips = _side_flip_count(sides)
    yaw_turn_flips = _label_flip_count(yaw_turns, "straight")
    osc_flips = side_flips if side_flips >= yaw_turn_flips else yaw_turn_flips
    diagnosis = "startup_relatively_stable"
    if osc_flips >= 2:
        if convergence == "improving":
            diagnosis = "startup_snake_then_settle" if residual_jitter == "low" else "startup_snake_then_settle_but_jitter_remains"
        elif convergence == "worsening":
            diagnosis = "startup_snake_and_not_settling"
        else:
            diagnosis = "startup_snake_persistent"
    else:
        if convergence == "improving" and residual_jitter != "low":
            diagnosis = "startup_wobble_then_partial_settle"
        elif residual_jitter != "low":
            diagnosis = "mild_startup_wobble_with_residual_jitter"

    return {
        "segments": segs,
        "window_s": win_s,
        "segment_s": seg_s,
        "side_seq": " -> ".join(sides) if sides else "none",
        "side_flips": side_flips,
        "yaw_turn_seq": " -> ".join(yaw_turns) if yaw_turns else "none",
        "yaw_turn_flips": yaw_turn_flips,
        "early_yaw_abs_mean": early_yaw,
        "late_yaw_abs_mean": late_yaw,
        "yaw_abs_ratio": yaw_ratio,
        "early_yr_rms": early_yr,
        "late_yr_rms": late_yr,
        "early_out_diff_abs_mean": early_od,
        "late_out_diff_abs_mean": late_od,
        "convergence": convergence,
        "residual_jitter": residual_jitter,
        "diagnosis": diagnosis,
    }


def print_startup_behavior(startup: Dict[str, object]) -> None:
    segments = startup.get("segments", [])
    if not segments:
        return
    print("STARTUP:")
    print(
        f"- window_s={float(startup.get('window_s', 0.0)):.2f}  segment_s={float(startup.get('segment_s', 0.0)):.2f}  seg_n={len(segments)}"
    )
    print(
        f"- side_seq: {startup.get('side_seq', 'none')}  side_flips={int(startup.get('side_flips', 0))}"
    )
    print(
        f"- yaw_turn_seq: {startup.get('yaw_turn_seq', 'none')}  yaw_turn_flips={int(startup.get('yaw_turn_flips', 0))}"
    )
    print(
        f"- yaw_abs early/late={float(startup.get('early_yaw_abs_mean', 0.0)):.3f}/{float(startup.get('late_yaw_abs_mean', 0.0)):.3f}  ratio={float(startup.get('yaw_abs_ratio', 0.0)):.3f}"
    )
    print(
        f"- yr_rms early/late={float(startup.get('early_yr_rms', 0.0)):.3f}/{float(startup.get('late_yr_rms', 0.0)):.3f}  out_diff_abs early/late={float(startup.get('early_out_diff_abs_mean', 0.0)):.3f}/{float(startup.get('late_out_diff_abs_mean', 0.0)):.3f}"
    )
    print(
        f"- convergence={startup.get('convergence', 'unknown')}  residual_jitter={startup.get('residual_jitter', 'unknown')}  diagnosis={startup.get('diagnosis', 'unknown')}"
    )


def print_compare_block(name_a: str, name_b: str, a: Dict[str, float | str], b: Dict[str, float | str]) -> None:
    print("COMPARE:")
    for key in [
        "lateral_final",
        "lateral_abs_per_path",
        "yaw_drift_robust",
        "out_diff_abs_mean",
        "dist_proxy",
        "speed_proxy",
        "lateral_short_abs_p95",
        "total_heading_variation",
    ]:
        va = float(a.get(key, 0.0))
        vb = float(b.get(key, 0.0))
        print(f"- {key}: {name_a}={va:.6f}  {name_b}={vb:.6f}  delta={vb-va:.6f}")

    side_a = str(a.get("y_side", "balanced"))
    side_b = str(b.get("y_side", "balanced"))
    conf_a = float(a.get("y_side_conf", 0.0))
    conf_b = float(b.get("y_side_conf", 0.0))
    print(f"- side_vote: {name_a}={side_a}({conf_a:.3f})  {name_b}={side_b}({conf_b:.3f})")

    slope_side_a = str(a.get("slope_side", "balanced"))
    slope_side_b = str(b.get("slope_side", "balanced"))
    slope_conf_a = float(a.get("slope_side_conf", 0.0))
    slope_conf_b = float(b.get("slope_side_conf", 0.0))
    print(f"- slope_vote: {name_a}={slope_side_a}({slope_conf_a:.3f})  {name_b}={slope_side_b}({slope_conf_b:.3f})")


def print_second_segments(run_use: List[Dict[str, float]], speed_scale: float) -> None:
    second_stats = [summarize_second_segments(seg, speed_scale) for seg in segment_rows(run_use, 1.0)] if run_use else []
    if not second_stats:
        return
    print("SECONDS:")
    for i, s in enumerate(second_stats, start=1):
        print(
            f"- sec{i:02d} t={float(s['t0_s']):.2f}->{float(s['t1_s']):.2f}s "
            f"xy=({float(s['x_final']):.2f},{float(s['y_final']):.2f}) len={float(s['path_len']):.2f} side={s['side']} "
            f"yaw={float(s['yaw_start']):.2f}->{float(s['yaw_end']):.2f} d={float(s['yaw_delta']):.2f} yr_rms={float(s['yr_rms']):.3f} "
            f"encL/R={float(s['el_abs_mean']):.2f}/{float(s['er_abs_mean']):.2f} ed={float(s['ed_mean']):.2f}/{float(s['ed_rms']):.2f} "
            f"outL/R={float(s['outL_mean']):.2f}/{float(s['outR_mean']):.2f} od={float(s['out_diff_mean']):.2f} "
            f"encAct={float(s['enc_active_ratio']):.2f} motion={s['motion_level']} stiction={int(float(s['stiction']) > 0.5)}"
        )


def print_time_windows(window_stats: List[Dict[str, float | str]], window_s: float) -> None:
    if not window_stats:
        return
    print(f"WINDOWS({float(window_s):.2f}s):")
    for i, s in enumerate(window_stats, start=1):
        print(
            f"- win{i:02d} run_seg={int(float(s['seg_index'])):02d} t={float(s['t0_s']):.2f}->{float(s['t1_s']):.2f}s "
            f"state={s['state']} motion={s['motion_level']} wheel={s['wheel_bias']} turn_conf={float(s.get('turn_conf', 0.0)):.2f} "
            f"encL/R={float(s['el_mean']):.2f}/{float(s['er_mean']):.2f} |abs|={float(s['el_abs_mean']):.2f}/{float(s['er_abs_mean']):.2f} "
            f"ed={float(s['ed_mean']):.2f}/{float(s['ed_rms']):.2f} "
            f"yaw={float(s['yaw_start']):.2f}->{float(s['yaw_end']):.2f} d={float(s['yaw_delta']):.2f} "
            f"yr={float(s['yr_mean']):.2f}/{float(s['yr_rms']):.2f} "
            f"traj_local=({float(s['x_local']):.2f},{float(s['y_local']):.2f}) len={float(s['path_len']):.2f} "
            f"traj_total=({float(s['x_total_end']):.2f},{float(s['y_total_end']):.2f}) "
            f"encAct={float(s['enc_active_ratio']):.2f} nz={float(s['enc_nonzero_ratio']):.2f} stiction={int(float(s['stiction']) > 0.5)}"
        )


def print_total_trajectory(total: Dict[str, object]) -> None:
    if int(total.get("run_segment_n", 0)) <= 0:
        return
    print("TOTAL TRAJECTORY:")
    print(
        f"- run_segments={int(total.get('run_segment_n', 0))}  points={int(total.get('point_n', 0))}  "
        f"final_xy=({float(total.get('final_x', 0.0)):.2f},{float(total.get('final_y', 0.0)):.2f})"
    )
    print(
        f"- path_len={float(total.get('path_len', 0.0)):.2f}  direct_len={float(total.get('direct_len', 0.0)):.2f}  "
        f"sinuosity={float(total.get('sinuosity', 1.0)):.3f}  lateral_abs_max={float(total.get('lateral_abs_max', 0.0)):.2f}"
    )
    print(
        f"- mean_y={float(total.get('mean_y', 0.0)):.3f}  mean_abs_y={float(total.get('mean_abs_y', 0.0)):.3f}  "
        f"final_y_per_path={float(total.get('final_y_per_path', 0.0)):.6f}  y_pos_ratio={float(total.get('y_pos_ratio', 0.0)):.3f}  y_neg_ratio={float(total.get('y_neg_ratio', 0.0)):.3f}  y_zero_ratio={float(total.get('y_zero_ratio', 0.0)):.3f}"
    )
    print(
        f"- bbox(w/h)={float(total.get('bbox_w', 0.0)):.2f}/{float(total.get('bbox_h', 0.0)):.2f}  "
        f"heading_change={float(total.get('heading_change', 0.0)):.2f}  heading_variation={float(total.get('heading_variation', 0.0)):.2f}"
    )
    print(
        f"- dominant_side={total.get('dominant_side', 'balanced')}({float(total.get('dominant_conf', 0.0)):.3f})  bias_severity={total.get('bias_severity', 'unknown')}  side_eps={float(total.get('trajectory_side_eps', 0.0)):.3f}"
    )
    print(
        f"- stability={total.get('stability', 'unknown')}  reuse_recommendation={total.get('reuse_recommendation', 'unknown')}"
    )
    segments = total.get("segments", [])
    if isinstance(segments, list):
        for seg in segments:
            if not isinstance(seg, dict):
                continue
            print(
                f"- stitched seg{int(float(seg.get('seg_index', 0.0))):02d} t={float(seg.get('t0_s', 0.0)):.2f}->{float(seg.get('t1_s', 0.0)):.2f}s "
                f"end_xy=({float(seg.get('x_end', 0.0)):.2f},{float(seg.get('y_end', 0.0)):.2f}) len={float(seg.get('path_len', 0.0)):.2f}"
            )


def print_phase_drift(phase_drift: Dict[str, object]) -> None:
    phases = phase_drift.get("phases", [])
    if not phases:
        return
    print("PHASES:")
    for p in phases:
        print(
            f"- {p['name']} xy=({float(p['x_final']):.2f},{float(p['y_final']):.2f}) len={float(p['path_len']):.2f} "
            f"side={p['side']} yaw_d={float(p['yaw_delta']):.2f} ed={float(p['ed_mean']):.2f} "
            f"encL/R={float(p['el_mean']):.2f}/{float(p['er_mean']):.2f} od={float(p['out_diff_mean']):.2f}"
        )
    print(
        f"phase drift trend: {phase_drift.get('trend_side', 'balanced')}({float(phase_drift.get('trend_conf', 0.0)):.3f})  "
        f"shift={phase_drift.get('phase_shift', 'none')}  late_strength={float(phase_drift.get('late_drift_strength', 0.0)):.6f}"
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--raw",
        default="",
        help="raw txt path. empty=auto pick latest from 000Data",
    )
    ap.add_argument(
        "--data-dir",
        default=os.path.join(os.path.dirname(__file__), "000Data"),
        help="where to auto-find latest *_raw.txt",
    )
    ap.add_argument(
        "--max-run-s",
        type=float,
        default=0.0,
        help="only analyze first N seconds after run=1 starts (0=disabled)",
    )
    ap.add_argument(
        "--speed-scale",
        type=float,
        default=1.0,
        help="scale factor for converting encoder speed to distance (relative units)",
    )
    ap.add_argument(
        "--skip-s",
        type=float,
        default=0.0,
        help="skip first N seconds of run segment (for steady-state analysis)",
    )
    ap.add_argument(
        "--tail-s",
        type=float,
        default=0.0,
        help="only analyze last N seconds of (possibly skipped) run segment (for steady-state analysis)",
    )
    ap.add_argument(
        "--robust",
        action="store_true",
        help="enable robust yaw/yr processing: rolling median + MAD outlier rejection + window slope stats",
    )
    ap.add_argument(
        "--med-win",
        type=int,
        default=5,
        help="rolling median window (odd, default=5) used when --robust",
    )
    ap.add_argument(
        "--mad-k",
        type=float,
        default=4.0,
        help="MAD outlier threshold multiplier (default=4.0) used when --robust",
    )
    ap.add_argument(
        "--win-s",
        type=float,
        default=0.40,
        help="window length in seconds for slope distribution (default=0.40) used when --robust",
    )
    ap.add_argument(
        "--at-lim",
        type=float,
        default=1.0,
        help="auto-trim limit (for saturation analysis only, default=1.0)",
    )
    ap.add_argument(
        "--plot",
        action="store_true",
        help="plot trajectory (requires matplotlib)",
    )
    ap.add_argument(
        "--save",
        default="",
        help="save plot to a png path (implies --plot)",
    )
    ap.add_argument(
        "--segment-s",
        type=float,
        default=0.5,
        help="segment length in seconds for staged diagnostics (default=0.5)",
    )
    ap.add_argument(
        "--startup-s",
        type=float,
        default=6.0,
        help="startup diagnosis window in seconds (default=6.0)",
    )
    ap.add_argument(
        "--startup-seg-s",
        type=float,
        default=1.0,
        help="startup diagnosis segment length in seconds (default=1.0)",
    )
    ap.add_argument(
        "--compare-raw",
        default="",
        help="optional second raw txt path for same-parameter run-to-run comparison",
    )
    ap.add_argument(
        "--auto-trim-tail",
        action="store_true",
        help="auto cut tail segment if collision/stuck signature is detected",
    )
    args = ap.parse_args()

    raw_path = args.raw.strip() or find_latest_raw_txt(args.data_dir)
    if not raw_path:
        raise SystemExit("No raw txt found")

    rows = parse_hb_rows(raw_path)
    if not rows:
        raise SystemExit(f"No HB rows parsed from: {raw_path}")

    use = clip_run_segment(rows, float(args.max_run_s))
    use = clip_steady_segment(use, float(args.skip_s), float(args.tail_s))

    ticks = [r["tick"] for r in use]
    dt = [ticks[i] - ticks[i - 1] for i in range(1, len(ticks))]
    dt_med = _median(dt)

    eds = [r["ed"] for r in use if r["run"] > 0.5]
    els = [r["el"] for r in use if r["run"] > 0.5]
    ers = [r["er"] for r in use if r["run"] > 0.5]
    yaws = [r["yaw_deg"] for r in use if r["run"] > 0.5]
    yrs = [r["yr"] for r in use if r["run"] > 0.5]
    gxs = [r["gx"] for r in use if r["run"] > 0.5]
    gys = [r["gy"] for r in use if r["run"] > 0.5]
    gzs = [r["gz"] for r in use if r["run"] > 0.5]
    ats = [r["at"] for r in use if r["run"] > 0.5]
    out_ls = [r["outL"] for r in use if r["run"] > 0.5]
    out_rs = [r["outR"] for r in use if r["run"] > 0.5]
    out_diffs = [float(a - b) for a, b in zip(out_ls, out_rs)] if out_ls and out_rs else []

    ed_mean = float(statistics.mean(eds)) if eds else 0.0
    ed_rms = math.sqrt(float(statistics.mean([x * x for x in eds]))) if eds else 0.0

    cur_trim = _last_nonzero_value(use, "trim") if use else 0.0
    next_trim, reason = suggest_trim(ed_mean, cur_trim)

    yaw_span = (max(yaws) - min(yaws)) if yaws else 0.0
    yr_rms = math.sqrt(float(statistics.mean([x * x for x in yrs]))) if yrs else 0.0
    gyro_mag = [math.sqrt(a * a + b * b + c * c) for a, b, c in zip(gxs, gys, gzs)] if (gxs and gys and gzs) else []
    gyro_mag_mean = float(statistics.mean(gyro_mag)) if gyro_mag else 0.0
    gyro_mag_rms = math.sqrt(float(statistics.mean([x * x for x in gyro_mag]))) if gyro_mag else 0.0
    at_mean = float(statistics.mean(ats)) if ats else 0.0
    at_rms = math.sqrt(float(statistics.mean([x * x for x in ats]))) if ats else 0.0

    run_use = [r for r in use if r["run"] > 0.5]
    run_ticks = [r["tick"] for r in run_use]
    run_t_s = [(t - run_ticks[0]) / 1000.0 for t in run_ticks] if run_ticks else []
    run_yaw_unwrapped = _unwrap_yaw_deg([r["yaw_deg"] for r in run_use])
    yaw_drift_deg_s = _linear_fit_slope(run_t_s, run_yaw_unwrapped) if (run_t_s and run_yaw_unwrapped) else 0.0
    s_along = reconstruct_s_along_path(run_use, float(args.speed_scale)) if run_use else []
    yaw_drift_deg_per_s = _linear_fit_slope(s_along, run_yaw_unwrapped) if (s_along and run_yaw_unwrapped and (max(s_along) > 1e-6)) else 0.0
    xs_out, ys_out, s_out, kappas_out = reconstruct_path_from_outputs(run_use, float(args.speed_scale)) if run_use else ([0.0], [0.0], [0.0], [0.0])
    jump_info = _find_first_yaw_jump(run_use) if run_use else {"idx": 0, "detected": False, "delta_deg": 0.0}
    jump_detected = bool(jump_info.get("detected", False))
    jump_idx = int(jump_info.get("idx", len(run_use))) if jump_detected else len(run_use)
    stuck_idx = _first_stuck_index(run_use, ys_out, s_out) if run_use else 0
    stuck_detected = bool(run_use) and (stuck_idx < len(run_use))
    valid_cut_idx = len(run_use)
    if jump_detected:
        valid_cut_idx = jump_idx
        if stuck_detected and abs(stuck_idx - jump_idx) <= 30:
            valid_cut_idx = min(stuck_idx, jump_idx)
    elif stuck_detected:
        valid_cut_idx = stuck_idx
    valid_trimmed = bool(args.auto_trim_tail) and (valid_cut_idx < len(run_use))
    valid_run_use = run_use[:valid_cut_idx] if valid_trimmed else run_use
    valid_run_t_s = run_t_s[:len(valid_run_use)] if valid_run_use else []
    valid_ys_out = ys_out[:len(valid_run_use)] if valid_run_use else []
    valid_s_out = s_out[:len(valid_run_use)] if valid_run_use else []
    valid_metrics = _run_metrics(valid_run_use, float(args.speed_scale))
    micro_sig = valid_metrics["micro_sig"]
    valid_eds = [r["ed"] for r in valid_run_use]
    valid_ed_mean = float(statistics.mean(valid_eds)) if valid_eds else ed_mean
    valid_cur_trim = _last_nonzero_value(valid_run_use, "trim") if valid_run_use else cur_trim
    valid_next_trim, valid_reason = suggest_trim(valid_ed_mean, valid_cur_trim)
    if valid_trimmed:
        next_trim = valid_next_trim
        reason = f"基于有效段: {valid_reason}"
    analysis_run_use = valid_run_use if valid_run_use else run_use
    run_segments = split_run_stretches(analysis_run_use)
    window_stats = build_time_windows(run_segments, float(args.speed_scale), float(args.segment_s))
    total_traj = build_total_trajectory(run_segments, float(args.speed_scale))
    lateral_final = ys_out[-1] if ys_out else 0.0
    forward_final = xs_out[-1] if xs_out else 0.0
    path_len = s_out[-1] if s_out else 0.0
    curvature_rms = _rms(kappas_out) if kappas_out else 0.0
    curvature_abs_mean = _mean_abs(kappas_out) if kappas_out else 0.0
    curvature_p95 = _quantile([abs(k) for k in kappas_out], 0.95) if kappas_out else 0.0
    net_heading_change = (run_yaw_unwrapped[-1] - run_yaw_unwrapped[0]) if len(run_yaw_unwrapped) >= 2 else 0.0
    total_heading_variation = float(sum(abs(run_yaw_unwrapped[i] - run_yaw_unwrapped[i - 1]) for i in range(1, len(run_yaw_unwrapped)))) if len(run_yaw_unwrapped) >= 2 else 0.0
    sinuosity = (path_len / max(1e-9, math.sqrt(forward_final * forward_final + lateral_final * lateral_final))) if path_len > 1e-9 else 1.0
    lateral_per_path = (lateral_final / path_len) if path_len > 1e-9 else 0.0
    lateral_abs_per_path = (abs(lateral_final) / path_len) if path_len > 1e-9 else 0.0
    out_diff_mean = _mean(out_diffs) if out_diffs else 0.0
    out_diff_abs_mean = _mean_abs(out_diffs) if out_diffs else 0.0
    out_l_mean = _mean(out_ls) if out_ls else 0.0
    out_r_mean = _mean(out_rs) if out_rs else 0.0
    out_asym_ratio = (out_r_mean / out_l_mean) if abs(out_l_mean) > 1e-9 else 0.0
    yaw_step = [run_yaw_unwrapped[i] - run_yaw_unwrapped[i - 1] for i in range(1, len(run_yaw_unwrapped))] if len(run_yaw_unwrapped) >= 2 else []
    out_diff_step = out_diffs[1:1 + len(yaw_step)] if len(out_diffs) >= len(yaw_step) + 1 else out_diffs[:len(yaw_step)]
    corr_out_yawstep = _corr(out_diff_step, yaw_step[:len(out_diff_step)]) if out_diff_step and yaw_step else 0.0
    lateral_window_slopes = _window_endpoint_slopes(run_t_s, ys_out, 0.5) if (run_t_s and len(ys_out) == len(run_t_s)) else []
    lateral_drift_mean = _mean(lateral_window_slopes) if lateral_window_slopes else 0.0
    lateral_drift_abs_p95 = _quantile([abs(v) for v in lateral_window_slopes], 0.95) if lateral_window_slopes else 0.0
    seg_stats = [summarize_segment(seg, float(args.speed_scale)) for seg in segment_rows(run_use, float(args.segment_s))] if run_use else []
    side_bias = summarize_side_bias(ys_out, lateral_window_slopes) if ys_out else _empty_side_bias()
    phase_drift = valid_metrics["phase_drift"] if valid_metrics else summarize_phase_drift(run_use, float(args.speed_scale))
    startup_behavior = summarize_startup_behavior(run_use, float(args.speed_scale), float(args.startup_s), float(args.startup_seg_s)) if run_use else {"segments": []}

    robust_used = False
    robust_keep_ratio = 1.0
    robust_conf = 0.0
    robust_yaw_drift_deg_s = yaw_drift_deg_s
    robust_yaw_drift_std = 0.0
    robust_yr_rms = yr_rms
    robust_yr_zc = _count_zero_crossings(yrs, eps=0.2) if yrs else 0

    if bool(args.robust) and run_t_s and run_yaw_unwrapped:
        robust_used = True

        yaw_sm = _rolling_median(run_yaw_unwrapped, int(args.med_win))
        res = [float(a - b) for a, b in zip(run_yaw_unwrapped, yaw_sm)]
        mask = _robust_keep_mask(res, float(args.mad_k))

        x_f = [t for t, ok in zip(run_t_s, mask) if ok]
        y_f = [y for y, ok in zip(yaw_sm, mask) if ok]
        keep_n = len(x_f)
        total_n = len(run_t_s)
        robust_keep_ratio = (float(keep_n) / float(total_n)) if total_n > 0 else 0.0

        robust_yaw_drift_deg_s = _linear_fit_slope(x_f, y_f) if keep_n >= 4 else yaw_drift_deg_s
        win_slopes = _window_slopes(x_f, y_f, float(args.win_s))
        robust_yaw_drift_std = _std(win_slopes) if win_slopes else 0.0

        yr_sm = _rolling_median(yrs, int(args.med_win)) if yrs else []
        robust_yr_rms = _rms(yr_sm) if yr_sm else yr_rms
        robust_yr_zc = _count_zero_crossings(yr_sm, eps=0.2) if yr_sm else robust_yr_zc

        conf = 1.0
        conf *= max(0.0, min(1.0, (robust_keep_ratio - 0.70) / 0.25))
        conf *= max(0.0, min(1.0, 1.0 - (robust_yaw_drift_std / 6.0)))
        robust_conf = max(0.0, min(1.0, conf))

    print(f"RAW: {raw_path}")
    print(f"HB rows parsed: {len(rows)}")
    print(f"USE rows: {len(use)}  (run rows: {sum(1 for r in use if r['run']>0.5)})")
    print(f"tick dt median(ms): {dt_med:.1f}")
    print(f"trim(last): {cur_trim:.4f}")
    if eds:
        print(f"ed mean: {ed_mean:.3f}  ed rms: {ed_rms:.3f}")
    if els and ers:
        mean_el = float(statistics.mean([abs(x) for x in els]))
        mean_er = float(statistics.mean([abs(x) for x in ers]))
        ratio = (mean_er / mean_el) if mean_el > 1e-6 else 0.0
        print(f"mean|el|: {mean_el:.3f}  mean|er|: {mean_er:.3f}  ratio(er/el): {ratio:.3f}")
    if yaws:
        print(f"yaw span(deg): {yaw_span:.2f}")
    if run_t_s and run_yaw_unwrapped:
        print(f"yaw drift slope(deg/s): {yaw_drift_deg_s:.4f}")
        if robust_used:
            print(f"yaw drift slope robust(deg/s): {robust_yaw_drift_deg_s:.4f}  win_std: {robust_yaw_drift_std:.3f}  keep: {robust_keep_ratio:.2f}  conf: {robust_conf:.2f}")
    if s_along and run_yaw_unwrapped and (max(s_along) > 1e-6):
        print(f"yaw drift per dist(deg/rel_dist): {yaw_drift_deg_per_s:.4f}")
    if yrs:
        print(f"yaw_rate yr rms: {yr_rms:.3f}")
        print(f"yaw_rate yr zc(eps=0.2): {_count_zero_crossings(yrs, eps=0.2)}")
        if robust_used:
            print(f"yaw_rate yr rms robust: {robust_yr_rms:.3f}  zc: {robust_yr_zc}")
    if out_diffs:
        print(f"out diff mean(OL-OR): {out_diff_mean:.3f}  abs_mean: {out_diff_abs_mean:.3f}")
        print(f"out mean L/R: {out_l_mean:.3f}/{out_r_mean:.3f}  ratio(R/L): {out_asym_ratio:.3f}")
    if run_use:
        motion_state = summarize_motion_state(run_use, run_t_s[-1] if run_t_s else 0.0)
        print(f"path final x/y(rel): {forward_final:.3f}/{lateral_final:.3f}")
        print(f"path len(rel): {path_len:.3f}  sinuosity: {sinuosity:.3f}")
        print(f"lateral drift per path: {lateral_per_path:.6f}  abs_per_path: {lateral_abs_per_path:.6f}")
        print(f"net heading change(deg): {net_heading_change:.3f}  total heading variation(deg): {total_heading_variation:.3f}")
        print(f"curvature rms: {curvature_rms:.6f}  abs_mean: {curvature_abs_mean:.6f}  p95: {curvature_p95:.6f}")
        print(
            f"motion state: {motion_state['motion_level']}  dist_proxy={float(motion_state['dist_proxy']):.3f}  speed_proxy={float(motion_state['speed_proxy']):.3f}"
        )
        print(
            f"motion left/right proxy: {float(motion_state['left_dist_proxy']):.3f}/{float(motion_state['right_dist_proxy']):.3f}  enc_nonzero={float(motion_state['enc_nonzero_ratio']):.3f}  enc_active={float(motion_state['enc_active_ratio']):.3f}"
        )
        print(
            f"motion out_nonzero={float(motion_state['out_nonzero_ratio']):.3f}  out_strong={float(motion_state['out_strong_ratio']):.3f}  output_only_ratio={float(motion_state['output_only_ratio']):.3f}  stiction={int(float(motion_state['stiction_suspected']) > 0.5)}"
        )
        if str(motion_state['motion_level']) != 'moving':
            print("WARN motion: 当前轨迹/直线性指标可能建立在“车几乎没动或仅低速爬行”的基础上，需优先判断静摩擦/起步不足。")
        print(f"corr(out_diff, yaw_step): {corr_out_yawstep:.4f}")
        print(f"lateral short-window drift mean: {lateral_drift_mean:.6f}  abs_p95: {lateral_drift_abs_p95:.6f}")
        print(
            f"micro drift delta/slope: {float(micro_sig['micro_delta_mean']):.6f}/{float(micro_sig['micro_slope_mean']):.6f}  side={micro_sig['micro_side']}({float(micro_sig['micro_conf']):.3f}) slope_side={micro_sig['micro_slope_side']}({float(micro_sig['micro_slope_conf']):.3f})"
        )
        print(
            f"side bias y@25/50/75/100: {float(side_bias['y25']):.4f}/{float(side_bias['y50']):.4f}/{float(side_bias['y75']):.4f}/{float(side_bias['y100']):.4f}"
        )
        print(
            f"side vote path/slopes: {side_bias['y_side']}({float(side_bias['y_conf']):.3f}) / {side_bias['slope_side']}({float(side_bias['slope_conf']):.3f})"
        )
        print_phase_drift(phase_drift)
        print_startup_behavior(startup_behavior)
        if jump_detected:
            jump_t = float(run_use[jump_idx]["tick"] - run_use[0]["tick"]) / 1000.0
            print(
                f"yaw jump detected at t={jump_t:.3f}s  delta={float(jump_info['delta_deg']):.3f}deg  prev/cur yaw={float(jump_info['prev_yaw']):.3f}/{float(jump_info['cur_yaw']):.3f}"
            )
            print(
                f"yaw jump context prev(pc/hd,pwm,aa/ae/ao,outL/outR,ed,trim,yr)=({float(jump_info['prev_pc']):.1f}/{float(jump_info['prev_hd']):.1f},{float(jump_info['prev_pwm']):.1f},{float(jump_info['prev_aa']):.3f}/{float(jump_info['prev_ae']):.3f}/{float(jump_info['prev_ao']):.3f},{float(jump_info['prev_outL']):.1f}/{float(jump_info['prev_outR']):.1f},{float(jump_info['prev_ed']):.1f},{float(jump_info['prev_trim']):.4f},{float(jump_info['prev_yr']):.3f}) cur=({float(jump_info['cur_pc']):.1f}/{float(jump_info['cur_hd']):.1f},{float(jump_info['cur_pwm']):.1f},{float(jump_info['cur_aa']):.3f}/{float(jump_info['cur_ae']):.3f}/{float(jump_info['cur_ao']):.3f},{float(jump_info['cur_outL']):.1f}/{float(jump_info['cur_outR']):.1f},{float(jump_info['cur_ed']):.1f},{float(jump_info['cur_trim']):.4f},{float(jump_info['cur_yr']):.3f})"
            )
        if stuck_detected:
            stuck_t = float(run_use[stuck_idx]["tick"] - run_use[0]["tick"]) / 1000.0
            print(f"collision/stuck detected at t={stuck_t:.3f}s  valid_rows={len(valid_run_use)}/{len(run_use)}  auto_trim_tail={'on' if bool(args.auto_trim_tail) else 'off'}")
        if valid_trimmed and valid_run_use:
            valid_side_bias = valid_metrics["side_bias"]
            valid_micro_sig = valid_metrics["micro_sig"]
            print(
                f"valid segment x/y(rel): {float(valid_metrics['forward_final']):.3f}/{float(valid_metrics['lateral_final']):.3f}  len={float(valid_metrics['path_len']):.3f}  lateral_per_path={float(valid_metrics['lateral_per_path']):.6f}"
            )
            print(
                f"valid segment yaw drift/dist: {float(valid_metrics['yaw_drift_deg_s']):.4f}deg/s / {float(valid_metrics['yaw_drift_deg_per_s']):.4f}deg_per_rel  total_var={float(valid_metrics['total_heading_variation']):.3f}"
            )
            print(
                f"valid segment micro drift: {float(valid_micro_sig['micro_delta_mean']):.6f}/{float(valid_micro_sig['micro_slope_mean']):.6f}  side={valid_micro_sig['micro_side']}({float(valid_micro_sig['micro_conf']):.3f})"
            )
            print(
                f"valid segment side vote: {valid_side_bias['y_side']}({float(valid_side_bias['y_conf']):.3f}) / {valid_side_bias['slope_side']}({float(valid_side_bias['slope_conf']):.3f})"
            )
            print(f"valid segment trim suggestion: {valid_next_trim:.4f}  reason: {valid_reason}")
    if gyro_mag:
        print(f"gyro |g| mean: {gyro_mag_mean:.3f}  rms: {gyro_mag_rms:.3f}")
    if ats:
        print(f"auto_trim at mean: {at_mean:.3f}  rms: {at_rms:.3f}  range: ({min(ats):.3f},{max(ats):.3f})")

    if seg_stats:
        print("SEGMENTS:")
        for i, s in enumerate(seg_stats, start=1):
            print(
                f"- seg{i:02d} t={s['t0_s']:.2f}->{s['t1_s']:.2f}s "
                f"yaw_span={s['yaw_span_deg']:.2f} drift={s['yaw_drift_deg_s']:.3f}deg/s "
                f"yr_rms={s['yr_rms']:.3f} ed_mean={s['ed_mean']:.2f} ed_rms={s['ed_rms']:.2f} "
                f"out_diff={s['out_diff_mean']:.2f}/{s['out_diff_abs_mean']:.2f} "
                f"xy=({s['forward_final']:.2f},{s['lateral_final']:.2f}) curv_rms={s['curvature_rms']:.6f}"
            )

    print_time_windows(window_stats, float(args.segment_s))
    print_total_trajectory(total_traj)

    print("SUGGEST TRIM:")
    print(f"- next_trim: {next_trim:.4f}")
    print(f"- reason: {reason}")

    at_sugg = suggest_at_gains(yrs, ats, float(args.at_lim))
    if at_sugg:
        print("SUGGEST AT:")
        for s in at_sugg:
            print(f"- {s}")

    if args.compare_raw:
        rows_b = parse_hb_rows(args.compare_raw)
        if not rows_b:
            raise SystemExit(f"No HB rows parsed from compare file: {args.compare_raw}")
        use_b = clip_run_segment(rows_b, float(args.max_run_s))
        use_b = clip_steady_segment(use_b, float(args.skip_s), float(args.tail_s))
        run_b = [r for r in use_b if r["run"] > 0.5]
        ticks_b = [r["tick"] for r in run_b]
        ts_b = [(t - ticks_b[0]) / 1000.0 for t in ticks_b] if ticks_b else []
        yaw_b = _unwrap_yaw_deg([r["yaw_deg"] for r in run_b])
        s_b = reconstruct_s_along_path(run_b, float(args.speed_scale)) if run_b else []
        xs_b, ys_b, s_out_b, _ = reconstruct_path_from_outputs(run_b, float(args.speed_scale)) if run_b else ([0.0], [0.0], [0.0], [0.0])
        out_ls_b = [r["outL"] for r in use_b if r["run"] > 0.5]
        out_rs_b = [r["outR"] for r in use_b if r["run"] > 0.5]
        out_diffs_b = [float(a - b) for a, b in zip(out_ls_b, out_rs_b)] if out_ls_b and out_rs_b else []
        lateral_window_slopes_b = _window_endpoint_slopes(ts_b, ys_b, 0.5) if (ts_b and len(ys_b) == len(ts_b)) else []
        side_bias_b = summarize_side_bias(ys_b, lateral_window_slopes_b) if ys_b else {
            "y_side": "balanced",
            "slope_side": "balanced",
            "y_conf": 0.0,
            "slope_conf": 0.0,
        }
        metrics_a = {
            "lateral_final": lateral_final,
            "lateral_abs_per_path": lateral_abs_per_path,
            "yaw_drift_robust": robust_yaw_drift_deg_s,
            "out_diff_abs_mean": out_diff_abs_mean,
            "dist_proxy": float(motion_state["dist_proxy"]),
            "speed_proxy": float(motion_state["speed_proxy"]),
            "lateral_short_abs_p95": lateral_drift_abs_p95,
            "total_heading_variation": total_heading_variation,
            "y_side": side_bias["y_side"],
            "slope_side": side_bias["slope_side"],
            "y_side_conf": side_bias["y_conf"],
            "slope_side_conf": side_bias["slope_conf"],
        }
        metrics_b = {
            "lateral_final": (ys_b[-1] if ys_b else 0.0),
            "lateral_abs_per_path": (abs(ys_b[-1]) / s_out_b[-1]) if ys_b and s_out_b and s_out_b[-1] > 1e-9 else 0.0,
            "yaw_drift_robust": (_linear_fit_slope(ts_b, yaw_b) if (ts_b and yaw_b) else 0.0),
            "out_diff_abs_mean": _mean_abs(out_diffs_b) if out_diffs_b else 0.0,
            "dist_proxy": float(summarize_motion_state(run_b, ts_b[-1] if ts_b else 0.0)["dist_proxy"]),
            "speed_proxy": float(summarize_motion_state(run_b, ts_b[-1] if ts_b else 0.0)["speed_proxy"]),
            "lateral_short_abs_p95": _quantile([abs(v) for v in lateral_window_slopes_b], 0.95) if lateral_window_slopes_b else 0.0,
            "total_heading_variation": float(sum(abs(yaw_b[i] - yaw_b[i - 1]) for i in range(1, len(yaw_b)))) if len(yaw_b) >= 2 else 0.0,
            "y_side": side_bias_b["y_side"],
            "slope_side": side_bias_b["slope_side"],
            "y_side_conf": side_bias_b["y_conf"],
            "slope_side_conf": side_bias_b["slope_conf"],
        }
        print_compare_block(os.path.basename(raw_path), os.path.basename(args.compare_raw), metrics_a, metrics_b)

    do_plot = bool(args.plot or args.save)
    if do_plot:
        if plt is None:
            raise SystemExit("matplotlib not available, cannot plot")

        xs, ys, _ts = reconstruct_path(use, float(args.speed_scale))
        fig, axes = plt.subplots(2, 2, figsize=(10, 8))
        ax0 = axes[0][0]
        ax1 = axes[0][1]
        ax2 = axes[1][0]
        ax3 = axes[1][1]

        ax0.plot(xs, ys, "-k", linewidth=1, label="enc path")
        ax0.plot(xs_out, ys_out, "-r", linewidth=1, label="out path")
        ax0.axis("equal")
        ax0.grid(True)
        ax0.set_title("plane trajectory")
        ax0.set_xlabel("x (relative)")
        ax0.set_ylabel("y (relative)")
        ax0.legend()

        ax1.plot(run_t_s, run_yaw_unwrapped, "-b", linewidth=1)
        ax1.grid(True)
        ax1.set_title("yaw vs time")
        ax1.set_xlabel("t (s)")
        ax1.set_ylabel("yaw (deg)")

        if out_diffs:
            ax2.plot(run_t_s[:len(out_diffs)], out_diffs, "-m", linewidth=1, label="OL-OR")
        if eds:
            ax2.plot(run_t_s[:len(eds)], eds, "-g", linewidth=1, alpha=0.7, label="ed")
        ax2.grid(True)
        ax2.set_title("output diff and encoder diff")
        ax2.set_xlabel("t (s)")
        ax2.legend()

        ax3.plot(run_t_s[:len(kappas_out)], kappas_out, "-c", linewidth=1, label="curvature")
        if yrs:
            ax3.plot(run_t_s[:len(yrs)], yrs, "-y", linewidth=1, alpha=0.6, label="yr")
        ax3.grid(True)
        ax3.set_title("curvature and yaw rate")
        ax3.set_xlabel("t (s)")
        ax3.legend()

        if total_traj.get("xs") and total_traj.get("ys"):
            ax0.cla()
            ax0.plot(total_traj["xs"], total_traj["ys"], "-k", linewidth=1, label="enc path stitched")
            ax0.axis("equal")
            ax0.grid(True)
            ax0.set_title("plane trajectory")
            ax0.set_xlabel("x (relative)")
            ax0.set_ylabel("y (relative)")
            ax0.legend()

        fig.suptitle(os.path.basename(raw_path))
        fig.tight_layout()

        if args.save:
            out_path = args.save
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
            plt.savefig(out_path, dpi=150)
            print("PLOT SAVED:", out_path)
        else:
            plt.show()


if __name__ == "__main__":
    main()
