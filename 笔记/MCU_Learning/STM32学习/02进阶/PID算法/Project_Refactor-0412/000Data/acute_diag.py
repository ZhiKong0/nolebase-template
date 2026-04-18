"""
acute_diag.py — 锐角检测诊断: 10次×4s, 每次间隔4s等待复位
分析每次跑中 S1/S8+中间 同时亮的模式, 判断锐角检测是否触发
"""
import time
import serial
import sys
from pathlib import Path

PORT = "COM18"
BAUD = 115200
RUNS = 10
RUN_SEC = 4.0
PAUSE_SEC = 4.0

# 传感器位定义
S1 = 0x01; S2 = 0x02; S3 = 0x04; S4 = 0x08
S5 = 0x10; S6 = 0x20; S7 = 0x40; S8 = 0x80
MID = S4 | S5                 # 当前固件锐角退出仍看 S4|S5
TRIGGER_ZONE = S3 | S4 | S5 | S6  # 当前固件锐角触发区 0x3C
ACUTE_WINDOW_MS = 120

def sb_str(sb):
    """传感器位可视化: 12345678"""
    return ''.join(str(i+1) if sb & (1 << i) else '.' for i in range(8))

def parse_hb(line):
    if not line.startswith("HB:"):
        return None
    kv = {}
    for pair in line[3:].split(","):
        if "=" in pair:
            k, v = pair.split("=", 1)
            kv[k.strip()] = v.strip()
    try:
        return {
            "t": int(kv.get("t", "0")),
            "run": int(kv.get("run", "0")),
            "m": kv.get("m", "S"),
            "el": int(kv.get("el", "0")),
            "er": int(kv.get("er", "0")),
            "sb": int(kv.get("sb", "0")),
            "lp": float(kv.get("lp", "0")),
            "yaw": float(kv.get("yaw", "0")),
            "yr": float(kv.get("yr", "0")),
            "pc": int(kv.get("pc", "0")),
            "hd": int(kv.get("hd", "0")),
            "OL": int(kv.get("OL", "0")),
            "OR": int(kv.get("OR", "0")),
            "acuteState": int(kv.get("acuteState", "0")),
            "acuteYawDelta": float(kv.get("acuteYawDelta", "0")),
            "acuteRearmTick": int(kv.get("acuteRearmTick", "0")),
            "st": kv.get("st", ""),
            "cd": kv.get("cd", "-"),
            "cy": float(kv.get("cy", "0")),
            "cb": int(kv.get("cb", "0")),
            "cm": int(kv.get("cm", "0")),
            "cg": int(kv.get("cg", "0")),
            "ch": int(kv.get("ch", "0")),
        }
    except:
        return None


def analyze_internal_acute_windows(records):
    windows = []
    start = None
    for i, r in enumerate(records):
        if r["acuteState"] == 2 and start is None:
            start = i
        elif r["acuteState"] != 2 and start is not None:
            windows.append((start, i - 1))
            start = None
    if start is not None:
        windows.append((start, len(records) - 1))
    return windows


