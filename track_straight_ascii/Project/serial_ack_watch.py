import os
import re
import time
import argparse
import json
import random
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
from typing import Any
import serial
from serial.serialutil import SerialException

PORT = "COM8"
BAUD = 115200
RUN_S = 5.0
LOG_NAME = f"com8_{time.strftime('%Y%m%d_%H%M%S')}_ack_watch.txt"

DATA_DIR = os.path.join(os.path.dirname(__file__), "000Data")

USE_MANUAL_TRIM = False
MANUAL_TRIM_VALUE = 0


@dataclass
class RunConfig:
    spd: int = 4
    so: int = 80
    ramp: int = 10
    min_pwm: int = 30
    db: float = 1.0
    hd: float = 0.0010
    hs: float = 0.6
    hp: float = 6.0
    at: int = 0
    trim: int = 0
    seconds: float = 3.5
    kp: Optional[int] = None
    km: Optional[int] = None


def clamp_int(v: int, lo: int, hi: int) -> int:
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def clamp_float(v: float, lo: float, hi: float) -> float:
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def parse_hb_line(line: str) -> Optional[Dict[str, float]]:
    if not line.startswith("HB ") and not line.startswith("STAT "):
        return None
    d: Dict[str, float] = {}
    parts = line.split()
    for p in parts[1:]:
        if "=" not in p:
            continue
        k, v = p.split("=", 1)
        try:
            d[k] = float(v)
        except ValueError:
            continue
    return d


def parse_kv_line(line: str) -> Dict[str, Any]:
    # 解析固件输出: "HB ... k=v ..." / "STAT ... k=v ..."
    # 返回一个尽量以int为主的字典，便于做stall统计。
    if not line.startswith("HB ") and not line.startswith("STAT "):
        return {}
    out: Dict[str, Any] = {}
    parts = line.strip().split()
    for p in parts[1:]:
        if "=" not in p:
            continue
        k, v = p.split("=", 1)
        if not k:
            continue
        try:
            vv = v[1:] if v.startswith("+") else v
            if "." in vv:
                out[k] = float(vv)
            else:
                out[k] = int(vv)
        except Exception:
            out[k] = v
    return out


def summarize_run_kv(lines: List[str], stall_pwm_th: int = 12) -> Dict[str, float]:
    # 从日志行中提取关键字段，判断是否出现“run=1但PWM很小导致顶不动”的stall。
    runs: List[int] = []
    Ls: List[int] = []
    Rs: List[int] = []
    els: List[int] = []
    ers: List[int] = []
    okc: List[int] = []
    failc: List[int] = []

    for ln in lines:
        kv = parse_kv_line(ln)
        if not kv:
            continue
        if "run" in kv:
            try:
                runs.append(int(kv["run"]))
            except Exception:
                pass
        for k, arr in (("L", Ls), ("R", Rs), ("el", els), ("er", ers), ("ok", okc), ("fail", failc)):
            if k in kv:
                try:
                    arr.append(int(kv[k]))
                except Exception:
                    pass

    def mean(xs: List[int]) -> float:
        return (sum(xs) / len(xs)) if xs else 0.0

    def mn(xs: List[int]) -> float:
        return float(min(xs)) if xs else 0.0

    def mx(xs: List[int]) -> float:
        return float(max(xs)) if xs else 0.0

    n = min(len(Ls), len(Rs))
    stall = 0
    for i in range(n):
        if Ls[i] <= stall_pwm_th and Rs[i] <= stall_pwm_th:
            stall += 1
    stall_ratio = (stall / n) if n else 0.0
    run0_ratio = (sum(1 for r in runs if r == 0) / len(runs)) if runs else 0.0

    ok_delta = float(okc[-1] - okc[0]) if len(okc) >= 2 else 0.0
    fail_delta = float(failc[-1] - failc[0]) if len(failc) >= 2 else 0.0

    return {
        "hb_n": float(n),
        "stall_pwm_th": float(stall_pwm_th),
        "stall_ratio": float(stall_ratio),
        "run0_ratio": float(run0_ratio),
        "L_mean": mean(Ls),
        "R_mean": mean(Rs),
        "L_min": mn(Ls),
        "R_min": mn(Rs),
        "el_mean": mean(els),
        "er_mean": mean(ers),
        "el_min": mn(els),
        "er_min": mn(ers),
        "ok_delta_tele": ok_delta,
        "fail_delta_tele": fail_delta,
        "fail_max": mx(failc),
    }


