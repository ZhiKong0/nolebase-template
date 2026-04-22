"""
track_test.py — 循迹模式快速测试：切换到TRACK模式，跑N秒，采集HB遥测，自动停车
用法: python track_test.py [--duration 5] [--port COM18]
"""
import argparse
import time
import serial

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM18")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=5.0)
    ap.add_argument("--spd", type=float, default=None, help="Override speed target")
    ap.add_argument("--lkp", type=float, default=None, help="Override line KP")
    ap.add_argument("--lkd", type=float, default=None, help="Override line KD")
    args = ap.parse_args()

    hb_lines = []

    def send_wait(ser, cmd):
        ser.write(cmd.encode("utf-8"))
        time.sleep(0.2)
        while ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line and not line.startswith("HB:"):
                print(f"  [resp] {line}")

    with serial.Serial(args.port, baudrate=args.baud, timeout=0.1) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.3)

        # 1) STOP (clean state)
        ser.write(b"#STOP!")
        time.sleep(0.5)
        ser.reset_input_buffer()

        # 2) Switch to TRACK mode (this loads defaults including speed target)
        send_wait(ser, "#MODE=TRACK!")

        # 3) Override params AFTER mode switch (so LoadDefaults doesn't overwrite)
        if args.spd is not None:
            send_wait(ser, f"#SPD={args.spd:.2f}!")
            print(f"  [set] SPD={args.spd}")
        if args.lkp is not None:
            send_wait(ser, f"#LKP={args.lkp:.3f}!")
            print(f"  [set] LKP={args.lkp}")
        if args.lkd is not None:
            send_wait(ser, f"#LKD={args.lkd:.3f}!")
            print(f"  [set] LKD={args.lkd}")

        # 4) RUN
        print(f"\n=== START tracking for {args.duration}s ===")
        ser.write(b"#RUN!")
        t0 = time.time()

        # 4) Collect HB lines
        while (time.time() - t0) < args.duration:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            if line.startswith("HB:"):
                hb_lines.append(line)
                print(line)
            elif line.startswith("OK"):
                print(f"  [resp] {line}")

        # 5) STOP
        ser.write(b"#STOP!")
        time.sleep(0.3)
        # drain remaining
        while ser.in_waiting:
            raw = ser.readline()
            if raw:
                line = raw.decode("utf-8", errors="ignore").strip()
                if line.startswith("HB:"):
                    hb_lines.append(line)
                    print(line)
                elif line:
                    print(f"  [resp] {line}")

    print(f"\n=== DONE: collected {len(hb_lines)} HB records ===")

    if not hb_lines:
        return

    # --- Parse all fields ---
    records = []
    for l in hb_lines:
        if not l.startswith("HB:"):
            continue
        kv = {}
        for pair in l[3:].split(","):
            if "=" in pair:
                k, v = pair.split("=", 1)
                kv[k.strip()] = v.strip()
        try:
            records.append({
                "t": int(kv.get("t", "0")),
                "m": kv.get("m", "S"),
                "run": int(kv.get("run", "0")),
                "el": int(kv.get("el", "0")),
                "er": int(kv.get("er", "0")),
                "pc": int(kv.get("pc", "0")),
                "hd": int(kv.get("hd", "0")),
                "OL": int(kv.get("OL", "0")),
                "OR": int(kv.get("OR", "0")),
                "sb": int(kv.get("sb", "0")),
                "lp": float(kv.get("lp", "0")),
                "yaw": float(kv.get("yaw", "0")),
            })
        except:
            pass

    if not records:
        print("No valid records parsed.")
        return

    # Deduplicate by timestamp (firmware sends duplicates)
    seen_t = set()
    unique = []
    for r in records:
        if r["t"] not in seen_t and r["m"] == "T" and r["run"] == 1:
            seen_t.add(r["t"])
            unique.append(r)
    print(f"去重后有效样本: {len(unique)} (原始 {len(records)})")

    if len(unique) < 3:
        print("样本太少，跳过分析")
        return

    # --- 基础统计 ---
    sb_vals = [r["sb"] for r in unique]
    lp_vals = [r["lp"] for r in unique]
    ol_vals = [r["OL"] for r in unique]
    or_vals = [r["OR"] for r in unique]
    pc_vals = [r["pc"] for r in unique]
    el_vals = [r["el"] for r in unique]
    er_vals = [r["er"] for r in unique]
    hd_vals = [r["hd"] for r in unique]

    zero_sb = sum(1 for s in sb_vals if s == 0)
    print(f"\n{'='*50}")
    print(f"{'基础统计':^50}")
    print(f"{'='*50}")
    print(f"时间范围: {unique[0]['t']}ms ~ {unique[-1]['t']}ms ({(unique[-1]['t']-unique[0]['t'])/1000:.1f}s)")
    print(f"传感器全灭: {zero_sb}/{len(sb_vals)} ({100*zero_sb/len(sb_vals):.1f}%)")
    print(f"位置(lp):   min={min(lp_vals):.0f}, max={max(lp_vals):.0f}, mean={sum(lp_vals)/len(lp_vals):.1f}")
    print(f"偏差(hd):   min={min(hd_vals)}, max={max(hd_vals)}, mean={sum(hd_vals)/len(hd_vals):.1f}")
    print(f"速度环(pc): min={min(pc_vals)}, max={max(pc_vals)}, mean={sum(pc_vals)/len(pc_vals):.0f}")
    print(f"左轮(OL):   min={min(ol_vals)}, max={max(ol_vals)}, mean={sum(ol_vals)/len(ol_vals):.0f}")
    print(f"右轮(OR):   min={min(or_vals)}, max={max(or_vals)}, mean={sum(or_vals)/len(or_vals):.0f}")
    print(f"编码器左(el): min={min(el_vals)}, max={max(el_vals)}, mean={sum(el_vals)/len(el_vals):.0f}")
    print(f"编码器右(er): min={min(er_vals)}, max={max(er_vals)}, mean={sum(er_vals)/len(er_vals):.0f}")

    # --- 速度断续分析 ---
    print(f"\n{'='*50}")
    print(f"{'速度断续分析':^50}")
    print(f"{'='*50}")

    # pc=0 or very low means speed stall
    pc_stall = [r for r in unique if r["pc"] < 10]
    pc_low = [r for r in unique if 10 <= r["pc"] < 40]
    pc_normal = [r for r in unique if r["pc"] >= 40]
    print(f"速度环输出: 停滞(pc<10): {len(pc_stall)} ({100*len(pc_stall)/len(unique):.1f}%)")
    print(f"            低速(10≤pc<40): {len(pc_low)} ({100*len(pc_low)/len(unique):.1f}%)")
    print(f"            正常(pc≥40): {len(pc_normal)} ({100*len(pc_normal)/len(unique):.1f}%)")

    # Detect speed dips: pc drops >30 between consecutive samples
    dips = []
    for i in range(1, len(unique)):
        drop = unique[i-1]["pc"] - unique[i]["pc"]
        if drop > 30:
            dips.append((unique[i]["t"], unique[i-1]["pc"], unique[i]["pc"], drop))
    print(f"\n速度骤降(>30): {len(dips)} 次")
    for t, before, after, drop in dips[:10]:
        print(f"  t={t}ms: pc {before} → {after} (降{drop})")

    # Both wheels stall (OL < 10 AND OR < 10)
    both_stall = [r for r in unique if r["OL"] < 10 and r["OR"] < 10]
    print(f"\n双轮同时停滞(OL<10 & OR<10): {len(both_stall)} 次")
    for r in both_stall[:5]:
        print(f"  t={r['t']}ms: OL={r['OL']}, OR={r['OR']}, pc={r['pc']}, sb={r['sb']}")

    # One wheel stall (OL=0 or OR=0 but other >0)
    one_stall_l = [r for r in unique if r["OL"] == 0 and r["OR"] > 20]
    one_stall_r = [r for r in unique if r["OR"] == 0 and r["OL"] > 20]
    print(f"左轮停(OL=0,OR>20): {len(one_stall_l)} ({100*len(one_stall_l)/len(unique):.1f}%)")
    print(f"右轮停(OR=0,OL>20): {len(one_stall_r)} ({100*len(one_stall_r)/len(unique):.1f}%)")

    # --- 速度连续性分析 ---
    print(f"\n{'='*50}")
    print(f"{'速度连续性(编码器)':^50}")
    print(f"{'='*50}")
    avg_speed = [(r["el"] + r["er"]) / 2.0 for r in unique]
    near_zero = sum(1 for s in avg_speed if s < 10)
    print(f"平均编码器: min={min(avg_speed):.0f}, max={max(avg_speed):.0f}, "
          f"mean={sum(avg_speed)/len(avg_speed):.0f}")
    print(f"近零速(avg<10): {near_zero}/{len(unique)} ({100*near_zero/len(unique):.1f}%)")

    # Speed segments: group consecutive samples by speed level
    segments = []
    current_level = "moving" if avg_speed[0] >= 10 else "stall"
    seg_start = unique[0]["t"]
    for i in range(1, len(unique)):
        level = "moving" if avg_speed[i] >= 10 else "stall"
        if level != current_level:
            segments.append((current_level, seg_start, unique[i-1]["t"]))
            current_level = level
            seg_start = unique[i]["t"]
    segments.append((current_level, seg_start, unique[-1]["t"]))

    stall_segs = [(lbl, s, e) for lbl, s, e in segments if lbl == "stall"]
    move_segs = [(lbl, s, e) for lbl, s, e in segments if lbl == "moving"]
    print(f"\n走走停停段数: 移动={len(move_segs)}, 停滞={len(stall_segs)}")
    if stall_segs:
        stall_durs = [e - s for _, s, e in stall_segs]
        print(f"停滞段时长(ms): {stall_durs[:15]}")
    if move_segs:
        move_durs = [e - s for _, s, e in move_segs]
        print(f"移动段时长(ms): {move_durs[:15]}")

    # --- 画龙/摆动分析 ---
    print(f"\n{'='*50}")
    print(f"{'画龙/摆动分析':^50}")
    print(f"{'='*50}")

    import math

    # 1. 位置RMS (越小越居中)
    lp_rms = math.sqrt(sum(v**2 for v in lp_vals) / len(lp_vals))
    print(f"位置RMS: {lp_rms:.1f} (越小越居中, <30优, 30-60中, >60差)")

    # 2. 位置零点穿越次数 (lp符号翻转 = 穿过中线)
    zero_cross = 0
    for i in range(1, len(unique)):
        if unique[i-1]["lp"] * unique[i]["lp"] < 0:
            zero_cross += 1
    duration_s = (unique[-1]["t"] - unique[0]["t"]) / 1000.0
    zc_per_s = zero_cross / max(duration_s, 0.1)
    print(f"零点穿越: {zero_cross}次 ({zc_per_s:.2f}次/秒)")

    # 3. 位置方向反转次数 (lp导数符号翻转 = 来回摆)
    reversals = 0
    for i in range(2, len(unique)):
        d1 = unique[i-1]["lp"] - unique[i-2]["lp"]
        d2 = unique[i]["lp"] - unique[i-1]["lp"]
        if d1 * d2 < 0 and abs(d1) > 5 and abs(d2) > 5:
            reversals += 1
    rev_per_s = reversals / max(duration_s, 0.1)
    print(f"方向反转(>5): {reversals}次 ({rev_per_s:.2f}次/秒)")

    # 4. devSpeed符号翻转 (纠偏方向反复切换 = 画龙)
    hd_flips = 0
    for i in range(1, len(unique)):
        if unique[i-1]["hd"] * unique[i]["hd"] < 0:
            hd_flips += 1
    hd_flip_per_s = hd_flips / max(duration_s, 0.1)
    print(f"纠偏翻转: {hd_flips}次 ({hd_flip_per_s:.2f}次/秒)")

    # 5. 传感器跳变次数 (sb变化)
    sb_changes = 0
    for i in range(1, len(unique)):
        if unique[i]["sb"] != unique[i-1]["sb"]:
            sb_changes += 1
    sb_chg_per_s = sb_changes / max(duration_s, 0.1)
    print(f"传感器跳变: {sb_changes}次 ({sb_chg_per_s:.2f}次/秒)")

    # 6. 摆动幅度统计 (连续同方向运动的峰谷差)
    swings = []
    swing_start_lp = unique[0]["lp"]
    swing_peak = swing_start_lp
    swing_dir = 0  # 0=unknown, 1=up, -1=down
    for i in range(1, len(unique)):
        dlp = unique[i]["lp"] - unique[i-1]["lp"]
        if abs(dlp) < 2:
            continue
        new_dir = 1 if dlp > 0 else -1
        if swing_dir == 0:
            swing_dir = new_dir
            swing_peak = unique[i]["lp"]
        elif new_dir != swing_dir and abs(unique[i]["lp"] - swing_peak) > 10:
            swings.append(abs(unique[i-1]["lp"] - swing_start_lp))
            swing_start_lp = unique[i-1]["lp"]
            swing_peak = unique[i]["lp"]
            swing_dir = new_dir
        else:
            swing_peak = unique[i]["lp"]
    if swings:
        avg_swing = sum(swings) / len(swings)
        max_swing = max(swings)
        print(f"摆动次数: {len(swings)}, 平均幅度: {avg_swing:.0f}, 最大: {max_swing:.0f}")
        print(f"  (<20=稳定, 20-50=轻微画龙, 50-100=明显画龙, >100=严重)")
    else:
        print("摆动次数: 0 (非常稳定)")

    # 7. 画龙综合评分 (0-100, 越低越好)
    score = min(100, int(
        0.3 * min(lp_rms, 150) / 1.5 +       # RMS贡献 (0-30)
        0.3 * min(rev_per_s * 20, 30) +        # 反转频率贡献 (0-30)
        0.2 * min(hd_flip_per_s * 15, 20) +    # 纠偏翻转贡献 (0-20)
        0.2 * min((avg_swing if swings else 0) / 5, 20)  # 摆幅贡献 (0-20)
    ))
    labels = {0: "极佳", 20: "良好", 40: "一般", 60: "画龙", 80: "严重画龙"}
    label = "极佳"
    for threshold in sorted(labels.keys()):
        if score >= threshold:
            label = labels[threshold]
    print(f"\n★ 画龙评分: {score}/100 ({label})")
    print(f"  0-20=极佳, 20-40=良好, 40-60=一般, 60-80=画龙, 80-100=严重")

    # --- 速度环 vs 时间趋势（分段平均）---
    print(f"\n{'='*50}")
    print(f"{'时间趋势 (每2秒)':^50}")
    print(f"{'='*50}")
    t_start = unique[0]["t"]
    t_end = unique[-1]["t"]
    bucket_ms = 2000
    t = t_start
    print(f"{'时段':>10} {'pc均':>6} {'|hd|均':>7} {'lpRMS':>7} {'反转':>4} {'sb变':>4} {'lp均':>7}")
    while t < t_end:
        bucket = [r for r in unique if t <= r["t"] < t + bucket_ms]
        if bucket:
            pc_m = sum(r["pc"] for r in bucket) / len(bucket)
            hd_m = sum(abs(r["hd"]) for r in bucket) / len(bucket)
            lp_bucket = [r["lp"] for r in bucket]
            lp_rms_b = math.sqrt(sum(v**2 for v in lp_bucket) / len(lp_bucket))
            lp_m = sum(lp_bucket) / len(lp_bucket)
            # count reversals in bucket
            rev_b = 0
            for j in range(2, len(bucket)):
                d1 = bucket[j-1]["lp"] - bucket[j-2]["lp"]
                d2 = bucket[j]["lp"] - bucket[j-1]["lp"]
                if d1 * d2 < 0 and abs(d1) > 5 and abs(d2) > 5:
                    rev_b += 1
            sb_b = sum(1 for j in range(1, len(bucket)) if bucket[j]["sb"] != bucket[j-1]["sb"])
            label = f"{t//1000}-{(t+bucket_ms)//1000}s"
            print(f"{label:>10} {pc_m:6.0f} {hd_m:7.0f} {lp_rms_b:7.0f} {rev_b:4d} {sb_b:4d} {lp_m:7.1f}")
        t += bucket_ms

if __name__ == "__main__":
    main()