def detect_firmware_acute_events(records):
    """按当前固件逻辑模拟锐角时间窗状态机。

    触发条件:
      acuteState == 0
      S3~S6 任意亮 + S1 或 S8 亮

    判定:
      120ms 内对侧最外侧也亮 -> 取消(按交叉口处理)
      120ms 到期仍未见对侧 -> 确认锐角
    """
    events = []
    state = 0
    start_rec = None
    side = ""
    opp_bit = 0

    for r in records:
        sb = r["sb"]
        if state == 0:
            if sb & TRIGGER_ZONE:
                if sb & S1:  # 固件里先判左，再判右
                    state = 1
                    start_rec = r
                    side = "LEFT(S1)"
                    opp_bit = S8
                elif sb & S8:
                    state = 1
                    start_rec = r
                    side = "RIGHT(S8)"
                    opp_bit = S1
            continue

        elapsed = r["t"] - start_rec["t"]
        if sb & opp_bit:
            events.append({
                "side": side,
                "status": "cancelled",
                "start_t": start_rec["t"],
                "end_t": r["t"],
                "elapsed_ms": elapsed,
                "start_sb": start_rec["sb"],
                "end_sb": sb,
            })
            state = 0
            start_rec = None
            continue

        if elapsed >= ACUTE_WINDOW_MS:
            events.append({
                "side": side,
                "status": "confirmed",
                "start_t": start_rec["t"],
                "end_t": r["t"],
                "elapsed_ms": elapsed,
                "start_sb": start_rec["sb"],
                "end_sb": sb,
            })
            state = 0
            start_rec = None

    if state == 1 and start_rec is not None:
        last = records[-1]
        events.append({
            "side": side,
            "status": "pending",
            "start_t": start_rec["t"],
            "end_t": last["t"],
            "elapsed_ms": last["t"] - start_rec["t"],
            "start_sb": start_rec["sb"],
            "end_sb": last["sb"],
        })

    return events

