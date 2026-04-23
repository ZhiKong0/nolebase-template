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
