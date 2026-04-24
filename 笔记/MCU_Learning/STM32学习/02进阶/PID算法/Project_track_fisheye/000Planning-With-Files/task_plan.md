# Task Plan: 单链循迹 PID 与自动调参骨架

## Goal
把 `Project_track_fisheye` 当前 `TRACK` 模式从“分层梯度 + 多组比例/阈值耦合链”重构为“单一连续误差 + 单一循迹 PD + 最小找线状态机”，并在此基础上补齐“不扫目标速度、围绕固定 40 速度档做复测驱动联调”的自动调参骨架。

## Current Phase
Phase 44

## Phases

### Phase 1: 锁定参考实现与重构边界
- [x] 对照 `Project_track_infrared` 的单链实现
- [x] 读取当前 `fisheye` 的 `config.h / line_track.h / line_track.c / main.c`
- [x] 确认需要保留的兼容面：`LineTrack_*` 接口、主循环框架、串口遥测
- **Status:** complete

### Phase 2: 重构 TRACK 主链
- [x] 将 `line_track` 收敛为单一 `linePos -> PD -> devSpeed`
- [x] 删除中心/中间/边缘分层增益与差速梯度
- [x] 保留独立找线/交叉状态机，不把它们混进主控制律
- **Status:** complete

### Phase 3: 清理参数面与命令口
- [x] 清理旧的 `center/mid/edge` 相关宏
- [x] 清理旧的短命令口 `#CSR/#CSM/#CMR/#CMM/#EDR/#EDM/#RCD/#CDB/#OCB/#CHT`
- [x] 保留并统一为 `#TDR/#STB/#TDB/#PLF/#DLF/#RCT/#STF/#STS/#STO`
- **Status:** complete

### Phase 4: 编译验证
- [x] 编译 `project.uvprojx`
- [x] 检查 `bsp_uart` 遥测状态字符串与新状态枚举兼容
- [x] 确认工程 `0 Error(s), 0 Warning(s)`
- **Status:** complete

### Phase 5: 文档收口
- [x] 更新 `task_plan.md`
- [x] 更新 `findings.md`
- [x] 更新 `progress.md`
- **Status:** complete

### Phase 6: 自动调参骨架收口
- [x] 将 `track_adaptive_tuner.py` 扩展为“中心稳定 + S 弯稳定 + 搜线恢复”评分骨架
- [x] 将 `config.yaml` 改成固定 `speed_target=40`、三阶段复测骨架
- [x] 完成 `Python` 语法、`YAML` 结构与 `--help` 入口验证
- [ ] 后续如需实跑，再单独启动自动复测，不在本轮直接扫描
- **Status:** in progress

### Phase 8: 提速与停机链清理
- [x] 提高 `TRACK` 默认速度档
- [x] 提高大偏差时的基础速度下限
- [x] 删除 `line_track` 内部自动停车链
- **Status:** complete

### Phase 9: 编译与记录
- [x] 编译 `project.uvprojx`
- [x] 记录本轮“提速 + 关停机链”结论到规划文件
- **Status:** complete

### Phase 10: exp266 限速定位
- [x] 读取 `exp266` 日志
- [x] 确认 `S` 弯拖速主要来自速度斜坡与恢复速度重置
- **Status:** complete

### Phase 11: S 弯恢复提速
- [x] 提高起步斜坡
- [x] 提高找线/转角退出后的恢复目标
- [x] 保持单链 PD 主控制不变
- **Status:** complete

### Phase 12: 编译与记录
- [x] 编译 `project.uvprojx`
- [x] 将 `exp266` 结论写入规划文件
- **Status:** complete

### Phase 22: exp287 主循迹力度复核
- [x] 读取 `exp287` 日志并区分 `SCRV` 与 `EDGE` 区间
- [x] 确认问题主因是大偏差回中力度不足，而不是搜索方向链本身
- **Status:** complete

### Phase 23: 单链参数强化并烧录
- [x] 提高单链 `KP`
- [x] 降低 `TRACK_FOLLOW_ERROR_SCALE`
- [x] 提高 `TRACK_FOLLOW_DEV_RATIO / STEP_LIMIT`
- [x] 编译并烧录到当前板子
- **Status:** complete

### Phase 24: exp289 抓线力增强
- [x] 读取 `exp289` 日志并确认主限制点在单链主 PD 输出
- [x] 继续提高主链 `KP` 与误差换算强度
- [x] 编译并烧录到当前板子
- **Status:** complete

