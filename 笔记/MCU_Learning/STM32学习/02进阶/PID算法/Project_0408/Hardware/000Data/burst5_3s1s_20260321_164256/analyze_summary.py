import os,re,glob,csv,statistics
out=os.environ["OUT_DIR"]
files=sorted(glob.glob(os.path.join(out,"*_raw.txt")))
pat=re.compile(r"(\\w+)=(-?\\d+(?:\\.\\d+)?)")
def kv(line):
    d={}
    for k,v in pat.findall(line):
        try: d[k]=float(v)
        except: pass
    return d
rows=[]
for fp in files:
    name=os.path.basename(fp)
    text=open(fp,"rb").read().decode("utf-8","ignore").splitlines()
    samp=[kv(l) for l in text if l.startswith("HB ")]
    run=[s for s in samp if int(s.get("run",0))==1]
    def mean(key):
        vals=[s[key] for s in run if key in s]
        return statistics.mean(vals) if vals else ""
    def peak_abs(key):
        vals=[abs(s[key]) for s in run if key in s]
        return max(vals) if vals else ""
    ol=mean("OL"); orr=mean("OR")
    rows.append({
        "file":name,
        "n_run":len(run),
        "OL_mean":ol,
        "OR_mean":orr,
        "OL_minus_OR_mean": (ol-orr) if (ol!="" and orr!="") else "",
        "el_mean":mean("el"),
        "er_mean":mean("er"),
        "ed_mean":mean("ed"),
        "OL_peak_abs":peak_abs("OL"),
        "OR_peak_abs":peak_abs("OR"),
        "el_peak_abs":peak_abs("el"),
        "er_peak_abs":peak_abs("er"),
    })
csv_path=os.path.join(out,"summary.csv")
with open(csv_path,"w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0].keys()) if rows else ["file"])
    w.writeheader()
    w.writerows(rows)
print("WROTE",csv_path)
print("FILES",len(files))
