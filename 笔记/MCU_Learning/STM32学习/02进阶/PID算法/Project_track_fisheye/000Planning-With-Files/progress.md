# Progress Log

## Session: 2026-04-24 单链循迹 PID 重构

### Phase 1: 对齐参考实现
- **Status:** complete
- Actions taken:
  - 读取 [`E:\Download\Refine Line Tracking PID Parameters.md`](E:\Download\Refine%20Line%20Tracking%20PID%20Parameters.md) 的核心要求
  - 对照 [`Project_track_infrared/Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_infrared/Hardware/line_track.h)、[`Project_track_infrared/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_infrared/Hardware/line_track.c)、[`Project_track_infrared/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_infrared/Hardware/config.h)
  - 确认 `Project_track_fisheye` 当前问题不是单个参数，而是整条多梯度控制链和调参面耦合

### Phase 2: 主链重构
- **Status:** complete
- Actions taken:
  - 用单链实现替换 [`Project_track_fisheye/Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h)
  - 用单链实现替换 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 在 `fisheye` 侧收口差异：
    - 移除 `auxBits` 依赖
    - 保留 `dbgScoreEnabled`
    - 保留最小找线状态机

### Phase 3: 参数面与命令口清理
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 删除旧的分层梯度宏
  - 在 [`Project_track_fisheye/User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 将短命令口切换为：
    - `#TDR`
    - `#STB`
    - `#TDB`
    - `#PLF`
    - `#DLF`
    - `#RCT`
    - `#STF`
    - `#STS`
    - `#STO`
  - 在 [`Project_track_fisheye/Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c) 把旧 `CORN` 状态改成 `EDGE`

### Phase 4: 联动脚本收口
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py) 清理旧分层短命令映射
  - 在 [`Project_track_fisheye/000Project_PC_Control/config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml) 把调参阶段与参数表改成单链口径

### Phase 5: 编译验证
- **Status:** complete
- Actions taken:
  - 编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| 单链 PID 重构后编译 | `UV4.exe -b project.uvprojx -j0 -t "Target 1"` | 工程可正常构建 | `0 Error(s), 0 Warning(s)` | pass |

## Current State
- `TRACK` 主链已经切换为单一连续误差 + 单一循迹 PD
- 旧的分层增益和短命令口已经从主实现与脚本参数面中清掉
- 工程当前可编译，后续可以直接围绕单链参数做板上调试

## Session: 2026-04-24 自动调参骨架收口

### Phase 6: 自动调参脚本骨架补齐
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py) 新增恢复评分指标：
    - `edge_recovery_success_ratio`
    - `edge_recovery_mean_s`
    - `search_recovery_success_ratio`
    - `search_recovery_mean_s`
  - 保持“单链 PID + 固定速度 40 档”前提，不把 `speed_target` 纳入本轮搜索维度
  - 在 [`Project_track_fisheye/000Project_PC_Control/config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml) 切成三阶段骨架：
    - `fit`
    - `pid`
    - `recovery`
  - 将恢复链参数纳入参数表：
    - `recover_ticks`
    - `search_turn_fast`
    - `search_turn_slow`
    - `search_timeout`

### Phase 7: 骨架验证
- **Status:** complete
- Actions taken:
  - 用 `py_compile` 验证 [`track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py) 语法通过
  - 用 `yaml.safe_load()` 验证 [`config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml) 结构通过
  - 用 `--help` 验证脚本命令入口可用

## Current State
- 单链 PID 主实现不再改动
- 自动调参链当前处于“骨架完成，可随时实跑”的状态
- 当前没有实际启动参数扫描，符合本轮“只做骨架”的边界

## Session: 2026-04-24 提速与关闭自动停车

### Phase 8: 提速与停机链清理
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 提升默认速度档与弯中基础速度：
    - `PID_TRACK_SPEED_TARGET = 44.0`
    - `TRACK_FOLLOW_BASE_MIN_PWM = 250`
    - `TRACK_DEFAULT_CROSSINGS = 0`
  - 在 [`Project_track_fisheye/Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h) 删除已经失效的 `autoFlag` 字段与标志宏
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 去掉：
    - 交叉计数到点自动停
    - `LineTrack_Update()` 内部因 `autoFlag` 触发的 idle/stop 分支
    - 将 `LineTrack_Start(uint8_t crossings)` 中的 `crossings` 收为兼容占位

### Phase 9: 编译验证
- **Status:** complete
- Actions taken:
  - 编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`

## Session: 2026-04-24 exp266 S 弯限速修正

### Phase 10: 日志定位
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0266_20260424_064949_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0266_20260424_064949_KEY_T.txt)
  - 确认 `SCRV` 本身不是唯一限速源，更大的拖速来自：
    - 起步斜坡偏慢
    - `cornerDone` 后恢复速度目标被压得过低

### Phase 11: S 弯速度恢复调整
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `SPEED_ENTRY = 12.0`
    - `SPEED_RAMP_RATE = 36.0`
    - `TRACK_RESUME_SPEED_MIN = 28.0`
    - `TRACK_RESUME_SPEED_BOOST = 8.0`
    - `TRACK_RESUME_SPEED_MAX = 44.0`
  - 在 [`Project_track_fisheye/User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 将 `cornerDone` 后的恢复目标从“当前速度直接恢复”改成“当前速度 + 恢复抬升，并带最小恢复地板”

