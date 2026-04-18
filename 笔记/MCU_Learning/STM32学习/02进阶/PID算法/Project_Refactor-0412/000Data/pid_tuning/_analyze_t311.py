lines = open(r'T311_tune_dt_fix_antiwindup_20260413_135158_raw.txt').readlines()
total = 0
pc0 = 0
neg = 0
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
    if int(kv.get("pc", "0")) <= 0:
        pc0 += 1
    el = int(kv.get("el", "0"))
    er = int(kv.get("er", "0"))
    if el < 0 or er < 0:
        neg += 1

print(f"Total run=1 HB: {total}")
print(f"pc<=0: {pc0} ({pc0/max(total,1)*100:.1f}%)")
print(f"el/er<0: {neg} ({neg/max(total,1)*100:.1f}%)")

# Compare with T309
lines2 = open(r'T309_tune_core_clamp_test_20260413_133815_raw.txt').readlines()
total2 = 0
pc0_2 = 0
for line in lines2:
    if not line.startswith("HB:"):
        continue
    kv = {}
    for p in line[3:].split(","):
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k.strip()] = v.strip()
    if int(kv.get("run", "0")) == 0:
        continue
    total2 += 1
    if int(kv.get("pc", "0")) <= 0:
        pc0_2 += 1

print(f"\n--- Comparison ---")
print(f"T309 (before dt fix): pc<=0 = {pc0_2}/{total2} ({pc0_2/max(total2,1)*100:.1f}%)")
print(f"T311 (after dt fix):  pc<=0 = {pc0}/{total} ({pc0/max(total,1)*100:.1f}%)")
