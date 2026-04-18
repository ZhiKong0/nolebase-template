# Findings & Decisions

## Requirements
- 参考 `5路循迹-stm32代码` 的五路循迹逻辑，设计八路循迹代码。
- 八路布局中间是两个红外对射传感器 `s4`、`s5`。
- 目标工程是 `Project_Refactor-0415-real`，需要全面整改。
- 接线与编译烧录必须遵循 `000/mcu-build-flash.md` 和 `000/接线总表.md`。
- 需要保留直线模式和循迹模式，且尽量保持文件组织和直线模式格式一致。

## Research Findings
- 五路参考工程的关键逻辑在 `User/bsp_track.c`：
  - 正常循迹通过 `bearing_dev` 离散误差驱动 `bsp_pid_control.c`。
  - 当传感器全灭时，若 `last_data` 记录到左外侧线则左转，记录到右外侧线则右转。
  - 转角退出条件很简单：任意一路重新见线就退出转角。
- 目标工程已经具备完整双模式框架：
  - `User/main.c` 中 `MODE_STRAIGHT` / `MODE_TRACK` 切换已完成。
  - `Hardware/sensor_fusion.c` 中 `LineSensor_Init/Read` 已按八路并口读取实现。
  - `Hardware/config.h` 的八路引脚定义与 `000/接线总表.md` 一致。
- 原有 `Hardware/line_track.c` 实现包含锐角、直角辅助、弧线接管等复杂状态机，与“五路转角逻辑保持一致”的要求不完全一致。
- 第二轮明确整改要求后，新的实现不再保留旧的八路自定义判断链，而是：
  - `S1/S2 -> L1`
  - `S3 -> L0`
  - `S4/S5 -> M`
  - `S6 -> R0`
  - `S7/S8 -> R1`
  - 然后直接按五路参考的 `Signal_Handler/corner_handler/Track_Handler` 执行。
- `Project_Refactor-0416-inception` 的串口实验记录核心并不在分析脚本本身，而在两段协议配合：
  - MCU 端支持 `#EXP?!`、`#EXP=`、`#EXPHOST=`，并在开始/结束时发 `EVT:EXP_START/EVT:EXP_STOP`
  - 遥测 `HB:` 帧里带 `exp=`，这样 PC 端即使错过开始事件也能按实验编号补开文件
- 当前工程原本已经具备 `#RUN!/#STOP!/#MODE=` 和 `HB:`，所以迁移成本主要集中在实验编号与事件层，不需要重做底层串口收发。
- 当前机器上 `COM18` 会被遗留的 Python 实验脚本长期占用；如果再次出现 `PermissionError(13, '拒绝访问')`，优先检查是否有旧的 `experiment_logger.py` 还在后台挂着。

## Technical Decisions
| Decision | Rationale |
|----------|-----------|
| 先做参考代码比对，再落到目标工程 | 目标是“保持五路转角逻辑”，不能只按经验重写 |
| 八路分工采用 `S1/S2=左外侧`、`S3=左侧`、`S4/S5=中间`、`S6=右侧`、`S7/S8=右外侧` | 与用户给定“中间是 S4/S5”一致，也能自然继承五路转角语义 |
| 循迹模式内部不再使用旧的八路速度环/锐角状态机 | 用户要求“全部按照五路代码”，因此只保留五路式判断链 |
| 直线模式完全保留，循迹模式仍挂在现有主工程模式框架下 | 保证项目仍有双模式，同时避免影响直线模式 |
| 仅保留原串口/显示接口的外壳字段，复杂状态一律置零或简化 | 兼容现有调试链路，同时不让旧判断继续参与控制 |
| 交叉口检测恢复为简单计数去抖逻辑 | 保留目标工程的自动停车能力，但避免原先复杂状态机造成误判 |
| 遥测里把 `exp=` 同时加到直线和循迹模式 | 这样脚本对两种模式都能统一落盘，后续不需要再分两套采集器 |
| 当前工程先不引入参考工程的 OLED 实验号显示接口 | 对当前目标没有收益，还会额外扩大改动面 |
| 串口实验记录继续沿用参考工程 `000Data/serial_runs/experiments` 目录结构 | 方便后续多工程共用同一套分析脚本和命名规则 |

## Issues Encountered
| Issue | Resolution |
|-------|------------|
| `line_track.c` 首轮编译出现未使用变量告警 | 删除冗余局部变量并重新编译，最终 `0 Error(s), 0 Warning(s)` |
| Keil 第一次只做了增量编译 | 改用 `-r` 全量重建，确认 `main.c` 和 `line_track.c` 全部进入最终固件 |
| `System/usart.c` 仍有历史告警 | 已尝试修正源码，但 Keil 仍报告同一条旧式 `_sys_exit` 告警；与本次循迹重构无关 |
| 首次串口验证时报 `COM18` 拒绝访问 | 发现是旧的 `Project_Refactor-0417` `experiment_logger.py` 持续占口，结束进程后恢复 |

## Resources
- `F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\5路循迹-stm32代码`
- `F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0415-real`
- `F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0415-real\000\mcu-build-flash.md`
- `F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor-0415-real\000\接线总表.md`

## Visual/Browser Findings
- 暂无。