def analyze_run(run_id, records):
    """分析单次跑的锐角检测情况"""
    if len(records) < 3:
        print(f"  [Run {run_id}] 样本太少({len(records)}), 跳过")
        return None

    # 去重
    seen = set()
    unique = []
    for r in records:
        if r["t"] not in seen and r["m"] == "T" and r["run"] == 1:
            seen.add(r["t"])
            unique.append(r)

    if len(unique) < 3:
        print(f"  [Run {run_id}] 有效样本太少({len(unique)}), 跳过")
        return None

    dur_s = (unique[-1]["t"] - unique[0]["t"]) / 1000.0
    print(f"\n{'='*60}")
    print(f"  Run {run_id}: {len(unique)} 样本, {dur_s:.1f}s")
    print(f"{'='*60}")

    # ---- 1. 按当前固件条件找候选触发采样 ----
    trigger_samples = []  # (index, side, sb)
    for i, r in enumerate(unique):
        sb = r["sb"]
        trigger_on = sb & TRIGGER_ZONE
        s1_on = sb & S1
        s8_on = sb & S8
        if trigger_on:
            if s1_on:
                trigger_samples.append((i, "LEFT(S1)", sb))
            elif s8_on:
                trigger_samples.append((i, "RIGHT(S8)", sb))

    print(f"\n  [锐角候选采样] 当前固件条件 S1/S8 + S3~S6: {len(trigger_samples)} 次")
    for idx, side, sb in trigger_samples:
        r = unique[idx]
        print(f"    t={r['t']}ms  sb={sb_str(sb)}(0x{sb:02X})  side={side}  "
              f"yaw={r['yaw']:.1f}  lp={r['lp']:.0f}  el={r['el']} er={r['er']}")

    firmware_events = detect_firmware_acute_events(unique)
    confirmed_events = [e for e in firmware_events if e["status"] == "confirmed"]
    cancelled_events = [e for e in firmware_events if e["status"] == "cancelled"]
    pending_events = [e for e in firmware_events if e["status"] == "pending"]

    print(f"\n  [固件时间窗判定] T={ACUTE_WINDOW_MS}ms")
    print(f"    确认锐角: {len(confirmed_events)}")
    print(f"    取消为交叉/对侧触发: {len(cancelled_events)}")
    print(f"    窗口未走完: {len(pending_events)}")
    for e in firmware_events:
        status_map = {
            "confirmed": "确认锐角",
            "cancelled": "对侧触发取消",
            "pending": "窗口未完成",
        }
        print(f"    {status_map[e['status']]}  "
              f"{e['side']:>10s}  "
              f"{e['start_t']}→{e['end_t']}ms  "
              f"Δt={e['elapsed_ms']}ms  "
              f"sb:{sb_str(e['start_sb'])}->{sb_str(e['end_sb'])}")

    internal_windows = analyze_internal_acute_windows(unique)
    if internal_windows:
        print(f"\n  [内部锐角状态] acuteState==2 窗口: {len(internal_windows)} 段")
        for wi, (si, ei) in enumerate(internal_windows, start=1):
            start_r = unique[si]
            end_r = unique[ei]
            exit_r = unique[ei + 1] if (ei + 1) < len(unique) else None
            first_zone = next((r for r in unique[si:ei+1] if r["sb"] & TRIGGER_ZONE), None)
            first_mid = next((r for r in unique[si:ei+1] if r["sb"] & MID), None)
            print(f"    win{wi}: {start_r['t']}→{end_r['t']}ms  "
                  f"yawΔ={start_r['acuteYawDelta']:+.1f}→{end_r['acuteYawDelta']:+.1f}  "
                  f"rearm={end_r['acuteRearmTick']}")
            if first_zone is not None:
                print(f"      first_zone: t={first_zone['t']}  "
                      f"yawΔ={first_zone['acuteYawDelta']:+.1f}  sb={sb_str(first_zone['sb'])}")
            if first_mid is not None:
                print(f"      first_mid:  t={first_mid['t']}  "
                      f"yawΔ={first_mid['acuteYawDelta']:+.1f}  sb={sb_str(first_mid['sb'])}")
            if exit_r is not None:
                print(f"      exit:       t={exit_r['t']}  "
                      f"state={exit_r['acuteState']}  yawΔ={exit_r['acuteYawDelta']:+.1f}  "
                      f"sb={sb_str(exit_r['sb'])}  rearm={exit_r['acuteRearmTick']}")

    # ---- 2. 找 S1/S8 亮但触发区不亮 (线已偏到极端) ----
    extreme_only = []
    for i, r in enumerate(unique):
        sb = r["sb"]
        if (sb & S1 or sb & S8) and not (sb & TRIGGER_ZONE):
            side = "S1" if sb & S1 else "S8"
            extreme_only.append((i, side, sb))
    print(f"\n  [极端偏移] S1/S8亮但S3~S6不亮: {len(extreme_only)} 次")
    for idx, side, sb in extreme_only[:8]:
        r = unique[idx]
        print(f"    t={r['t']}ms  sb={sb_str(sb)}(0x{sb:02X})  {side}  "
              f"yaw={r['yaw']:.1f}  lp={r['lp']:.0f}")

    # ---- 3. 全灭段 (丢线) ----
    dark_segs = []
    in_dark = False
    dark_start = 0
    for i, r in enumerate(unique):
        if r["sb"] == 0:
            if not in_dark:
                in_dark = True
                dark_start = i
        else:
            if in_dark:
                dark_segs.append((dark_start, i - 1))
                in_dark = False
    if in_dark:
        dark_segs.append((dark_start, len(unique) - 1))

    print(f"\n  [丢线段] 全灭(sb=0): {len(dark_segs)} 段")
    for si, ei in dark_segs:
        t0 = unique[si]["t"]
        t1 = unique[ei]["t"]
        dur = t1 - t0
        # 丢线前的传感器状态
        pre = unique[max(0, si-1)]
        # 丢线后恢复的状态
        post = unique[min(len(unique)-1, ei+1)]
        yaw_before = unique[si]["yaw"]
        yaw_after = unique[ei]["yaw"]
        dyaw = yaw_after - yaw_before
        if dyaw > 180: dyaw -= 360
        if dyaw < -180: dyaw += 360
        print(f"    {t0}~{t1}ms ({dur}ms)  "
              f"前sb={sb_str(pre['sb'])} 后sb={sb_str(post['sb'])}  "
              f"Δyaw={dyaw:+.1f}°")

    # ---- 4. 大角度偏航变化 (实际转弯) ----
    yaw_events = []
    for i in range(1, len(unique)):
        dy = unique[i]["yaw"] - unique[i-1]["yaw"]
        if dy > 180: dy -= 360
        if dy < -180: dy += 360
        if abs(dy) > 5:  # 单tick偏航变化>5° = 快速旋转
            yaw_events.append((i, dy, unique[i]["yaw"]))

    print(f"\n  [快速偏航] 单tick Δyaw>5°: {len(yaw_events)} 次")
    for idx, dy, yaw in yaw_events[:10]:
        r = unique[idx]
        print(f"    t={r['t']}ms  Δyaw={dy:+.1f}°  yaw={yaw:.1f}°  "
              f"sb={sb_str(r['sb'])}  OL={r['OL']} OR={r['OR']}")

    # ---- 5. 传感器序列 (时间线) ----
    print(f"\n  [传感器时间线] (变化时刻)")
    last_sb = -1
    changes = []
    for r in unique:
        if r["sb"] != last_sb:
            changes.append(r)
            last_sb = r["sb"]
    # 只打印前30个变化
    for r in changes[:30]:
        sb = r["sb"]
        marker = ""
        if (sb & TRIGGER_ZONE) and (sb & S1):
            marker = " [TRIGGER-L S1+S3~S6]"
        elif (sb & TRIGGER_ZONE) and (sb & S8):
            marker = " [TRIGGER-R S8+S3~S6]"
        elif sb == 0:
            marker = " [DARK]"
        print(f"    t={r['t']}ms  sb={sb_str(sb)}(0x{sb:02X})  "
              f"yaw={r['yaw']:.1f}  lp={r['lp']:.0f}{marker}")

    # ---- 6. 电机输出模式分析 ----
    # 检测是否出现了锐角差速模式 (一轮大正 + 一轮小负或小正)
    acute_motor = []
    for r in unique:
        ol, or_ = r["OL"], r["OR"]
        if (ol > 150 and or_ < 0) or (or_ > 150 and ol < 0):
            acute_motor.append(r)
        elif (ol > 150 and -80 <= or_ <= 50) or (or_ > 150 and -80 <= ol <= 50):
            acute_motor.append(r)
    print(f"\n  [差速转弯电机] 疑似锐角差速(一轮>150,另一轮<50): {len(acute_motor)} 次")
    for r in acute_motor[:10]:
        print(f"    t={r['t']}ms  OL={r['OL']} OR={r['OR']}  "
              f"sb={sb_str(r['sb'])}  yaw={r['yaw']:.1f}")

    # ---- 7. 原地旋转检测 (两轮反向) ----
    spin_motor = []
    for r in unique:
        ol, or_ = r["OL"], r["OR"]
        if (ol > 80 and or_ < -80) or (or_ > 80 and ol < -80):
            spin_motor.append(r)
    print(f"\n  [原地旋转电机] 两轮反向(>80): {len(spin_motor)} 次")
    for r in spin_motor[:10]:
        print(f"    t={r['t']}ms  OL={r['OL']} OR={r['OR']}  "
              f"sb={sb_str(r['sb'])}  yaw={r['yaw']:.1f}")

    # 返回摘要
    return {
        "run": run_id,
        "samples": len(unique),
        "acute_candidates": len(firmware_events),
        "acute_confirmed": len(confirmed_events),
        "acute_cancelled": len(cancelled_events),
        "dark_segs": len(dark_segs),
        "acute_motor": len(acute_motor),
        "spin_motor": len(spin_motor),
        "yaw_range": max(r["yaw"] for r in unique) - min(r["yaw"] for r in unique),
    }