### Phase 12: 编译验证
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`

## Session: 2026-04-24 exp271 丢线找线偏慢修正

### Phase 13: 日志复盘与根因收敛
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0271_20260424_065825_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0271_20260424_065825_KEY_T.txt)
  - 确认症状不是单一“主 `P` 偏软”，而是：
    - 丢线确认偏慢
    - pivot 找线 PWM 偏弱
    - 搜索退出太早
    - 回线后再次丢线时方向历史不够黏

### Phase 14: 单链 PID 与找线恢复增强
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 提高：
    - `PID_TRACK_LINE_KP = 11.0`
    - `TRACK_FOLLOW_DEV_RATIO = 0.58`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 34`
  - 同时加快找线进入与 pivot 力度：
    - `TRACK_LOST_CONFIRM_TICKS = 2`
    - `TRACK_LOST_FAST_CONFIRM_TICKS = 1`
    - `TRACK_SEARCH_BLIND_TICKS = 0`
    - `TRACK_SEARCH_ARC_PWM_FAST/SLOW = 240/160`
    - `TRACK_SEARCH_TURN_PWM_FAST/SLOW = 290/180`
    - `TRACK_RECOVER_TICKS = 10`
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 增加恢复窗口方向黏性
  - 将 `track_search_exit_ready()` 收紧为“重新进入中心带再退出搜索”，避免边灯一闪就过早交还给跟线主链

