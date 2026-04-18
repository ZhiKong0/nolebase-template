# Progress

## Completed

### 项目熟悉与基线确认

- 已完整熟悉 `0415-real` 代码结构
- 已确认 `0412` 仅作为历史参考，不再作为修改目标
- 已识别当前控制核心文件：
  - [config.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/config.h)
  - [line_track.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.h)
  - [line_track.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.c)

### 已做过的主要技术方向

- 普通 `TRK` 弯道增强已加入
- `R90` 入口加了位置阈值和连续确认
- `R90/CSR` 从单纯偏航角主导，改到更偏传感器渐进接线
- `R90` 专属 entry boost / arc / handoff 参数已经存在
- `R90` handoff 前加入了首帧位置限幅

### 已明确做错并回退/否定的方向

- `R90 -> CSR` fallback
  - 已被证明方向错误
  - 应视为禁用思路

### 已做过的关键实测

- 多次单轮 `12s` 串口复测
- 一次 `90s` 测试
- 一次 4 轮循环 `12s` 测试，目录：
  - [multi12s_reposition_20260416_172324](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/multi12s_reposition_20260416_172324)

### 当前代码与板子状态

- 本线程最后一次写代码后，代码已成功构建
- 最近一次板子复测日志是：
  - [track12s_recheck_20260416_170313.txt](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/track12s_recheck_20260416_170313.txt)
- 之后还做了更进一步的 `R90` 局部调节与构建/烧录
- 也就是说：**当前源码树比 `track12s_recheck_20260416_170313.txt` 略更新**

## Not Completed

### 1. 角点链还没有稳定收口

目前没有一个版本能在多轮 `12s` 下稳定表现为：

- 无明显 `R90` 误触发
- 无长 `AWN->ATN->ARC->CSR`
- 无 `R90->CSR`
- handoff 后首帧 `lp` 始终较小

### 2. 当前主要残留问题还没选定单点突破

仍需要在以下两项中选一个作为下一阶段唯一目标：

- `AWN->ATN->ARC->CSR` 偏长
- `R90` handoff 后首帧 `lp` 偏大

不要同时再改两项。

## Next Step

新 AI 接手后建议这样做：

1. **先读交接文件**
   - [task_plan.md](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/task_plan.md)
   - [findings.md](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/findings.md)
   - [progress.md](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/progress.md)

2. **再读源码**
   - [config.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/config.h)
   - [line_track.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.h)
   - [line_track.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.c)

3. **最后读测试证据**
   - 先读多轮目录：
     [multi12s_reposition_20260416_172324](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/multi12s_reposition_20260416_172324)
   - 再读近几次关键单轮日志：
     - [track12s_targetfix_20260416_163002.txt](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/track12s_targetfix_20260416_163002.txt)
     - [track12s_chaincheck_20260416_163738.txt](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/track12s_chaincheck_20260416_163738.txt)
     - [track12s_recheck_20260416_170313.txt](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/track12s_recheck_20260416_170313.txt)

4. **然后只选一个问题继续改**
   - 推荐先固定当前源码与板子状态，再做一轮新的 `12s` 复测
   - 之后只盯：
     - `AWN->ATN->ARC->CSR` 长链
     - 或 `R90` handoff
   - 只选一个，不要混改

