# Findings

## Confirmed Facts

### 项目与主文件

- 当前主项目是 [Project_Refactor-0415-real](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real)
- 当前真正的控制主战场是：
  - [config.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/config.h)
  - [line_track.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.h)
  - [line_track.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.c)
  - [main.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/User/main.c)
  - [bsp_uart.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/bsp_uart.c)

### 交叉线状态

- `0412` 历史对话中有 `CRS`
- `0415-real` 当前代码里没有 `CRS`
- 当前主线调试不在交叉线

### 构建与烧录

- 本机可用：
  - `UV4.exe`
  - `pyocd 0.43.1`
  - `Horco CMSIS-DAP`
- 本线程里每次关键改动后都做过：
  - Keil 构建
  - pyOCD 烧录
- 构建长期保持：
  - `0 Error(s), 1 Warning(s)`
- 这个 warning 是旧的 `line_track.c` 里 `count` 未使用，不是当前 blocker

### 当前源码状态

- 当前源码里已经存在多轮调过的角点参数，不是初始版本
- 目前源码里仍保留：
  - 普通弯道增强
  - `R90` 入口双帧确认
  - `R90` 专属 entry boost / arc / handoff 限幅
  - `R90` handoff 首帧位置限幅

## Technical Judgments

### 1. 角点问题不是单一点导致

过去已经确认过多个阶段性主因：

- 普通 `TRK` 弯道约束力太弱
- `R90` 入口过宽导致误触发
- `R90/CSR` 执行链过慢、过黏
- handoff 后首帧 `lp` 过大

现在这些问题已经被压掉一部分，但还没有完全收口。

### 2. `R90 -> CSR` fallback 方向不对

已经实测证明：

- 让 `R90` 中途降级到 `CSR`
- 不会真正缩短整条角点链
- 只会把纯 `R90` 长链变成更长的 `R90->CSR` 复合链

结论：

- 这个方向应该保持关闭
- `R90` 问题尽量在 `R90` 内部解决

### 3. 当前波动很大，不能只信单次 12s

已经做过四轮 `12s` 循环测试，目录在：

- [multi12s_reposition_20260416_172324](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/000Data/multi12s_reposition_20260416_172324)

多轮结果说明：

- 同一固件会出现不同链型
- 常见坏链包括：
  - `AWN->ATN->ARC->CSR`
  - 纯 `R90`
  - `R90->CSR`
- 所以后续判断必须看多轮，不要只看单轮

### 4. 当前剩余主问题

当前更像是两个稳定残留：

- `AWN->ATN->ARC->CSR` 偏长
- `R90` handoff 后首帧 `lp` 仍有时偏大

这两个问题不宜再一起大改。

## Constraints

### 硬件/测试约束

- 串口固定：`COM18`
- 协议固定：当前看的是文本 `HB:` 遥测
- 常用测试长度：`5s`、`10s`、`12s`、`90s`
- 用户会手动摆车；多轮测试之间通常留 `6s`

### 工作方式约束

- 手工文件修改必须用 `apply_patch`
- 当前工作树外有大量不相关脏改动，不要误清理
- 不建议大改项目结构，优先局部修正

## Pitfalls

### 1. 不要只看平均值

曾出现这种情况：

- 平均 handoff `|lp|` 变好
- 但波动更大
- 单次事件反而更糟

所以要同时看：

- 平均值
- 最大值
- 最差事件

### 2. 不要只看“最长链缩短了”

曾出现：

- 最长链缩短
- 但总非 `TRK` 时间增加

说明只是把问题拆散了，不是解决了。

### 3. 小心 build log 尾部旧片段

Keil 的 `project.build_log.htm` 可能包含旧内容片段。
真正可信的是：

- 文件最后的 summary
- 最终 `0 Error(s)` / `1 Warning(s)`
- 文件时间戳

### 4. `R90` 起转更猛不等于更好

已经验证过：

- 只增强 `R90` 起转
- 可能让行为更躁
- 不一定更快收口

