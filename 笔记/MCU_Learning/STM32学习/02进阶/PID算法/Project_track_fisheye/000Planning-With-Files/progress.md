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
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`
  - 串口烟测尝试打开 `COM18` 失败，错误为 `PermissionError(13, '拒绝访问。')`，说明当前端口被外部进程占用，待释放后再补 `#STAT! / #TS12?!` 回读

## Session: 2026-04-24 转角链重构为 FOLLOW 强化 + SEARCH 兜底 + RECOVER 收回

### Phase 38: 控制逻辑重构
- **Status:** complete
- Actions taken:
  - 在 [`Project_track_fisheye/Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 新增：
    - `track_pick_visible_dir()`
    - `track_is_turnin_follow_case()`
    - `track_apply_directional_floor()`
    - `track_apply_follow_guidance()`
  - 将 `FOLLOW` 改成两档：
    - 小偏差：仍走普通单链 `PD`
    - 大偏差且同侧明显占优：在 `PD` 后施加同向强化下限
  - 将 `RECOVER` 改成显式“同向收回”：
    - 搜索退出后记录 `recoverDir`
    - 在恢复窗口里优先沿 `recoverDir` 继续收回
    - 重新回到中心后才清掉 `recoverDir/recoverTicks`
  - 将 `SEARCH` 收成单向 `pivot`：
    - 只在 `bits==0` 时进入
    - 不再保留 `ARC -> PIVOT` 切换链
  - 在 [`Project_track_fisheye/Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 删除：
    - `TRACK_SEARCH_ARC_TICKS`
    - `TRACK_SEARCH_ARC_PWM_FAST`
    - `TRACK_SEARCH_ARC_PWM_SLOW`
  - 在 [`Project_track_fisheye/Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h) 删除运行时 `searchArcPwm*`
  - 在 [`Project_track_fisheye/Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c) 清理对 `ARC` 阶段字符的引用

### Phase 39: 编译与烧录
- **Status:** complete
- Actions taken:
  - 重新编译 [`Project_track_fisheye/project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
  - 构建日志 [`Project_track_fisheye/Objects/project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 显示：
    - `0 Error(s), 0 Warning(s)`
  - 使用 `pyOCD` 按顺序完成：
    - `list --probes`
    - `erase --chip --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164`
    - `load --no-config -t stm32f103rc -M under-reset -f 10000000 -u 031305620164 -e sector project.hex`
    - `reset --no-config -t stm32f103rc -u 031305620164`
  - 追加串口烟测尝试：
    - 打开 `COM18` 返回 `PermissionError(13, '拒绝访问。')`
    - 本轮未做最小 `#STAT!` 回读，原因是端口被外部进程占用

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
