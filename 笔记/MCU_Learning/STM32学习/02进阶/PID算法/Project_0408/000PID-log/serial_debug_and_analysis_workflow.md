# 串口调试、自动采数与数据分析工作流

本文档说明本工程中几个 Python 工具脚本的用途、常见命令、数据格式，以及如何用这些工具定位控制问题。

## 1. 脚本分工

### 1.1 `serial_term.py`
用途：交互式串口终端，适合人工手动调参、即时观察串口输出。

特点：
- 实时显示 MCU 返回的 `HB`、`STAT`、`TRACE`、`OK`、`ERR`
- 支持快捷命令，例如 `run`、`stop`、`stat`
- 支持参数快捷设置，例如 `spd=5`、`trim=0`、`so=90`
- 支持录制原始日志 `rec=3.0`
- 支持实验快捷录制 `shot=117,1200`
- 支持对最近一次 raw 数据直接调用分析器

适合场景：
- 在线试参数
- 观察某个命令是否立即生效
- 看 `HB` 是否连续
- 看启动瞬间 `y / yr / c / L / R`

### 1.2 `startup_probe.py`
用途：自动化串口探测脚本，适合复现 `#RUN!` / `#EXP=START` / `#EXP=RUN` 相关问题。

特点：
- 自动打开串口
- 自动发送一组预设参数
- 自动发送 `#RUN!` 或实验命令
- 自动保存完整原始日志到 `000Data`
- 日志里会插入 `CMD ...` 和 `RES ...` 标记，便于回看

适合场景：
- 复现“发 `#RUN!` 后失联”
- 复现“启动瞬间大幅偏转”
- 固件改完后快速做一轮探测

### 1.3 `serial_run_regression.py`
用途：一键回归脚本，适合固件改完后的自动验收。

特点：
- 自动执行一套标准回归流程
- 自动统计：
  - 是否出现 `OK RUN`
  - 是否出现 `TRACE RUN_OK`
  - 是否出现 `run=1`
  - `HB` 是否持续
  - 是否疑似复位/失联
- 输出两份文件：
  - 原始日志 `.txt`
  - 判定结果 `.json`

适合场景：
- 每次改完 MCU 固件后快速做“冒烟测试”
- 作为固定回归工具验证 `#RUN!` 是否正常

### 1.4 `trajectory_analyzer.py`
用途：对采集到的 `HB/STAT` 原始日志做离线轨迹与控制量分析。

特点：
- 解析 `HB` / `STAT` 中的 key-value 字段
- 提取 `tick / run / y / yr / L / R / el / er / ed / trim / at`
- 支持对运行段、稳态段做裁剪分析
- 可计算均值、RMS、标准差、零交叉、斜率等指标
- 可辅助判断：
  - 是否蛇形过大
  - 是否单侧轮偏差明显
  - 航向控制是否过冲
  - 起步段是否存在大幅瞬态

适合场景：
- 调 `TRIM`
- 调 `HP/HD/HI`
- 分析“为什么先掉头再蛇形”
- 分析长距离直线稳定性

## 2. 常用串口命令

### 2.1 基础控制
- `#RUN!`：开始运行
- `#STOP!`：停止运行
- `#STAT!`：打印当前状态快照
- `#CAL!`：锁定当前航向并清零部分控制量

### 2.2 速度与输出相关
- `#SPD=5!`：设置目标速度
- `#SO=90!`：设置速度环输出上限
- `#PWM_MAX=60!`：设置最大 PWM
- `#DIFF_MAX=20!`：设置差速修正上限
- `#MIN=30!`：设置最小前进 PWM
- `#KP=70!` / `#KM=900!`：起步冲量参数
- `#RAMP=2!`：PWM 爬升步长

### 2.3 航向控制相关
- `#TRIM=0!`：静态左右轮修正
- `#HP=...!`：航向比例
- `#HD=...!`：航向微分/阻尼
- `#HI=...!`：航向积分
- `#HIL=...!`：航向积分限幅
- `#DB=...!`：航向死区

