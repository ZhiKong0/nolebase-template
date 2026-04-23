# Task Plan: 421 接线边界与循迹输入链重构

## Goal
按当前真实硬件边界收口 `Project_Refactor_track-421 - 副本 (3)`：

- 循迹前端仍是 `74HC4051 + 12 路模块 A3~A10`
- MCU 内部将扫描结果整理成 `8` 位状态帧供 `LineSensor_Read()` / `line_track` 消费
- `DAPlink` 主串口固定为 `USART1` 重映射 `PB6/PB7`
- `PB14/PB15` 预留给无线串口，不再被 `BNO085_RST` 占用
- 项目内 `000Planning-With-Files` 作为后续默认上下文恢复位置

## Current Phase
Phase 4

## Phases

### Phase 1: 核对真实硬件链
- [x] 读取 [`000/接线总表——END.md`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/000/接线总表——END.md)
- [x] 核对当前工程 `config / sensor_fusion / bsp_uart / stm32f10x_it`
- [x] 识别“外部 UART 数据帧”与“4051 扫描后内部状态帧”这两个概念的歧义
- **Status:** complete

### Phase 2: 修正输入主链
- [x] 将 `LineSensor` 主链收回到 `74HC4051` 扫描实现
- [x] 移除误接入的串口循迹帧缓存逻辑
- [x] 保留 `LineSensor_Read()` 对 `line_track` 的既有消费接口
- **Status:** complete

### Phase 3: 修正串口与引脚边界
- [x] 保持 `USART1(PB6/PB7 remap)` 作为 `DAPlink/命令串口`
- [x] 将 `BNO085_RST` 从 `PB14` 移到 `PB11`
- [x] 明确 `PB14/PB15` 为无线串口预留位
- **Status:** complete

### Phase 4: 文档与验证
- [x] 修正接线总表里 `DAPlink` / 无线串口 / `74HC4051` 的角色描述
- [x] 用项目内 `000Planning-With-Files` 刷新当前任务记录
- [x] 完成 Keil 编译验证
- **Status:** complete

### Phase 5: 收尾
- [ ] 整理最终说明
- [ ] 按需要提交推送
- **Status:** in_progress

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| 采用 `lean-context-stack = Continue + planning-with-files + files-to-prompt` | 当前任务跨多步，但已缩到少量热文件 |
| 项目内 `000Planning-With-Files/` 作为默认上下文恢复位置 | 符合用户要求，也能减少后续线程上下文占用 |
| 这次“数据帧”按 MCU 内部 `8` 位状态帧理解，不再误判成外部 UART 传感器持续输入 | 用户已明确实际硬件仍是 `74HC4051` 前端 |
| `PB6/PB7` 只留给 `DAPlink/命令串口`，不再在文档里写成循迹 UART 外设输入 | 避免把命令串口和循迹前端混成一条物理线 |
| `PB14/PB15` 作为无线串口预留位，`BNO085_RST` 改到 `PB11` | 让接线表和代码边界一致 |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| 把“数据帧”误解释成外部 UART 循迹模块输入 | 1 | 按用户澄清和旧 4051 适配记录，恢复为“4051 扫描 -> MCU 内部状态帧” |

## Hot Files
- [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/config.h)
- [`Hardware/sensor_fusion.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.h)
- [`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.c)
- [`Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/bsp_uart.c)
- [`User/stm32f10x_it.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/User/stm32f10x_it.c)
- [`000/接线总表——END.md`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/000/接线总表——END.md)