### Phase 15: 编译验证
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`

## Session: 2026-04-24 exp277 主 P 与找线退出继续增强

### Phase 16: exp277 日志定位
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0277_20260424_070545_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0277_20260424_070545_KEY_T.txt)
  - 确认 `exp277` 的主问题有两条：
    - `EDGE` 区回中力度仍偏软
    - 搜索退出条件“只认中心带”导致恢复偏慢

### Phase 17: 单链 P 与搜索退出门槛修正
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `PID_TRACK_LINE_KP = 12.2`
    - `TRACK_FOLLOW_DEV_RATIO = 0.62`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 38`
    - `TRACK_SEARCH_TURN_PWM_FAST/SLOW = 320/200`
    - `TRACK_RECOVER_TICKS = 12`
    - `TRACK_SEARCH_SIDE_EXIT_TICKS = 2`
  - 在 [`Project_track_fisheye/Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h) 增加 `searchSeenTicks`
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 将搜索退出收成：
    - 中心/内侧/交叉：立即退出
    - 同侧外缘：稳定 2 拍后退出
    - 其余：继续 pivot 搜索

### Phase 18: 编译与烧录
- **Status:** complete
- Actions taken:
  - 编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 产物时间戳校验通过，`project.hex` 晚于热文件源码
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`
  - 板上串口参数回读尝试失败，原因是 `COM18` 被其他串口进程占用；烧录链本身成功

## Session: 2026-04-24 exp284/285 根因回退修正

## Session: 2026-04-24 12路输入链重构

### Phase 36: 固件输入链升级到12路
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 将 `LINE_SENSOR_COUNT` 扩成 `12`
  - 增加 4 路直连输入定义：
    - `A1 -> PA12`
    - `A2 -> PB11`
    - `A11 -> PB15`
    - `A12 -> PC13`
  - 保留 `74HC4051` 扫描 `A3~A10`
  - 在 [`Project_track_fisheye/Hardware/sensor_fusion.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.h) 将 `bits` 升为 `uint16_t`
  - 在 [`Project_track_fisheye/Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.c) 重构 `LineSensor_Read()`：
    - 先读 `A1/A2`
    - 再扫 `A3~A10`
    - 最后读 `A11/A12`
    - 最终合成为 `bit0~bit11 = A1~A12`
  - 在 [`Project_track_fisheye/Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h)、[`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 将位宽、掩码、位置表、找线与交叉逻辑统一升级到 12 位
  - 在 [`Project_track_fisheye/Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c) 和 [`Project_track_fisheye/User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 同步升级遥测与 `TS1..TS12` 命令
  - 在 [`Project_track_fisheye/000/接线总表——END.md`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000/接线总表——END.md) 写明 12 路接线与位序

### Phase 37: PC侧工具链同步与板上验证
- **Status:** in progress
- Actions taken:
  - 在 [`Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py) 更新：
    - `LT_MASK_CENTER = 0x0060`
    - `LT_MASK_OUTER = 0x0F0F`
    - `LT_CENTER_CORE_STATES = (0x0020, 0x0040, 0x0060)`
    - `track.sensor_scale1..12` 命令映射与回读
  - 在 [`Project_track_fisheye/000Project_PC_Control/config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml) 更新：
    - `line_sensor.positions_m` 扩成 12 项
    - `fit` 和 `recovery` 阶段参数面扩成 12 路
    - `sensor_scale9..12` 参数定义补齐
  - 完成 PC 侧自检：
    - `py_compile ok`
    - `yaml ok ... 12`
    - `track_adaptive_tuner.py --help` 正常
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)

## Session: 2026-04-24 exp404 不降速抓线增强

### Phase 39: FOLLOW/RECOVER 同向抓线增强
- **Status:** complete
- Actions taken:
  - 复盘 [`exp_0404_20260424_091906_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0404_20260424_091906_KEY_T.txt)，确认主因不是速度太低，而是 `FOLLOW/RECOVER` 同向抓线在大偏差时过平
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 新增 `track_build_guidance_severity()`，让同向抓线按 `abs(linePos)` 连续增强
  - 将 `track_is_turnin_follow_case()` 的强化起点前移到“已经越过 `SMALL_MAX` 且同侧外缘明显占优”
  - 将 `track_apply_follow_guidance()` 从固定抓线下限改为随偏差插值增强：
    - `FOLLOW`: `ratio 0.44 -> 0.58`, `min 80 -> 124`
    - `RECOVER`: `ratio 0.52 -> 0.62`, `min 96 -> 136`
  - 保持 `PID_TRACK_SPEED_TARGET=50.0`、`TRACK_FOLLOW_BASE_MIN_PWM=250` 不变，不用降速换稳定

### Phase 40: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 校验 `project.hex` 时间戳晚于最新源码
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 exp422 直线稳定性回收

### Phase 41: 根因定位
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0422_20260424_093105_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0422_20260424_093105_KEY_T.txt)
  - 确认直线失稳不是速度档问题，而是 `RECOVER` 沿旧 `recoverDir` 握得过久
  - 关键证据是多段 `TRMR/TRML` 已经重见中心带附近位型，但输出仍持续沿旧方向大幅单边拉扯，随后自己打到 `sb=0` 再掉回搜索

### Phase 42: 恢复窗口放手条件修正
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 新增 `track_should_hold_recover_guidance()`
  - 将恢复窗口统一改成：
    - 线已回到中心锁定：立即清掉 `recoverDir/recoverTicks`
    - 线已偏到 `recoverDir` 反侧，且进入 `CENTER_BAND` 或 `abs(linePos) <= MEDIUM_MAX`：立即放手交还普通 `PD`
    - 只有仍明显处在原恢复方向一侧时，才继续沿旧方向收回
  - `track_apply_follow_guidance()` 与 `track_signal_update()` 共用同一判定，避免状态放手和控制放手不一致
  - 保持 `exp404` 引入的 `FOLLOW` 大偏差抓线增强不变，不降速度、不压基础 PWM

### Phase 43: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 校验 `project.hex` 时间戳晚于最新源码
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 exp432 单链误差整形

### Phase 44: 根因定位
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0432_20260424_094239_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0432_20260424_094239_KEY_T.txt)
  - 确认症状不是单一“`P` 太大或太小”，而是同一条线性误差映射同时带来：
    - 直线/近中心区略敏感，容易左右轻摆
    - `S` 弯外侧区又不够硬，抓线偏拖
  - 判断最合适切口不是直接改 `KP`，而是保留单链结构，重做 `linePos -> error` 的连续整形

### Phase 45: 单链误差映射整形
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 新增 `track_shape_follow_error()`
  - 将 `track_build_follow_error()` 改成：
    - 先 `deadband`
    - 再按连续三段比例整形
    - 最后统一除以 `errorScale`
  - 整形尺度为：
    - `<= SMALL_MAX`: `0.78`
    - `SMALL_MAX ~ MEDIUM_MAX`: 连续过渡到 `1.00`
    - `MEDIUM_MAX ~ LARGE_MAX`: 连续过渡到 `1.18`
  - 保持速度档、基础 PWM、`FOLLOW/RECOVER` 抓线增强链不变，不回退之前已验证有效的高速抓线逻辑

### Phase 46: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 校验 `project.hex` 时间戳晚于最新源码
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`
  - 串口烟测尝试打开 `COM18` 失败，错误为 `PermissionError(13, '拒绝访问。')`，说明当前端口被外部进程占用，待释放后再补 `#STAT! / #TS12?!` 回读

### Phase 19: 日志根因定位
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0284_20260424_071315_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0284_20260424_071315_KEY_T.txt)
  - 读取 [`exp_0285_20260424_071323_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0285_20260424_071323_KEY_T.txt)
  - 确认两轮共同根因不是单纯 `P` 太大，而是：
    - 丢线确认门槛过低，短暂 `sb=0` 过快进搜索
    - `recoverDir` 最高优先级导致方向判错
    - 搜索退出只认同侧/中心，造成错误方向搜索锁死

### Phase 20: 触发链回退与收敛
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 回调：
    - `PID_TRACK_LINE_KP = 11.4`
    - `TRACK_FOLLOW_DEV_RATIO = 0.58`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 34`
    - `TRACK_LOST_CONFIRM_TICKS = 3`
    - `TRACK_LOST_FAST_CONFIRM_TICKS = 2`
    - `TRACK_SEARCH_TURN_PWM_FAST/SLOW = 300/190`
    - `TRACK_RECOVER_TICKS = 10`
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)：
    - 降低 `recoverDir` 优先级
    - 将搜索重获线改成“中心/内侧立即退出、任一外侧稳定两拍退出”

### Phase 21: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 产物时间戳校验通过
  - 使用 `pyOCD` 顺序完成：
    - `erase --chip`
    - `load project.hex`
    - `reset`

## Session: 2026-04-24 exp287 主循迹力度增强

### Phase 22: exp287 日志复核
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0287_20260424_072003_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0287_20260424_072003_KEY_T.txt)
  - 确认普通 `SCRV` 区间 `lp≈55、bd=±1` 时表现正常
  - 确认真正“跟线不积极”的区间是 `EDGE`：`lp≈191、bd=±4` 时输出差速仍偏小
  - 判断这轮应优先增强单链主循环，不动刚修稳的搜索方向逻辑

### Phase 23: 主链参数强化
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `PID_TRACK_LINE_KP = 12.6`
    - `TRACK_FOLLOW_ERROR_SCALE = 72.0`
    - `TRACK_FOLLOW_DEV_RATIO = 0.62`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 40`
  - 保持 `TRACK_LOST_CONFIRM_TICKS / FAST_CONFIRM_TICKS / SEARCH_* / RECOVER_*` 不变，避免重新引入 `exp284/285` 的误入搜索问题

### Phase 24: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 exp289 抓线力继续增强

### Phase 25: exp289 日志复核
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0289_20260424_073012_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0289_20260424_073012_KEY_T.txt)
  - 确认普通 `SCRV` 区间并不差，真正“抓线力弱”的区间在 `EDGE/TRMR`
  - 确认 `EDGE` 时 `dev_ratio` 尚未成为主限制，主限制是 `kp + deadband + errorScale`

### Phase 26: 单链主 PD 再强化
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `PID_TRACK_LINE_KP = 13.8`
    - `TRACK_FOLLOW_DEADBAND = 6.0`
    - `TRACK_FOLLOW_ERROR_SCALE = 64.0`
    - `TRACK_FOLLOW_DEV_RATIO = 0.64`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 44`
  - 搜索链参数保持不变，继续避免把 `exp284/285` 的问题带回

### Phase 27: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 exp293 主循迹与回正找线联调

### Phase 28: exp293 根因拆分
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0293_20260424_073421_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0293_20260424_073421_KEY_T.txt)
  - 确认主循迹问题仍在 `EDGE/TRMR`：`lp≈191、bd=±4` 时差速输出仍偏小
  - 确认找线“回正慢”不是方向选错，而是同侧重获线退出链被 `2` 拍门槛拖慢