### 2.4 实验模式
- `#EXP=START,117,1200!`：启动实验状态，但不立即跑
- `#EXP=RUN,117,1200!`：直接开始一轮实验
- `#EXP=STOP,117!`：停止某个实验
- `#EXP=DUMP,117!`：导出实验样本
- `#EXP=STREAM,1!`：开启实验流式输出

## 3. 数据文件与数据格式

### 3.1 原始日志保存位置
通常保存到：
- `Project_Refactor/000Data/*.txt`
- `Project_Refactor/000Data/*_raw.txt`
- `Project_Refactor/000Data/*.json`

### 3.2 常见输出类型

#### `HB ...`
周期心跳，最重要。
常见字段：
- `tick`：毫秒节拍
- `run`：是否运行中
- `spd`：目标速度
- `y`：yaw 角
- `yr`：yaw rate
- `c`：headingCorr
- `hi`：headingI
- `L / R`：左右输出 PWM
- `el / er`：左右速度误差
- `ed`：左右误差差值
- `trim`：静态 trim
- `at`：auto trim
- `pmax / dmax`：输出限制
- `rx`：串口接收计数

#### `STAT ...`
状态快照，比 `HB` 更全。
额外常见字段：
- `ty`：targetYaw
- `e`：yawErr
- `ax/ay/az/gx/gy/gz`：IMU 原始/相对值
- `icm_ok / icm_fail`
- `ok / fail / rx`

#### `TRACE ...`
诊断标记，用来定位运行链路。
例如：
- `TRACE RUN_CMD`
- `TRACE RUN_STEP=ENTER`
- `TRACE RUN_STEP=READ_IMU`
- `TRACE RUN_STEP=IMU_OK`
- `TRACE RUN_STEP=ENABLE`
- `TRACE RUN_STEP=DONE`
- `TRACE RUN_OK`

#### `OK ...` / `ERR`
命令是否被正确解析执行。
例如：
- `OK RUN`
- `OK EXP_START`
- `OK SPD`
- `ERR`

#### `BOOT ...`
复位来源标记。
如果出现新的 `BOOT ...`，说明 MCU 很可能重新启动了。

## 4. 如何用这些脚本调试

## 4.1 手动调试：用 `serial_term.py`
示例：

```powershell
python .\serial_term.py --port COM18 --baud 115200
```

启动后常用输入：

```text
stop
spd=5
trim=0
so=90
pwm=60
diff=20
run
stat
```

如果想录一段 4 秒原始数据：

```text
log=manual
run
stat
logoff
```

或者：

```text
rec=4.0
```

如果想自动做一次实验并保存：

```text
shot=301,4000
```

## 4.2 自动探测：用 `startup_probe.py`
示例：

```powershell
python .\startup_probe.py --port COM18 --baud 115200 --mode exp_start_run --exp-id 301 --ms 4000 --spd 5 --so 90 --trim 0 --pwm-max 60 --diff-max 20 --watch-s 4.8 --echo
```

它会：
- 先发 `#STOP!`
- 再发一组参数命令
- 再发 `#EXP=START,...!`
- 再发 `#RUN!`
- 最后发 `#STAT!`

适合复现启动问题。

## 4.3 自动回归：用 `serial_run_regression.py`
示例：

```powershell
python .\serial_run_regression.py --port COM18 --baud 115200 --mode exp_start_run --exp-id 2001 --ms 1200 --spd 5 --so 90 --trim 0 --pwm-max 60 --diff-max 20
```

它会输出：
- 原始日志路径
- json 判定路径
- `passed/reasons`

## 5. 如何分析“启动先大幅掉头”

这是当前最关键的问题之一。分析时重点看启动后的前 `0 ~ 500ms`。

### 5.1 先看这些字段
- `y`
- `yr`
- `c`
- `L / R`
- `el / er / ed`
- `ty`
- `e`

