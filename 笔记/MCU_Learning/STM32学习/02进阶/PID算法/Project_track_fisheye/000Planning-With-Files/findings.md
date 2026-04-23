# Findings

## 2026-04-24 实验记录器接管

### 关键证据
- `experiment_logger.py` 的记录入口已经是 `EVT:EXP_START / EVT:EXP_STOP`，并带有 `HB` 回退补开逻辑。
- 固件端仍然保留了：
  - `#EXP?!`
  - `#EXP=...!`
  - `#EXPHOST=...!`
  - `EVT:EXP_START`
  - `EVT:EXP_STOP`
  - `HB:...,exp=...`
- 原脚本的问题不在记录逻辑本身，而在两个运行体验点：
  1. `stdout` 被重定向后默认缓冲，导致外部看起来像“没启动”。
  2. 首次 `#EXP?!` 同步太激进，偶尔会打出一条假警告。

### 判断
- 当前最优做法不是再改固件，而是让 `experiment_logger` 常驻占住 `COM18`。
- 只要板子后续发出 `EVT:EXP_START / EVT:EXP_STOP` 或运行心跳里的 `exp` 号，记录器就会自动落盘。
- 需要保留一个单独的 `logger_session_*.out.txt` 作为记录器自身运行日志，避免用户误以为它没启动。

### 本轮修正
- `experiment_logger.ps1` 改为 `python -u` / `py -3 -u`
- `experiment_logger.py` 开启 `stdout/stderr` 行缓冲
- `sync_experiment_base()` 为 `#EXP?!` 增加 3 次重试

## 2026-04-24 exp257 误左转复盘

### 关键证据
- [`exp_0257_20260424_013942_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0257_20260424_013942_KEY_T.txt) 中，跟线阶段已经出现多次 `sb=0`。
- 首次进入搜索是 `FNDR`，随后一帧 `S3|S4` 就退出搜索。
- 退出搜索时仍处在恢复窗口内，又马上二次丢线，随后切成 `FNDL` 并持续左找线到结束。

### 判断
- 这次“直线突然左转”不是正常 `PD` 修正，而是：
  - 假丢线触发搜索
  - 搜索退出条件过宽
  - 恢复窗口内方向历史被一帧对侧位型改写
  - 第二次丢线后被状态机锁成左找线

### 本轮修正
- `TRACK_LOST_STRAIGHT_CONFIRM_TICKS` 提高中心附近全灭进入找线的门槛
- `TRACK_SEARCH_EXIT_CONFIRM_TICKS` 要求同侧稳定重见线后才退出搜索
- 恢复窗口内再次丢线时，优先沿 `recoverDir` 继续搜索
- 恢复窗口内对侧瞬时位型不再重写 `lastData / lastTurnDir`

## 2026-04-24 IMU:3 再次出现

### 关键证据
- 串口当前稳定返回：
  - `OK:IMU=3,addr=00,fail=5,ready=0,rx=1,ch=0,rid=0,len=0`
- 最近这轮修正只触碰了 `line_track/config/main`，没有再改 `sensor_fusion`。
- 说明这不是新的 IMU 初始化逻辑回归，而是 OLED 又把已有的稳定失败态顶到了前台。

### 判断
- 板子主循环没死，`#STAT!` 正常回。
- 当前该优先修的是显示策略，而不是再次误判为 IMU 逻辑新崩了。

### 本轮修正
- `update_display()` 不再在 `!BNO085_IsReady()` 时直接 `return`
- 现在 idle OLED 仍显示正常状态页，仅第 4 行显示 `IMU:x A:xx F:x`

## 2026-04-24 IMU 阻塞调查

### 关键证据
- `PA15` 作为 `INT`、`PB14` 作为 `RST`，和当前代码完全一致，不是引脚宏写错。
- 固件改成“直接握手，不依赖空写扫描”后，板上实测回包：
  - `OK:IMU=3,addr=00,fail=5,ready=0,rx=1,ch=2,rid=0,len=48`
- 间隔多次读取 `#IMU?!`，结果保持一致，没有再回到 `IMU12345` 循环。
- 该回包说明：
  - `stage=3`：已推进到 `PID` 握手阶段
  - `fail=5`：`BNO_FAIL_PRODUCT_ID`
  - `addr=00`：握手失败后按现有逻辑清空显示地址

### 判断
- 当前主因已经不再是 `PROBE` 的空写假阴性。
- 真实失败点在 `BNO085` 的 `product id` 控制握手阶段，说明芯片仍未进入可正常 SHTP 通信的状态。
- `PID`、`TRACK`、`S` 弯控制都不是当前一号阻塞项。

### 已做的软件修正
- 初始化期加入“无 `INT` 周期轮询首包”兜底，避免 `INT` 门控导致假性卡 `WAIT`。
- 加入 `failCode` 诊断。
- OLED 增补 `FAIL` 显示。
- 串口增补 `#IMU?!`。
- 缩短超时并删除无效地址，避免 IMU 初始化把整机拖成“像死机一样”。
- 去掉对 `BNO08x` 不友好的空写扫描，改成直接 `product id` 握手。
- 首次初始化失败后不再自动重试刷屏。

### 当前最可能的硬件检查项
1. `BNO085` 的 `SCL/SDA` 是否确实接在 `PB12/PB13`，且确有上拉。
2. `ADDR=GND` 是否真实稳定，让模块固定在 `0x4A`。
3. `PS0/PS1=GND` 的模式脚是否真接到位，没有虚焊或被板载默认电路拉偏。
4. `VDD/GND` 与上电时序是否正常。
5. `RST` 是否真的接到 `PB14`，且没有被别的模块占用。

## 2026-04-24 全局关闭 IMU

### 关键证据
- `main.c` 里真正会触发 IMU 的入口只有三类：
  - 启动阶段的 `BNO085_Init()`
  - 周期调度里的 `run_imu_update()`
  - 运行起始时的 `BNO085_ResetAttitude()`
- 即使不初始化，只要 `update_display()` 仍在 `!BNO085_IsReady()` 时显示诊断，OLED 仍会持续出现 IMU 行。
- 板上重新烧录后，串口实测：
  - `#IMU?! -> OK:IMU=0,addr=00,fail=0,ready=0,rx=0,ch=0,rid=0,len=0`
  - `#STAT!` 仍正常返回 `STOP/STRAIGHT`

### 判断
- 对当前目标，最稳妥的方案不是继续修 `BNO085`，而是把 IMU 改成编译期总开关。
- 单改 `main.c` 不够，必须同时给 `sensor_fusion.c` 的 `BNO085_*` 公共接口做禁用桩，才能彻底阻止初始化和自动恢复链。
- 关闭后，`TRACK/STRAIGHT` 仍可运行，只是所有 yaw/gyro 相关量统一回零。

### 本轮修正
- 在 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 增加 `IMU_ENABLE 0`
- 在 [`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.c) 为 `BNO085_Init/ReadAll/UpdateYaw/IsReady/Get*` 增加禁用分支
- 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 切断启动显示、启动初始化、周期更新和 idle OLED 的 IMU 诊断显示
- 烧录时额外确认：当前 `Horco CMSIS-DAP` 需要 `PYOCD_CMSIS_DAP_LIMIT_PACKETS=1` 才稳定
