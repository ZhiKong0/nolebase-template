"""
渐进式 PID 自动调参系统

特性：
1. 渐进式实验时间：从短时间开始，逐步增加
2. 多种优化算法支持：坐标下降、Twiddle、Nelder-Mead、贝叶斯优化
3. 智能评分函数：评估直线行驶效果
4. 自动安全防护：防止小车跑太远

使用方法：
    python auto_tune_pid.py --port COM13 --algorithm twiddle --max-ms 5000
"""

import os
import sys
import time
import math
import json
import random
import argparse
import subprocess
from dataclasses import dataclass, asdict
from typing import Dict, List, Tuple, Optional, Callable, Any
from pathlib import Path

# 尝试导入优化库
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("[WARN] numpy 未安装，将使用纯 Python 实现")

try:
    from scipy.optimize import minimize
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False
    print("[WARN] scipy 未安装，Nelder-Mead 将使用纯 Python 实现")


@dataclass
class PIDParams:
    """PID 参数结构"""
    hp: float = 2.0      # 航向 P
    hd: float = 0.005    # 航向 D
    hi: float = 0.0      # 航向 I
    kpp: float = 2.0     # 速度 P
    kpi: float = 0.2     # 速度 I
    kpd: float = 0.0     # 速度 D
    trim: float = 0.0    # 左右轮平衡
    
    def to_dict(self) -> Dict[str, float]:
        return asdict(self)
    
    def to_list(self) -> List[float]:
        return [self.hp, self.hd, self.hi, self.kpp, self.kpi, self.kpd, self.trim]
    
    @classmethod
    def from_list(cls, values: List[float]) -> "PIDParams":
        return cls(
            hp=values[0], hd=values[1], hi=values[2],
            kpp=values[3], kpi=values[4], kpd=values[5],
            trim=values[6] if len(values) > 6 else 0.0
        )


@dataclass
class TuneConfig:
    """调参配置"""
    # 串口配置
    port: str = "COM13"
    baud: int = 115200
    
    # 实验配置
    start_ms: int = 500      # 起始实验时长
    max_ms: int = 5000       # 最大实验时长
    ms_step: int = 500       # 每次增加的时长
    
    # 实验编号
    start_exp_id: int = 1    # 起始实验编号 (默认从 exp01 开始)
    pause_every_n: int = 2   # 每 N 次实验暂停，让用户放回小车
    batch_limit: int = 1     # 每轮跑这么多个实验后退出等待指令
    
    # 小车速度设置
    target_speed: float = 20.0   # 目标速度 TS
    
    # 额外限幅（强制降速，避免速度环把PWM顶满导致看起来“全速跑”）
    pwm_max: int = 20
    diff_max: int = 10
    
    # 优化算法
    algorithm: str = "twiddle"  # twiddle, nelder_mead, coordinate_descent, bayesian, zn

    # Ziegler–Nichols (ZN) 参数（仅 algorithm=zn 使用）
    zn_ms: int = 8000                 # 每轮实验时长（默认 8 秒）
    zn_transient_ms: int = 1500       # 忽略前段瞬态
    zn_min_cycles: int = 4            # 至少检测到这么多周期才认为可用
    zn_amp_cv_max: float = 0.35       # 振幅变异系数阈值（越小越接近“持续振荡”）
    zn_hp_step_min: float = 0.05      # hp 调整最小步长（用于二分收敛）
    zn_hp_init: Optional[float] = None
    zn_hp_max_trials: int = 25
    
    # 参数边界
    hp_range: Tuple[float, float] = (0.5, 8.0)
    hd_range: Tuple[float, float] = (0.001, 0.02)
    hi_range: Tuple[float, float] = (0.0, 1.0)
    kpp_range: Tuple[float, float] = (0.5, 5.0)
    kpi_range: Tuple[float, float] = (0.0, 1.0)
    kpd_range: Tuple[float, float] = (0.0, 0.5)
    trim_range: Tuple[float, float] = (-5.0, 5.0)
    
    # 算法参数
    max_iterations: int = 100    # 最大迭代次数（全局）
    tolerance: float = 0.001     # 收敛容差
    
    # Twiddle 特有参数
    twiddle_init_dp: float = 0.1  # 初始步长比例
    twiddle_param_order: Optional[List[int]] = None
    
    # Bayesian 优化参数
    bayesian_init_points: int = 5   # 初始随机点
    bayesian_n_iter: int = 20       # 优化迭代次数
    
    # 评分权重
    w_yaw_error: float = 1.0     # 航向误差权重
    w_straight: float = 2.0      # 直线度权重
    w_stability: float = 1.0     # 稳定性权重
    w_distance: float = 0.5      # 行驶距离权重
    
    # 安全限制
    max_yaw_drift: float = 45.0   # 最大允许航向偏移（度）
    min_encoder_counts: int = 50  # 最小编码器计数（确保车在动）


