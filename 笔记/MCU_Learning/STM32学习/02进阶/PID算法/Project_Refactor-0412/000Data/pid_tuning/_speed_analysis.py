"""Detailed speed oscillation analysis - compare runs."""
import statistics, sys

fname = sys.argv[1] if len(sys.argv) > 1 else r'T319_tune_base30_r2_20260413_141539_raw.txt'
print(f"=== Analyzing: {fname.split(chr(92))[-1]} ===\n")
lines = open(fname).readlines()

records = []
for line in lines:
    if not line.startswith("HB:"):
        continue
    kv = {}
    for p in line[3:].split(","):
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k.strip()] = v.strip()
    if int(kv.get("run", "0")) == 0:
        continue
    t = int(kv.get("t", "0"))
    # Skip duplicates
    if records and records[-1]["t"] == t:
        continue
    records.append({
        "t": t,
        "el": int(kv.get("el", "0")),
        "er": int(kv.get("er", "0")),
        "pc": int(kv.get("pc", "0")),
        "ol": int(kv.get("OL", "0")),
        "or": int(kv.get("OR", "0")),
    })

# 1. OL time series (actual motor PWM)
ol_vals = [r["ol"] for r in records]
pc_vals = [r["pc"] for r in records]
el_vals = [r["el"] for r in records]

print("=== 电机实际PWM (OL) 统计 ===")
print(f"  mean={statistics.mean(ol_vals):.1f}  std={statistics.stdev(ol_vals):.1f}  "
      f"min={min(ol_vals)}  max={max(ol_vals)}")

print("\n=== 速度PID输出 (pc) 统计 ===")
print(f"  mean={statistics.mean(pc_vals):.1f}  std={statistics.stdev(pc_vals):.1f}  "
      f"min={min(pc_vals)}  max={max(pc_vals)}")

print("\n=== 编码器读数 (el) 统计 ===")
print(f"  mean={statistics.mean(el_vals):.1f}  std={statistics.stdev(el_vals):.1f}  "
      f"min={min(el_vals)}  max={max(el_vals)}")

# 2. OL histogram
print("\n=== OL 分布 ===")
ranges = [(0, 0), (1, 79), (80, 80), (81, 90), (91, 100), (101, 200)]
for lo, hi in ranges:
    cnt = sum(1 for v in ol_vals if lo <= v <= hi)
    pct = cnt / len(ol_vals) * 100
    bar = "#" * int(pct / 2)
    print(f"  OL {lo:>3}-{hi:>3}: {cnt:>4} ({pct:5.1f}%) {bar}")

# 3. pc histogram
print("\n=== pc 分布 ===")
pc_ranges = [(0, 1), (2, 20), (21, 50), (51, 80), (81, 100), (101, 200)]
for lo, hi in pc_ranges:
    cnt = sum(1 for v in pc_vals if lo <= v <= hi)
    pct = cnt / len(pc_vals) * 100
    bar = "#" * int(pct / 2)
    print(f"  pc {lo:>3}-{hi:>3}: {cnt:>4} ({pct:5.1f}%) {bar}")

# 4. Speed time series: consecutive OL changes
print("\n=== OL 逐采样变化 (slew) ===")
slews = [abs(ol_vals[i] - ol_vals[i-1]) for i in range(1, len(ol_vals))]
print(f"  mean_slew={statistics.mean(slews):.1f}  max_slew={max(slews)}  "
      f"slew>20: {sum(1 for s in slews if s > 20)} ({sum(1 for s in slews if s > 20)/len(slews)*100:.1f}%)")

# 5. Show 20 consecutive samples from cruising phase (t > 5000ms)
print("\n=== 巡航段连续20样本 (t>5s) ===")
print(f"{'t_ms':>7} {'el':>5} {'er':>5} {'pc':>4} {'OL':>4} {'OR':>4} {'dt':>5}")
shown = 0
prev_t = 0
for r in records:
    if r["t"] < 5000:
        prev_t = r["t"]
        continue
    dt = r["t"] - prev_t
    print(f"{r['t']:>7} {r['el']:>5} {r['er']:>5} {r['pc']:>4} {r['ol']:>4} {r['or']:>4} {dt:>5}")
    prev_t = r["t"]
    shown += 1
    if shown >= 20:
        break