def print_summary(all_summaries, title):
    print(f"\n\n{'#'*72}")
    print(f"{title:^72}")
    print(f"{'#'*72}")
    print(f"{'Run':>4} {'样本':>5} {'候选':>5} {'确认':>5} {'取消':>5} {'丢线段':>6} {'差速':>5} {'旋转':>5} {'yaw范围':>8}")
    for s in all_summaries:
        print(f"{s['run']:4d} {s['samples']:5d} {s['acute_candidates']:5d} "
              f"{s['acute_confirmed']:5d} {s['acute_cancelled']:5d} "
              f"{s['dark_segs']:6d} {s['acute_motor']:5d} {s['spin_motor']:5d} "
              f"{s['yaw_range']:8.1f}°")

    candidates = [s for s in all_summaries if s["acute_candidates"] > 0]
    confirmed = [s for s in all_summaries if s["acute_confirmed"] > 0]
    cancelled = [s for s in all_summaries if s["acute_cancelled"] > 0]
    used_acute = [s for s in all_summaries if s["acute_motor"] > 0]
    used_spin = [s for s in all_summaries if s["spin_motor"] > 0]

    print(f"\n固件候选窗口: {len(candidates)}/{len(all_summaries)} 次")
    print(f"时间窗确认锐角: {len(confirmed)}/{len(all_summaries)} 次")
    print(f"时间窗取消(对侧触发): {len(cancelled)}/{len(all_summaries)} 次")
    print(f"进入差速转弯: {len(used_acute)}/{len(all_summaries)} 次")
    print(f"进入原地旋转: {len(used_spin)}/{len(all_summaries)} 次")


