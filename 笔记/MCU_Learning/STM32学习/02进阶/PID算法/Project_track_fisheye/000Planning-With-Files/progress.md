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

## Session: 2026-04-24 实验记录器接管

### Phase 1: 停止自动调参与串口占用
- **Status:** complete
- Actions taken:
  - 检查后台 `track_adaptive_tuner` 相关 `python/powershell` 进程
  - 当前未发现仍在占用 `COM18` 的调参进程

### Phase 2: 核对记录器脚本
- **Status:** complete
- Actions taken:
  - 读取 [`000Project_PC_Control/experiment_logger.ps1`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_logger.ps1)
  - 读取 [`000Project_PC_Control/experiment_logger.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_logger.py)
  - 确认脚本按 `EVT:EXP_START / EVT:EXP_STOP` 为主，`HB` 为补偿开关文件

### Phase 3: 修正记录器启动体验
- **Status:** complete
- Actions taken:
  - 把 [`experiment_logger.ps1`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_logger.ps1) 改成 `python -u`
  - 在 [`experiment_logger.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_logger.py) 启用行缓冲
  - 在 [`experiment_logger.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_logger.py) 为 `#EXP?!` 初始同步加入重试

### Phase 4: 常驻启动
- **Status:** complete
- Actions taken:
  - 后台拉起 `powershell.exe -> python.exe -u experiment_logger.py --port COM18`
  - 启动日志落在：
    - `000Data/serial_runs/experiments/logger_session_20260424_013803.out.txt`
    - `000Data/serial_runs/experiments/logger_session_20260424_013803.err.txt`
  - 当前 `stdout` 已实时写出：
    - `[logger] port=COM18 baud=115200`
    - `[logger] waiting for EVT:EXP_START / EVT:EXP_STOP ...`
  - 已实际捕获新的实验文件：
    - `exp_0254_20260424_013923_KEY_T.txt`
    - `exp_0255_20260424_013929_KEY_T.txt`
    - `exp_0256_20260424_013933_KEY_T.txt`
    - `exp_0257_20260424_013942_KEY_T.txt`
  - 其中 `exp_0257` 已包含完整的 `EVT:EXP_START -> HB -> EVT:EXP_STOP` 链

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| 记录器后台拉起 | `powershell.exe -File experiment_logger.ps1 --port COM18` | 记录器常驻且不再静默 | `powershell.exe + python.exe -u` 进程均存在 | pass |
| 启动日志落盘 | `logger_session_*.out.txt` | 可立即看到启动状态 | 已写出 `port/output/waiting` | pass |
| 按键启停自动建档 | 板端真实 `KEY` 启停 | 生成新的 `exp_*.txt` | 已生成 `exp_0254 ~ exp_0257` | pass |

## Session: 2026-04-24 误入找线与 IMU 显示收口

### Phase 1: 复盘 exp257
- **Status:** complete
- Actions taken:
  - 读取 [`exp_0257_20260424_013942_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0257_20260424_013942_KEY_T.txt)
  - 对照 [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 确认是“假丢线 + 搜索退出过宽 + 恢复期方向翻写”的组合问题

### Phase 2: 收紧找线状态机
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 新增：
    - `TRACK_LOST_STRAIGHT_CONFIRM_TICKS`
    - `TRACK_SEARCH_EXIT_CONFIRM_TICKS`
  - 在 [`Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h) 增加 `searchExitCount`
  - 在 [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 收紧搜索进入/退出和恢复期方向黏性

### Phase 3: 编译烧录与串口验证
- **Status:** complete
- Actions taken:
  - 编译 [`project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)，结果 `0 Error(s), 0 Warning(s)`
  - 通过 `pyOCD` 完成 `erase -> load -> reset`
  - 串口验证 `#STAT!`、`#MODE=TRACK!` 正常

### Phase 4: IMU 失败页不再霸占 OLED
- **Status:** complete
- Actions taken:
  - 串口读取 `#IMU?!`，确认当前仍是 `IMU=3,fail=5`
  - 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 修改 `update_display()`
  - 现在即使 IMU 未 ready，也继续显示主状态页，只在第 4 行显示 IMU 诊断
  - 重新编译烧录并恢复 `experiment_logger`

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| 误入找线修正后编译 | `UV4.exe -b project.uvprojx -j0 -t "Target 1"` | 工程正常编译 | `0 Error(s), 0 Warning(s)` | pass |
| 误入找线修正后烧录 | `pyocd erase/load/reset` | 板端接受新固件 | 成功 | pass |
| IMU 诊断读取 | `#IMU?!` | 返回稳定失败态 | `IMU=3,fail=5` | pass |
| 记录器恢复 | `experiment_logger.ps1 --port COM18` | 重新接管串口 | `powershell.exe + python.exe -u` 进程存在 | pass |

## Session: 2026-04-24 IMU 刷屏抑制

### Phase 1: 排除假阴性探测
- **Status:** complete
- Actions taken:
  - 确认 `BNO085=SCL/PB12,SDA/PB13,INT/PA15,RST/PB14,ADDR/GND`
  - 结合 `BNO08x` 的 I2C 特性，把初始化链从“空写扫描”改成“直接 product-id 握手”
  - 板上实测 `#IMU?!` 从 `IMU=1,fail=1` 推进到 `IMU=3,fail=5`

### Phase 2: 压制反复自动重试
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.c) 增加 `s_bnoEverReady`
  - 仅当 IMU 过去成功初始化过时，才允许后续自动恢复
  - 首次初始化失败时保持稳定失败态，不再周期性重进 `BNO085_Init()`

### Phase 3: 保持 OLED 显示 IMU 失败态
- **Status:** complete
- Actions taken:
  - 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 的 `update_display()` 中，`IMU` 未就绪时优先显示 `IMU/ADDR/FAIL`
  - 收口 `BspOled_ShowExperimentId()` 的几处调用，避免 `EXP000` 覆盖失败态

### Phase 4: 编译烧录与串口验证
- **Status:** complete
- Actions taken:
  - 编译 [`project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)，结果 `0 Error(s), 0 Warning(s)`
  - 按顺序完成 `pyocd erase -> load -> reset`
  - 两次间隔读取 `#IMU?!`，均稳定返回 `IMU=3,fail=5`，未再出现周期性循环

## Session: 2026-04-23 IMU 阻塞排查

### Phase 1: 重新定性 IMU 阶段号
- **Status:** complete
- Actions taken:
  - 读取 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/config.h)、[`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.c)、[`Hardware/bsp_oled.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/bsp_oled.c)、[`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/User/main.c)
  - 确认 `IMU:1=PROBE`、`IMU:5=WAIT`
  - 用户补充确认：`INT=PA15`、`RST=PB14`，与当前代码一致

### Phase 2: 软件鲁棒性修正
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.c) 增加初始化失败码
  - 初始化等待首包时加入无 `INT` 轮询兜底
  - 同时打开 `GAME_ROT_VEC` 与 `ROT_VEC`
  - 将无效 `0x28/0x29` 地址探测移除
  - 将 `present/product-id/first-report` 超时收紧，避免长时间阻塞启动

### Phase 3: 可观测性补强
- **Status:** complete
- Actions taken:
  - 在 [`Hardware/bsp_oled.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/bsp_oled.c) 显示 `IMU/ADDR/FAIL`
  - 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/User/main.c) 新增 `#IMU?!`
  - 在 [`Hardware/sensor_fusion.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(3)/Hardware/sensor_fusion.h) 暴露诊断 getter

### Phase 4: 编译、烧录、板上验证
- **Status:** complete
- Actions taken:
  - 使用 `D:\keil\Keil-v5\Arm\UV4\UV4.exe` 完成实际编译
  - 编译结果：`0 Error(s), 0 Warning(s)`
  - 按顺序执行 `pyocd list -> erase -> load -> reset`
  - 串口 `COM18` 实测：
    - `#STAT!` 已恢复正常回包
    - `#IMU?!` 返回 `OK:IMU=1,addr=00,fail=1,ready=0,rx=2,ch=2,rid=0,len=48`

### 结论
- 本轮已经排除“代码把 `INT/RST` 宏写错”的可能。
- 当前最直接的根因是：`BNO085` 在 I2C `PROBE` 阶段未应答，属于硬件链或模块模式问题。
- 本轮软件修改的价值在于：
  - 不再长时间假死
  - 板上可以直接通过串口看到失败码
  - 后续排查可以直接围绕 `SCL/SDA/PS0/PS1/ADDR/VDD/RST` 做
