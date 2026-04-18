"""Show context around pc=0 events in T311, and analyze actual motor speed at DZ."""
lines = open(r'T311_tune_dt_fix_antiwindup_20260413_135158_raw.txt').readlines()

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
    records.append(kv)

# Show 3 samples before and after each pc=0 event (skip consecutive duplicates)
print("=== Context around pc=0 events (first 5) ===")
shown = 0
for i, kv in enumerate(records):
    if int(kv.get("pc", "0")) > 0:
        continue
    if shown >= 5:
        break
    # Skip if previous record has same t (duplicate)
    if i > 0 and records[i-1].get("t") == kv.get("t"):
        continue
    shown += 1
    print(f"\n--- Event {shown} ---")
    start = max(0, i-3)
    end = min(len(records), i+4)
    for j in range(start, end):
        r = records[j]
        t = r.get("t", "?")
        el = r.get("el", "?")
        er = r.get("er", "?")
        pc = r.get("pc", "?")
        ol = r.get("OL", "?")
        or_ = r.get("OR", "?")
        hd = r.get("hd", "?")
        marker = " <<<" if j == i else ""
        print(f"  t={t:>6} el={el:>4} er={er:>4} pc={pc:>4} OL={ol:>4} OR={or_:>4} hd={hd:>3}{marker}")

# Analyze actual speed at different OL levels
print("\n\n=== Motor speed vs OL analysis ===")
prev_t = None
for i, kv in enumerate(records):
    t = int(kv.get("t", "0"))
    el = int(kv.get("el", "0"))
    er = int(kv.get("er", "0"))
    ol = int(kv.get("OL", "0"))
    or_ = int(kv.get("OR", "0"))
    if i > 0:
        prev = records[i-1]
        prev_t_val = int(prev.get("t", "0"))
        dt_ms = t - prev_t_val
        if dt_ms > 0:
            # el is per-control-period. dt_ms is between HB samples.
            # We don't know how many control periods happened.
            pass

# Just show el distribution when motor is running (pc > 50)
el_vals = []
for kv in records:
    if int(kv.get("pc", "0")) > 50:
        el_vals.append(int(kv.get("el", "0")))
if el_vals:
    import statistics
    print(f"When pc>50: el mean={statistics.mean(el_vals):.1f}, median={statistics.median(el_vals):.0f}, "
          f"min={min(el_vals)}, max={max(el_vals)}, n={len(el_vals)}")

# Show el distribution when pc=0
el_at_0 = []
for kv in records:
    if int(kv.get("pc", "0")) <= 0:
        el_at_0.append(int(kv.get("el", "0")))
if el_at_0:
    print(f"When pc=0:  el mean={statistics.mean(el_at_0):.1f}, median={statistics.median(el_at_0):.0f}, "
          f"min={min(el_at_0)}, max={max(el_at_0)}, n={len(el_at_0)}")