def main_live():
    all_summaries = []

    with serial.Serial(PORT, baudrate=BAUD, timeout=0.1) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.3)

        # 确保停车
        ser.write(b"#STOP!")
        time.sleep(0.5)
        ser.reset_input_buffer()

        # 切到TRACK模式
        ser.write(b"#MODE=TRACK!")
        time.sleep(0.3)
        while ser.in_waiting:
            ser.readline()

        for run_id in range(1, RUNS + 1):
            print(f"\n{'#'*60}")
            print(f"  准备 Run {run_id}/{RUNS} — 请将小车放在线上")
            print(f"  {'等待中...' if run_id > 1 else '即将开始...'}")
            if run_id > 1:
                for remaining in range(int(PAUSE_SEC), 0, -1):
                    print(f"    {remaining}s...", end="\r")
                    time.sleep(1)
            else:
                time.sleep(1)

            # 清缓冲
            ser.reset_input_buffer()
            hb_lines = []

            # RUN
            print(f"\n  >>> Run {run_id} 开始 ({RUN_SEC}s) <<<")
            ser.write(b"#RUN!")
            t0 = time.time()

            while (time.time() - t0) < RUN_SEC:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="ignore").strip()
                if line.startswith("HB:"):
                    hb_lines.append(line)

            # STOP
            ser.write(b"#STOP!")
            time.sleep(0.3)
            while ser.in_waiting:
                raw = ser.readline()
                if raw:
                    line = raw.decode("utf-8", errors="ignore").strip()
                    if line.startswith("HB:"):
                        hb_lines.append(line)

            print(f"  收集 {len(hb_lines)} 条HB记录")

            # 解析
            records = []
            for l in hb_lines:
                r = parse_hb(l)
                if r:
                    records.append(r)

            # 分析
            summary = analyze_run(run_id, records)
            if summary:
                all_summaries.append(summary)

            # 保存原始数据
            fname = f"acute_diag_run{run_id:02d}.txt"
            with open(fname, "w") as f:
                for l in hb_lines:
                    f.write(l + "\n")

    print_summary(all_summaries, "总结: 10次跑锐角检测")


def analyze_saved_files(paths):
    all_summaries = []
    for run_id, path in enumerate(paths, start=1):
        text = Path(path).read_text(encoding="utf-8", errors="ignore")
        records = []
        for line in text.splitlines():
            r = parse_hb(line)
            if r:
                records.append(r)
        print(f"\n[离线分析] {Path(path).name}")
        summary = analyze_run(run_id, records)
        if summary:
            all_summaries.append(summary)
    if all_summaries:
        print_summary(all_summaries, "总结: 保存日志锐角检测")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        analyze_saved_files(sys.argv[1:])
    else:
        main_live()