def drain_lines(ser: serial.Serial, duration_s: float, realtime: bool, f) -> List[str]:
    start = now()
    end = start + duration_s
    text_buf = ""
    out_lines: List[str] = []
    while now() < end:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue
        if f is not None:
            f.write(b)
            f.flush()
        text_buf += b.decode("utf-8", errors="ignore")
        for ln in iter_lines_from_bytes(text_buf):
            out_lines.append(ln)
            if realtime and (ln.startswith("HB ") or ln.startswith("STAT ") or ln.startswith("OK") or ln.startswith("ERR")):
                print(ln)
        if len(text_buf) > 4096:
            text_buf = text_buf[-4096:]
    return out_lines


def build_seq(cfg: RunConfig) -> List[Tuple[str, str]]:
    seq: List[Tuple[str, str]] = [
        ("#STOP!", r"OK\s+STOP"),
        ("#RAW=0!", r"OK\s+RAW0"),
        ("#CAL!", r"OK\s+CAL"),
        (f"#AT={cfg.at}!", r"OK\s+AT"),
        (f"#TRIM={cfg.trim}!", r"OK\s+TRIM"),
    ]
    if cfg.kp is not None:
        seq.append((f"#KP={int(cfg.kp)}!", r"OK\s+KP"))
    if cfg.km is not None:
        seq.append((f"#KM={int(cfg.km)}!", r"OK\s+KM"))
    seq += [
        (f"#SO={cfg.so}!", r"OK\s+SO"),
        (f"#RAMP={cfg.ramp}!", r"OK\s+RAMP"),
        (f"#MIN={cfg.min_pwm}!", r"OK\s+MIN"),
        (f"#DB={cfg.db:.2f}!", r"OK\s+DB"),
        (f"#HD={cfg.hd:.4f}!", r"OK\s+HD"),
        (f"#HS={cfg.hs:.2f}!", r"OK\s+HS"),
        (f"#HP={cfg.hp:.2f}!", r"OK\s+HP"),
        (f"#SPD={cfg.spd}!", r"OK\s+SPD"),
        ("#RUN!", r"OK\s+RUN"),
        ("#STAT!", r"^STAT\b"),
    ]
    return seq


def build_seq_no_cal(cfg: RunConfig) -> List[Tuple[str, str]]:
    seq = build_seq(cfg)
    return [x for x in seq if x[0] != "#CAL!"]


