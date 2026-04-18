#!/usr/bin/env python3
"""
drift_analyzer.py — 精细直线偏移分析工具
=========================================
专为 Project_Refactor-0412 TEXT 遥测设计。

功能：
  1. 串口实时采集 或 读取保存的日志文件
  2. 解析 HB: 遥测帧，提取全部字段
  3. 自动清洗重复帧 / 时间戳复位造成的脏数据
  4. 基于编码器差速重建实际航迹，并保留 IMU yaw 参考航迹
  5. 同时评估：
     - 起点坐标系下的终点横偏
     - 最佳拟合直线下的横向误差(真正的“直线性”)
  6. 分段统计（启动 / 早期巡航 / 晚期巡航）
  7. 逐秒详细报表
  8. matplotlib 四面板可视化
  9. 导出 CSV + JSON 报告

用法：
  # 实时采集 8 秒
  python drift_analyzer.py --live --duration 8

  # 读已保存日志
  python drift_analyzer.py --file captured.log

  # 不画图（CI / 脚本模式）
  python drift_analyzer.py --live --duration 8 --no-plot
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional

import numpy as np

# ─── 机器人物理参数 ───
WHEEL_DIAMETER_M = 0.065
ENCODER_CPR = 390.0
DIST_PER_COUNT = math.pi * WHEEL_DIAMETER_M / ENCODER_CPR  # ≈0.524 mm
TRACK_WIDTH_M = 0.145
CONTROL_PERIOD_MS = 10  # 固件控制周期

# ─── 分析阈值 ───
PHASE_STARTUP_END_S = 2.0
PHASE_EARLY_END_S = 5.0
MOTION_THRESHOLD_ENC = 1  # encoder delta 阈值


# ════════════════════════════════════════════════════════════════
# 数据结构
# ════════════════════════════════════════════════════════════════

@dataclass
class Sample:
    """一条 HB 遥测帧解析后的数据"""
    t_ms: int = 0
    run: int = 0
    el: int = 0       # encoder left delta
    er: int = 0       # encoder right delta
    yaw: float = 0.0  # deg
    yr: float = 0.0   # yaw rate (deg/s)
    pc: int = 0       # pwm core
    hd: int = 0       # heading diff
    dp: int = 0       # dPostDZ (legacy)
    ol: int = 0       # actual motor left
    or_: int = 0      # actual motor right
    hi: float = 0.0   # heading integral
    # 计算字段
    t_s: float = 0.0
    dt_s: float = 0.0
    ds_m: float = 0.0   # 前进距离
    v_mps: float = 0.0  # 线速度
    # 编码器差速里程计(主航迹，反映实际路径)
    x_m: float = 0.0    # 航迹 x (前进方向)
    y_m: float = 0.0    # 航迹 y (侧向，正=左偏)
    y_mm: float = 0.0   # 侧向偏移 mm
    theta_enc: float = 0.0  # 编码器推算航向 (rad)
    # IMU yaw 航迹(参考，可能掩盖实际漂移)
    x_imu: float = 0.0
    y_imu: float = 0.0
    y_imu_mm: float = 0.0
    cum_el: int = 0
    cum_er: int = 0


@dataclass
class CleanStats:
    raw_count: int = 0
    segment_count: int = 0
    selected_segment_index: int = 0
    dropped_reset_samples: int = 0
    duplicates_removed: int = 0
    time_offset_ms: int = 0
    final_count: int = 0


@dataclass
class StraightnessStats:
    fit_sample_count: int = 0
    fit_start_s: float = 0.0
    fit_end_s: float = 0.0
    line_heading_deg: float = 0.0
    cross_track_end_mm: float = 0.0
    cross_track_rms_mm: float = 0.0
    cross_track_max_abs_mm: float = 0.0
    start_axis_end_mm: float = 0.0


def parse_hb_line(line: str) -> Optional[Sample]:
    """解析一条 HB: 遥测行"""
    if not line.startswith("HB:"):
        return None
    try:
        kv: dict[str, str] = {}
        for pair in line[3:].split(","):
            if "=" not in pair:
                continue
            k, v = pair.split("=", 1)
            kv[k.strip()] = v.strip()

        s = Sample()
        s.t_ms = int(kv.get("t", "0"))
        s.run = int(kv.get("run", "0"))
        s.el = int(kv.get("el", "0"))
        s.er = int(kv.get("er", "0"))
        s.yaw = float(kv.get("yaw", "0"))
        s.yr = float(kv.get("yr", "0"))
        s.pc = int(kv.get("pc", "0"))
        s.hd = int(kv.get("hd", "0"))
        s.dp = int(kv.get("dp", "0"))
        s.ol = int(kv.get("OL", "0"))
        s.or_ = int(kv.get("OR", "0"))
        s.hi = float(kv.get("hi", "0"))
        return s
    except Exception:
        return None


def sample_identity(sample: Sample) -> tuple[Any, ...]:
    return (
        sample.t_ms, sample.el, sample.er, sample.yaw, sample.yr,
        sample.pc, sample.hd, sample.dp, sample.ol, sample.or_, sample.hi,
    )


def sanitize_samples(samples: list[Sample]) -> tuple[list[Sample], CleanStats]:
    """清洗样本:
    1. 只保留时间戳单调递增的最长连续段，去掉 run 重启前残帧
    2. 去掉连续重复的 HB 帧，避免里程计/横偏重复积分
    3. 时间归一化到 0ms，便于分阶段统计
    """
    info = CleanStats(raw_count=len(samples))
    if not samples:
        return samples, info

    segments: list[list[Sample]] = []
    current: list[Sample] = []
    prev_t: Optional[int] = None

    for sample in samples:
        if prev_t is not None and sample.t_ms < prev_t:
            if current:
                segments.append(current)
            current = [sample]
        else:
            current.append(sample)
        prev_t = sample.t_ms
    if current:
        segments.append(current)

    info.segment_count = len(segments)
    if not segments:
        return [], info

    selected_index = max(
        range(len(segments)),
        key=lambda i: (
            segments[i][-1].t_ms - segments[i][0].t_ms,
            len(segments[i]),
        ),
    )
    info.selected_segment_index = selected_index
    info.dropped_reset_samples = len(samples) - len(segments[selected_index])

    cleaned: list[Sample] = []
    prev_key: Optional[tuple[Any, ...]] = None
    for sample in segments[selected_index]:
        key = sample_identity(sample)
        if key == prev_key:
            info.duplicates_removed += 1
            continue
        cleaned.append(sample)
        prev_key = key

    if cleaned:
        info.time_offset_ms = cleaned[0].t_ms
        t0 = cleaned[0].t_ms
        for sample in cleaned:
            sample.t_ms -= t0
    info.final_count = len(cleaned)
    return cleaned, info


def unwrap_degrees(values: np.ndarray) -> np.ndarray:
    if values.size == 0:
        return values
    return np.degrees(np.unwrap(np.radians(values)))
    try:
        kv: dict[str, str] = {}
        for pair in line[3:].split(","):
            if "=" not in pair:
                continue
            k, v = pair.split("=", 1)
            kv[k.strip()] = v.strip()

        s = Sample()
        s.t_ms = int(kv.get("t", "0"))
        s.run = int(kv.get("run", "0"))
        s.el = int(kv.get("el", "0"))
        s.er = int(kv.get("er", "0"))
        s.yaw = float(kv.get("yaw", "0"))
        s.yr = float(kv.get("yr", "0"))
        s.pc = int(kv.get("pc", "0"))
        s.hd = int(kv.get("hd", "0"))
        s.dp = int(kv.get("dp", "0"))
        s.ol = int(kv.get("OL", "0"))
        s.or_ = int(kv.get("OR", "0"))
        s.hi = float(kv.get("hi", "0"))
        return s
    except Exception:
        return None


# ════════════════════════════════════════════════════════════════
# 航迹重建
# ════════════════════════════════════════════════════════════════

def reconstruct_trajectory(samples: list[Sample]) -> list[Sample]:
    """双路径重建：IMU yaw 航迹(主) + 编码器差速航迹(参考)。

    BNO085 旋转向量提供绝对航向，精度远超编码器差速积分。
    因此主航迹使用 IMU yaw 做航向、编码器做前进距离。
    编码器差速航迹仅作参考（会因轮径差/打滑大幅漂移）。
    """
    # ── IMU 主航迹 ──
    x_imu, y_imu = 0.0, 0.0
    # ── 编码器差速航迹(参考) ──
    x_enc, y_enc, theta_enc = 0.0, 0.0, 0.0
    cum_el, cum_er = 0, 0
    prev_t_ms: Optional[int] = None
    prev_yaw_rad: Optional[float] = None

    for s in samples:
        # ── 时间 ──
        if prev_t_ms is not None:
            dt_ms = s.t_ms - prev_t_ms
            if dt_ms <= 0:
                dt_ms = 1  # sanitize already removed true dups; 1ms for safety
        else:
            dt_ms = CONTROL_PERIOD_MS
        s.dt_s = dt_ms * 0.001
        s.t_s = s.t_ms * 0.001

        # ── 编码器距离 ──
        dl = s.el * DIST_PER_COUNT
        dr = s.er * DIST_PER_COUNT
        ds = (dl + dr) * 0.5
        s.ds_m = ds
        s.v_mps = ds / s.dt_s if s.dt_s > 0.001 else 0.0

        # ── 主航迹: 编码器距离 + IMU 航向 ──
        # IMU yaw 使用 CW-positive 约定 (正=顺时针=右转)
        # 航迹数学使用 CCW-positive 标准约定，需取反
        yaw_rad = -math.radians(s.yaw)
        if prev_yaw_rad is not None:
            yaw_mid = 0.5 * (prev_yaw_rad + yaw_rad)
            if abs(yaw_rad - prev_yaw_rad) > math.pi:
                yaw_mid = yaw_rad
        else:
            yaw_mid = yaw_rad

        x_imu += ds * math.cos(yaw_mid)
        y_imu += ds * math.sin(yaw_mid)

        # ── 编码器差速航迹(参考) ──
        # 左轮快(dl>dr) → 车右转(CW) → CCW-positive 下 dtheta < 0
        dtheta_enc = (dl - dr) / TRACK_WIDTH_M
        theta_mid_enc = theta_enc + 0.5 * dtheta_enc
        x_enc += ds * math.cos(theta_mid_enc)
        y_enc += ds * math.sin(theta_mid_enc)
        theta_enc += dtheta_enc

        # ── 存储: 主航迹 = IMU, 参考 = 编码器 ──
        s.x_m = x_imu
        s.y_m = y_imu
        s.y_mm = y_imu * 1000.0
        s.theta_enc = theta_enc
        s.x_imu = x_enc          # 字段名保持兼容，但现在存编码器航迹
        s.y_imu = y_enc
        s.y_imu_mm = y_enc * 1000.0

        cum_el += s.el
        cum_er += s.er
        s.cum_el = cum_el
        s.cum_er = cum_er

        prev_t_ms = s.t_ms
        prev_yaw_rad = yaw_rad

    return samples


# ════════════════════════════════════════════════════════════════
# 统计分析
# ════════════════════════════════════════════════════════════════

@dataclass
class PhaseStats:
    """一个时间段的统计"""
    name: str = ""
    t_start_s: float = 0.0
    t_end_s: float = 0.0
    n_samples: int = 0
    # Yaw
    yaw_mean: float = 0.0
    yaw_std: float = 0.0
    yaw_min: float = 0.0
    yaw_max: float = 0.0
    yaw_abs_max: float = 0.0
    yaw_drift_rate_dps: float = 0.0  # 线性拟合斜率
    # 侧向偏移
    lat_start_mm: float = 0.0
    lat_end_mm: float = 0.0
    lat_net_mm: float = 0.0
    lat_max_abs_mm: float = 0.0
    lat_rms_mm: float = 0.0
    lat_drift_rate_mm_s: float = 0.0
    # 电机
    ol_mean: float = 0.0
    or_mean: float = 0.0
    motor_diff_mean: float = 0.0   # OL - OR 均值
    motor_diff_abs_mean: float = 0.0
    hd_mean: float = 0.0
    hd_abs_mean: float = 0.0
    # 编码器
    el_sum: int = 0
    er_sum: int = 0
    enc_diff: int = 0  # el_sum - er_sum
    enc_ratio: float = 1.0
    # PID 积分
    hi_mean: float = 0.0
    hi_end: float = 0.0
    hi_abs_max: float = 0.0
    # 速度
    speed_mean_mps: float = 0.0
    pc_mean: float = 0.0


def compute_phase_stats(samples: list[Sample], name: str,
                        t_start: float, t_end: float) -> PhaseStats:
    """计算一个时间段的详细统计"""
    ps = PhaseStats(name=name, t_start_s=t_start, t_end_s=t_end)
    phase = [s for s in samples if t_start <= s.t_s < t_end and s.run == 1]
    if not phase:
        return ps

    ps.n_samples = len(phase)

    # Yaw
    yaws = np.array([s.yaw for s in phase])
    yaws_unwrapped = unwrap_degrees(yaws)
    ps.yaw_mean = float(np.mean(yaws))
    ps.yaw_std = float(np.std(yaws))
    ps.yaw_min = float(np.min(yaws))
    ps.yaw_max = float(np.max(yaws))
    ps.yaw_abs_max = float(np.max(np.abs(yaws)))

    # Yaw 线性拟合
    ts = np.array([s.t_s for s in phase])
    if len(ts) >= 2 and (ts[-1] - ts[0]) > 0.1:
        coeffs = np.polyfit(ts, yaws_unwrapped, 1)
        ps.yaw_drift_rate_dps = float(coeffs[0])

    # 侧向偏移
    lats = np.array([s.y_mm for s in phase])
    ps.lat_start_mm = float(lats[0])
    ps.lat_end_mm = float(lats[-1])
    ps.lat_net_mm = ps.lat_end_mm - ps.lat_start_mm
    ps.lat_max_abs_mm = float(np.max(np.abs(lats)))
    ps.lat_rms_mm = float(np.sqrt(np.mean(lats ** 2)))
    if len(ts) >= 2 and (ts[-1] - ts[0]) > 0.1:
        coeffs = np.polyfit(ts, lats, 1)
        ps.lat_drift_rate_mm_s = float(coeffs[0])

    # 电机
    ols = np.array([s.ol for s in phase], dtype=float)
    ors = np.array([s.or_ for s in phase], dtype=float)
    ps.ol_mean = float(np.mean(ols))
    ps.or_mean = float(np.mean(ors))
    diffs = ols - ors
    ps.motor_diff_mean = float(np.mean(diffs))
    ps.motor_diff_abs_mean = float(np.mean(np.abs(diffs)))
    hds = np.array([s.hd for s in phase], dtype=float)
    ps.hd_mean = float(np.mean(hds))
    ps.hd_abs_mean = float(np.mean(np.abs(hds)))

    # 编码器
    ps.el_sum = sum(s.el for s in phase)
    ps.er_sum = sum(s.er for s in phase)
    ps.enc_diff = ps.el_sum - ps.er_sum
    if ps.er_sum != 0:
        ps.enc_ratio = ps.el_sum / ps.er_sum
    else:
        ps.enc_ratio = float('inf') if ps.el_sum != 0 else 1.0

    # PID 积分
    his = np.array([s.hi for s in phase])
    ps.hi_mean = float(np.mean(his))
    ps.hi_end = float(his[-1])
    ps.hi_abs_max = float(np.max(np.abs(his)))

    # 速度: 用 Σdist/Σtime 代替 mean(v_mps)，避免变采样率失真
    total_dist = sum(s.ds_m for s in phase)
    total_time = ts[-1] - ts[0] if len(ts) >= 2 else 1.0
    ps.speed_mean_mps = total_dist / total_time if total_time > 0.01 else 0.0
    pcs = np.array([s.pc for s in phase], dtype=float)
    ps.pc_mean = float(np.mean(pcs))

    return ps


def compute_straightness(samples: list[Sample]) -> StraightnessStats:
    """对主航迹做最佳拟合直线，衡量真正的“直线性”。

    - `lat_end_mm`: 起点坐标系终点横偏，反映“最终偏到哪边”
    - `cross_track_*`: 相对最佳拟合直线的误差，反映“走得直不直”
    """
    stats = StraightnessStats()
    run = [s for s in samples if s.run == 1]
    if len(run) < 2:
        return stats

    moving = [s for s in run if (abs(s.el) + abs(s.er)) >= MOTION_THRESHOLD_ENC and s.t_s >= PHASE_STARTUP_END_S]
    if len(moving) < 10:
        moving = [s for s in run if (abs(s.el) + abs(s.er)) >= MOTION_THRESHOLD_ENC]
    if len(moving) < 2:
        return stats

    pts = np.array([[s.x_m, s.y_m] for s in moving], dtype=float)
    center = np.mean(pts, axis=0)
    _, _, vh = np.linalg.svd(pts - center, full_matrices=False)
    tangent = vh[0]
    span = pts[-1] - pts[0]
    if float(np.dot(tangent, span)) < 0.0:
        tangent = -tangent
    normal = np.array([-tangent[1], tangent[0]], dtype=float)
    signed = (pts - center) @ normal

    stats.fit_sample_count = len(moving)
    stats.fit_start_s = moving[0].t_s
    stats.fit_end_s = moving[-1].t_s
    stats.line_heading_deg = math.degrees(math.atan2(float(tangent[1]), float(tangent[0])))
    stats.cross_track_end_mm = float(signed[-1] * 1000.0)
    stats.cross_track_rms_mm = float(np.sqrt(np.mean(signed ** 2)) * 1000.0)
    stats.cross_track_max_abs_mm = float(np.max(np.abs(signed)) * 1000.0)
    stats.start_axis_end_mm = float(run[-1].y_mm)
    return stats


def compute_per_second(samples: list[Sample]) -> list[PhaseStats]:
    """逐秒统计"""
    if not samples:
        return []
    run_samples = [s for s in samples if s.run == 1]
    if not run_samples:
        return []
    t_max = max(s.t_s for s in run_samples)
    results = []
    t = 0.0
    sec = 0
    while t < t_max:
        ps = compute_phase_stats(samples, f"sec{sec}", t, t + 1.0)
        if ps.n_samples > 0:
            results.append(ps)
        t += 1.0
        sec += 1
    return results


# ════════════════════════════════════════════════════════════════
# 偏移方向判定
# ════════════════════════════════════════════════════════════════

def classify_drift(full: PhaseStats, straightness: StraightnessStats) -> dict[str, Any]:
    """综合判定偏移方向和严重程度"""
    direction = "直行"
    severity = "优秀"
    straightness_grade = "标准直线"

    # 方向: yaw CW-positive (正=右转), 经取反后 y > 0 = 左偏; yaw < 0 → 左偏
    lat_final = straightness.start_axis_end_mm
    yaw_avg = full.yaw_mean

    if abs(lat_final) < 5.0 and abs(yaw_avg) < 0.3:
        direction = "直行"
    elif lat_final > 0 or yaw_avg < -0.2:
        direction = "左偏"
    elif lat_final < 0 or yaw_avg > 0.2:
        direction = "右偏"

    straight_max = abs(straightness.cross_track_max_abs_mm)
    if straight_max < 5.0:
        severity = "优秀 (< 5mm)"
        straightness_grade = "标准直线"
    elif straight_max < 15.0:
        severity = "良好 (5-15mm)"
        straightness_grade = "较直"
    elif straight_max < 30.0:
        severity = "一般 (15-30mm)"
        straightness_grade = "轻微弯折"
    elif straight_max < 60.0:
        severity = "较差 (30-60mm)"
        straightness_grade = "明显弯折"
    else:
        severity = "很差 (> 60mm)"
        straightness_grade = "严重偏航/弯折"

    # 抖动评估
    yaw_std = full.yaw_std
    if yaw_std < 0.5:
        stability = "稳定"
    elif yaw_std < 1.5:
        stability = "轻微抖动"
    elif yaw_std < 3.0:
        stability = "明显抖动"
    else:
        stability = "剧烈抖动"

    return {
        "direction": direction,
        "severity": severity,
        "straightness": straightness_grade,
        "stability": stability,
        "final_lat_mm": round(lat_final, 2),
        "straight_rms_mm": round(straightness.cross_track_rms_mm, 2),
        "straight_max_mm": round(straightness.cross_track_max_abs_mm, 2),
        "fit_heading_deg": round(straightness.line_heading_deg, 3),
        "yaw_mean_deg": round(yaw_avg, 3),
        "yaw_std_deg": round(yaw_std, 2),
        "yaw_abs_max_deg": round(full.yaw_abs_max, 2),
        "yaw_drift_rate_dps": round(full.yaw_drift_rate_dps, 4),
        "lat_drift_rate_mm_s": round(full.lat_drift_rate_mm_s, 3),
        "enc_ratio_LR": round(full.enc_ratio, 4),
    }


# ════════════════════════════════════════════════════════════════
# 报告生成
# ════════════════════════════════════════════════════════════════

def print_report(samples: list[Sample],
                 phases: dict[str, PhaseStats],
                 per_sec: list[PhaseStats],
                 verdict: dict[str, Any],
                 clean: CleanStats,
                 straightness: StraightnessStats) -> str:
    """生成详细的文本报告"""
    lines: list[str] = []
    L = lines.append

    run_samples = [s for s in samples if s.run == 1]
    duration = (run_samples[-1].t_s - run_samples[0].t_s) if len(run_samples) > 1 else 0
    distance_m = sum(s.ds_m for s in run_samples)

    L("=" * 72)
    L("  直线行驶偏移精细分析报告")
    L("=" * 72)
    L("")
    L(
        f"  原始帧: {clean.raw_count}  清洗后: {len(run_samples)}  "
        f"去重: {clean.duplicates_removed}  丢弃复位前残帧: {clean.dropped_reset_samples}"
    )
    L(f"  总时长: {duration:.2f}s  行驶距离: {distance_m*1000:.1f}mm")
    L("")

    # ── 总体判定 ──
    imu_lat = run_samples[-1].y_imu_mm if run_samples else 0
    L("┌────────────────────────────────────────────┐")
    L(f"│  偏移方向: {verdict['direction']:>8s}                       │")
    L(f"│  严重程度: {verdict['severity']:<28s}  │")
    L(f"│  直线评级: {verdict['straightness']:<28s}  │")
    L(f"│  行驶稳定: {verdict['stability']:<28s}  │")
    L(f"│  终点横偏: {verdict['final_lat_mm']:>+8.2f} mm (IMU航向主航迹)  │")
    L(f"│  编码器偏: {imu_lat:>+8.2f} mm (差速里程计,仅参考) │")
    L(f"│  直线RMS:  {verdict['straight_rms_mm']:>8.2f} mm              │")
    L(f"│  直线峰值: {verdict['straight_max_mm']:>8.2f} mm              │")
    L(f"│  拟合方向: {verdict['fit_heading_deg']:>+8.3f} deg                    │")
    L(f"│  yaw均值:  {verdict['yaw_mean_deg']:>+8.3f} deg                    │")
    L(f"│  yaw抖动:  {verdict['yaw_std_deg']:>8.2f} deg (std)              │")
    L(f"│  yaw峰值:  {verdict['yaw_abs_max_deg']:>8.2f} deg                  │")
    L(f"│  横偏漂率: {verdict['lat_drift_rate_mm_s']:>+8.3f} mm/s                   │")
    L(f"│  编码器比: {verdict['enc_ratio_LR']:>8.4f} (L/R)                 │")
    L("└────────────────────────────────────────────┘")
    L("")

    # ── 分段统计 ──
    L("── 分段统计 ──────────────────────────────────────────────────")
    L(f"{'阶段':<10s} {'时间':>10s} {'N':>4s} │ {'yaw均':>7s} {'yaw漂':>8s} │"
      f" {'横偏净':>8s} {'横偏率':>8s} {'最大偏':>8s} │"
      f" {'OL均':>5s} {'OR均':>5s} {'hd均':>5s} │ {'hi末':>6s} │"
      f" {'Σel':>6s} {'Σer':>6s} {'差':>5s}")
    L("─" * 120)
    for key in ["startup", "early", "late", "full"]:
        ps = phases.get(key)
        if ps is None or ps.n_samples == 0:
            continue
        t_range = f"{ps.t_start_s:.1f}-{ps.t_end_s:.1f}s"
        L(f"{ps.name:<10s} {t_range:>10s} {ps.n_samples:>4d} │"
          f" {ps.yaw_mean:>+7.2f} {ps.yaw_drift_rate_dps:>+8.4f} │"
          f" {ps.lat_net_mm:>+8.2f} {ps.lat_drift_rate_mm_s:>+8.3f} {ps.lat_max_abs_mm:>8.2f} │"
          f" {ps.ol_mean:>5.1f} {ps.or_mean:>5.1f} {ps.hd_mean:>+5.2f} │"
          f" {ps.hi_end:>+6.3f} │"
          f" {ps.el_sum:>6d} {ps.er_sum:>6d} {ps.enc_diff:>+5d}")
    L("")

    L("── 直线性评估 ────────────────────────────────────────────────")
    L(f"拟合区间: {straightness.fit_start_s:.1f}s ~ {straightness.fit_end_s:.1f}s")
    L(f"拟合样本: {straightness.fit_sample_count}")
    L(f"起点坐标系终点横偏: {straightness.start_axis_end_mm:+.2f} mm")
    L(f"最佳拟合线终点横误差: {straightness.cross_track_end_mm:+.2f} mm")
    L(f"最佳拟合线 RMS 横误差: {straightness.cross_track_rms_mm:.2f} mm")
    L(f"最佳拟合线最大横误差: {straightness.cross_track_max_abs_mm:.2f} mm")
    L("")

    # ── 逐秒统计 ──
    L("── 逐秒统计 ──────────────────────────────────────────────────")
    L(f"{'秒':<5s} │ {'yaw':>6s} {'yr':>6s} │ {'横偏':>8s} {'Δ横偏':>8s} │"
      f" {'OL':>5s} {'OR':>5s} {'hd':>5s} │ {'hi':>6s} │"
      f" {'Σel':>5s} {'Σer':>5s} {'v mm/s':>7s}")
    L("─" * 95)
    prev_lat = 0.0
    for ps in per_sec:
        delta_lat = ps.lat_end_mm - prev_lat
        L(f"{ps.name:<5s} │ {ps.yaw_mean:>+6.2f} {ps.yaw_drift_rate_dps:>+6.3f} │"
          f" {ps.lat_end_mm:>+8.2f} {delta_lat:>+8.2f} │"
          f" {ps.ol_mean:>5.1f} {ps.or_mean:>5.1f} {ps.hd_mean:>+5.2f} │"
          f" {ps.hi_end:>+6.3f} │"
          f" {ps.el_sum:>5d} {ps.er_sum:>5d} {ps.speed_mean_mps*1000:>7.1f}")
        prev_lat = ps.lat_end_mm
    L("")

    # ── 起步细节 ──
    L("── 起步详细 (前2秒) ──────────────────────────────────────────")
    L(f"{'t_ms':>6s} {'yaw':>6s} {'hi':>7s} {'hd':>3s} {'OL':>4s} {'OR':>4s}"
      f" {'差':>4s} {'pc':>3s} {'el':>4s} {'er':>4s} {'横偏mm':>8s}")
    L("─" * 68)
    for s in run_samples:
        if s.t_s > 2.0:
            break
        L(f"{s.t_ms:>6d} {s.yaw:>+6.1f} {s.hi:>+7.3f} {s.hd:>+3d}"
          f" {s.ol:>4d} {s.or_:>4d} {s.ol - s.or_:>+4d} {s.pc:>3d}"
          f" {s.el:>+4d} {s.er:>+4d} {s.y_mm:>+8.3f}")
    L("")

    report = "\n".join(lines)
    print(report)
    return report


# ════════════════════════════════════════════════════════════════
# 可视化
# ════════════════════════════════════════════════════════════════

def plot_analysis(samples: list[Sample], phases: dict[str, PhaseStats],
                  verdict: dict[str, Any], straightness: StraightnessStats,
                  save_path: Optional[str] = None):
    """四面板分析图"""
    try:
        import matplotlib
        matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
        matplotlib.rcParams['axes.unicode_minus'] = False
        import matplotlib.pyplot as plt
        from matplotlib.gridspec import GridSpec
    except ImportError:
        print("[WARN] matplotlib 不可用，跳过绘图")
        return

    run = [s for s in samples if s.run == 1]
    if not run:
        return

    ts = [s.t_s for s in run]
    yaws = [s.yaw for s in run]
    y_mm = [s.y_mm for s in run]
    his = [s.hi for s in run]
    ols = [s.ol for s in run]
    ors = [s.or_ for s in run]
    hds = [s.hd for s in run]
    pcs = [s.pc for s in run]
    els = [s.el for s in run]
    ers = [s.er for s in run]

    fig = plt.figure(figsize=(16, 11))
    fig.suptitle(
        f"直线偏移分析  |  方向: {verdict['direction']}  |  "
        f"终点横偏: {verdict['final_lat_mm']:+.2f}mm  |  "
        f"评级: {verdict['severity']}",
        fontsize=13, fontweight='bold'
    )
    gs = GridSpec(3, 2, figure=fig, hspace=0.35, wspace=0.3)

    # ── Panel 1: Yaw + 横偏 ──
    ax1 = fig.add_subplot(gs[0, 0])
    color_yaw = '#2196F3'
    color_lat = '#F44336'
    ax1.plot(ts, yaws, color=color_yaw, linewidth=1.2, label='yaw (deg)')
    ax1.set_ylabel('yaw (deg)', color=color_yaw)
    ax1.tick_params(axis='y', labelcolor=color_yaw)
    ax1.axhline(0, color='gray', linewidth=0.5, linestyle='--')
    ax1r = ax1.twinx()
    ax1r.plot(ts, y_mm, color=color_lat, linewidth=1.5, label='横偏 (mm)')
    ax1r.set_ylabel('横向偏移 (mm)', color=color_lat)
    ax1r.tick_params(axis='y', labelcolor=color_lat)
    ax1.set_xlabel('时间 (s)')
    ax1.set_title('Yaw 角 & 横向偏移')
    # 阶段分界线
    for t_line, lbl in [(PHASE_STARTUP_END_S, '启动结束'), (PHASE_EARLY_END_S, '早期结束')]:
        ax1.axvline(t_line, color='#9E9E9E', linewidth=0.8, linestyle=':')
    ax1.legend(loc='upper left', fontsize=8)
    ax1r.legend(loc='upper right', fontsize=8)
    ax1.grid(True, alpha=0.3)

    # ── Panel 2: PID 积分 hi ──
    ax2 = fig.add_subplot(gs[0, 1])
    ax2.plot(ts, his, color='#4CAF50', linewidth=1.2)
    ax2.axhline(0, color='gray', linewidth=0.5, linestyle='--')
    ax2.set_xlabel('时间 (s)')
    ax2.set_ylabel('heading integral (hi)')
    ax2.set_title('航向积分项 hi')
    ax2.grid(True, alpha=0.3)
    # 标注终值
    if his:
        ax2.annotate(f'hi={his[-1]:+.3f}', xy=(ts[-1], his[-1]),
                     fontsize=9, color='#4CAF50', fontweight='bold')

    # ── Panel 3: 电机 OL/OR + hd ──
    ax3 = fig.add_subplot(gs[1, 0])
    ax3.plot(ts, ols, color='#FF9800', linewidth=1, label='OL (左电机)')
    ax3.plot(ts, ors, color='#03A9F4', linewidth=1, label='OR (右电机)')
    ax3.fill_between(ts, ols, ors, alpha=0.15, color='#9C27B0')
    ax3.set_xlabel('时间 (s)')
    ax3.set_ylabel('PWM')
    ax3.set_title('实际电机 PWM (死区后)')
    ax3.legend(fontsize=8)
    ax3.grid(True, alpha=0.3)
    # hd on secondary axis
    ax3r = ax3.twinx()
    ax3r.plot(ts, hds, color='#9C27B0', linewidth=0.8, alpha=0.6, label='hd')
    ax3r.set_ylabel('heading diff (hd)', color='#9C27B0')
    ax3r.tick_params(axis='y', labelcolor='#9C27B0')

    # ── Panel 4: 编码器 ──
    ax4 = fig.add_subplot(gs[1, 1])
    cum_els = np.cumsum(els)
    cum_ers = np.cumsum(ers)
    ax4.plot(ts, cum_els, color='#FF9800', linewidth=1.2, label='Σel (左)')
    ax4.plot(ts, cum_ers, color='#03A9F4', linewidth=1.2, label='Σer (右)')
    enc_diff = cum_els - cum_ers
    ax4r = ax4.twinx()
    ax4r.plot(ts, enc_diff, color='#F44336', linewidth=0.8, alpha=0.7, label='Σel-Σer')
    ax4r.set_ylabel('编码器差', color='#F44336')
    ax4r.tick_params(axis='y', labelcolor='#F44336')
    ax4.set_xlabel('时间 (s)')
    ax4.set_ylabel('累计编码器')
    ax4.set_title('编码器累计 & 左右差')
    ax4.legend(loc='upper left', fontsize=8)
    ax4r.legend(loc='upper right', fontsize=8)
    ax4.grid(True, alpha=0.3)

    # ── Panel 5: 2D 航迹 (编码器 vs IMU) ──
    ax5 = fig.add_subplot(gs[2, :])
    x_mm = [s.x_m * 1000 for s in run]
    y_mm_plot = [s.y_mm for s in run]
    ax5.plot(x_mm, y_mm_plot, color='#F44336', linewidth=1.8, label='IMU航向+编码器距离(主)')
    x_imu_mm = [s.x_imu * 1000 for s in run]
    y_imu_plot = [s.y_imu_mm for s in run]
    ax5.plot(x_imu_mm, y_imu_plot, color='#9E9E9E', linewidth=1, linestyle='--',
             alpha=0.6, label='编码器差速航迹(参考,会漂移)')
    ax5.plot(x_mm[0], y_mm_plot[0], 'go', markersize=8, label='起点')
    ax5.plot(x_mm[-1], y_mm_plot[-1], 'r^', markersize=10, label=f'终点 ({y_mm_plot[-1]:+.1f}mm)')
    # 标注终点
    if x_mm and y_mm_plot:
        ax5.annotate(
            f'实际终点\ny={y_mm_plot[-1]:+.1f}mm',
            xy=(x_mm[-1], y_mm_plot[-1]),
            xytext=(x_mm[-1] - max(x_mm) * 0.15,
                    y_mm_plot[-1] + (max(y_mm_plot) - min(y_mm_plot)) * 0.3),
            fontsize=9, color='red', fontweight='bold',
            arrowprops=dict(arrowstyle='->', color='red', lw=1.5)
        )
    # 起点坐标系理想直线
    ax5.axhline(0, color='green', linewidth=1, linestyle='--', alpha=0.5, label='理想直线')
    if straightness.fit_sample_count >= 2:
        heading = math.radians(straightness.line_heading_deg)
        center_x = 0.5 * (x_mm[0] + x_mm[-1]) if x_mm else 0.0
        center_y = 0.5 * (y_mm_plot[0] + y_mm_plot[-1]) if y_mm_plot else 0.0
        span_mm = max(x_mm) - min(x_mm) if len(x_mm) > 1 else 1000.0
        dx = 0.6 * span_mm * math.cos(heading)
        dy = 0.6 * span_mm * math.sin(heading)
        ax5.plot(
            [center_x - dx, center_x + dx],
            [center_y - dy, center_y + dy],
            color='#3F51B5',
            linewidth=1.2,
            linestyle='-.',
            label='最佳拟合直线',
        )
    ax5.set_xlabel('前进距离 (mm)')
    ax5.set_ylabel('横向偏移 (mm)')
    ax5.set_title('2D 航迹俯视图 — 红=IMU主航迹, 蓝=最佳拟合线, 灰=编码器参考')
    ax5.legend(fontsize=8, loc='best')
    ax5.grid(True, alpha=0.3)

    plt.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"[INFO] 图表已保存: {save_path}")

    plt.show()


# ════════════════════════════════════════════════════════════════
# 数据采集
# ════════════════════════════════════════════════════════════════

def capture_serial(port: str = "COM18", baud: int = 115200,
                   duration_s: float = 8.0,
                   auto_run: bool = True) -> tuple[list[str], list[Sample]]:
    """串口实时采集"""
    import serial as ser

    print(f"[INFO] 连接 {port} @ {baud}...")
    s = ser.Serial(port, baud, timeout=0.1)
    time.sleep(0.5)
    s.reset_input_buffer()

    raw_lines: list[str] = []
    samples: list[Sample] = []

    if auto_run:
        print("[INFO] 发送 #RUN!...")
        s.write(b'#RUN!\r\n')
        time.sleep(0.05)

    print(f"[INFO] 采集 {duration_s}s...")
    t0 = time.time()
    while time.time() - t0 < duration_s:
        line = s.readline().decode(errors='replace').strip()
        if not line:
            continue
        raw_lines.append(line)

    if auto_run:
        s.write(b'#STOP!\r\n')
        time.sleep(0.3)
        s.read(s.in_waiting)

    s.close()

    # Re-parse from collected raw lines (avoids serial timing/encoding issues)
    for line in raw_lines:
        sample = parse_hb_line(line)
        if sample and sample.run == 1:
            samples.append(sample)

    print(f"[INFO] 采集完成: {len(samples)} 有效帧, {len(raw_lines)} 总行")
    return raw_lines, samples


def load_log_file(path: str) -> list[Sample]:
    """从日志文件加载"""
    samples = []
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            sample = parse_hb_line(line)
            if sample and sample.run == 1:
                samples.append(sample)
    print(f"[INFO] 从 {path} 加载 {len(samples)} 帧")
    return samples


# ════════════════════════════════════════════════════════════════
# 主流程
# ════════════════════════════════════════════════════════════════

def analyze(samples: list[Sample], no_plot: bool = False,
            save_dir: Optional[str] = None) -> dict[str, Any]:
    """执行完整分析"""
    if not samples:
        print("[ERROR] 无有效数据")
        return {}

    samples, clean_info = sanitize_samples(samples)
    if not samples:
        print("[ERROR] 清洗后无有效数据")
        return {}

    samples = reconstruct_trajectory(samples)

    # 计算分段统计
    t_max = max(s.t_s for s in samples)
    phases = {
        "startup": compute_phase_stats(samples, "启动",
                                       0, PHASE_STARTUP_END_S),
        "early":   compute_phase_stats(samples, "早期巡航",
                                       PHASE_STARTUP_END_S, PHASE_EARLY_END_S),
        "late":    compute_phase_stats(samples, "晚期巡航",
                                       PHASE_EARLY_END_S, t_max + 1),
        "full":    compute_phase_stats(samples, "全程",
                                       0, t_max + 1),
    }

    per_sec = compute_per_second(samples)
    straightness = compute_straightness(samples)

    # 偏移判定
    verdict = classify_drift(phases["full"], straightness)

    # 打印报告
    report_text = print_report(samples, phases, per_sec, verdict, clean_info, straightness)

    # JSON 结构化结果
    result = {
        "verdict": verdict,
        "cleaning": asdict(clean_info),
        "straightness": asdict(straightness),
        "phases": {k: asdict(v) for k, v in phases.items()},
        "per_second": [asdict(ps) for ps in per_sec],
        "sample_count": len(samples),
        "duration_s": round(t_max, 2),
    }

    # 保存
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        stamp = time.strftime("%Y%m%d_%H%M%S")

        # CSV
        csv_path = os.path.join(save_dir, f"drift_{stamp}.csv")
        with open(csv_path, 'w', encoding='utf-8') as f:
            header = ("t_ms,t_s,yaw,yr,hi,hd,pc,ol,or,el,er,cum_el,cum_er,"
                      "x_imu_mm,y_imu_mm,x_enc_mm,y_enc_mm,theta_enc_deg,v_mps")
            f.write(header + "\n")
            for s in samples:
                f.write(f"{s.t_ms},{s.t_s:.4f},{s.yaw:.2f},{s.yr:.2f},"
                        f"{s.hi:.4f},{s.hd},{s.pc},{s.ol},{s.or_},"
                        f"{s.el},{s.er},{s.cum_el},{s.cum_er},"
                        f"{s.x_m*1000:.3f},{s.y_mm:.3f},"
                        f"{s.x_imu*1000:.3f},{s.y_imu_mm:.3f},"
                        f"{math.degrees(s.theta_enc):.3f},"
                        f"{s.v_mps:.4f}\n")
        print(f"[INFO] CSV: {csv_path}")

        # JSON
        json_path = os.path.join(save_dir, f"drift_{stamp}.json")
        with open(json_path, 'w', encoding='utf-8') as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
        print(f"[INFO] JSON: {json_path}")

        # Report
        txt_path = os.path.join(save_dir, f"drift_{stamp}.txt")
        with open(txt_path, 'w', encoding='utf-8') as f:
            f.write(report_text)
        print(f"[INFO] Report: {txt_path}")

        # Plot
        if not no_plot:
            png_path = os.path.join(save_dir, f"drift_{stamp}.png")
            plot_analysis(samples, phases, verdict, straightness, save_path=png_path)
        return result

    # 显示图表
    if not no_plot:
        plot_analysis(samples, phases, verdict, straightness)

    return result


def main():
    parser = argparse.ArgumentParser(
        description="直线行驶偏移精细分析工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python drift_analyzer.py --live --duration 8
  python drift_analyzer.py --live --duration 10 --save
  python drift_analyzer.py --file captured.log
  python drift_analyzer.py --live --no-plot --duration 5
        """
    )
    parser.add_argument("--live", action="store_true", help="串口实时采集")
    parser.add_argument("--file", type=str, help="读取日志文件")
    parser.add_argument("--port", type=str, default="COM18", help="串口端口")
    parser.add_argument("--baud", type=int, default=115200, help="波特率")
    parser.add_argument("--duration", type=float, default=8.0, help="采集时长(秒)")
    parser.add_argument("--no-plot", action="store_true", help="不显示图表")
    parser.add_argument("--save", action="store_true", help="保存结果到 000Data/drift/")
    parser.add_argument("--save-dir", type=str, default=None, help="自定义保存目录")
    parser.add_argument("--no-run", action="store_true", help="不自动发送 RUN/STOP")

    args = parser.parse_args()

    if not args.live and not args.file:
        parser.print_help()
        print("\n[ERROR] 请指定 --live 或 --file")
        sys.exit(1)

    # 采集/加载
    raw_lines = []
    if args.live:
        raw_lines, samples = capture_serial(
            port=args.port, baud=args.baud,
            duration_s=args.duration,
            auto_run=not args.no_run
        )
    else:
        samples = load_log_file(args.file)

    # 保存路径
    save_dir = None
    if args.save or args.save_dir:
        if args.save_dir:
            save_dir = args.save_dir
        else:
            base = Path(__file__).parent.parent / "000Data" / "drift"
            save_dir = str(base)

    # 保存原始日志
    if save_dir and raw_lines:
        os.makedirs(save_dir, exist_ok=True)
        stamp = time.strftime("%Y%m%d_%H%M%S")
        log_path = os.path.join(save_dir, f"raw_{stamp}.log")
        with open(log_path, 'w', encoding='utf-8') as f:
            f.write("\n".join(raw_lines))
        print(f"[INFO] Raw log: {log_path}")

    # 分析
    result = analyze(samples, no_plot=args.no_plot, save_dir=save_dir)

    if result:
        v = result.get("verdict", {})
        print(f"\n{'='*40}")
        print(f"  结论: {v.get('direction', '?')} | {v.get('severity', '?')}")
        print(f"  终点横偏: {v.get('final_lat_mm', 0):+.2f} mm")
        print(f"{'='*40}")


if __name__ == "__main__":
    main()
