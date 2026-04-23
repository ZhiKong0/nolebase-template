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
