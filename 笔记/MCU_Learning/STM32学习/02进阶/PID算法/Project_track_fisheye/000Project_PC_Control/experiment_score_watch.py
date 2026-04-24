#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import time
from dataclasses import asdict, dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WATCH_DIR = PROJECT_ROOT / "000Data" / "serial_runs" / "experiments"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "000Data" / "serial_runs" / "analysis"
DEFAULT_STATE_PATH = DEFAULT_OUTPUT_DIR / "watch_state.json"

SEARCH_STATES = {"FNDL", "FNDR"}
RECOVER_STATES = {"TRML", "TRMR"}
EDGE_STATES = {"EDGE", "SCRV"}
CENTER_BAND_BITS = (5, 6)  # A6 / A7, 0-based in s12 string
MAX_ABS_LINE_POS = 275.0


@dataclass
class HBRecord:
    t_ms: int
    run: int
    exp_id: int
    el: int
    er: int
    pc: int
    ol: int
    or_: int
    lp: float
    st: str
    sbh: int
    s12: str


@dataclass
class EventMetrics:
    count: int
    success_ratio: float
    mean_success_s: float


@dataclass
class ScoreSummary:
    file_name: str
    experiment_id: int
    mode: str
    analysis_start_ms: int
    duration_s: float
    sample_count: int
    grip_score: float
    speed_smoothness_score: float
    center_score: float
    total_score: float
    mean_abs_lp: float
    lp_flip_rate_hz: float
    mean_forward_counts: float
    forward_counts_std: float
    forward_slew_mean: float
    speed_imbalance_mean: float
    a67_cover_ratio: float
    a67_exact_ratio: float
    center_single_ratio: float
    loss_ratio: float
    search_ratio: float
    edge_ratio: float
    recover_ratio: float
    straight_ratio: float
    edge_recovery_success_ratio: float
    edge_recovery_mean_s: float
    search_recovery_success_ratio: float
    search_recovery_mean_s: float
    primary_param: str
    primary_action: str
    primary_reason: str
    secondary_param: str
    secondary_action: str
    secondary_reason: str
    evaluation: str


def parse_kv_line(line: str, prefix: str) -> dict[str, str] | None:
    marker = line.find(prefix)
    if marker < 0:
        return None
    line = line[marker:]
    data: dict[str, str] = {}
    for part in line[len(prefix):].split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        data[key.strip()] = value.strip()
    return data


def bits_to_s12(bits: int) -> str:
    return "".join("1" if (bits & (1 << idx)) else "0" for idx in range(12))


def parse_hb_line(line: str) -> HBRecord | None:
    kv = parse_kv_line(line, "HB:")
    if kv is None:
        return None
    sbh_raw = kv.get("sbh")
    try:
        if sbh_raw:
            try:
                sensor_bits = int(sbh_raw, 16)
            except ValueError:
                sensor_bits = int(kv.get("sb", "0") or "0")
        else:
            sensor_bits = int(kv.get("sb", "0") or "0")
    except ValueError:
        return None

    s12 = kv.get("s12", "").strip()
    if len(s12) != 12:
        s12 = bits_to_s12(sensor_bits)

    try:
        return HBRecord(
            t_ms=int(kv.get("t", "0") or "0"),
            run=int(kv.get("run", "0") or "0"),
            exp_id=int(kv.get("exp", "0") or "0"),
            el=int(kv.get("el", "0") or "0"),
            er=int(kv.get("er", "0") or "0"),
            pc=int(kv.get("pc", "0") or "0"),
            ol=int(kv.get("OL", "0") or "0"),
            or_=int(kv.get("OR", "0") or "0"),
            lp=float(kv.get("lp", "0") or "0"),
            st=kv.get("st", "-").strip(),
            sbh=sensor_bits,
            s12=s12,
        )
    except ValueError:
        return None


def sensor_active(rec: HBRecord, sensor_index_1based: int) -> bool:
    idx = sensor_index_1based - 1
    if 0 <= idx < len(rec.s12):
        return rec.s12[idx] == "1"
    return False