def score_lines(lines: List[str]) -> Tuple[float, Dict[str, float]]:
    ed_abs: List[float] = []
    e_abs: List[float] = []
    y_abs: List[float] = []
    yr_abs: List[float] = []
    ed_signed: List[float] = []
    e_signed: List[float] = []
    y_signed: List[float] = []
    l_list: List[float] = []
    r_list: List[float] = []
    ok_list: List[float] = []
    fail_list: List[float] = []
    for ln in lines:
        d = parse_hb_line(ln)
        if not d:
            continue
        if "ed" in d:
            ed_abs.append(abs(d["ed"]))
            ed_signed.append(d["ed"])
        if "e" in d:
            e_abs.append(abs(d["e"]))
            e_signed.append(d["e"])
        if "y" in d:
            y_abs.append(abs(d["y"]))
            y_signed.append(d["y"])
        if "yr" in d:
            yr_abs.append(abs(d["yr"]))
        if "L" in d:
            l_list.append(d["L"])
        if "R" in d:
            r_list.append(d["R"])
        if "ok" in d:
            ok_list.append(d["ok"])
        if "fail" in d:
            fail_list.append(d["fail"])

    def mean(xs: List[float]) -> float:
        return sum(xs) / len(xs) if xs else 0.0

    mean_ed = mean(ed_abs)
    mean_e = mean(e_abs)
    mean_y = mean(y_abs)
    mean_yr = mean(yr_abs)
    mean_ed_signed = mean(ed_signed)
    mean_e_signed = mean(e_signed)
    mean_y_signed = mean(y_signed)
    mean_lr = mean([abs(a - b) for a, b in zip(l_list, r_list)]) if l_list and r_list and len(l_list) == len(r_list) else 0.0
    ok0 = ok_list[0] if ok_list else 0.0
    ok1 = ok_list[-1] if ok_list else 0.0
    fail0 = fail_list[0] if fail_list else 0.0
    fail1 = fail_list[-1] if fail_list else 0.0
    ok_delta = ok1 - ok0
    fail_delta = fail1 - fail0

    score = mean_ed * 1.0 + mean_lr * 0.2 + mean_e * 0.02 + mean_y * 0.01 + mean_yr * 0.002 + fail_delta * 0.5
    meta = {
        "mean_ed": mean_ed,
        "mean_ed_signed": mean_ed_signed,
        "mean_lr": mean_lr,
        "mean_e": mean_e,
        "mean_e_signed": mean_e_signed,
        "mean_y": mean_y,
        "mean_y_signed": mean_y_signed,
        "mean_yr": mean_yr,
        "ok_delta": ok_delta,
        "fail_delta": fail_delta,
    }
    return score, meta


def drift_from_meta(meta: Dict[str, float]) -> str:
    es = float(meta.get("mean_e_signed", 0.0))
    if abs(es) >= 5.0:
        return "偏右" if es > 0.0 else "偏左"

    eds = float(meta.get("mean_ed_signed", 0.0))
    if abs(eds) < 30.0:
        return "基本直"
    return "偏右" if eds < 0.0 else "偏左"


def drift_from_es_eds(es: float, eds: float, es_neg_is_left: bool) -> str:
    if abs(es) >= 5.0:
        if es_neg_is_left:
            return "偏左" if es < 0.0 else "偏右"
        return "偏左" if es > 0.0 else "偏右"
    if abs(eds) < 30.0:
        return "基本直"
    return "偏右" if eds < 0.0 else "偏左"


def score_lines_trim_only(lines: List[str]) -> Tuple[float, Dict[str, float]]:
    score, meta = score_lines(lines)
    score2 = meta.get("mean_ed", 0.0) * 1.0 + meta.get("mean_lr", 0.0) * 0.2 + meta.get("fail_delta", 0.0) * 0.5
    return score2, meta


def score_lines_heading(lines: List[str]) -> Tuple[float, Dict[str, float]]:
    score, meta = score_lines(lines)
    score2 = (
        meta.get("mean_e", 0.0) * 0.08
        + meta.get("mean_y", 0.0) * 0.05
        + meta.get("mean_ed", 0.0) * 0.25
        + meta.get("mean_lr", 0.0) * 0.10
        + meta.get("fail_delta", 0.0) * 0.5
    )
    return score2, meta


def to_dict(cfg: RunConfig) -> Dict[str, float]:
    return {
        "spd": cfg.spd,
        "so": cfg.so,
        "ramp": cfg.ramp,
        "min_pwm": cfg.min_pwm,
        "db": cfg.db,
        "hd": cfg.hd,
        "hs": cfg.hs,
        "hp": cfg.hp,
        "at": cfg.at,
        "trim": cfg.trim,
        "seconds": cfg.seconds,
    }


def now() -> float:
    return time.time()


def read_some(ser: serial.Serial) -> bytes:
    return ser.read(4096)


def iter_lines_from_bytes(buf: str):
    while "\n" in buf:
        ln, buf = buf.split("\n", 1)
        ln = ln.strip("\r")
        if ln.strip():
            yield ln
    return