### Phase 29: 主链与找线回正同步修正
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `PID_TRACK_LINE_KP = 15.0`
    - `TRACK_FOLLOW_DEADBAND = 4.0`
    - `TRACK_FOLLOW_ERROR_SCALE = 58.0`
    - `TRACK_FOLLOW_DEV_RATIO = 0.66`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 48`
    - `TRACK_SEARCH_TURN_PWM_FAST/SLOW = 320/210`
    - `TRACK_SEARCH_SIDE_EXIT_TICKS = 1`
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 重构 `track_search_reacquire_class()`：
    - 同侧外缘才计作可退出
    - 反侧外缘不计退出
    - 中心/内侧/交叉仍立即退出

### Phase 30: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 exp305 主循迹响应提速

### Phase 31: exp305 日志复核
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0305_20260424_074215_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0305_20260424_074215_KEY_T.txt)
  - 确认用户抱怨的“跟线不够及时”主要出现在 `EDGE/TRMR` 段
  - 确认主因是单链主循迹响应迟滞，而不是搜索方向链本身

### Phase 32: 单链响应提速
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `PID_TRACK_LINE_KP = 16.2`
    - `TRACK_LINE_POS_STEP = 60`
    - `TRACK_FOLLOW_POS_LPF_ALPHA = 0.74`
    - `TRACK_FOLLOW_D_LPF_ALPHA = 0.46`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT = 60`
    - `TRACK_SEARCH_TURN_PWM_FAST/SLOW = 340/220`
  - 保持 `TRACK_FOLLOW_DEV_RATIO`、搜索确认、搜索方向和搜索退出条件不变，避免把前一轮已修好的搜索链带坏

### Phase 33: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 找线自转提速

### Phase 34: 搜索自转参数复核
- **Status:** complete
- Actions taken:
  - 核对 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 中找线参数
  - 核对 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 的 `track_drive_search()`，确认用户抱怨对应 `PIVOT` 分支
  - 确认本轮边界是“只加快自转找线”，不碰主循迹和丢线判定

### Phase 35: 自转找线提速
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 调整：
    - `TRACK_SEARCH_TURN_PWM_FAST = 380`
    - `TRACK_SEARCH_TURN_PWM_SLOW = 250`
  - 保持 `TRACK_SEARCH_ARC_PWM_*`、主循迹参数、丢线确认参数不变

### Phase 36: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`

## Session: 2026-04-24 回退 ddc2eb7 主循迹基线

### Phase 37: 差异确认与定向回退
- **Status:** complete
- Actions taken:
  - 确认用户要求回到 `ddc2eb7 track: refactor 12-route turn-in and recovery chain`
  - 对比 `ddc2eb7` 到当前工程的差异范围，确认功能行为需要回退的核心文件是 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 使用定向 `git restore --source ddc2eb7` 恢复该文件，不影响其他工程目录

### Phase 38: 编译与烧录回退版固件
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 首次 `pyOCD erase` 失败，原因是本机同时有两个 `UV4` 进程占用调试探针
  - 关闭占用的 `UV4` 进程后，重新执行并完成：
    - `pyocd list --probes`
    - `pyocd erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -vv`
    - `pyocd load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex -vv`
    - `pyocd reset --no-config -t stm32f103rc -u 031305620164 -vv`
  - 当前板上固件已切回 `ddc2eb7` 对应的 `line_track.c` 主循迹实现

## Session: 2026-04-24 定位 12 路 git 版本

### Phase 39: 12 路历史定位
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye) 上执行 git 历史检索：
    - `git log --grep '12-route|12路'`
    - `git log -S 'LINE_SENSOR_COUNT 12'`
    - `git log -S 'TRACK_LINE_POS_S9'`
  - 结论：`12` 路版本的明确起点提交是：
    - `ddc2eb7 track: refactor 12-route turn-in and recovery chain`
  - 同时核对当前工作树，确认目前仍保留：
    - `LINE_SENSOR_COUNT = 12`
    - `TRACK_LINE_POS_S9 ~ S12`
    - `uint16_t bits`
  - 进一步确认后续 12 路演化提交包括：
    - `c20f11a`
    - `32394d0`
    - `d8d4556`
    - `b422998`
  - 其中 `b422998` 的含义是：
    - 将 `line_track.c` 回退到 `ddc2eb7` 行为基线
    - 但不移除 12 路基础结构

