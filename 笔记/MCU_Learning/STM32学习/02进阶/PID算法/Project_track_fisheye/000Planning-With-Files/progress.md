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
