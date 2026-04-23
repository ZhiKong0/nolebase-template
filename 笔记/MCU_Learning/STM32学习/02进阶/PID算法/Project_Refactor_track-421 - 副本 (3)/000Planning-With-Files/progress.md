# Progress Log

## Session: 2026-04-23

### Phase 1: 重新核对硬件链
- **Status:** complete
- Actions taken:
  - 按用户要求启用 `lean-context-stack`
  - 读取项目内 `000Planning-With-Files` 旧文件，确认需要切换到本轮任务
  - 重新读取 [`000/接线总表——END.md`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/000/接线总表——END.md)、[`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/config.h)、[`Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/bsp_uart.c)
  - 根据用户补充确认：当前真实循迹前端仍是 `74HC4051`，此前把“数据帧”理解成外部 UART 输入是错误假设

### Phase 2: 拉回 4051 主链
- **Status:** complete
- Actions taken:
  - 把 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/config.h) 的循迹输入定义恢复为 `74HC4051`
  - 把 [`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.c) 的 `LineSensor` 实现恢复为 `4051` 逐路扫描
  - 删除 [`Hardware/sensor_fusion.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.h) 里为串口循迹帧临时加上的接口
  - 清理 [`Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/bsp_uart.c) 与 [`User/stm32f10x_it.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/User/stm32f10x_it.c) 中误接入的串口循迹帧调用

### Phase 3: 引脚边界收口
- **Status:** complete
- Actions taken:
  - 保留 `USART1(PB6/PB7 remap)` 作为 `DAPlink` 主串口
  - 根据用户补充确认：无线透传能力来自 `DAPlink` 本体，因此物理上仍落在 `PB6/PB7`
  - 把 `BNO085_RST` 恢复到 `PB14`
  - 删除 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/config.h) 中错误的 `PB14/PB15` 无线串口预留宏

### Phase 4: 文档与验证
- **Status:** complete
- Actions taken:
  - 修正 [`000/接线总表——END.md`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/000/接线总表——END.md)：
    - `PB6/PB7` 明确为 `DAPlink` 的有线/无线主串口
    - `4051` 恢复为现行循迹方案
    - “数据帧”改为 MCU 内部 `8` 位状态帧的解释
    - 删除 `PB14/PB15` 无线串口预留的错误描述
  - 使用 `UV4.exe -b project.uvprojx -j0 -t "Target 1"` 完成实际编译
  - 构建结果：`0 Error(s), 0 Warning(s)`

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| 接线边界修正后编译 | `UV4.exe -b project.uvprojx -j0 -t "Target 1"` | 工程可直接编译 | `0 Error(s), 0 Warning(s)` | pass |

## Error Log
| Timestamp | Error | Attempt | Resolution |
|-----------|-------|---------|------------|
| 2026-04-23 | 把“数据帧”误解释成外部 UART 循迹模块输入 | 1 | 依据用户澄清与旧 4051 适配记录，恢复为“4051 扫描 -> MCU 内部状态帧” |

## Current State
- 代码主线已经回到真实硬件边界
- 文档已同步修正
- `BNO085_RST` 已恢复为 `PB14`，不需要为了这一轮再额外改 IMU 复位线
