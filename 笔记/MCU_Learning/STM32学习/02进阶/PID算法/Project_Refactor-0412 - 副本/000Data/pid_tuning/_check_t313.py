"""Compare T309 (core>=0), T311 (dt fix), T313 (core>=1)."""
import statistics

def analyze(fname, label):
    lines = open(fname).readlines()
    total = pc0 = pc1 = neg = 0
    ol_vals = []
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
        total += 1
        pc = int(kv.get("pc", "0"))
        if pc <= 0: pc0 += 1
        if pc <= 1: pc1 += 1
        el = int(kv.get("el", "0"))
        er = int(kv.get("er", "0"))
        if el < 0 or er < 0: neg += 1
        ol = int(kv.get("OL", "0"))
        ol_vals.append(ol)
    
    ol_std = statistics.stdev(ol_vals) if len(ol_vals) > 1 else 0
    print(f"{label}: n={total} pc<=0={pc0}({pc0/max(total,1)*100:.1f}%) "
          f"pc<=1={pc1}({pc1/max(total,1)*100:.1f}%) "
          f"el<0={neg}({neg/max(total,1)*100:.1f}%) "
          f"OL_std={ol_std:.1f}")

analyze("T309_tune_core_clamp_test_20260413_133815_raw.txt", "T309 core>=0       ")
analyze("T311_tune_dt_fix_antiwindup_20260413_135158_raw.txt", "T311 dt+antiwindup  ")
analyze("T313_tune_core_min1_20260413_140214_raw.txt", "T313 core>=1 (BEST)")