class ExperimentRunner:
    """实验执行器 - 封装 exp_runner.py 的调用"""
    
    def __init__(self, config: TuneConfig):
        self.config = config
        self.exp_counter = config.start_exp_id  # 从配置的 start_exp_id 开始（默认 exp01）
        self.project_dir = Path(__file__).parent
        self.data_dir = self.project_dir / "000Data"
        
    def run(self, params: PIDParams, duration_ms: int, raw_mode: bool = False) -> Tuple[str, Dict[str, Any]]:
        """
        运行一次实验
        
        Args:
            params: PID 参数
            duration_ms: 实验时长（毫秒）
            raw_mode: 是否使用 RAW 模式（无 PID）
        
        Returns:
            (raw_file_path, metrics_dict)
        """
        exp_id = self.exp_counter
        
        # 构建命令
        cmd = [
            "python", "exp_runner.py",
            "--port", self.config.port,
            "--baud", str(self.config.baud),
            "--id", str(exp_id),
            "--ms", str(duration_ms),
            "--pwm-max", str(int(self.config.pwm_max)),
            "--diff-max", str(int(self.config.diff_max)),
            "--hp", str(params.hp),
            "--hd", str(params.hd),
            "--hi", str(params.hi),
            "--kpp", str(params.kpp),
            "--kpi", str(params.kpi),
            "--kpd", str(params.kpd),
            "--trim", str(params.trim),
            "--ts", str(self.config.target_speed),
            "--no-cal",
            "--no-dump",  # 使用 no-dump 模式加快实验
            "--quick",
        ]
        
        if raw_mode:
            cmd.extend(["--raw", "18"])
        
        # 执行实验
        try:
            result = subprocess.run(
                cmd,
                cwd=str(self.project_dir),
                capture_output=True,
                text=True,
                timeout=(duration_ms / 1000.0 + 25)  # 加上一些超时缓冲（避免误判）
            )
            
            if result.returncode != 0:
                print(f"[ERROR] 实验 {exp_id} 失败: {result.stderr}")
                return None, {}
            
        except subprocess.TimeoutExpired:
            print(f"[ERROR] 实验 {exp_id} 超时")
            return None, {}
        except Exception as e:
            print(f"[ERROR] 实验 {exp_id} 异常: {e}")
            return None, {}
        
        # 查找最新的 raw 文件
        raw_file = self._find_latest_raw(exp_id)
        if not raw_file:
            print(f"[ERROR] 无法找到实验 {exp_id} 的 raw 文件")
            return None, {}

        # 仅在实验成功并找到raw后，才消耗exp_id
        self.exp_counter += 1
        
        # 分析结果
        metrics = self._analyze_raw(raw_file, duration_ms)
        yaw_trend = self._extract_yaw_trend(raw_file)
        metrics.update(yaw_trend)
        metrics["exp_id"] = exp_id
        metrics["duration_ms"] = duration_ms
        metrics["params"] = params.to_dict()
        
        return str(raw_file), metrics

    def _extract_yaw_trend(self, raw_file: Path) -> Dict[str, Any]:
        """从 raw 文本中提取 run=1 段的 yaw 起点/终点/漂移量"""
        y_first = None
        y_last = None
        try:
            with open(raw_file, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    if " run=1 " not in line:
                        continue
                    if " y=" not in line:
                        continue
                    # 解析形如 y=-39.4
                    try:
                        # 找到 y= 后到空格前
                        y_part = line.split(" y=", 1)[1].split()[0]
                        y_val = float(y_part)
                    except Exception:
                        continue

                    if y_first is None:
                        y_first = y_val
                    y_last = y_val
        except Exception:
            pass

        if y_first is None or y_last is None:
            return {
                "imu_yaw_first": None,
                "imu_yaw_last": None,
                "imu_yaw_delta": None,
                "imu_yaw_direction": "unknown",
            }

        dy = y_last - y_first
        direction = "unknown"
        if abs(dy) < 0.5:
            direction = "stable"
        elif dy > 0:
            direction = "ccw"
        else:
            direction = "cw"

        return {
            "imu_yaw_first": y_first,
            "imu_yaw_last": y_last,
            "imu_yaw_delta": dy,
            "imu_yaw_direction": direction,
        }
    
    def _find_latest_raw(self, exp_id: int) -> Optional[Path]:
        """查找指定实验的最新 raw 文件"""
        pattern = f"exp{exp_id:02d}_*_raw.txt"
        files = list(self.data_dir.glob(pattern))
        if not files:
            return None
        return max(files, key=lambda p: p.stat().st_mtime)
    
    def _analyze_raw(self, raw_file: Path, duration_ms: int) -> Dict[str, Any]:
        """分析 raw 文件获取指标"""
        # 调用 analyze_last_dump.py
        cmd = [
            "python", "analyze_last_dump.py",
            "--raw", str(raw_file),
            "--max-run-s", str(duration_ms / 1000.0 * 0.9),  # 分析 90% 的数据
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=str(self.project_dir),
                capture_output=True,
                text=True,
                timeout=10
            )
            output = result.stdout
        except Exception as e:
            print(f"[WARN] 分析失败: {e}")
            output = ""
        
        # 解析输出
        metrics = self._parse_analysis_output(output)
        return metrics

    def _parse_analysis_output(self, output: str) -> Dict[str, Any]:
        """解析 analyze_last_dump.py 的输出"""
        metrics = {
            "encL_mean": 0.0,
            "encR_mean": 0.0,
            "enc_diff": 0.0,
            "ed_mean": 0.0,
            "ed_std": 0.0,
            "dropout_ratio": 0.0,
            "yaw_mean": 0.0,
            "yaw_std": 0.0,
            "yaw_err_rms": 0.0,
            "pwm_L_mean": 0.0,
            "pwm_R_mean": 0.0,
            "heading_corr_mean": 0.0,
        }

        for line in output.split("\n"):
            # encL(min,mean,max)
            if "encL(min,mean,max):" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["encL_mean"] = float(parts[1])
                except Exception:
                    pass

            # encR(min,mean,max)
            elif "encR(min,mean,max):" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["encR_mean"] = float(parts[1])
                except Exception:
                    pass

            # ed(min,mean,max)
            elif "ed(min,mean,max):" in line and "mean_signed" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["ed_mean"] = float(parts[1])
                except Exception:
                    pass

            # dropout_ratio
            elif "dropout_ratio(el==0 xor er==0" in line:
                try:
                    metrics["dropout_ratio"] = float(line.split(":")[1].strip())
                except Exception:
                    pass

            # yaw
            elif "yaw y(min,mean,max):" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["yaw_mean"] = float(parts[1])
                except Exception:
                    pass

            # yaw_err rms
            elif "yaw_err e(min,mean,max):" in line and "rms:" in line:
                try:
                    rms_part = line.split("rms:")[1].split()[0]
                    metrics["yaw_err_rms"] = float(rms_part)
                except Exception:
                    pass

            # PWM
            elif "PWM_L(min,mean,max):" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["pwm_L_mean"] = float(parts[1])
                except Exception:
                    pass
            elif "PWM_R(min,mean,max):" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["pwm_R_mean"] = float(parts[1])
                except Exception:
                    pass

            # heading_corr
            elif "heading_corr c(min,mean,max):" in line:
                try:
                    parts = line.split(":")[1].strip().strip("()").split(",")
                    metrics["heading_corr_mean"] = float(parts[1])
                except Exception:
                    pass

        # 计算编码器差值
        metrics["enc_diff"] = abs(metrics["encL_mean"] - metrics["encR_mean"])
        return metrics

    def parse_s_timeseries(self, raw_file: Path) -> Dict[str, List[float]]:
        """解析 raw 文本中的 S 行时序数据。

        期望固件输出格式（逗号分隔）：
        S t_ms,run,ts,yawErr,ed,headingCorr,leftPWM,rightPWM,yawRate,trim,pmax,dmax,ax,ay,az,gx,gy,gz
        """
        ts: Dict[str, List[float]] = {
            "t_ms": [],
            "run": [],
            "ts": [],
            "yaw_err": [],
            "ed": [],
            "heading_corr": [],
            "pwm_l": [],
            "pwm_r": [],
            "yaw_rate": [],
            "trim": [],
            "pmax": [],
            "dmax": [],
            "ax": [],
            "ay": [],
            "az": [],
            "gx": [],
            "gy": [],
            "gz": [],
        }

        try:
            with open(raw_file, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    if not line.startswith("S "):
                        continue
                    s = line[2:].strip()
                    parts = s.split(",")
                    if len(parts) < 18:
                        continue
                    try:
                        vals = [float(x) for x in parts[:18]]
                    except Exception:
                        continue
                    (t_ms, run, target_speed, yaw_err, ed, hc, pwm_l, pwm_r, yaw_rate, trim,
                     pmax, dmax, ax, ay, az, gx, gy, gz) = vals
                    ts["t_ms"].append(t_ms)
                    ts["run"].append(run)
                    ts["ts"].append(target_speed)
                    ts["yaw_err"].append(yaw_err)
                    ts["ed"].append(ed)
                    ts["heading_corr"].append(hc)
                    ts["pwm_l"].append(pwm_l)
                    ts["pwm_r"].append(pwm_r)
                    ts["yaw_rate"].append(yaw_rate)
                    ts["trim"].append(trim)
                    ts["pmax"].append(pmax)
                    ts["dmax"].append(dmax)
                    ts["ax"].append(ax)
                    ts["ay"].append(ay)
                    ts["az"].append(az)
                    ts["gx"].append(gx)
                    ts["gy"].append(gy)
                    ts["gz"].append(gz)
        except Exception:
            pass

        return ts


def _zn_find_peaks(y: List[float]) -> List[int]:
    """非常轻量的峰值检测：返回局部极大值索引。"""
    idx: List[int] = []
    if len(y) < 3:
        return idx
    for i in range(1, len(y) - 1):
        if y[i] > y[i - 1] and y[i] >= y[i + 1]:
            idx.append(i)
    return idx


def zn_estimate_ku_pu(ts: Dict[str, List[float]], transient_ms: int, min_cycles: int, amp_cv_max: float) -> Dict[str, Any]:
    """基于 yaw_err 时序估计是否出现持续振荡，并估计 Pu。

    返回：
    - sustained: bool
    - pu_s: Optional[float]
    - amp_cv: Optional[float]
    - n_cycles: int
    """
    t = ts.get("t_ms", [])
    e = ts.get("yaw_err", [])
    if len(t) < 10 or len(e) != len(t):
        return {"sustained": False, "pu_s": None, "amp_cv": None, "n_cycles": 0}

    # 仅分析 run=1 段，并忽略瞬态
    run = ts.get("run", [1.0] * len(t))
    t2: List[float] = []
    e2: List[float] = []
    for ti, ei, ri in zip(t, e, run):
        if int(ri) != 1:
            continue
        if ti < transient_ms:
            continue
        t2.append(ti)
        e2.append(ei)

    if len(t2) < 10:
        return {"sustained": False, "pu_s": None, "amp_cv": None, "n_cycles": 0}

    peaks = _zn_find_peaks(e2)
    if len(peaks) < (min_cycles + 1):
        return {"sustained": False, "pu_s": None, "amp_cv": None, "n_cycles": max(0, len(peaks) - 1)}

    # 周期估计：相邻峰值时间差的均值
    periods_ms: List[float] = []
    amps: List[float] = []
    for a, b in zip(peaks[:-1], peaks[1:]):
        periods_ms.append(t2[b] - t2[a])
        amps.append(abs(e2[a]))

    if not periods_ms:
        return {"sustained": False, "pu_s": None, "amp_cv": None, "n_cycles": 0}

    pu_ms = sum(periods_ms) / len(periods_ms)
    amp_mean = sum(amps) / max(1, len(amps))
    if amp_mean <= 1e-6:
        return {"sustained": False, "pu_s": None, "amp_cv": None, "n_cycles": len(periods_ms)}

    amp_var = sum((a - amp_mean) ** 2 for a in amps) / max(1, len(amps))
    amp_std = math.sqrt(amp_var)
    amp_cv = amp_std / amp_mean

    sustained = (len(periods_ms) >= min_cycles) and (amp_cv <= amp_cv_max)
    return {
        "sustained": sustained,
        "pu_s": float(pu_ms) / 1000.0,
        "amp_cv": float(amp_cv),
        "n_cycles": int(len(periods_ms)),
    }


class ZNOptimizer:
    """Ziegler–Nichols 自动调参。

    实现方式：
    1) 固定 hi=0, hd=0，仅扫描 hp。
    2) 找到能产生“持续振荡”的最小 hp，视为 Ku。
    3) 由振荡周期估计 Pu。
    4) 按经典 ZN PID 公式生成 (hp, hi, hd)。
    """

    def __init__(self, config: TuneConfig, param_bounds: List[Tuple[float, float]]):
        self.config = config
        self.bounds = param_bounds
        self.hp_low = float(config.hp_range[0])
        self.hp_high = float(config.hp_range[1])
        self.hp = float(config.zn_hp_init) if config.zn_hp_init is not None else float((self.hp_low + self.hp_high) / 2.0)
        self.last_ts: Optional[Dict[str, List[float]]] = None
        self.last_zn: Optional[Dict[str, Any]] = None
        self.trials = 0

        # 保持速度环和 trim 不动（使用范围中点作为保守默认）
        self.kpp = float((config.kpp_range[0] + config.kpp_range[1]) / 2.0)
        self.kpi = float((config.kpi_range[0] + config.kpi_range[1]) / 2.0)
        self.kpd = float((config.kpd_range[0] + config.kpd_range[1]) / 2.0)
        self.trim = 0.0

        self.done = False
        self.best_pid: Optional[List[float]] = None

    def get_params(self) -> Optional[List[float]]:
        if self.done:
            return None
        # 扫描阶段：hi=0 hd=0
        return [self.hp, 0.0, 0.0, self.kpp, self.kpi, self.kpd, self.trim]

    def update_with_raw(self, hp: float, raw_file: str, runner: ExperimentRunner) -> Optional[List[float]]:
        self.trials += 1
        ts = runner.parse_s_timeseries(Path(raw_file))
        zn = zn_estimate_ku_pu(
            ts,
            transient_ms=int(self.config.zn_transient_ms),
            min_cycles=int(self.config.zn_min_cycles),
            amp_cv_max=float(self.config.zn_amp_cv_max),
        )
        self.last_ts = ts
        self.last_zn = zn

        sustained = bool(zn.get("sustained", False))
        pu_s = zn.get("pu_s", None)

        # 二分：找到最小的 sustained hp
        if sustained:
            self.hp_high = min(self.hp_high, hp)
        else:
            self.hp_low = max(self.hp_low, hp)

        # 收敛条件
        if (self.hp_high - self.hp_low) <= float(self.config.zn_hp_step_min) or self.trials >= int(self.config.zn_hp_max_trials):
            if pu_s is None or pu_s <= 0:
                # 无法估计周期，结束
                self.done = True
                return None

            ku = float(self.hp_high if sustained else self.hp_low)
            pu = float(pu_s)

            # 经典 ZN PID
            kp = 0.6 * ku
            ki = 1.2 * ku / pu
            kd = 0.075 * ku * pu

            # 映射到固件：corr = hp*e + hi*∫e dt - hd*yawRate
            # 近似 d(e)/dt ≈ -yawRate，因此 hd ~= kd
            hp_new = max(self.config.hp_range[0], min(self.config.hp_range[1], kp))
            hi_new = max(self.config.hi_range[0], min(self.config.hi_range[1], ki))
            hd_new = max(self.config.hd_range[0], min(self.config.hd_range[1], kd))

            self.best_pid = [hp_new, hd_new, hi_new, self.kpp, self.kpi, self.kpd, self.trim]
            self.done = True
            return self.best_pid.copy()

        # 下一次 hp
        self.hp = float((self.hp_low + self.hp_high) / 2.0)
        return [self.hp, 0.0, 0.0, self.kpp, self.kpi, self.kpd, self.trim]


class ScoreFunction:
    """评分函数 - 评估直线行驶效果"""
    
    def __init__(self, config: TuneConfig):
        self.config = config
    
    def calculate(self, metrics: Dict[str, Any]) -> float:
        """
        计算综合评分（越低越好）
        
        评分组成：
        1. 航向误差：yaw_err_rms
        2. 直线度：encoder 差值 / 平均值
        3. 稳定性：ed_std（编码器差值的标准差）
        4. 行驶距离：鼓励走得更远
        5. 安全检查：惩罚大偏移或掉线
        """
        score = 0.0
        
        # 1. 航向误差（越小越好）
        yaw_err = metrics.get("yaw_err_rms", 0.0)
        score += self.config.w_yaw_error * yaw_err
        
        # 2. 直线度：编码器差值比例
        encL = metrics.get("encL_mean", 0.0)
        encR = metrics.get("encR_mean", 0.0)
        enc_mean = (abs(encL) + abs(encR)) / 2.0
        enc_diff = metrics.get("enc_diff", 0.0)
        
        if enc_mean > 0:
            straightness = enc_diff / enc_mean  # 越接近 0 越直
            score += self.config.w_straight * straightness
        
        # 3. 稳定性（暂时用 ed 的变异系数近似）
        ed_mean = abs(metrics.get("ed_mean", 0.0))
        if enc_mean > 0:
            stability = ed_mean / enc_mean
            score += self.config.w_stability * stability
        
        # 4. 行驶距离（鼓励走得更远）
        # 负奖励：走得越远，分数越低（越好）
        if enc_mean > 0:
            distance_bonus = -self.config.w_distance * math.log1p(enc_mean / 100.0)
            score += distance_bonus
        
        # 5. 安全检查
        # 检查航向偏移
        yaw_abs = abs(metrics.get("yaw_mean", 0.0))
        if yaw_abs > self.config.max_yaw_drift:
            score += 1000.0  # 大惩罚
            print(f"[SAFETY] 航向偏移 {yaw_abs:.1f}° 超过限制 {self.config.max_yaw_drift}°")
        
        # 检查编码器掉线
        dropout = metrics.get("dropout_ratio", 0.0)
        if dropout > 0.05:  # 5% 掉线率
            score += 500.0
            print(f"[SAFETY] 掉线率 {dropout:.3f} 过高")
        
        # 检查车是否在动
        if enc_mean < self.config.min_encoder_counts:
            score += 1000.0
            print(f"[SAFETY] 编码器计数 {enc_mean:.1f} 过低，车可能未动")
        
        return score


class TwiddleOptimizer:
    """Twiddle 算法优化器"""
    
    def __init__(self, config: TuneConfig, param_bounds: List[Tuple[float, float]]):
        self.config = config
        self.bounds = param_bounds
        self.n_params = len(param_bounds)
        
        # 初始化参数（范围中点）
        self.params = [(low + high) / 2.0 for low, high in param_bounds]
        
        # 初始化步长（范围的 10%）
        self.dp = [(high - low) * config.twiddle_init_dp for low, high in param_bounds]
        
        self.best_score = float('inf')
        self.best_params = self.params.copy()
        self.iteration = 0
        self.param_order = config.twiddle_param_order
        if self.param_order is None:
            self.param_order = list(range(self.n_params))
    
    def get_params(self) -> List[float]:
        """获取当前参数"""
        # 保障任何情况下都不会返回越界参数
        self.params = self._clip_params(self.params)
        return self.params.copy()
    
    def update(self, score: float) -> Optional[List[float]]:
        """
        根据评分更新参数
        
        Returns:
            下一组参数，或 None 如果收敛
        """
        self.iteration += 1
        
        if self.iteration > self.config.max_iterations:
            print(f"[Twiddle] 达到最大迭代次数 {self.config.max_iterations}")
            return None
        
        # 尝试增加当前参数（支持自定义调参顺序）
        order = self.param_order or list(range(self.n_params))
        param_idx = order[(self.iteration - 1) % len(order)]
        
        if score < self.best_score:
            # 找到更好的解
            self.best_score = score
            self.best_params = self.params.copy()
            self.dp[param_idx] *= 1.1  # 增加该方向的步长
            print(f"[Twiddle] 迭代 {self.iteration}: 新最佳分数 {score:.4f}, 参数 {param_idx} 成功")
        else:
            # 尝试减少该参数
            self.params[param_idx] -= 2 * self.dp[param_idx]
            # 注意：这里需要重新评估，所以返回新参数
            self.params = self._clip_params(self.params)
            return self.params.copy()
        
        # 移动到下一个参数
        param_idx = order[self.iteration % len(order)]
        self.params[param_idx] += self.dp[param_idx]
        self.params = self._clip_params(self.params)
        
        # 检查收敛
        if sum(self.dp) < self.config.tolerance:
            print(f"[Twiddle] 收敛！总步长 {sum(self.dp):.6f} < {self.config.tolerance}")
            return None
        
        return self._clip_params(self.params.copy())
    
    def _clip_params(self, params: List[float]) -> List[float]:
        """将参数裁剪到合法范围"""
        clipped = []
        for p, (low, high) in zip(params, self.bounds):
            clipped.append(max(low, min(high, p)))
        return clipped

    def export_state(self) -> Dict[str, Any]:
        return {
            "params": self.params,
            "dp": self.dp,
            "best_score": self.best_score,
            "best_params": self.best_params,
            "iteration": self.iteration,
            "param_order": self.param_order,
        }

    def import_state(self, state: Dict[str, Any]) -> None:
        try:
            self.params = list(state.get("params", self.params))
            self.dp = list(state.get("dp", self.dp))
            self.best_score = float(state.get("best_score", self.best_score))
            self.best_params = list(state.get("best_params", self.best_params))
            self.iteration = int(state.get("iteration", self.iteration))
            po = state.get("param_order", None)
            if isinstance(po, list) and po:
                self.param_order = [int(x) for x in po]
            # 恢复后也要裁剪，避免历史状态越界
            self.params = self._clip_params(self.params)
            self.best_params = self._clip_params(self.best_params)
        except Exception:
            # 恢复失败则保持默认
            pass


class CoordinateDescentOptimizer:
    """坐标下降优化器"""
    
    def __init__(self, config: TuneConfig, param_bounds: List[Tuple[float, float]]):
        self.config = config
        self.bounds = param_bounds
        self.n_params = len(param_bounds)
        
        # 初始化参数
        self.params = [(low + high) / 2.0 for low, high in param_bounds]
        self.best_score = float('inf')
        self.best_params = self.params.copy()
        
        self.current_param = 0
        self.current_direction = 1  # 1: 正向, -1: 反向
        self.step_size = [(high - low) * 0.1 for low, high in param_bounds]
        self.iteration = 0
        self.no_improve_count = 0
    
    def get_params(self) -> List[float]:
        return self.params.copy()
    
    def update(self, score: float) -> Optional[List[float]]:
        self.iteration += 1
        
        if self.iteration > self.config.max_iterations:
            return None
        
        if score < self.best_score:
            self.best_score = score
            self.best_params = self.params.copy()
            self.step_size[self.current_param] *= 1.2  # 增加步长
            self.no_improve_count = 0
            print(f"[CoordDesc] 迭代 {self.iteration}: 新最佳 {score:.4f} @ 参数 {self.current_param}")
        else:
            # 反向尝试
            if self.current_direction == 1:
                self.params[self.current_param] -= 2 * self.step_size[self.current_param]
                self.current_direction = -1
                return self._clip_params(self.params.copy())
            else:
                # 两个方向都不行，减小步长并移动到下一个参数
                self.params[self.current_param] = self.best_params[self.current_param]
                self.step_size[self.current_param] *= 0.5
                self.current_direction = 1
                self.current_param = (self.current_param + 1) % self.n_params
                self.no_improve_count += 1
        
        # 检查收敛
        if self.no_improve_count >= self.n_params * 2:
            print(f"[CoordDesc] 收敛！无改进次数 {self.no_improve_count}")
            return None
        
        # 尝试下一个方向
        self.params[self.current_param] += self.step_size[self.current_param] * self.current_direction
        return self._clip_params(self.params.copy())
    
    def _clip_params(self, params: List[float]) -> List[float]:
        clipped = []
        for p, (low, high) in zip(params, self.bounds):
            clipped.append(max(low, min(high, p)))
        return clipped

    def export_state(self) -> Dict[str, Any]:
        return {
            "params": self.params,
            "best_score": self.best_score,
            "best_params": self.best_params,
            "current_param": self.current_param,
            "current_direction": self.current_direction,
            "step_size": self.step_size,
            "iteration": self.iteration,
            "no_improve_count": self.no_improve_count,
        }

    def import_state(self, state: Dict[str, Any]) -> None:
        try:
            self.params = list(state.get("params", self.params))
            self.best_score = float(state.get("best_score", self.best_score))
            self.best_params = list(state.get("best_params", self.best_params))
            self.current_param = int(state.get("current_param", self.current_param))
            self.current_direction = int(state.get("current_direction", self.current_direction))
            self.step_size = list(state.get("step_size", self.step_size))
            self.iteration = int(state.get("iteration", self.iteration))
            self.no_improve_count = int(state.get("no_improve_count", self.no_improve_count))
        except Exception:
            pass


class NelderMeadOptimizer:
    """Nelder-Mead 单纯形优化器"""
    
    def __init__(self, config: TuneConfig, param_bounds: List[Tuple[float, float]]):
        self.config = config
        self.bounds = param_bounds
        self.n_params = len(param_bounds)
        
        # 初始化单纯形
        self.simplex = []
        self.scores = []
        
        # 中心点
        center = [(low + high) / 2.0 for low, high in param_bounds]
        self.simplex.append(center)
        
        # 其他顶点
        for i in range(self.n_params):
            point = center.copy()
            step = (param_bounds[i][1] - param_bounds[i][0]) * 0.1
            point[i] += step
            self.simplex.append(point)
        
        self.iteration = 0
        self.eval_count = 0
        self.state = "init"  # init, reflect, expand, contract, shrink
        self.centroid = None
        self.reflected = None
    
    def get_params(self) -> List[float]:
        """获取下一个需要评估的参数"""
        if self.eval_count < len(self.simplex):
            return self._clip_params(self.simplex[self.eval_count].copy())
        
        # 进入迭代阶段
        if self.state == "init":
            self._sort_simplex()
            self.state = "reflect"
        
        if self.state == "reflect":
            self.centroid = self._compute_centroid()
            alpha = 1.0
            self.reflected = self._reflect(self.centroid, self.simplex[-1], alpha)
            return self._clip_params(self.reflected.copy())
        
        # 其他状态由 update 方法处理
        return None
    
    def update(self, score: float) -> Optional[List[float]]:
        """更新单纯形"""
        self.eval_count += 1
        
        if self.eval_count <= len(self.simplex):
            # 初始评估阶段
            self.scores.append(score)
            if self.eval_count < len(self.simplex):
                return self._clip_params(self.simplex[self.eval_count].copy())
            else:
                self._sort_simplex()
                self.state = "reflect"
                self.centroid = self._compute_centroid()
                alpha = 1.0
                self.reflected = self._reflect(self.centroid, self.simplex[-1], alpha)
                return self._clip_params(self.reflected.copy())
        
        # 迭代阶段
        self.iteration += 1
        if self.iteration > self.config.max_iterations:
            print(f"[NelderMead] 达到最大迭代次数")
            return None
        
        # Nelder-Mead 逻辑简化版
        best_score = self.scores[0]
        worst_score = self.scores[-1]
        
        if score < best_score:
            # 扩展
            expanded = self._reflect(self.centroid, self.simplex[-1], 2.0)
            self.simplex[-1] = expanded
            self.scores[-1] = score
            self._sort_simplex()
        elif score < worst_score:
            # 接受反射
            self.simplex[-1] = self.reflected
            self.scores[-1] = score
            self._sort_simplex()
        else:
            # 收缩
            contracted = self._reflect(self.centroid, self.simplex[-1], 0.5)
            self.simplex[-1] = contracted
            self.scores[-1] = score
            self._sort_simplex()
        
        # 检查收敛
        if max(self.scores) - min(self.scores) < self.config.tolerance:
            print(f"[NelderMead] 收敛！分数范围 {max(self.scores) - min(self.scores):.6f}")
            return None
        
        # 下一轮反射
        self.centroid = self._compute_centroid()
        self.reflected = self._reflect(self.centroid, self.simplex[-1], 1.0)
        return self._clip_params(self.reflected.copy())
    
    def _sort_simplex(self):
        """按分数排序单纯形"""
        indexed = list(zip(self.scores, self.simplex))
        indexed.sort(key=lambda x: x[0])
        self.scores = [x[0] for x in indexed]
        self.simplex = [x[1] for x in indexed]
    
    def _compute_centroid(self) -> List[float]:
        """计算最佳 n 个点的重心"""
        n = len(self.simplex) - 1
        centroid = []
        for i in range(self.n_params):
            coord = sum(self.simplex[j][i] for j in range(n)) / n
            centroid.append(coord)
        return centroid
    
    def _reflect(self, centroid: List[float], worst: List[float], alpha: float) -> List[float]:
        """反射最坏点"""
        reflected = []
        for c, w in zip(centroid, worst):
            r = c + alpha * (c - w)
            reflected.append(r)
        return reflected
    
    def _clip_params(self, params: List[float]) -> List[float]:
        clipped = []
        for p, (low, high) in zip(params, self.bounds):
            clipped.append(max(low, min(high, p)))
        return clipped


class BayesianOptimizer:
    """简化版贝叶斯优化器（使用随机采样+高斯过程近似）"""
    
    def __init__(self, config: TuneConfig, param_bounds: List[Tuple[float, float]]):
        self.config = config
        self.bounds = param_bounds
        self.n_params = len(param_bounds)
        
        self.X = []  # 参数历史
        self.y = []  # 分数历史
        self.iteration = 0
        
        # 随机阶段
        self.random_phase = True
        self.random_count = 0
    
    def get_params(self) -> List[float]:
        """获取下一组参数"""
        if self.random_phase and self.random_count < self.config.bayesian_init_points:
            # 随机采样 Latin Hypercube
            params = self._latin_hypercube_sample()
            self.random_count += 1
            return params
        
        self.random_phase = False
        
        # 使用上置信界 (UCB) 选择下一个点
        # 简化版：随机采样多个候选点，选择 UCB 最高的
        best_ucb = float('-inf')
        best_params = None
        
        for _ in range(100):
            candidate = self._random_sample()
            ucb = self._compute_ucb(candidate)
            if ucb > best_ucb:
                best_ucb = ucb
                best_params = candidate
        
        return best_params
    
    def update(self, score: float) -> Optional[List[float]]:
        """更新模型"""
        self.iteration += 1
        
        if self.iteration > self.config.max_iterations:
            return None
        
        return self.get_params()
    
    def _latin_hypercube_sample(self) -> List[float]:
        """Latin Hypercube 采样"""
        params = []
        for i, (low, high) in enumerate(self.bounds):
            # 将当前维度分成 n 个区间
            n = self.config.bayesian_init_points
            idx = self.random_count
            # 在该区间内随机采样
            bin_low = low + (high - low) * idx / n
            bin_high = low + (high - low) * (idx + 1) / n
            val = random.uniform(bin_low, bin_high)
            params.append(val)
        return params
    
    def _random_sample(self) -> List[float]:
        """完全随机采样"""
        return [random.uniform(low, high) for low, high in self.bounds]
    
    def _compute_ucb(self, x: List[float], kappa: float = 2.0) -> float:
        """计算上置信界（简化版，使用径向基函数核）"""
        if len(self.X) == 0:
            return random.random()
        
        # 计算到所有采样点的距离
        mu = 0.0
        sigma = 1.0
        
        weights = []
        for xi in self.X:
            dist = sum((a - b) ** 2 for a, b in zip(x, xi))
            w = math.exp(-dist / (2 * self.n_params))
            weights.append(w)
        
        total_w = sum(weights) + 1e-8
        mu = sum(w * yi for w, yi in zip(weights, self.y)) / total_w
        
        # 简化的方差估计
        sigma = 1.0 - max(weights) / total_w if weights else 1.0
        
        return mu - kappa * sigma  # 注意：分数越低越好，所以用 mu - kappa*sigma


class ProgressiveTuner:
    """渐进式调参器 - 逐步增加实验时间"""
    
    def __init__(self, config: TuneConfig):
        self.config = config
        self.runner = ExperimentRunner(config)  # 实验执行器
        self.global_exp_count = 0  # 全局实验计数器
        self.scorer = ScoreFunction(config)
        self._stop_after_batch = False
        
        # 参数边界
        self.param_bounds = [
            config.hp_range,
            config.hd_range,
            config.hi_range,
            config.kpp_range,
            config.kpi_range,
            config.kpd_range,
            config.trim_range,
        ]
        
        # 根据算法选择优化器
        if config.algorithm == "twiddle":
            self.optimizer = TwiddleOptimizer(config, self.param_bounds)
        elif config.algorithm == "coordinate_descent":
            self.optimizer = CoordinateDescentOptimizer(config, self.param_bounds)
        elif config.algorithm == "nelder_mead":
            self.optimizer = NelderMeadOptimizer(config, self.param_bounds)
        elif config.algorithm == "bayesian":
            self.optimizer = BayesianOptimizer(config, self.param_bounds)
        elif config.algorithm == "zn":
            self.optimizer = ZNOptimizer(config, self.param_bounds)
        else:
            raise ValueError(f"未知算法: {config.algorithm}")
        
        # 历史记录
        self.history: List[Dict[str, Any]] = []
        self.best_result: Optional[Dict[str, Any]] = None
        
    def run(self, start_phase: int = 1) -> Dict[str, Any]:
        """
        运行渐进式调参
        
        Args:
            start_phase: 从第几个阶段开始（用于恢复）
        
        流程：
        1. 从 start_ms 开始
        2. 在当前时长下优化直到收敛或达到迭代次数
        3. 增加时长到下一个级别
        4. 重复直到 max_ms
        """
        # 每次启动只跑一轮（batch_limit 次），因此本轮计数从 0 开始
        self.global_exp_count = 0

        # 计算当前阶段对应的时长
        current_ms = self.config.start_ms + (start_phase - 1) * self.config.ms_step
        current_ms = min(current_ms, self.config.max_ms)
        phase = start_phase
        
        print(f"\n{'='*60}")
        print(f"渐进式 PID 自动调参")
        print(f"算法: {self.config.algorithm}")
        print(f"速度: TS={self.config.target_speed}")
        print(f"限幅: PWM_MAX={int(self.config.pwm_max)}  DIFF_MAX={int(self.config.diff_max)}")
        print(f"起始阶段: {start_phase}, 时长: {current_ms}ms, 最大: {self.config.max_ms}ms")
        print(f"{'='*60}\n")
        
        while current_ms <= self.config.max_ms:
            print(f"\n{'='*60}")
            print(f"阶段 {phase}: 实验时长 {current_ms}ms")
            print(f"{'='*60}")
            
            # 在此时长下进行优化
            self._stop_after_batch = False
            converged = self._optimize_for_duration(current_ms, phase)
            
            if converged:
                print(f"\n[阶段 {phase}] 已收敛")
            
            # 到达批次上限：严格退出，不进入下一阶段
            if self._stop_after_batch:
                # 批次结束不视为“完成调参”，避免main里清理检查点
                return {}
            
            # 保存阶段结果
            self._save_checkpoint(phase, current_ms)
            
            # 进入下一阶段
            if current_ms >= self.config.max_ms:
                break
            
            current_ms = min(current_ms + self.config.ms_step, self.config.max_ms)
            phase += 1
            
            # 在下一阶段开始时使用上一阶段的最佳参数
            if self.best_result:
                print(f"\n[阶段 {phase}] 继承上一阶段最佳参数")
        
        print(f"\n{'='*60}")
        print("调参完成！")
        if self.best_result:
            print(f"最佳分数: {self.best_result['score']:.4f}")
            print(f"最佳参数:")
            for k, v in self.best_result['params'].items():
                print(f"  {k}: {v:.4f}")
        print(f"{'='*60}\n")
        
        return self.best_result or {}
    
    def _optimize_for_duration(self, duration_ms: int, phase: int) -> bool:
        """在给定时长下进行优化，返回是否收敛"""
        iteration = 0
        no_improve = 0
        exp_count_in_phase = 0  # 本阶段实验计数
        
        while iteration < self.config.max_iterations:
            # 获取当前参数
            params_list = self.optimizer.get_params()
            if params_list is None:
                print(f"[优化器] 已收敛")
                return True
            
            params = PIDParams.from_list(params_list)
            
            print(
                f"\n[阶段{phase}-迭代{iteration+1}] 参数: "
                f"hp={params.hp:.3f}, hd={params.hd:.4f}, hi={params.hi:.3f}, "
                f"kpp={params.kpp:.3f}, kpi={params.kpi:.3f}, kpd={params.kpd:.3f}, "
                f"trim={params.trim:.2f}"
            )
            
            # 运行实验
            raw_file, metrics = self.runner.run(params, duration_ms)
            
            if raw_file is None:
                print(f"[ERROR] 实验失败，跳过")
                iteration += 1
                continue
            
            # 计算分数
            score = self.scorer.calculate(metrics)
            
            print(f"[结果] 分数: {score:.4f}, encL={metrics['encL_mean']:.1f}, "
                  f"encR={metrics['encR_mean']:.1f}, yaw_err={metrics['yaw_err_rms']:.3f}")
            
            # 记录历史
            result = {
                "phase": phase,
                "iteration": iteration,
                "duration_ms": duration_ms,
                "params": params.to_dict(),
                "score": score,
                "metrics": metrics,
                "raw_file": raw_file,
            }
            self.history.append(result)
            
            # 更新最佳结果
            if self.best_result is None or score < self.best_result['score']:
                self.best_result = result
                no_improve = 0
                print(f"[NEW BEST] 阶段{phase} 新最佳分数: {score:.4f}")
            else:
                no_improve += 1
            
            # 更新优化器
            if self.config.algorithm == "zn" and isinstance(self.optimizer, ZNOptimizer):
                next_params = self.optimizer.update_with_raw(params.hp, raw_file, self.runner)
                if next_params is not None and self.optimizer.done and self.optimizer.best_pid is not None:
                    # ZN 找到 PID 后：用计算出的 PID 再跑一轮作为验证，并将其作为 best_result 推荐
                    zn_pid = PIDParams.from_list(self.optimizer.best_pid)
                    print(
                        f"[ZN] Ku/Pu 已估计，建议参数: hp={zn_pid.hp:.3f}, hd={zn_pid.hd:.4f}, hi={zn_pid.hi:.3f}"
                    )
                    raw2, metrics2 = self.runner.run(zn_pid, duration_ms)
                    if raw2 is not None:
                        score2 = self.scorer.calculate(metrics2)
                        result2 = {
                            "phase": phase,
                            "iteration": iteration,
                            "duration_ms": duration_ms,
                            "params": zn_pid.to_dict(),
                            "score": score2,
                            "metrics": metrics2,
                            "raw_file": raw2,
                        }
                        self.history.append(result2)
                        if self.best_result is None or score2 < self.best_result["score"]:
                            self.best_result = result2
                            print(f"[ZN] 验证实验分数: {score2:.4f} (已更新 best)")
                    return True
            else:
                next_params = self.optimizer.update(score)
            
            if next_params is None:
                print(f"[优化器] 已收敛")
                return True
            
            # 检查无改进次数
            if no_improve >= 10:
                print(f"[阶段{phase}] 10次无改进，提前结束")
                return False
            
            # 检查全局实验次数限制（每2次后强制退出）
            self.global_exp_count += 1
            print(f"[计数] 全局第 {self.global_exp_count} 次实验")
            
            if self.global_exp_count >= self.config.batch_limit:
                print(f"\n{'='*60}")
                print(f"[批次结束] 已完成 {self.global_exp_count} 次实验")
                print(f"请将小车放回安全起始位置")
                self._print_last_batch_report()
                print(f"确认位置安全后，在终端运行：python auto_tune_pid.py --port {self.config.port} --resume")
                print(f"{'='*60}")
                self._save_checkpoint(phase, duration_ms, pause_flag=True, exp_count=self.global_exp_count)
                self._stop_after_batch = True
                return False  # 退出等待用户指令
            
            # 短暂暂停
            time.sleep(0.5)
            
            iteration += 1
            exp_count_in_phase += 1
        
        return False
    
    def _print_last_batch_report(self) -> None:
        n = max(1, int(self.config.batch_limit))
        last = self.history[-n:] if len(self.history) >= 1 else []
        if not last:
            print("[报告] 无可用历史记录")
            return
        print("[报告] 本轮实验摘要：")
        for r in last:
            m = r.get("metrics", {})
            exp_id = m.get("exp_id", r.get("exp_id", "?"))
            dur = m.get("duration_ms", r.get("duration_ms", "?"))
            encL = m.get("encL_mean", 0.0)
            encR = m.get("encR_mean", 0.0)
            dropout = m.get("dropout_ratio", 0.0)
            score = r.get("score", 0.0)
            yaw_first = m.get("imu_yaw_first", None)
            yaw_last = m.get("imu_yaw_last", None)
            yaw_delta = m.get("imu_yaw_delta", None)
            yaw_dir = m.get("imu_yaw_direction", "unknown")
            imu_str = "imu=NA"
            if yaw_delta is not None:
                imu_str = f"imu_dyaw={float(yaw_delta):+.2f}({yaw_dir})"
            print(f"  exp{int(exp_id):02d}  {int(dur)}ms  encL={encL:.1f}  encR={encR:.1f}  dropout={dropout:.3f}  score={score:.4f}  {imu_str}")

        if self.best_result:
            print("[报告] 截至目前全历史最佳：")
            print(f"  best_score={self.best_result['score']:.4f}")
            bp = self.best_result.get('params', {})
            if bp:
                print(
                    "  params="
                    + " ".join([f"{k}={float(v):.4f}" for k, v in bp.items()])
                )
    
    def _save_checkpoint(self, phase: int, duration_ms: int, pause_flag: bool = False, exp_count: int = 0):
        """保存检查点"""
        optimizer_state = None
        if hasattr(self.optimizer, "export_state"):
            try:
                optimizer_state = {
                    "algorithm": self.config.algorithm,
                    "state": self.optimizer.export_state(),
                }
            except Exception:
                optimizer_state = None

        checkpoint = {
            "phase": phase,
            "duration_ms": duration_ms,
            "exp_count": exp_count,
            "paused": pause_flag,
            "config": {
                "algorithm": self.config.algorithm,
                "port": self.config.port,
                "target_speed": self.config.target_speed,
                "pwm_max": int(self.config.pwm_max),
                "diff_max": int(self.config.diff_max),
            },
            "best_result": self.best_result,
            "history": self.history,
            "exp_counter": self.runner.exp_counter,  # 保存实验计数器
            "optimizer_state": optimizer_state,
        }
        
        checkpoint_file = self.runner.data_dir / "tune_checkpoint_latest.json"
        with open(checkpoint_file, 'w', encoding='utf-8') as f:
            json.dump(checkpoint, f, indent=2, ensure_ascii=False, default=str)
        
        print(f"[检查点] 已保存到 {checkpoint_file}")


def main():
    ap = argparse.ArgumentParser(description="渐进式 PID 自动调参")
    
    # 串口配置
    ap.add_argument("--port", default="COM13", help="串口号 (默认: COM13)")
    ap.add_argument("--baud", type=int, default=115200, help="波特率 (默认: 115200)")
    
    # 实验配置
    ap.add_argument("--start-ms", type=int, default=500, help="起始实验时长 ms (默认: 500)")
    ap.add_argument("--max-ms", type=int, default=5000, help="最大实验时长 ms (默认: 5000)")
    ap.add_argument("--ms-step", type=int, default=500, help="时长增量 ms (默认: 500)")
    ap.add_argument("--batch-limit", type=int, default=1, help="每轮实验次数（默认: 1；用于每跑一次就停）")
    ap.add_argument("--ts", dest="target_speed", type=float, default=20.0, help="小车目标速度 TS")
    ap.add_argument("--pwm-max", type=int, default=20, help="PWM_MAX 限幅 (默认: 20)")
    ap.add_argument("--diff-max", type=int, default=10, help="DIFF_MAX 限幅 (默认: 10)")
    
    # 优化算法
    ap.add_argument("--algorithm", type=str, default="twiddle",
                    choices=["twiddle", "coordinate_descent", "nelder_mead", "bayesian", "zn"],
                    help="优化算法 (默认: twiddle)")

    ap.add_argument("--zn-ms", type=int, default=8000, help="ZN 模式每轮实验时长 ms (默认: 8000)")

    ap.add_argument("--focus-trim", default="off", choices=["off", "priority", "only"],
                    help="Twiddle 调参时是否优先/仅调 trim (默认: off)")
    
    # 算法参数
    ap.add_argument("--max-iterations", type=int, default=30, help="每阶段最大迭代次数 (默认: 30)")
    ap.add_argument("--tolerance", type=float, default=0.001, help="收敛容差 (默认: 0.001)")
    
    # 评分权重
    ap.add_argument("--w-yaw", type=float, default=1.0, help="航向误差权重 (默认: 1.0)")
    ap.add_argument("--w-straight", type=float, default=2.0, help="直线度权重 (默认: 2.0)")
    ap.add_argument("--w-stability", type=float, default=1.0, help="稳定性权重 (默认: 1.0)")
    
    # 安全限制
    ap.add_argument("--max-yaw-drift", type=float, default=45.0, help="最大航向偏移 (默认: 45°)")
    
    # 恢复模式
    ap.add_argument("--resume", action="store_true", help="从检查点恢复继续调参")
    ap.add_argument("--reset", action="store_true", help="删除检查点，重新开始")
    
    args = ap.parse_args()
    
    data_dir = Path(__file__).parent / "000Data"
    checkpoint_file = data_dir / "tune_checkpoint_latest.json"
    
    # 处理重置请求：清空000Data并从exp01开始
    if args.reset:
        try:
            for p in data_dir.glob("exp*_raw.txt"):
                try:
                    p.unlink()
                except Exception:
                    pass
            for p in data_dir.glob("exp*_dump.csv"):
                try:
                    p.unlink()
                except Exception:
                    pass
            for p in data_dir.glob("tune_checkpoint*.json"):
                try:
                    p.unlink()
                except Exception:
                    pass
        except Exception:
            pass
        print("[INFO] 已清空 000Data，将从 exp01 重新开始")
    
    # 创建配置
    config = TuneConfig(
        port=args.port,
        baud=args.baud,
        start_ms=args.start_ms,
        max_ms=args.max_ms,
        ms_step=args.ms_step,
        batch_limit=args.batch_limit,
        target_speed=args.target_speed,
        pwm_max=args.pwm_max,
        diff_max=args.diff_max,
        algorithm=args.algorithm,
        zn_ms=args.zn_ms,
        twiddle_param_order=None,
        max_iterations=args.max_iterations,
        tolerance=args.tolerance,
        w_yaw_error=args.w_yaw,
        w_straight=args.w_straight,
        w_stability=args.w_stability,
        max_yaw_drift=args.max_yaw_drift,
    )

    # ZN 模式：固定每轮时长为 zn_ms，并强制单阶段（不渐进）
    if config.algorithm == "zn":
        config.start_ms = int(config.zn_ms)
        config.max_ms = int(config.zn_ms)
        config.ms_step = int(config.zn_ms)

    # Twiddle 调参顺序控制：参数顺序为 [hp, hd, hi, kpp, kpi, kpd, trim]
    if args.algorithm == "twiddle":
        trim_idx = 6
        if args.focus_trim == "priority":
            config.twiddle_param_order = [trim_idx, 0, 1, 2, 3, 4, 5]
        elif args.focus_trim == "only":
            config.twiddle_param_order = [trim_idx]
        if args.focus_trim != "off":
            # focus-trim 长距离更容易撞障：收窄 trim 范围
            config.trim_range = (-3.0, 3.0)
    
    # 创建调参器
    tuner = ProgressiveTuner(config)
    
    # 尝试加载检查点
    start_phase = 1
    if args.resume and checkpoint_file.exists():
        try:
            with open(checkpoint_file, 'r', encoding='utf-8') as f:
                checkpoint = json.load(f)
            
            # 恢复历史记录
            tuner.history = checkpoint.get('history', [])
            tuner.best_result = checkpoint.get('best_result')

            # 恢复实验计数器
            if 'exp_counter' in checkpoint:
                tuner.runner.exp_counter = checkpoint['exp_counter']
                print(f"[恢复] 实验计数器: exp{tuner.runner.exp_counter:02d}")
            
            # 恢复优化器状态
            if 'optimizer_state' in checkpoint and checkpoint['optimizer_state']:
                opt_pack = checkpoint['optimizer_state']
                if isinstance(opt_pack, dict):
                    alg = opt_pack.get('algorithm', '')
                    st = opt_pack.get('state', None)
                    if alg == config.algorithm and st is not None and hasattr(tuner.optimizer, 'import_state'):
                        try:
                            tuner.optimizer.import_state(st)
                            print(f"[恢复] 已恢复优化器状态: {alg}")
                        except Exception as e:
                            print(f"[WARN] 恢复优化器状态失败: {e}")

            # 若用户指定 focus-trim，则覆盖从 checkpoint 恢复的顺序，确保下一轮优先调 trim
            if config.algorithm == "twiddle" and args.focus_trim != "off":
                try:
                    if hasattr(tuner.optimizer, 'param_order') and config.twiddle_param_order:
                        tuner.optimizer.param_order = list(config.twiddle_param_order)
                    # 限制 trim 的步长上限，避免trim一次跳太大导致撞障
                    if hasattr(tuner.optimizer, 'dp') and isinstance(getattr(tuner.optimizer, 'dp'), list):
                        trim_idx = 6
                        if 0 <= trim_idx < len(tuner.optimizer.dp):
                            tuner.optimizer.dp[trim_idx] = min(float(tuner.optimizer.dp[trim_idx]), 0.5)
                            print(f"[恢复] trim_dp={tuner.optimizer.dp[trim_idx]:.3f}")
                    print(f"[恢复] focus-trim={args.focus_trim}，已覆盖 Twiddle 调参顺序")
                except Exception as e:
                    print(f"[WARN] focus-trim 覆盖失败: {e}")

            # 确定从哪个阶段开始
            start_phase = checkpoint.get('phase', 1)
            print(f"[恢复] 从阶段 {start_phase} 继续")
            print(f"[恢复] 已有 {len(tuner.history)} 条历史记录")
            if tuner.best_result:
                print(f"[恢复] 当前最佳分数: {tuner.best_result['score']:.4f}")
            
        except Exception as e:
            print(f"[WARN] 加载检查点失败: {e}，将重新开始")
            start_phase = 1
    
    # 运行调参
    try:
        result = tuner.run(start_phase=start_phase)
        
        if result:
            print("\n" + "="*60)
            print("最终推荐参数:")
            print("="*60)
            for k, v in result['params'].items():
                print(f"  {k:10s}: {v:.4f}")
            print(f"\n最终分数: {result['score']:.4f}")
            print(f"实验文件: {result['raw_file']}")
            print("="*60)
            
            # 清理检查点文件（成功完成）
            if checkpoint_file.exists():
                checkpoint_file.unlink()
                print("[INFO] 调参完成，已清理检查点")
    except KeyboardInterrupt:
        print("\n\n[中断] 用户停止调参")
        if tuner.best_result:
            print(f"\n当前最佳参数:")
            for k, v in tuner.best_result['params'].items():
                print(f"  {k}: {v:.4f}")
        print(f"\n使用 --resume 参数可以继续调参")


if __name__ == "__main__":
    main()