### Phase 25: exp293 根因拆分
- [x] 读取 `exp293` 日志并区分主循迹偏软与找线回正偏慢
- [x] 确认找线重获线退出链存在“同侧也要等两拍”的拖慢点
- **Status:** complete

### Phase 26: 主循迹与找线回正联动修正
- [x] 继续提高单链 `KP`
- [x] 继续降低 `deadband / errorScale`
- [x] 将同侧重获线改成按方向判定并加快退出搜索
- [x] 编译并烧录到当前板子
- **Status:** complete

### Phase 31: exp305 响应滞后区间复核
- [x] 读取 `exp305` 日志并定位“跟线不够及时”的主要区段
- [x] 区分主因是主循迹链响应迟滞，而不是搜索方向链本身
- **Status:** complete

### Phase 32: 单链响应提速
- [x] 提高单链 `KP`
- [x] 放快 `linePos` 跟随速度与位置/导数低通
- [x] 放宽差速步进限幅并抬高找线 pivot
- **Status:** complete

### Phase 33: 编译与烧录
- [x] 重新编译 `project.uvprojx`
- [x] 使用 `pyOCD` 完成 `erase -> load -> reset`
- **Status:** complete

### Phase 34: 找线自转速度复核
- [x] 确认当前找线 `pivot` 参数与调用点
- [x] 保持主循迹与丢线判定不变，只调整自转找线速度
- **Status:** complete

### Phase 35: 编译与烧录
- [x] 重新编译 `project.uvprojx`
- [x] 使用 `pyOCD` 完成 `erase -> load -> reset`
- **Status:** complete

### Phase 36: 12路输入链重构
- [x] 将 `LINE_SENSOR_COUNT` 从 `8` 扩成 `12`
- [x] 将 `LineSensor_Read()` 重构为 `4 路直连 + 74HC4051 扫 8 路`
- [x] 将 `line_track`、遥测与串口调参口径联动升级到 `12` 位
- **Status:** complete

### Phase 37: 12路工具链与板上验证
- [x] 将 `track_adaptive_tuner.py / config.yaml` 同步为 `12` 路掩码与 `sensor_scale1..12`
- [x] 重新编译 `project.uvprojx`
- [x] 使用 `pyOCD` 完成 `erase -> load -> reset`
- [ ] 待串口口释放后补做 `COM18` 参数回读烟测
- **Status:** in progress

### Phase 38: 转角链重构为FOLLOW强化+SEARCH兜底+RECOVER收回
- [x] 将 `FOLLOW` 拆成“小偏差普通PD / 大偏差同向强化转入”两档
- [x] 将 `SEARCH` 收成“只在全灭进入、单向 pivot、不保留 ARC 扫线”
- [x] 将 `RECOVER` 改成“重新见到同侧线后继续同向收回，回中心后再交还 PD”
- [x] 重新编译并烧录到当前板子
- [ ] 待串口口释放后补做 `COM18` 烟测
- **Status:** in progress

### Phase 39: exp404 不降速抓线增强
- [x] 读取 `exp404` 日志并确认主因不是速度档，而是大偏差同向抓线过平
- [x] 将 `FOLLOW/RECOVER` 的同向抓线从固定下限改为按 `linePos` 增强
- [x] 保持 `PID_TRACK_SPEED_TARGET` 与基础速度下限不变
- [x] 重新编译并烧录到当前板子
- **Status:** complete

### Phase 41: exp422 直线稳定性回收
- [x] 读取 `exp422` 日志并确认直线失稳不是速度档，而是 `RECOVER` 沿旧方向握得过久
- [x] 将恢复窗口改成“线已跨回中心带或偏到反侧时立即放手”
- [x] 保持 `exp404` 引入的大偏差抓线增强不回退
- [x] 重新编译并烧录到当前板子
- **Status:** complete

### Phase 44: exp432 单链误差映射整形
- [x] 读取 `exp432` 日志并确认直线抖动与 S 弯抓线偏弱同时存在
- [x] 将单链 `linePos -> error` 从纯线性改成“中心压小、外侧放大”的连续映射
- [x] 保持速度档、基础 PWM 和大偏差抓线增强链不回退
- [x] 重新编译并烧录到当前板子
- **Status:** complete

