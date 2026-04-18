# PID 调试与检测方法（PC 侧脚本 + robust 轨迹分析）

本文档总结当前项目中用于 **下发实验参数**、**采集 raw 遥测**、**解析姿态/轨迹**、以及基于 **robust/conf** 的“自动二分微调 TRIM”方法。

## 1. 目录与文件

- `exp_runner.py`
  - 作用: PC 端串口实验运行器（下发命令、等待运行、保存 raw 日志）
- `trajectory_analyzer.py`
  - 作用: 解析 `000Data/*_raw.txt` 的 HB 遥测，输出速度/偏航/gyro/auto-trim 统计，并给出调参建议
- `000Data/`
  - 作用: raw 日志保存目录，文件名形如 `exp73_2500ms_YYYYMMDD_HHMMSS_raw.txt`

## 2. exp_runner.py：实验下发与数据采集

### 2.1 常用命令形态

以 `exp73` 为例（直线标定隔离实验，COM18，115200）：

```bash
python .\exp_runner.py \
  --id 73 --port COM18 --baud 115200 \
  --ms 2500 --spd 7 --so 160 --trim -0.25 \
  --pwm-max 150 --diff-max 0 \
  --hp 0 --hd 0 --hs 0 --hi 0 \
  --at 0 --at-lim 1.0 \
  --bin 0 --no-dump --fast-start --realtime --cal-wait 0.5
```

### 2.2 关键参数说明（与固件命令对应）

- `--id`：实验编号（用于文件命名）
- `--ms`：运行时长（建议标定阶段用 2500ms/4000ms，避免撞墙污染数据）
- `--spd`：目标速度档
- `--so`：速度环输出限幅（Speed Output Limit）
- `--trim`：左右轮静态偏置（最终 PWM 混控中：`outL += trim`，`outR -= trim`）
- `--pwm-max`：PWM 最大值
- `--diff-max`：航向差速混控最大值（为 0 表示完全关差速）
- `--hp/--hd/--hs/--hi`：航向环参数
  - 注意：只设 `hp/hd/hs=0` **不足以关闭航向环**，必须把 `hi=0` 才能避免 I 项累积导致的隐性差速
- `--at/--at-lim`：auto-trim 使能与限幅
- `--bin`：串口输出模式（推荐实验采集 `--bin 0`，避免二进制流污染 raw 文件）

### 2.3 安全采集原则（避免“撞墙污染”）

- 标定阶段（定位 TRIM 零点）建议：
  - `so=160`、`ms=2500`、`diff-max=0`、`hp/hd/hs/hi=0`、`at=0`
- 一旦出现撞墙/碰撞/明显打滑，该轮数据应当视为 **不可用于拟合/二分**（后续由 `conf` 低分自动提示）

## 3. trajectory_analyzer.py：遥测解析与 robust/conf 评估

### 3.1 基本用法

```bash
python .\trajectory_analyzer.py --raw .\000Data\exp73_2500ms_..._raw.txt
```

### 3.2 robust 模式（推荐用于自动决策）

```bash
python .\trajectory_analyzer.py \
  --raw .\000Data\exp73_2500ms_..._raw.txt \
  --robust --med-win 7 --mad-k 4.0 --win-s 0.40 --at-lim 1.0
```

robust 模式做了什么：

- **滚动中位数平滑（rolling median）**：对解包后的 `yaw` / `yr` 去噪
- **MAD 异常点剔除**：识别碰撞/打滑尖峰并剔除
- **滑窗斜率分布**：用窗口拟合得到多个斜率样本，输出 `win_std`
- **置信度评分 conf（0~1）**：综合 `keep`（保留比例）与 `win_std`（稳定度）给出

### 3.3 关键输出字段解释

- `yaw drift slope(deg/s)`：全段线性拟合的偏航漂移斜率
- `yaw drift slope robust(deg/s)`：稳健估计斜率（建议用于自动决策）
- `win_std`：滑窗斜率标准差（越小越稳定）
- `keep`：异常点剔除后保留比例
- `conf`：本次数据可用于自动调参的可信度评分

## 4. 用 robust/conf 做 TRIM 二分微调（推荐流程）

### 4.1 隔离条件（保证“只测 TRIM”）

用于 TRIM 零点定位的推荐隔离参数：

- `so=160`
- `ms=2500`
- `diff-max=0`
- `hp=0 hd=0 hs=0 hi=0`
- `at=0`

### 4.2 二分决策规则

1. 每跑一轮 `expXX` 后用 `trajectory_analyzer.py --robust` 分析。
2. **只有当 `conf >= 0.60`** 时，才接受本轮 `yaw drift slope robust` 用于更新二分区间。
3. 判定方向：
   - `robust slope > 0`：仍有左转漂移 → `trim` 往“更正”方向调一点
   - `robust slope < 0`：开始右转漂移 → `trim` 往“更负”方向回一点
4. 若 `conf < 0.60`：
   - 视为数据被干扰（起跑角度、地面、轻微碰撞、打滑等）
   - 建议重跑同参数，或缩短 `ms` / 降低 `so`

### 4.3 TRIM 量化注意事项

固件侧 `trim` 往往存在 **1/16（0.0625）步进量化**（例如 -0.20 可能记录成 -0.1875 或 -0.25）。

建议：
- 直接用 0.0625 的整数倍下发（例如 `-0.21875`、`-0.25`、`-0.1875`）
- 或在分析时以日志 `trim(last)` 为准

## 5. 从“标定”回到“实跑提速”的恢复步骤

当隔离条件下 TRIM 已接近零漂（例如 `exp73` 直线非常好）后：

1. 固定当前 TRIM
2. 逐步恢复差速混控：`diff-max` 从 0 → 20（或你常用值）
3. 再决定是否开启航向 I（`hi`）或 auto-trim（`at`）
4. 分阶段提速：`so=180 → 200 → 220`，每一步都用 `--robust` 检查 `conf` 与漂移是否恶化

---

维护建议：
- 每次出现“撞墙/异常”的实验，建议在对应 `expXX` 记录里标注“数据污染”，避免误用。
- 推荐将“最直/最稳”的参数结果追加到 `PID-log/pid_params.md`，便于回滚与对比。
