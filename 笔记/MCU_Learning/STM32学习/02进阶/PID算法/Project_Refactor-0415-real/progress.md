# Progress Log

## Session: 2026-04-17

### Phase 1: 需求与参考梳理
- **Status:** complete
- **Started:** 2026-04-17
- Actions taken:
  - 读取用户约束，确认需要跨工程迁移五路循迹到八路循迹。
  - 检查 `Project_Refactor-0415-real` 与 `5路循迹-stm32代码` 的目录结构。
  - 初始化 `task_plan.md`、`findings.md`、`progress.md`。
- Files created/modified:
  - `task_plan.md` (created)
  - `findings.md` (created)
  - `progress.md` (created)

### Phase 2: 八路策略设计
- **Status:** complete
- Actions taken:
  - 分析五路参考工程 `bsp_track.c` / `bsp_pid_control.c`，提炼出“全灭后按外侧历史转角，见线即退出”的核心规则。
  - 对照目标工程 `line_track.c` 与 `config.h`，确认双模式框架和八路 GPIO 定义已经具备。
  - 确定八路语义映射：`S1/S2` 左外侧，`S3` 左侧，`S4/S5` 中间，`S6` 右侧，`S7/S8` 右外侧。
- Files created/modified:
  - `findings.md` (updated)
  - `task_plan.md` (updated)

### Phase 3: 工程实现
- **Status:** complete
- Actions taken:
  - 第一轮将 `line_track` 收敛成较简单的五路式转角逻辑。
  - 根据后续明确要求，第二轮再次重写 `Hardware/line_track.c` / `Hardware/line_track.h`。
  - 新实现先把 8 路输入映射为 5 路等效信号，再直接按五路参考代码的 `Signal_Handler/corner_handler/Track_Handler` 执行。
  - 修改 `User/main.c`，让循迹模式不再走旧的速度环/锐角判断链，只保留直线模式原逻辑。
  - 调整 `Hardware/config.h` 的循迹常量为五路参考风格的缩放版本。
- Files created/modified:
  - `Hardware/line_track.c` (rewritten)
  - `Hardware/line_track.h` (updated)
  - `Hardware/config.h` (updated)
  - `User/main.c` (updated)

### Phase 4: 校验与整理
- **Status:** complete
- Actions taken:
  - 按 `000/mcu-build-flash.md` 的 Keil 命令行流程先做增量编译。
  - 发现需要确认 `main.c` 也被重新编进固件后，改为 `Keil -r` 全量重建。
  - 构建结果为 `0 Error(s), 1 Warning(s)`，唯一告警来自历史文件 `System/usart.c`，与本次循迹逻辑无关。
- Files created/modified:
  - `Hardware/line_track.c` (updated)
  - `System/usart.c` (updated)

### Phase 5: 交付说明
- **Status:** complete
- Actions taken:
  - 整理接线一致性、修改范围和剩余上板调参点。
- Files created/modified:
  - `progress.md` (updated)
  - `findings.md` (updated)
  - `task_plan.md` (updated)

### Phase 6: 串口实验记录链路
- **Status:** complete
- Actions taken:
  - 对照 `Project_Refactor-0416-inception`，确认其核心链路是 `#EXP?!/#EXP=/#EXPHOST` + `EVT:EXP_START/EVT:EXP_STOP` + `HB:...exp=...`。
  - 在当前工程 `User/main.c` 中补入实验编号同步、按键/串口来源标记、起停事件上报。
  - 在 `Hardware/bsp_uart.c/.h` 中给直线/循迹遥测都补上 `exp=` 字段。
  - 新增 `000Project_PC_Control/experiment_logger.py` 与 `experiment_logger.ps1`，常驻 `COM18` 并将每段实验写入 `000Data/serial_runs/experiments`。
  - 用 Keil 重新编译后，通过 `pyOCD` 串行执行 `erase -> load -> reset` 重烧固件。
  - 清理占用 `COM18` 的旧 `experiment_logger.py` 进程后，跑 `--uart-test-seconds 1.2` 自动验证，成功生成 `exp_0250_20260417_221938_UART_T.txt`。
- Files created/modified:
  - `User/main.c` (updated)
  - `Hardware/bsp_uart.c` (updated)
  - `Hardware/bsp_uart.h` (updated)
  - `Hardware/config.h` (updated)
  - `000Project_PC_Control/experiment_logger.py` (created)
  - `000Project_PC_Control/experiment_logger.ps1` (created)
  - `progress.md` (updated)
  - `findings.md` (updated)
  - `task_plan.md` (updated)

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| Keil 编译 | `UV4.exe -b project.uvprojx -j0 -t "Target 1"` | 成功生成 `Objects/project.axf` 和 `Objects/project.hex` | `0 Error(s), 0 Warning(s)`，HEX 已生成 | ✓ |
| Keil 全量重建 | `UV4.exe -r project.uvprojx -j0 -t "Target 1"` | `main.c` / `line_track.c` 都重新参与构建 | 全量编译成功，`0 Error(s), 1 Warning(s)` | ✓ |
| 串口实验日志编译检查 | `py -3 -m py_compile 000Project_PC_Control/experiment_logger.py` | Python 脚本语法正确 | 通过，无报错 | ✓ |
| 重新烧录 | `pyocd erase --chip` + `pyocd load project.hex` + `pyocd reset` | 新固件写入板子 | 成功擦除并写入 `36864 bytes` | ✓ |
| 实验日志落盘 | `experiment_logger.py --port COM18 --uart-mode TRACK --uart-test-seconds 1.2 --max-seconds 8` | 自动生成一段实验文件 | 成功生成 `exp_0250_20260417_221938_UART_T.txt`，包含 `EVT:EXP_START/STOP` 与 `HB:...exp=250...` | ✓ |

## Error Log
| Time | Error | Attempt | Resolution |
|------|-------|---------|------------|
| 2026-04-17 22:16 | 首次烧录把 `erase/load/reset` 并行执行，导致 `pyOCD` 报错 | 改为串行执行，并显式指定 `-u 031305620164` | 烧录成功 |
| 2026-04-17 22:18 | `COM18` 被旧的 `Project_Refactor-0417` `experiment_logger.py` 占用，脚本报 `PermissionError(13)` | 定位占用进程后停止对应 `py/python` 残留进程 | 串口验证恢复正常 |