## Decisions Made
- 本轮不在旧 `line_track.c` 上继续打补丁，而是直接以 `Project_track_infrared` 为真源迁移单链主实现。
- 自动调参骨架本轮固定 `speed_target=40`，不在本轮搜索目标速度。
- `TRACK` 主控制只保留一套运行时参数：
  - `sensorScale[8]`
  - `lkp / lkd`
  - `dev_ratio`
  - `deadband`
  - `pos_lpf / d_lpf`
  - `static_bias`
  - `recover_ticks`
  - `search_turn_fast / search_turn_slow / search_timeout`
- 为减少联动面，遥测里暂时保留 `dbgScoreEnabled` 字段，但其语义已简化为“搜索/丢线/交叉是否计分”。
- `exp305` 之后，单链主循迹优先沿“更快响应”方向调：先放快 `linePos` / `dev` 跟随，再适度增加 `KP`，不先动搜索方向链。
- 本轮“找线太慢”的处理边界限定为只提高 `pivot` 搜索 PWM，不顺手动主循迹和丢线确认链。
- 12 路输入链采用“`A1/A2/A11/A12` 直连 GPIO + `A3~A10` 继续走 `74HC4051`”方案，不引入第二片复用器。
- 12 路内部位序固定为：`bit0=A1 ... bit11=A12`，中心对固定为 `A6/A7`。
- PC 侧评分与参数接口必须和板端一起升级到 `12` 路；不能只改固件不改调参与分析脚本。
- 当前转角逻辑正式收成三段：
  - `FOLLOW`：小偏差普通 `PD`，大偏差且同侧明显占优时直接同向强化转入
  - `SEARCH`：只有全灭才进入，且只做单向 `pivot`
  - `RECOVER`：重见同侧线后继续顺着该方向收回，回中心后才完全交还普通 `PD`
- `exp404` 之后，主抓线增强不再继续靠降速或压低基础 PWM，而是优先提升 `FOLLOW/RECOVER` 内部同向抓线强度，并让增强量随偏差绝对值平滑增加。
- `exp422` 之后，`RECOVER` 不再只靠固定倒计时放手；一旦重新见线已跨回中心带或偏到反侧，必须立即解除旧方向的恢复约束，避免把直线段自己打穿。
- `exp432` 之后，单链主 `PD` 的输入误差不再是纯线性：中心小偏差要被压小，外侧大偏差要被放大，用一条连续误差整形同时兼顾直线稳定和 S 弯抓线。

## Hot Files
- `Hardware/config.h`
- `Hardware/line_track.h`
- `Hardware/line_track.c`
- `User/main.c`
- `Hardware/bsp_uart.c`
- `000Project_PC_Control/config.yaml`
- `000Project_PC_Control/track_adaptive_tuner.py`

## Session: 2026-04-24 回退到 ddc2eb7 基线

### Goal
- 将 `Project_track_fisheye` 的主循迹行为定向回退到 `ddc2eb7`
- 保持当前工程结构不动，只恢复需要的行为文件
- 重新编译并烧录，让当前车体回到“投影在线上”的旧基线再继续调试

### Checklist
- [x] 确认 `ddc2eb7` 到当前工程的实际差异范围
- [x] 只回退 `Hardware/line_track.c`，不重置整个仓库
- [x] 重新编译 `Project_track_fisheye`
- [x] 使用 `pyOCD` 完成擦除、下载和复位
- [x] 更新规划记录，标记当前固件已回到 `ddc2eb7` 主链

## Session: 2026-04-24 定位 12 路版本

### Goal
- 找出 `Project_track_fisheye` 刚改到 `12` 路时对应的 git 提交
- 区分“12 路起点提交”和“当前仍保留 12 路结构的最新工作落点”

### Checklist
- [x] 在 git 历史中搜索 `12-route / LINE_SENSOR_COUNT 12 / TRACK_LINE_POS_S9`
- [x] 核对当前工作树是否仍保留 12 路结构
- [x] 记录可直接回退或参考的提交号

## Session: 2026-04-24 输出 12 路电平到串口

### Goal
- 在不破坏现有 `experiment_logger` 和脚本兼容性的前提下，让 `HB:` 心跳直接带出 12 路数字电平信号

### Checklist
- [x] 确认心跳组包位置与 logger 落盘链路
- [x] 在 `HB:` 中保留 `sb=` 并追加 12 路可读字段
- [x] 编译并烧录到当前板子
- [x] 更新规划记录，说明新字段含义