def wait_for_pattern(ser: serial.Serial, pattern: str, timeout_s: float) -> bool:
    deadline = now() + timeout_s
    text_buf = ""
    prog = re.compile(pattern)

    while now() < deadline:
        b = read_some(ser)
        if not b:
            time.sleep(0.01)
            continue
        text_buf += b.decode("utf-8", errors="ignore")
        for ln in iter_lines_from_bytes(text_buf):
            if prog.search(ln):
                return True
        # keep buffer from growing unbounded (keep last 2KB)
        if len(text_buf) > 2048:
            text_buf = text_buf[-2048:]

    return False


def send_wait(ser: serial.Serial, cmd: str, ok_pat: str, timeout_s: float = 1.5, tries: int = 5) -> bool:
    for _ in range(tries):
        ser.write((cmd + "\r\n").encode("ascii", errors="ignore"))
        ser.flush()
        if wait_for_pattern(ser, ok_pat, timeout_s=timeout_s):
            return True
        time.sleep(0.05)
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["once", "trim-scan", "auto-tune", "human-loop", "loop-once"], default="once")
    ap.add_argument("--realtime", action="store_true")
    ap.add_argument("--fixed-trim", action="store_true")
    ap.add_argument("--seconds", type=float, default=RUN_S)
    ap.add_argument("--spd", type=int, default=4)
    ap.add_argument("--so", type=int, default=80)
    ap.add_argument("--ramp", type=int, default=10)
    ap.add_argument("--min", dest="min_pwm", type=int, default=30)
    ap.add_argument("--db", type=float, default=1.0)
    ap.add_argument("--hd", type=float, default=0.0010)
    ap.add_argument("--hs", type=float, default=0.6)
    ap.add_argument("--hp", type=float, default=6.0)
    ap.add_argument("--at", type=int, default=0)
    ap.add_argument("--trim", type=int, default=0)
    ap.add_argument("--port", type=str, default=PORT)
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--round-seconds", type=float, default=2.0)
    ap.add_argument("--rounds", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--loops", type=int, default=50)
    ap.add_argument("--loop-seconds", type=float, default=10.0)
    ap.add_argument("--trim-step", type=int, default=1)
    ap.add_argument("--es-neg-left", type=int, default=1)
    ap.add_argument("--kp", type=int, default=-1)
    ap.add_argument("--km", type=int, default=-1)
    ap.add_argument("--heading-off", action="store_true")
    args = ap.parse_args()

    port_tag = re.sub(r"[^A-Za-z0-9]+", "", str(args.port)) or "COM"

    os.makedirs(DATA_DIR, exist_ok=True)

    cfg = RunConfig(
        spd=args.spd,
        so=clamp_int(args.so, 0, 100),
        ramp=clamp_int(args.ramp, 1, 20),
        min_pwm=clamp_int(args.min_pwm, 0, 100),
        db=max(0.0, min(5.0, args.db)),
        hd=max(0.0, min(0.01, args.hd)),
        hs=max(0.0, min(5.0, args.hs)),
        hp=max(0.0, min(20.0, args.hp)),
        at=1 if args.at else 0,
        trim=clamp_int(args.trim, -20, 20),
        seconds=max(0.5, args.seconds),
        kp=None if args.kp < 0 else clamp_int(args.kp, 0, 100),
        km=None if args.km < 0 else clamp_int(args.km, 0, 2000),
    )

    try:
        ser = serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1, timeout=0.05)
    except (SerialException, PermissionError) as e:
        print(f"OPEN_SERIAL_FAIL: {e}")
        print("HINT: 请确认COM口未被VOFA+等上位机占用（同一时刻只能有一个程序打开同一个串口）。")
        return 2

    try:
        ser.reset_input_buffer()
    except (SerialException, PermissionError) as e:
        print(f"RESET_INPUT_FAIL: {e}")
        try:
            ser.close()
        except Exception:
            pass
        return 3

    def run_once(run_cfg: RunConfig, log_name: str) -> Tuple[bool, Dict[str, bool], List[str]]:
        seq = build_seq(run_cfg)
        acks: Dict[str, bool] = {}
        ok = True
        try:
            for cmd, pat in seq:
                a = send_wait(ser, cmd, pat)
                acks[cmd] = a
                if not a:
                    ok = False
        except (SerialException, PermissionError) as e:
            print(f"SEND_SEQ_FAIL: {e}")
            if isinstance(e, PermissionError):
                print("HINT: 串口被占用/拒绝访问。请关闭VOFA+或其它占用该COM口的程序后重试。")
            ok = False

        lines: List[str] = []
        aborted_reason = ""
        try:
            with open(log_name, "wb") as f:
                lines = drain_lines(ser, run_cfg.seconds, args.realtime, f)
        except KeyboardInterrupt:
            aborted_reason = "KEYBOARD_INTERRUPT"
        except (SerialException, PermissionError) as e:
            aborted_reason = f"READ_FAIL: {e}"
            if isinstance(e, PermissionError):
                print("HINT: 读串口时被拒绝访问，通常是另一个程序抢占了COM口。")

        try:
            ser.write(b"#STOP!\r\n")
            ser.flush()
            time.sleep(0.15)
        except (SerialException, PermissionError) as e:
            if not aborted_reason:
                aborted_reason = f"STOP_FAIL: {e}"

        if aborted_reason:
            print("ABORTED=" + aborted_reason)
            ok = False
        return ok, acks, lines

    def run_round(run_cfg: RunConfig, log_name: str, include_cal: bool) -> Tuple[bool, Dict[str, bool], List[str]]:
        seq = build_seq(run_cfg) if include_cal else build_seq_no_cal(run_cfg)
        acks: Dict[str, bool] = {}
        ok = True
        try:
            for cmd, pat in seq:
                a = send_wait(ser, cmd, pat)
                acks[cmd] = a
                if not a:
                    ok = False
        except (SerialException, PermissionError) as e:
            print(f"SEND_SEQ_FAIL: {e}")
            ok = False

        lines: List[str] = []
        aborted_reason = ""
        try:
            with open(log_name, "wb") as f:
                lines = drain_lines(ser, run_cfg.seconds, args.realtime, f)
        except KeyboardInterrupt:
            aborted_reason = "KEYBOARD_INTERRUPT"
        except (SerialException, PermissionError) as e:
            aborted_reason = f"READ_FAIL: {e}"

        try:
            ser.write(b"#STOP!\r\n")
            ser.flush()
            time.sleep(0.15)
        except (SerialException, PermissionError) as e:
            if not aborted_reason:
                aborted_reason = f"STOP_FAIL: {e}"

        if aborted_reason:
            print("ABORTED=" + aborted_reason)
            ok = False
        return ok, acks, lines

    if args.mode == "once":
        log_name = os.path.join(DATA_DIR, f"{port_tag}_{time.strftime('%Y%m%d_%H%M%S')}_once.txt")
        ok, acks, lines = run_once(cfg, log_name)
        score, meta = score_lines(lines)
        print("---")
        print("LOG_PATH=" + os.path.abspath(log_name))
        print("ACKS=" + str(acks))
        print("SCORE=" + f"{score:.3f}")
        print("META=" + str(meta))
        try:
            ser.close()
        except Exception:
            pass
        return 0 if ok else 5

    if args.mode == "auto-tune":
        random.seed(args.seed)
        round_s = max(1.5, min(4.0, args.round_seconds))
        total_rounds = clamp_int(args.rounds, 5, 200)

        base = RunConfig(**{**cfg.__dict__, "seconds": round_s})
        ts = time.strftime('%Y%m%d_%H%M%S')
        best_path = os.path.abspath(os.path.join(DATA_DIR, f"best_params_{port_tag}_{ts}.json"))

        print("AUTO_TUNE_LOG_DIR=" + os.path.abspath(DATA_DIR))
        print("BEST_JSON=" + best_path)

        best_cfg = base
        best_score = 1e18
        best_meta: Dict[str, float] = {}

        if total_rounds <= 6:
            stage1_rounds = total_rounds
            stage2_rounds = 0
        else:
            stage1_rounds = max(6, min(22, total_rounds // 2))
            if stage1_rounds > total_rounds:
                stage1_rounds = total_rounds
            stage2_rounds = total_rounds - stage1_rounds
            if stage2_rounds < 0:
                stage2_rounds = 0

        print(f"STAGE1_TRIM rounds={stage1_rounds}")
        center_trim = int(base.trim)
        for i in range(stage1_rounds):
            # 固定TRIM：用于你手动逐级验证 TRIM=1/2/3... 的肉眼偏航
            if args.fixed_trim:
                t = int(base.trim)
            else:
                # 自适应：优先跑当前center_trim，然后按ed符号向能纠偏的方向走1格
                t = center_trim
            t = clamp_int(t, -20, 20)
            run_cfg = RunConfig(**{**base.__dict__})
            run_cfg.trim = t
            run_cfg.hp = 0.0
            run_cfg.db = 5.0
            run_cfg.hd = 0.0
            run_cfg.hs = 0.0
            run_cfg.seconds = round_s
            log_name = os.path.join(DATA_DIR, f"{port_tag}_{ts}_auto_s1_{i:03d}_trim_{t}.txt")
            ok, acks, lines = run_round(run_cfg, log_name, include_cal=True)
            score, meta = score_lines_trim_only(lines)
            drift = drift_from_meta(meta)
            es = float(meta.get("mean_e_signed", 0.0))
            eds = float(meta.get("mean_ed_signed", 0.0))
            print(f"S1 i={i} TRIM={t} OK={ok} DRIFT={drift} es={es:.3f} eds={eds:.3f} SCORE={score:.3f} META={meta} LOG={os.path.abspath(log_name)}")
            if ok and score < best_score:
                best_score = score
                best_cfg = run_cfg
                best_meta = meta

            # 根据ed符号调trim：trim增大 -> 左PWM更大、右PWM更小
            # 根据你的车反馈：偏右时正TRIM会更严重，因此偏右应减小TRIM，偏左应增大TRIM
            if not args.fixed_trim:
                drift = drift_from_meta(meta)
                if drift == "偏右":
                    center_trim -= 1
                elif drift == "偏左":
                    center_trim += 1
                center_trim = clamp_int(center_trim, -5, 5)

        fixed_trim = int(best_cfg.trim)
        print(f"STAGE1_BEST_TRIM={fixed_trim} SCORE={best_score:.3f} META={best_meta}")

        best2_cfg = RunConfig(**{**base.__dict__})
        best2_cfg.trim = fixed_trim
        best2_score = 1e18
        best2_meta: Dict[str, float] = {}

        if stage2_rounds > 0:
            print(f"STAGE2_HEADING rounds={stage2_rounds}")
            for i in range(stage2_rounds):
                run_cfg = RunConfig(**{**base.__dict__})
                run_cfg.trim = fixed_trim
                run_cfg.db = clamp_float(random.uniform(0.6, 2.5), 0.0, 5.0)
                run_cfg.hp = clamp_float(random.uniform(2.0, 10.0), 0.0, 20.0)
                run_cfg.hd = clamp_float(random.uniform(0.0000, 0.0030), 0.0, 0.01)
                run_cfg.hs = clamp_float(random.uniform(0.3, 1.2), 0.0, 5.0)
                run_cfg.seconds = round_s
                log_name = os.path.join(DATA_DIR, f"{port_tag}_{ts}_auto_s2_{i:03d}.txt")
                ok, acks, lines = run_round(run_cfg, log_name, include_cal=True)
                score, meta = score_lines_heading(lines)
                drift = drift_from_meta(meta)
                print(f"S2 i={i} OK={ok} DRIFT={drift} SCORE={score:.3f} CFG={to_dict(run_cfg)} META={meta} LOG={os.path.abspath(log_name)}")
                if ok and score < best2_score:
                    best2_score = score
                    best2_cfg = run_cfg
                    best2_meta = meta

        out = {
            "best_trim_stage": {"cfg": to_dict(best_cfg), "score": best_score, "meta": best_meta},
            "best_heading_stage": {"cfg": to_dict(best2_cfg), "score": best2_score, "meta": best2_meta},
        }
        with open(best_path, "w", encoding="utf-8") as f:
            json.dump(out, f, ensure_ascii=False, indent=2)

        print("---")
        print("BEST_TRIM_CFG=" + str(out["best_trim_stage"]))
        print("BEST_HEADING_CFG=" + str(out["best_heading_stage"]))
        try:
            ser.close()
        except Exception:
            pass
        return 0

    if args.mode == "human-loop":
        print("ERR: human-loop需要交互输入，不适合当前自动执行方式。请改用 --mode loop-once 逐轮运行。")
        try:
            ser.close()
        except Exception:
            pass
        return 6

    if args.mode == "loop-once":
        loop_s = max(2.0, min(20.0, float(args.loop_seconds)))
        step = clamp_int(int(args.trim_step), 1, 10)
        es_neg_is_left = (int(args.es_neg_left) != 0)

        run_cfg = RunConfig(**{**cfg.__dict__})
        run_cfg.seconds = loop_s

        if args.heading_off:
            run_cfg.hp = 0.0
            run_cfg.hs = 0.0
            run_cfg.hd = 0.0
            run_cfg.db = 5.0

        ts = time.strftime('%Y%m%d_%H%M%S')
        log_name = os.path.join(DATA_DIR, f"{port_tag}_{ts}_loop_trim_{int(run_cfg.trim)}.txt")
        ok, acks, lines = run_round(run_cfg, log_name, include_cal=True)
        missing = [k for k, v in acks.items() if not v]
        score, meta = score_lines(lines)
        diag = summarize_run_kv(lines, stall_pwm_th=12)
        es = float(meta.get("mean_e_signed", 0.0))
        eds = float(meta.get("mean_ed_signed", 0.0))
        pred = drift_from_es_eds(es, eds, es_neg_is_left=es_neg_is_left)

        if pred == "偏左":
            suggest = clamp_int(int(run_cfg.trim) + step, -20, 20)
        elif pred == "偏右":
            suggest = clamp_int(int(run_cfg.trim) - step, -20, 20)
        else:
            suggest = int(run_cfg.trim)

        ok2 = bool(ok) and (len(missing) == 0)
        print(f"LOOP OK={ok2} TRIM={int(run_cfg.trim)} PRED={pred} es={es:.3f} eds={eds:.3f} SCORE={score:.3f} SUGGEST_TRIM={suggest} MISSING_ACK={missing} DIAG={diag} META={meta} LOG={os.path.abspath(log_name)}")
        try:
            ser.close()
        except Exception:
            pass
        return 0

    results: List[Tuple[int, float, Dict[str, float], str]] = []
    base = cfg
    for t in range(-5, 6):
        run_cfg = RunConfig(**{**base.__dict__, "trim": t, "seconds": max(1.8, min(3.0, base.seconds))})
        log_name = os.path.join(DATA_DIR, f"{port_tag}_{time.strftime('%Y%m%d_%H%M%S')}_trim_{t}.txt")
        ok, acks, lines = run_once(run_cfg, log_name)
        score, meta = score_lines(lines)
        results.append((t, score, meta, log_name))
        print(f"TRIM={t} OK={ok} SCORE={score:.3f} META={meta} LOG={os.path.abspath(log_name)}")
        time.sleep(0.4)

    best = min(results, key=lambda x: x[1]) if results else None
    print("---")
    if best is not None:
        print(f"BEST_TRIM={best[0]} SCORE={best[1]:.3f} META={best[2]} LOG={os.path.abspath(best[3])}")
    try:
        ser.close()
    except Exception:
        pass
    return 0


    


if __name__ == "__main__":
    raise SystemExit(main())
