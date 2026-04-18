# 双环 PID 调试与轨迹分析现场简卡

## 1. 现场固定配置

- **[串口]** `COM18`
- **[串口参数]** `115200 8N1`
- **[烧录链路]** `DAPLink + pyOCD`
- **[分析窗口]** `0.5s`
- **[禁止]** 不使用 `trim` 调偏置

> **安全警告**
>
> 烧录、`#RUN!`、`#EXP=RUN,...!`、`#RAW=...!` 前，先架空小车。

---

## 2. 一页版标准流程

### 步骤 1：编译与烧录

- **[编译检查]** `project.build_log.htm` 必须有 `0 Error(s)`
- **[HEX检查]** `Objects/project.hex` 必须是最新
- **[烧录顺序]** `erase --chip` -> `load`

### 步骤 2：上电后基线检查

```text
#VOFA=0!
#STOP!
#IMU_INIT!
#CAL!
#STAT!
```

重点看：

- **[IMU]** `who=0x85`
- **[偏航]** `y`、`yr` 静止正常
- **[编码器]** `el`、`er`、`ed` 合理

### 步骤 3：下发参数

```text
#SPD=1!
#SKP=0.10!
#SKI=0.012!
#SKD=0.00!
#AKP=7.00!
#AKI=0.08!
#AKD=0.35!
#DZ=10!
#DZT=1.0!
#DZL=10!
#DZR=12!
#SBF=2!
#SBV=18!
#SIV=18!
#HDT=400!
#STAT!
```

### 步骤 4：实验采集

```bash
python exp_runner.py --port COM18 --baud 115200 --id 101 --ms 10000 --spd 1 --skp 0.1 --ski 0.012 --skd 0.0 --akp 7.0 --aki 0.08 --akd 0.35 --dz 10 --dzt 1.0 --dzl 10 --dzr 12 --sbf 2 --sbv 18 --siv 18 --hdt 400 --realtime
```

### 步骤 5：轨迹复盘

```bash
python trajectory_analyzer.py --raw .\000Data\exp101_10000ms_xxx_raw.txt --segment-s 0.5 --plot --save .\000Data\exp101_plot.png
```

---

## 3. 起步偏置调参顺序

```text
DZL / DZR -> SBF -> SBV -> SIV -> HDT
```

### `DZL / DZR`

- **[用途]** 解决左右轮起步不同步
- **[原则]** 谁慢补谁
- **[步进]** 每次 `1 PWM`

### `SBF`

- **[用途]** 微调起步前段左右不对称
- **[方向]**
  - `SBF > 0`：补右轮
  - `SBF < 0`：补左轮
- **[步进]** 每次 `1 PWM`

### `SBV`

- **[用途]** 控制起步偏置退出速度
- **[偏置退太快]** 增大 `SBV`
- **[偏置拖太久]** 减小 `SBV`

### `SIV`

- **[用途]** 控制速度积分何时介入
- **[起步猛冲]** 增大 `SIV`

### `HDT`

- **[用途]** 控制航向环延迟介入
- **[刚起步就被强拉方向]** 增大 `HDT`

---

## 4. 0.5 秒窗口看什么

每个 `0.5s` 窗口至少看：

- **[偏航]** `yaw_start / yaw_end / yaw_delta`
- **[编码器]** `el_mean / er_mean / ed_mean`
- **[输出]** `outL_mean / outR_mean / out_diff_mean`
- **[轨迹]** `x_total_end / y_total_end / path_len`

### 快速判断

- **[起步就偏]** 先看 `DZL/DZR/SBF`
- **[起步猛冲]** 看 `SIV`
- **[刚起步就抢方向]** 看 `HDT`
- **[中后段持续弯]** 再看 PID 与航向控制

---

## 5. 三条最重要原则

- **[原则 1]** 先证据，后调参：先看 `STAT/HB/轨迹图`
- **[原则 2]** 一次只改一组参数
- **[原则 3]** 起步偏置问题先调 `DZL/DZR/SBF/SBV/SIV/HDT`，不要一上来就猛改 PID