## Session: 2026-04-24 串口输出 12 路数字电平

### Phase 40: 遥测链定位
- **Status:** complete
- Actions taken:
  - 确认实验记录器只负责原样落盘串口文本，不需要单独改 logger
  - 确认 `HB:` 主组包位置在 [`Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c) 的 `BspUart_SendTelemetryTrack()`
  - 确认当前已有 `sb=<decimal sensorBits>`，但对 12 路电平可读性不足

### Phase 41: 新增 12 路电平字段
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c) 新增 `track_sensor_bits_to_text()`
  - 在 `HB:` 中新增：
    - `sbh=0x%03X`
    - `s12=<S1~S12 的 12 位数字电平串>`
  - 保留 `sb=` 不变，避免现有 Python 脚本失效

### Phase 42: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)

## Session: 2026-04-24 起跑一致性复测链修正

### Phase 45: 评分窗口修正
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/000Project_PC_Control/experiment_score_watch.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.py) 新增 `--analysis-start-ms`
  - 默认将评分起点设为 `400ms`，跳过起跑瞬态
  - 重新对 [`exp_0474_20260424_185148_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0474_20260424_185148_UART_T.txt) 做基线重评，得到：
    - `total_score = 89.53`
    - `a67_cover_ratio = 99.18%`
    - `a67_exact_ratio = 81.47%`

### Phase 46: 串口恢复与最小链验证
- **Status:** complete
- Actions taken:
  - 发现 `COM18` 无输出时，执行 `pyocd reset --no-config -t stm32f103rc -u 031305620164` 可恢复板端空闲心跳和命令响应
  - 复位后使用 `#LINE?!` 成功读回：
    - `sbh=0x0C0, s12=000000110000, lp=0.5`
  - 证明“复位 -> 读线”这条起跑前检查链可用

