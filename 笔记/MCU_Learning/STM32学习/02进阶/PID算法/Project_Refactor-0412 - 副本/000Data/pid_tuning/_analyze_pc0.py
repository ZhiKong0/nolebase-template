import sys

lines = open(r'T309_tune_core_clamp_test_20260413_133815_raw.txt').readlines()
prev_t = 0
prev_pc = 999
print(f"{'t_ms':>7} {'el':>5} {'er':>5} {'pc':>4} {'OL':>4} {'OR':>4} {'hd':>4}  prev_pc  dt_ms")
for line in lines:
    if not line.startswith("HB:"):
        continue
    kv = {}
    for p in line[3:].split(","):
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k.strip()] = v.strip()
    run = int(kv.get("run", "0"))
    if run == 0:
        continue
    t = int(kv.get("t", "0"))
    el = int(kv.get("el", "0"))
    er = int(kv.get("er", "0"))
    pc = int(kv.get("pc", "0"))
    ol = int(kv.get("OL", "0"))
    or_ = int(kv.get("OR", "0"))
    hd = kv.get("hd", "0")
    dt = t - prev_t
    if pc <= 5:
        print(f"{t:>7} {el:>5} {er:>5} {pc:>4} {ol:>4} {or_:>4} {hd:>4}  ppc={prev_pc:>4}  dt={dt}")
    prev_t = t
    prev_pc = pc

# Also count pc=0 frequency
total = 0
pc0 = 0
for line in lines:
    if not line.startswith("HB:") or "run=0" in line:
        continue
    kv = {}
    for p in line[3:].split(","):
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k.strip()] = v.strip()
    total += 1
    if int(kv.get("pc", "0")) <= 0:
        pc0 += 1
print(f"\nTotal run=1 HB: {total}, pc<=0: {pc0} ({pc0/max(total,1)*100:.1f}%)")