def center_any(rec: HBRecord) -> bool:
    return sensor_active(rec, 6) or sensor_active(rec, 7)


def center_exact(rec: HBRecord) -> bool:
    return sensor_active(rec, 6) and sensor_active(rec, 7)


def center_single(rec: HBRecord) -> bool:
    return center_any(rec) and not center_exact(rec)


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def pstdev(values: list[float]) -> float:
    return statistics.pstdev(values) if len(values) >= 2 else 0.0


def mean_abs(values: list[float]) -> float:
    return mean([abs(v) for v in values])


def mean_abs_diff(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    return mean([abs(values[i] - values[i - 1]) for i in range(1, len(values))])


def sign_flips(values: list[float], deadband: float) -> int:
    flips = 0
    last_sign = 0
    for value in values:
        if value > deadband:
            sign = 1
        elif value < -deadband:
            sign = -1
        else:
            sign = 0
        if sign == 0:
            continue
        if last_sign != 0 and sign != last_sign:
            flips += 1
        last_sign = sign
    return flips


def state_ratio(records: list[HBRecord], names: set[str]) -> float:
    if not records:
        return 0.0
    return sum(1 for rec in records if rec.st in names) / float(len(records))


def detect_edge_recovery(records: list[HBRecord]) -> EventMetrics:
    events = 0
    success_times: list[float] = []
    for idx, rec in enumerate(records):
        prev_state = records[idx - 1].st if idx > 0 else ""
        if rec.st not in EDGE_STATES or prev_state in EDGE_STATES:
            continue
        events += 1
        start_t = rec.t_ms
        success = False
        for nxt in records[idx + 1:]:
            if nxt.t_ms - start_t > 800:
                break
            if nxt.st in SEARCH_STATES:
                break
            if center_any(nxt):
                success = True
                success_times.append((nxt.t_ms - start_t) / 1000.0)
                break
        if not success:
            continue
    return EventMetrics(
        count=events,
        success_ratio=(len(success_times) / float(events)) if events else 0.0,
        mean_success_s=mean(success_times),
    )


def detect_search_recovery(records: list[HBRecord]) -> EventMetrics:
    events = 0
    success_times: list[float] = []
    for idx, rec in enumerate(records):
        prev_state = records[idx - 1].st if idx > 0 else ""
        if rec.st not in SEARCH_STATES or prev_state in SEARCH_STATES:
            continue
        events += 1
        start_t = rec.t_ms
        for nxt in records[idx + 1:]:
            if nxt.t_ms - start_t > 1500:
                break
            if nxt.st in SEARCH_STATES:
                continue
            if center_any(nxt):
                success_times.append((nxt.t_ms - start_t) / 1000.0)
                break
    return EventMetrics(
        count=events,
        success_ratio=(len(success_times) / float(events)) if events else 0.0,
        mean_success_s=mean(success_times),
    )


def describe_overall(total_score: float) -> str:
    if total_score >= 85.0:
        return "整体已经接近目标，中线保持和恢复都比较稳。"
    if total_score >= 70.0:
        return "整体可用，但仍有明显可优化空间，主要是中线覆盖或恢复速度。"
    if total_score >= 55.0:
        return "能跑，但抓线和中线保持还不稳，容易在 S 弯或外侧放大偏差。"
    return "当前表现偏弱，优先解决抓线不足、搜索占比高或速度波动大的问题。"


def suggest_next_adjustment(metrics: dict[str, float]) -> tuple[tuple[str, str, str], tuple[str, str, str]]:
    search_ratio = metrics["search_ratio"]
    loss_ratio = metrics["loss_ratio"]
    cover_ratio = metrics["a67_cover_ratio"]
    exact_ratio = metrics["a67_exact_ratio"]
    smooth_score = metrics["speed_smoothness_score"]
    grip_score = metrics["grip_score"]
    edge_success = metrics["edge_recovery_success_ratio"]
    search_success = metrics["search_recovery_success_ratio"]
    lp_flip_rate = metrics["lp_flip_rate_hz"]

    if search_ratio > 0.12 or loss_ratio > 0.10:
        if search_success < 0.65 or metrics["search_recovery_mean_s"] > 0.35:
            primary = ("track.search_turn_fast", "+20", "搜索占比偏高，而且全灭后回线慢，优先加快单向 pivot。")
            secondary = ("track.search_turn_slow", "+10", "慢速 pivot 也略抬高，避免贴线后恢复仍拖。")
            return primary, secondary
        primary = ("track.follow_turnin_ratio", "+0.03", "还看得到外侧线时抓不回，应该在 FOLLOW 阶段更早强化同向转入。")
        secondary = ("track.lkp", "+0.6", "主跟线链仍偏软，需要更积极地把外侧偏差拉回。")
        return primary, secondary

    if cover_ratio < 0.45:
        if lp_flip_rate > 3.0 and smooth_score < 70.0:
            primary = ("track.lkd", "+0.4", "A6/A7 覆盖不足且中心翻向频繁，需要更强阻尼抑制左右抽动。")
            secondary = ("track.dev_step_limit", "-6", "输出爬升过快会放大抽动，先稍微收一点步进限幅。")
            return primary, secondary
        primary = ("track.lkp", "+0.6", "中线覆盖率偏低，优先增强主抓线力。")
        secondary = ("track.error_scale", "-4", "减小误差缩放，让同样 linePos 产生更大的等效纠偏。")
        return primary, secondary

    if grip_score < 72.0 or edge_success < 0.70:
        primary = ("track.follow_turnin_ratio", "+0.03", "S 弯或外侧区抓线不足，优先加强同向导向补强。")
        secondary = ("track.follow_turnin_min", "+8", "让大偏差时最小差速底线更高，避免已看见线却咬不进去。")
        return primary, secondary

    if smooth_score < 72.0:
        primary = ("track.lkd", "+0.3", "抓线已基本够，但速度平滑性一般，先增加阻尼压摆动。")
        secondary = ("track.dev_step_limit", "-6", "再稍收差速每拍变化，减少直线段左右抽动。")
        return primary, secondary

    if exact_ratio < 0.20:
        primary = ("track.static_bias", "±2 微调", "A6/A7 覆盖有了但双灯精确居中偏低，优先做静态偏置校正。")
        secondary = ("track.sensor_scale6", "±0.01 微调", "中心灯比例还可以再校准，让投影更稳定落在 A6/A7。")
        return primary, secondary

    primary = ("track.follow_turnin_ratio", "+0.02", "当前已经比较稳，下一步在不降速前提下继续提高 S 弯咬线力度。")
    secondary = ("track.search_turn_fast", "+10", "作为保底，继续缩短出线后的自转找线时间。")
    return primary, secondary


def analyze_records(path: Path, records: list[HBRecord], mode: str, analysis_start_ms: int) -> ScoreSummary:
    run_records = [rec for rec in records if rec.run == 1 and rec.t_ms >= analysis_start_ms]
    if not run_records:
        primary, secondary = suggest_next_adjustment({
            "search_ratio": 1.0,
            "loss_ratio": 1.0,
            "a67_cover_ratio": 0.0,
            "a67_exact_ratio": 0.0,
            "speed_smoothness_score": 0.0,
            "grip_score": 0.0,
            "edge_recovery_success_ratio": 0.0,
            "search_recovery_success_ratio": 0.0,
            "search_recovery_mean_s": 0.0,
            "lp_flip_rate_hz": 0.0,
        })
        return ScoreSummary(
            file_name=path.name,
            experiment_id=0,
            mode=mode,
            analysis_start_ms=analysis_start_ms,
            duration_s=0.0,
            sample_count=0,
            grip_score=0.0,
            speed_smoothness_score=0.0,
            center_score=0.0,
            total_score=0.0,
            mean_abs_lp=0.0,
            lp_flip_rate_hz=0.0,
            mean_forward_counts=0.0,
            forward_counts_std=0.0,
            forward_slew_mean=0.0,
            speed_imbalance_mean=0.0,
            a67_cover_ratio=0.0,
            a67_exact_ratio=0.0,
            center_single_ratio=0.0,
            loss_ratio=0.0,
            search_ratio=0.0,
            edge_ratio=0.0,
            recover_ratio=0.0,
            straight_ratio=0.0,
            edge_recovery_success_ratio=0.0,
            edge_recovery_mean_s=0.0,
            search_recovery_success_ratio=0.0,
            search_recovery_mean_s=0.0,
            primary_param=primary[0],
            primary_action=primary[1],
            primary_reason=primary[2],
            secondary_param=secondary[0],
            secondary_action=secondary[1],
            secondary_reason=secondary[2],
            evaluation="本轮没有有效运行样本，先检查实验是否真正启动。",
        )

    duration_s = max(0.0, (run_records[-1].t_ms - run_records[0].t_ms) / 1000.0)
    non_search = [rec for rec in run_records if rec.st not in SEARCH_STATES]
    visible_non_search = [rec for rec in non_search if rec.sbh != 0]
    forward = [(rec.el + rec.er) / 2.0 for rec in non_search]
    smooth_records = [
        rec for rec in non_search
        if center_any(rec) or abs(rec.lp) <= 80.0 or rec.st in {"STRA", "TRK"}
    ]
    if len(smooth_records) < 8:
        smooth_records = non_search
    smooth_forward = [(rec.el + rec.er) / 2.0 for rec in smooth_records]
    lp_visible = [rec.lp for rec in visible_non_search]
    lp_center = [rec.lp for rec in non_search if center_any(rec)]

    a67_cover_ratio = sum(1 for rec in non_search if center_any(rec)) / float(len(non_search) or 1)
    a67_exact_ratio = sum(1 for rec in non_search if center_exact(rec)) / float(len(non_search) or 1)
    center_single_ratio = sum(1 for rec in non_search if center_single(rec)) / float(len(non_search) or 1)
    loss_ratio = sum(1 for rec in run_records if rec.sbh == 0) / float(len(run_records))
    search_ratio = state_ratio(run_records, SEARCH_STATES)
    edge_ratio = state_ratio(run_records, EDGE_STATES)
    recover_ratio = state_ratio(run_records, RECOVER_STATES)
    straight_ratio = sum(1 for rec in run_records if rec.st in {"STRA", "TRK"}) / float(len(run_records))

    mean_abs_lp = mean_abs(lp_visible)
    mean_forward_counts = mean(forward)
    forward_counts_std = pstdev(smooth_forward)
    forward_slew_mean = mean_abs_diff(smooth_forward)
    speed_imbalance_mean = mean([abs(rec.el - rec.er) for rec in smooth_records])
    lp_flip_rate_hz = 0.0
    if duration_s > 0.0:
        lp_flip_rate_hz = sign_flips(lp_visible, deadband=18.0) / duration_s

    edge_recovery = detect_edge_recovery(run_records)
    search_recovery = detect_search_recovery(run_records)

    lp_norm = clamp01(mean_abs_lp / 180.0)
    grip_score = 100.0 * clamp01(
        0.30 * (1.0 - lp_norm)
        + 0.20 * (1.0 - loss_ratio)
        + 0.20 * (1.0 - search_ratio)
        + 0.15 * edge_recovery.success_ratio
        + 0.15 * search_recovery.success_ratio
    )

    mean_forward_abs = mean([abs(v) for v in smooth_forward])
    speed_cv = forward_counts_std / max(mean_forward_abs, 1.0)
    speed_slew_norm = forward_slew_mean / max(mean_forward_abs, 1.0)
    imbalance_norm = speed_imbalance_mean / max(mean_forward_abs, 1.0)
    speed_penalty = min(
        1.0,
        0.40 * (speed_cv / 0.45)
        + 0.20 * (speed_slew_norm / 0.25)
        + 0.40 * (imbalance_norm / 1.00),
    )
    speed_smoothness_score = 100.0 * (1.0 - speed_penalty)

    center_score = 100.0 * clamp01(0.80 * a67_cover_ratio + 0.20 * a67_exact_ratio)
    total_score = (
        0.45 * grip_score
        + 0.25 * speed_smoothness_score
        + 0.30 * center_score
        - 12.0 * search_ratio
        - 8.0 * loss_ratio
    )
    total_score = max(0.0, min(100.0, total_score))

    primary, secondary = suggest_next_adjustment({
        "search_ratio": search_ratio,
        "loss_ratio": loss_ratio,
        "a67_cover_ratio": a67_cover_ratio,
        "a67_exact_ratio": a67_exact_ratio,
        "speed_smoothness_score": speed_smoothness_score,
        "grip_score": grip_score,
        "edge_recovery_success_ratio": edge_recovery.success_ratio,
        "search_recovery_success_ratio": search_recovery.success_ratio,
        "search_recovery_mean_s": search_recovery.mean_success_s,
        "lp_flip_rate_hz": lp_flip_rate_hz,
    })

    return ScoreSummary(
        file_name=path.name,
        experiment_id=run_records[0].exp_id,
        mode=mode,
        analysis_start_ms=analysis_start_ms,
        duration_s=duration_s,
        sample_count=len(run_records),
        grip_score=grip_score,
        speed_smoothness_score=speed_smoothness_score,
        center_score=center_score,
        total_score=total_score,
        mean_abs_lp=mean_abs_lp,
        lp_flip_rate_hz=lp_flip_rate_hz,
        mean_forward_counts=mean_forward_counts,
        forward_counts_std=forward_counts_std,
        forward_slew_mean=forward_slew_mean,
        speed_imbalance_mean=speed_imbalance_mean,
        a67_cover_ratio=a67_cover_ratio,
        a67_exact_ratio=a67_exact_ratio,
        center_single_ratio=center_single_ratio,
        loss_ratio=loss_ratio,
        search_ratio=search_ratio,
        edge_ratio=edge_ratio,
        recover_ratio=recover_ratio,
        straight_ratio=straight_ratio,
        edge_recovery_success_ratio=edge_recovery.success_ratio,
        edge_recovery_mean_s=edge_recovery.mean_success_s,
        search_recovery_success_ratio=search_recovery.success_ratio,
        search_recovery_mean_s=search_recovery.mean_success_s,
        primary_param=primary[0],
        primary_action=primary[1],
        primary_reason=primary[2],
        secondary_param=secondary[0],
        secondary_action=secondary[1],
        secondary_reason=secondary[2],
        evaluation=describe_overall(total_score),
    )


def parse_experiment_file(path: Path) -> tuple[str, list[HBRecord], bool]:
    records: list[HBRecord] = []
    mode = "-"
    stopped = False
    idle_closed = False
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line:
                continue
            start_evt = parse_kv_line(line, "EVT:EXP_START,")
            if start_evt is not None:
                mode = start_evt.get("mode", mode)
                continue
            if parse_kv_line(line, "EVT:EXP_STOP,") is not None:
                stopped = True
                continue
            rec = parse_hb_line(line)
            if rec is not None:
                records.append(rec)
                if rec.run == 0:
                    idle_closed = True
    return mode, records, (stopped or idle_closed)


def write_summary(out_dir: Path, summary: ScoreSummary) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = Path(summary.file_name).stem
    json_path = out_dir / f"{stem}_score.json"
    md_path = out_dir / f"{stem}_score.md"

    with json_path.open("w", encoding="utf-8", newline="") as handle:
        json.dump(asdict(summary), handle, ensure_ascii=False, indent=2)

    lines = [
        f"# {summary.file_name}",
        "",
        f"- 总分：`{summary.total_score:.2f}`",
        f"- 抓线性：`{summary.grip_score:.2f}`",
        f"- 速度平滑性：`{summary.speed_smoothness_score:.2f}`",
        f"- A6/A7 覆盖得分：`{summary.center_score:.2f}`",
        "",
        "## 关键指标",
        f"- 评分起点：`{summary.analysis_start_ms}ms`",
        f"- 运行时长：`{summary.duration_s:.3f}s`",
        f"- 样本数：`{summary.sample_count}`",
        f"- 平均绝对 linePos：`{summary.mean_abs_lp:.2f}`",
        f"- linePos 翻向频率：`{summary.lp_flip_rate_hz:.2f} Hz`",
        f"- 平均前进计数：`{summary.mean_forward_counts:.2f}`",
        f"- 前进计数标准差：`{summary.forward_counts_std:.2f}`",
        f"- 前进计数变化率均值：`{summary.forward_slew_mean:.2f}`",
        f"- 左右速度不平衡均值：`{summary.speed_imbalance_mean:.2f}`",
        f"- A6/A7 覆盖比率：`{summary.a67_cover_ratio:.2%}`",
        f"- A6&A7 双灯精确居中比率：`{summary.a67_exact_ratio:.2%}`",
        f"- 中心单灯比率：`{summary.center_single_ratio:.2%}`",
        f"- 全灭比率：`{summary.loss_ratio:.2%}`",
        f"- 搜索比率：`{summary.search_ratio:.2%}`",
        f"- EDGE/SCRV 比率：`{summary.edge_ratio:.2%}`",
        f"- RECOVER 比率：`{summary.recover_ratio:.2%}`",
        f"- STRA/TRK 比率：`{summary.straight_ratio:.2%}`",
        f"- 外侧事件回中成功率：`{summary.edge_recovery_success_ratio:.2%}`",
        f"- 外侧事件平均回中时间：`{summary.edge_recovery_mean_s:.3f}s`",
        f"- 搜索回中成功率：`{summary.search_recovery_success_ratio:.2%}`",
        f"- 搜索平均回中时间：`{summary.search_recovery_mean_s:.3f}s`",
        "",
        "## 评价",
        f"- {summary.evaluation}",
        "",
        "## 下一步建议",
        f"- 首选：`{summary.primary_param}` `{summary.primary_action}`",
        f"  原因：{summary.primary_reason}",
        f"- 备选：`{summary.secondary_param}` `{summary.secondary_action}`",
        f"  原因：{summary.secondary_reason}",
    ]
    with md_path.open("w", encoding="utf-8", newline="") as handle:
        handle.write("\n".join(lines) + "\n")


def load_state(path: Path) -> dict[str, object]:
    if not path.exists():
        return {"processed": [], "best_score": -1.0, "best_file": ""}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {"processed": [], "best_score": -1.0, "best_file": ""}


def save_state(path: Path, state: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")


def append_leaderboard(out_dir: Path, summary: ScoreSummary) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "leaderboard.csv"
    exists = path.exists()
    with path.open("a", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        if not exists:
            writer.writerow([
                "file_name",
                "experiment_id",
                "mode",
                "total_score",
                "grip_score",
                "speed_smoothness_score",
                "center_score",
                "a67_cover_ratio",
                "a67_exact_ratio",
                "loss_ratio",
                "search_ratio",
                "primary_param",
                "primary_action",
            ])
        writer.writerow([
            summary.file_name,
            summary.experiment_id,
            summary.mode,
            f"{summary.total_score:.3f}",
            f"{summary.grip_score:.3f}",
            f"{summary.speed_smoothness_score:.3f}",
            f"{summary.center_score:.3f}",
            f"{summary.a67_cover_ratio:.6f}",
            f"{summary.a67_exact_ratio:.6f}",
            f"{summary.loss_ratio:.6f}",
            f"{summary.search_ratio:.6f}",
            summary.primary_param,
            summary.primary_action,
        ])


def update_latest(out_dir: Path, summary: ScoreSummary, best_score: float, best_file: str) -> None:
    latest_path = out_dir / "latest_report.md"
    best_note = ""
    if summary.total_score >= best_score:
        best_note = "本轮为当前最佳。"
    elif best_file:
        best_note = f"当前最佳仍是 `{best_file}`，分数 `{best_score:.2f}`。"

    lines = [
        f"# 最新实验评分：{summary.file_name}",
        "",
        f"- 总分：`{summary.total_score:.2f}`",
        f"- 抓线性：`{summary.grip_score:.2f}`",
        f"- 速度平滑性：`{summary.speed_smoothness_score:.2f}`",
        f"- A6/A7 覆盖得分：`{summary.center_score:.2f}`",
        f"- A6/A7 覆盖比率：`{summary.a67_cover_ratio:.2%}`",
        f"- A6&A7 双灯比率：`{summary.a67_exact_ratio:.2%}`",
        f"- 搜索比率：`{summary.search_ratio:.2%}`",
        f"- 全灭比率：`{summary.loss_ratio:.2%}`",
        f"- 评价：{summary.evaluation}",
        f"- 下一步首选：`{summary.primary_param}` `{summary.primary_action}`",
        f"  原因：{summary.primary_reason}",
        f"- 下一步备选：`{summary.secondary_param}` `{summary.secondary_action}`",
        f"  原因：{summary.secondary_reason}",
        "",
        best_note,
    ]
    latest_path.write_text("\n".join(lines).strip() + "\n", encoding="utf-8")


def process_file(path: Path, out_dir: Path, analysis_start_ms: int) -> ScoreSummary | None:
    mode, records, stopped = parse_experiment_file(path)
    if not stopped:
        return None
    summary = analyze_records(path, records, mode, analysis_start_ms)
    write_summary(out_dir, summary)
    append_leaderboard(out_dir, summary)
    return summary


def discover_files(watch_dir: Path) -> list[Path]:
    return sorted(watch_dir.glob("exp_*.txt"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Watch experiment logs and score 12-route line-follow quality.")
    parser.add_argument("--watch-dir", type=Path, default=DEFAULT_WATCH_DIR, help="Experiment directory to watch.")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUTPUT_DIR, help="Directory for score reports.")
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE_PATH, help="Processed-file state json.")
    parser.add_argument("--poll-s", type=float, default=0.6, help="Polling interval in seconds.")
    parser.add_argument("--min-age-s", type=float, default=0.5, help="Minimum file age before analysis.")
    parser.add_argument("--once", type=Path, default=None, help="Analyze one experiment file once and exit.")
    parser.add_argument("--print-json", action="store_true", help="Print summary as JSON.")
    parser.add_argument("--analysis-start-ms", type=int, default=400,
                        help="Ignore launch transient before this timestamp (default: 400ms).")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)

    if args.once is not None:
        summary = process_file(args.once, args.out_dir, args.analysis_start_ms)
        if summary is None:
            print(f"[score] file not ready: {args.once.name}")
            return 1
        if args.print_json:
            print(json.dumps(asdict(summary), ensure_ascii=False, indent=2))
        else:
            print(
                f"[score] {summary.file_name} total={summary.total_score:.2f} "
                f"grip={summary.grip_score:.2f} smooth={summary.speed_smoothness_score:.2f} "
                f"a67={summary.a67_cover_ratio:.2%} next={summary.primary_param} {summary.primary_action}"
            )
        return 0

    state = load_state(args.state)
    processed = set(str(name) for name in state.get("processed", []))
    best_score = float(state.get("best_score", -1.0))
    best_file = str(state.get("best_file", ""))

    print(f"[score] watch={args.watch_dir}")
    print(f"[score] output={args.out_dir}")
    print("[score] waiting for completed exp_*.txt ...")

    while True:
        for path in discover_files(args.watch_dir):
            if path.name in processed:
                continue
            age_s = time.time() - path.stat().st_mtime
            if age_s < args.min_age_s:
                continue
            summary = process_file(path, args.out_dir, args.analysis_start_ms)
            if summary is None:
                continue
            processed.add(path.name)
            if summary.total_score >= best_score:
                best_score = summary.total_score
                best_file = summary.file_name
            update_latest(args.out_dir, summary, best_score, best_file)
            state["processed"] = sorted(processed)
            state["best_score"] = best_score
            state["best_file"] = best_file
            save_state(args.state, state)
            print(
                f"[score] {summary.file_name} total={summary.total_score:.2f} "
                f"grip={summary.grip_score:.2f} smooth={summary.speed_smoothness_score:.2f} "
                f"a67={summary.a67_cover_ratio:.2%} next={summary.primary_param} {summary.primary_action}"
            )
        time.sleep(max(0.1, args.poll_s))


if __name__ == "__main__":
    raise SystemExit(main())