### Phase 47: 8秒实验重跑尝试
- **Status:** in progress
- Actions taken:
  - 复位后成功下发：
    - `#MODE=TRACK!`
    - `#RUN!`
  - 但本轮抓到的 [`exp_capture_20260424_194051_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_capture_20260424_194051_UART_T.txt) 是中途附着到已运行实验上的，不含完整 `EVT:EXP_START` 前段
  - 该文件仅可用于现象观察，不能用于和 `exp474` 严格比较

## Current State
- 当前最可信最优参数仍是 `exp474` 那组
- 下一轮复测前，必须先完成：
  - `pyocd reset`
  - `#LINE?!` 起跑确认
  - 从 `EVT:EXP_START` 前完整落盘
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -vv`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex -vv`
    - `reset --no-config -t stm32f103rc -u 031305620164 -vv`
  - 本轮未主动发 `#RUN!` 做动态串口实验，避免擅自启动小车跑动

## Session: 2026-04-24 exp447 S 弯抓线力分析

### Phase 43: 读取日志与主链参数
- **Status:** complete
- Actions taken:
  - 检查 [`exp_0447_20260424_102815_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0447_20260424_102815_KEY_T.txt) 的 `SCRV/EDGE/FNDR` 段
  - 交叉核对 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 中单链 `FOLLOW` 参数
  - 交叉核对 [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 中主误差、PD 输出、同向导向补强路径

### Phase 44: 提炼最有效变量
- **Status:** complete
- Actions taken:
  - 判断 `S` 弯抓线力最直接受这些量影响：
    - `PID_TRACK_LINE_KP`
    - `TRACK_FOLLOW_ERROR_SCALE`
    - `TRACK_FOLLOW_DEV_RATIO`
    - `TRACK_FOLLOW_DEV_STEP_LIMIT`
  - 判断次一级影响量：
    - `TRACK_FOLLOW_DEADBAND`
    - `TRACK_FOLLOW_POS_LPF_ALPHA`
    - `TRACK_FOLLOW_D_LPF_ALPHA`
  - 判断搜索链参数不是 `S` 弯本体抓线力的第一优先项

## Session: 2026-04-24 统一串口调参接口

### Phase 45: 识别 owner 与耦合点
- **Status:** complete
- Actions taken:
  - 确认当前 `#TCFG` 只覆盖了部分参数，且 `main.c` 仍直接写 `g_pid`
  - 确认 `line_track.c` 已有一套局部 `ParamSet/Get/List`
  - 识别出主耦合点是：
    - `UART -> g_pid`
    - `UART -> g_lineTrackCfg`

### Phase 46: DualLoop 参数 owner 抽取
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/pid_controller.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/pid_controller.h) / [`Hardware/pid_controller.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/pid_controller.c) 新增：
    - `g_dualLoopCfg`
    - `DualLoop_ResetRuntimeConfig()`
    - `DualLoop_ParamSet/Get/List()`
  - 将这些值收归 `DualLoop` owner：
    - `straight.speed_*`
    - `straight.heading_*`
    - `track.speed_*`
    - `heading.trim`
    - `heading.integral_*`
    - `system.speed_*`
    - `system.pid_deriv_lpf`
  - 让 `DualLoop_Load*Defaults()` 与速度斜坡/前馈改用运行时配置

### Phase 47: LineTrack 参数表扩展
- **Status:** complete
- Actions taken:
  - 扩展 [`Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h) 的 `LineTrack_RuntimeConfig_t`
  - 在 [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 将以下量迁入运行时配置：
    - 位置步长与 trim
    - `center/small/medium/large` 阈值
    - `deadband/errorScale/pos_lpf/d_lpf/dev_ratio/dev_step_limit/base_min_pwm`
    - `lost/search/recover/cross` 相关门槛
    - `follow/recover turnin ratio/min`
    - `resume_speed_*`
  - 将 `LineTrack_ParamSet/Get/List()` 重构为参数表驱动
  - 新增 `track_normalize_runtime_config()` 做阈值与时序归一

### Phase 48: UART 分发收口
- **Status:** complete
- Actions taken:
  - 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 新增：
    - `runtime_param_get()`
    - `runtime_param_set()`
    - `runtime_param_list()`
    - `runtime_load_defaults()`
  - `#TCFG GET/SET/LIST/LOAD_DEFAULTS` 改为只走 owner 接口
  - 旧命令 `#SPD/#SKP/#AKP/#LKP/#SFF/#HTR...` 保留，但只作为 key alias

### Phase 49: 编译烧录与串口烟测
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 按顺序完成：
    - `pyocd list --probes`
    - `pyocd erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `pyocd load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `pyocd reset --no-config -t stm32f103rc -u 031305620164`
  - 串口烟测尝试打开 `COM18` 时返回 `PermissionError(13)`，本轮无法读取 `#TCFG` 回包

## Session: 2026-04-24 实验日志并行评分脚本

### Phase 50: 梳理日志输入与监听边界
- **Status:** complete
- Actions taken:
  - 阅读 [`000Project_PC_Control/experiment_logger.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_logger.py)
  - 确认新脚本不应抢占 `COM18`
  - 确认新脚本应以 `EVT:EXP_STOP` 作为实验文件完成信号

### Phase 51: 设计评分指标与建议规则
- **Status:** complete
- Actions taken:
  - 定义 3 个主指标：
    - 抓线性
    - 速度平滑性
    - `A6/A7` 覆盖得分
  - 设计恢复事件统计：
    - `EDGE -> 中心`
    - `SEARCH -> 中心`
  - 设计参数建议规则：
    - `track.follow_turnin_ratio`
    - `track.lkp`
    - `track.error_scale`
    - `track.search_turn_fast`
    - `track.lkd`
    - `track.dev_step_limit`

### Phase 52: 实现并行评分脚本
- **Status:** complete
- Actions taken:
  - 新增 [`000Project_PC_Control/experiment_score_watch.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.py)
  - 新增 [`000Project_PC_Control/experiment_score_watch.ps1`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.ps1)
  - 输出：
    - `*_score.json`
    - `*_score.md`
    - `leaderboard.csv`
    - `latest_report.md`
    - `watch_state.json`

### Phase 53: 单文件验证
- **Status:** complete
- Actions taken:
  - 用 `py_compile` 验证新脚本语法
  - 用 `exp_0447_20260424_102815_KEY_T.txt` 做 `--once` 分析
  - 验证脚本能输出：
    - 总分
    - 抓线性
    - 速度平滑性
    - `A6/A7` 覆盖比率
    - 下一步首选/备选参数建议

## Session: 2026-04-24 8秒复测与微调复核

### Phase 54: 恢复板上最优参数并回读
- **Status:** complete
- Actions taken:
  - 用紧凑 `#TCFG=...,...!` 重新下发上一轮最优参数组
  - 对 `track.lkp / lkd / follow_turnin_ratio / follow_turnin_min / error_scale / dev_ratio / dev_step_limit / search_turn_fast / search_turn_slow / static_bias` 做回读
  - 确认板上当前值与目标值一致

### Phase 55: 运行 `exp476` 单轮 8 秒复测
- **Status:** complete
- Actions taken:
  - 通过 `experiment_logger.py --uart-test-seconds 8 --uart-mode TRACK --max-seconds 14` 启动实验
  - 生成 [`exp_0476_20260424_185531_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0476_20260424_185531_UART_T.txt)
  - 用 `experiment_score_watch.py --once --print-json` 评分：
    - `total_score = 45.54`
    - `a67_cover_ratio = 48.68%`
    - `search_ratio = 33.75%`
    - `loss_ratio = 31.25%`

### Phase 56: 连跑 `exp477~exp479` 做小范围微调对比
- **Status:** complete
- Actions taken:
  - 跑 3 组候选：
    - `baseline_repeat`
    - `ratio045`
    - `lkp168`
  - 生成：
    - [`exp_0477_20260424_185646_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0477_20260424_185646_UART_T.txt)
    - [`exp_0478_20260424_185707_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0478_20260424_185707_UART_T.txt)
    - [`exp_0479_20260424_185727_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0479_20260424_185727_UART_T.txt)
  - 评分结果：
    - `exp477 = 37.87`
    - `exp478 = 47.62`
    - `exp479 = 36.94`

### Phase 57: 确认当前自动复测失真来源
- **Status:** complete
- Actions taken:
  - 对比 `exp474 / exp476 / exp477` 的起跑前几拍 `HB`
  - 确认三轮起跑位型和 `linePos` 明显不同：
    - `exp474 @20ms: sbh=0x0C0, lp=47`
    - `exp476 @20ms: sbh=0x170, lp=-28`
    - `exp477 @20ms: sbh=0x030, lp=-47`
  - 结论：连续 `UART` 自动实验没有把车带回同一起跑姿态，导致后续评分不再可横向比较

## Session: 2026-04-24 ALIGN 预对中链

### Phase 58: 增加 `#ALIGN!` 串口链
- **Status:** complete
- Actions taken:
  - 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 增加：
    - `AlignState_t`
    - `align_start() / run_align() / align_finish()`
    - `#ALIGN!` 命令入口
  - 在 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 增加 `ALIGN_*` 参数
  - 行为边界：
    - `STOP` 态可运行
    - 不耦合 `LineTrack` 主链
    - 通过 `EVT:ALIGN,START/DONE/FAIL` 汇报结果

### Phase 59: 编译与低速 `pyOCD` 烧录链修复
- **Status:** complete
- Actions taken:
  - 重新编译，构建日志仍为 `0 Error(s), 0 Warning(s)`
  - 发现 `CMSIS-DAP` 在 `10MHz` 下载阶段不稳定
  - 按 [`000/mcu-build-flash.md`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000/mcu-build-flash.md) 改为：
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 100000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 100000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`
  - 低速烧录链已连续成功

### Phase 60: 实际板测 `ALIGN`
- **Status:** complete
- Actions taken:
  - 原地自转版 `ALIGN`：
    - 能启动
    - 但会翻向并 `FAIL`
  - 低速前进对中版 `ALIGN`：
    - 不再明显翻向
    - 但会丢线后 `FAIL`
  - 再补“无信号默认单向搜线”后：
    - 连续两次结果分别为：
      - `FAIL, sb=0x0030, lp=-0.5`
      - `FAIL, sb=0x0700, lp=1.8`

### Phase 61: 当前结论
- **Status:** complete
- Actions taken:
  - 确认 `ALIGN` 目前还没收敛到可重复的统一起跑姿态
  - 因此尚不能作为后续 `8s` 自动调参的稳定前置步骤

## Session: 2026-04-24 人工摆正后继续 8 秒调参

### Phase 62: 修正评分窗口并恢复 8 秒完整采集
- **Status:** complete
- Actions taken:
  - 将 [`experiment_score_watch.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.py) 改成默认从 `400ms` 后开始评分
  - 重新核算 [`exp_0474_20260424_185148_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0474_20260424_185148_UART_T.txt)
  - 确认 `exp474` 在新窗口下仍是当前最可信基线，得分约 `89.53`

### Phase 63: 串口恢复与完整 8 秒复测链打通
- **Status:** complete
- Actions taken:
  - 用 `pyocd reset --no-config -t stm32f103rc -f 10000000 -u 031305620164` 恢复板端串口
  - 确认 `#STAT!`、`#MODE=TRACK!`、`#LKP=...!`、`#LKD=...!`、`#TDR=...!`、`#STF=...!`、`#STS=...!`、`#STB=...!` 可稳定回包
  - 改用“单串口会话内下参 + 开跑 + 落盘 + 停车”的方式采集，不再依赖并行 logger

### Phase 64: 5 组候选参数对比
- **Status:** complete
- Actions taken:
  - 跑出 5 组完整 `8s` 日志并评分：
    - [`exp_auto_20260424_195535_cand_a.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195535_cand_a.txt)
      - `LKP=17.0, LKD=7.2, TDR=0.70, STF/STS=420/260, STB=-8`
      - `score=35.93`
    - [`exp_auto_20260424_195559_cand_b.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195559_cand_b.txt)
      - `LKP=17.6, LKD=7.6, TDR=0.72, STF/STS=430/270, STB=-8`
      - `score=36.17`
    - [`exp_auto_20260424_195753_cand_c_stb_pos8.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195753_cand_c_stb_pos8.txt)
      - `STB=+8`
      - `score=35.44`
    - [`exp_auto_20260424_195818_cand_d_stb_pos16.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195818_cand_d_stb_pos16.txt)
      - `STB=+16`
      - `score=28.13`
    - [`exp_auto_20260424_195920_cand_e_search440.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195920_cand_e_search440.txt)
      - `STF/STS=440/280`
      - `score=31.01`
    - [`exp_auto_20260424_195944_cand_f_search440_p168.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195944_cand_f_search440_p168.txt)
      - `LKP=16.8, LKD=6.8, TDR=0.68, STF/STS=440/280, STB=-8`
      - `score=37.76`
    - [`exp_auto_20260424_200101_cand_g_fine.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200101_cand_g_fine.txt)
      - `LKP=17.0, LKD=7.0, TDR=0.69, STF/STS=450/290, STB=-8`
      - `score=35.95`
  - [`exp_auto_20260424_200128_cand_h_recover8.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200128_cand_h_recover8.txt) 采集未完整收到 `EVT:EXP_STOP`，标记为 `capture_failed`

### Phase 65: 当前板上参数回写
- **Status:** complete
- Actions taken:
  - 将当前“近期候选中最好的一组”重新写回板子：
    - `LKP=16.8`
    - `LKD=6.8`
    - `TDR=0.68`
    - `STF=440`
    - `STS=280`
    - `STB=-8`

### Phase 66: 稳定性筛选复测
- **Status:** complete
- Actions taken:
  - 对 3 组候选做多轮 `8s` 稳定性复测：
    - `base474 = 16.2 / 6.8 / 0.66 / 400 / 240 / -8`
    - `mid166 = 16.6 / 6.8 / 0.67 / 430 / 270 / -8`
    - `cand_f  = 16.8 / 6.8 / 0.68 / 440 / 280 / -8`
  - 结果：
    - `base474`
      - [`exp_auto_20260424_200743_base474_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200743_base474_r1.txt)
      - [`exp_auto_20260424_201057_base474_r3.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201057_base474_r3.txt)
      - `avg=39.49, std=0.29`
    - `mid166`
      - [`exp_auto_20260424_200842_mid166_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200842_mid166_r1.txt)
      - [`exp_auto_20260424_200906_mid166_r2.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200906_mid166_r2.txt)
      - [`exp_auto_20260424_201122_mid166_r3.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201122_mid166_r3.txt)
      - `avg=36.51, std=4.40`
    - `cand_f`
      - [`exp_auto_20260424_200931_cand_f_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200931_cand_f_r1.txt)
      - [`exp_auto_20260424_200955_cand_f_r2.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_200955_cand_f_r2.txt)
      - [`exp_auto_20260424_201146_cand_f_r3.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201146_cand_f_r3.txt)
      - `avg=36.79, std=0.82`
  - 结论：
    - 当前“最好且最稳”的仍是 `base474`
    - 已将板上参数重新写回 `base474`

### Phase 67: 稳定窗口二次确认
- **Status:** complete
- Actions taken:
  - 在 `base474` 周围补做第二轮窄窗口复测，候选为：
    - `base474_s = 16.2 / 6.8 / 0.66 / 400 / 240 / -8`
    - `soft_follow = 15.9 / 6.6 / 0.65 / 400 / 240 / -8`
    - `search_tamed = 16.2 / 6.8 / 0.66 / 390 / 230 / -8`
    - `balanced = 16.0 / 6.8 / 0.65 / 410 / 250 / -8`
  - 完整 `8s` 结果：
    - `base474_s`
      - [`exp_auto_20260424_201716_base474_s_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201716_base474_s_r1.txt)
      - [`exp_auto_20260424_201741_base474_s_r2.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201741_base474_s_r2.txt)
      - `avg_total = 37.73`
      - `std_total = 1.69`
    - `soft_follow`
      - [`exp_auto_20260424_201805_soft_follow_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201805_soft_follow_r1.txt)
      - [`exp_auto_20260424_201830_soft_follow_r2.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201830_soft_follow_r2.txt)
      - `avg_total = 32.00`
      - `std_total = 0.11`
    - `search_tamed`
      - [`exp_auto_20260424_201854_search_tamed_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201854_search_tamed_r1.txt)
      - [`exp_auto_20260424_201919_search_tamed_r2.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201919_search_tamed_r2.txt)
      - `avg_total = 32.92`
      - `std_total = 0.59`
    - `balanced`
      - [`exp_auto_20260424_201943_balanced_r1.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_201943_balanced_r1.txt)
      - [`exp_auto_20260424_202007_balanced_r2.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_202007_balanced_r2.txt)
      - `avg_total = 31.04`
      - `std_total = 3.95`
  - 结论：
    - 第二轮稳定窗口没有出现超越 `base474` 的组合
    - “更软一点”只能换来低波动，换不来更高总分
    - 单独收 `search_turn` 也没有显著提升稳定性
  - 板上已再次恢复并串口回读确认：
    - `LKP=16.2`
    - `LKD=6.8`
    - `TDR=0.66`
    - `STF=400`
    - `STS=240`
    - `STB=-8`

## Session: 2026-04-24 保线优先抓线修正

### Phase 68: 根因复核
- **Status:** complete
- Actions taken:
  - 对照最近几轮稳定性复测日志，重点检查丢线前的 `lp / sbh / st`
  - 确认反复出现的模式是：
    - 先在单侧外侧可见区长期停留于 `lp≈-50` 或对称右侧
    - 然后一拍 `bits==0`
    - 再很快掉进 `FNDL/FNDR`
  - 对照 [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 确认两条主因：
    - `track_is_turnin_follow_case()` 以前固定要到 `mediumPosMax` 才介入同向强化
    - `track_build_follow_error()` 在 `bits==0` 时直接返回 `0`

### Phase 69: 保线优先逻辑落地
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 新增 `track_follow_turnin_threshold()`
  - 将同向强化阈值改成动态：
    - 看到外侧灯或中心灯已缺失时，前移到 `centerPosMax` 与 `smallPosMax` 之间的过渡阈值
    - 其余情况保留 `mediumPosMax`
  - 修改 `track_build_follow_error()`：
    - `bits==0` 但尚未正式进入 `SEARCH` 时，不再把误差清零
    - 继续沿当前 `linePos` 维持抓线方向，并按 `overrunCount / confirmTicks` 轻度增强
  - 第二次加固瞬时失线续抓：
    - 新增 `track_pick_loss_hold_dir()`
    - `track_apply_follow_guidance()` 在 `bits==0` 时直接沿最近有效方向套用恢复级别的同向地板
    - 避免瞬时失线那一拍只靠 `PD` 自己慢慢回抓

### Phase 70: 编译、烧录与烟测
- **Status:** complete
- Actions taken:
  - 使用 `D:\\keil\\Keil-v5\\Arm\\UV4\\UV4.exe` 重新编译 [`project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 确认：
    - `0 Error(s), 0 Warning(s)`
  - 按 `10MHz` 顺序完成：
    - `pyocd list --probes`
    - `pyocd erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `pyocd load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `pyocd reset --no-config -t stm32f103rc -u 031305620164`
  - 串口最小烟测：
    - `#STAT!` 成功返回
    - 固件在线，`lkp=16.2000, lkd=6.8000`