### 5.2 典型判据

#### 情况 A：`run=1` 后 `c` 立刻打满
例如：
- `c` 很快到 `+10` 或 `-10`
- 同时 `L/R` 明显一边大一边小

说明：
- 航向误差在起步瞬间被放得太大
- 或航向控制方向符号错了

#### 情况 B：`yr` 很大，但 `y` 还没明显变化时就已经强烈差速
说明：
- 启动瞬间 `yawRate` / IMU 姿态重置存在跃迁
- `Control_Start()` 后的最前几帧不稳定

#### 情况 C：`y` 一开始就与 `ty` 相差很大
说明：
- `targetYaw` 锁定时机不对
- 或 `ResetAttitude` 后 `targetYaw` 的参考系与运行时 `yaw` 不一致

#### 情况 D：`c` 还不大，但 `L/R` 已经很不对称
说明：
- 不是航向控制主因
- 更可能是左右轮起步能力不对称、`trim` 不合适、或编码器/电机方向参数有偏差

## 6. 当前代码里与该问题最相关的实现点

根据当前固件实现，`#RUN!` 进入 `Control_Start()` 后会执行：
- `ICM42688_ReadAll()`
- `control_imu_zero_update()`
- `ICM42688_ResetAttitude()`
- `sys->targetYaw = 0.0f`
- `sys->headingI = 0.0f`
- `sys->headingCorr = 0.0f`

这意味着：
- 启动时的航向参考是“重置后的 0 度”
- 如果姿态重置后前几帧 yaw / yawRate 不稳定，就可能造成起步时大幅度修正

## 7. 推荐调试顺序

### 第一步：先确认串口和命令链路正常
看是否存在：
- `TRACE RUN_OK`
- `OK RUN`
- `run=1`
- `HB` 连续输出

### 第二步：采一段 4 秒数据
建议参数：
- `spd=5`
- `so=90`
- `trim=0`
- `pwm-max=60`
- `diff-max=20`
- `ms=4000`

### 第三步：重点看前 0.5 秒
如果前 0.5 秒已经开始大幅 `c` 打满，那么问题不在蛇形本身，而在启动瞬态。

### 第四步：再看后 1~4 秒
如果后续变成规律蛇形，说明：
- 主控制链路已经工作
- 问题集中在启动瞬态，而不是整体闭环失效

## 8. 现阶段实操建议

如果你要手动先做一轮最小复现，推荐顺序：

```text
stop
spd=5
trim=0
so=90
pwm=60
diff=20
stat
run
```

如果要自动采 4 秒：

```powershell
python .\startup_probe.py --port COM18 --baud 115200 --mode exp_start_run --exp-id 301 --ms 4000 --spd 5 --so 90 --trim 0 --pwm-max 60 --diff-max 20 --watch-s 4.8 --echo
```

## 9. 后续如何配合调参

### 调 `TRIM`
看稳态段：
- 如果 `c` 平均值长期偏一个方向
- 同时 `L/R` 有固定偏置
- 而 `y` 漂移单侧明显

则优先调 `TRIM`。

### 调 `HP/HD/HI`
看启动段与蛇形段：
- 启动瞬间过猛：先降 `HP` 或增加启动保护
- 蛇形频率高：适当增 `HD`
- 长期偏置纠不回来：再考虑少量 `HI`

## 10. 总结

本工程里推荐的工作流是：
- 手动调试用 `serial_term.py`
- 自动复现问题用 `startup_probe.py`
- 固件改后验收用 `serial_run_regression.py`
- 数据离线分析用 `trajectory_analyzer.py`

当问题是“启动先大幅掉头、随后蛇形”时，最关键的是采到启动后的前几百毫秒，并检查：
- `y / yr / c`
- `L / R`
- `ty / e`

只要能拿到这段数据，基本就能判断是：
- 启动瞬态参考锁定问题
- 控制符号问题
- 输出上限过大
- 或左右轮起步不对称
