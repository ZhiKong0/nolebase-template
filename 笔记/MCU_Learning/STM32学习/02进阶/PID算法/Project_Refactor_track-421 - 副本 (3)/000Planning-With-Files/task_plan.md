# Task Plan: IMU 初始化阻塞排查

## Goal
解决 `Project_Refactor_track-421 - 副本 (3)` 当前 `BNO085` 初始化卡在 `IMU:1 / IMU:5` 的问题，先恢复板上可观测性和可启动性，再决定是否继续 `TRACK` 联调。

## Current Phase
Phase 5

## Phases

### Phase 1: 核对 IMU 引脚与状态号
- [x] 读取 `config.h / sensor_fusion.c / main.c / bsp_oled.c`
- [x] 确认 `INT=PA15`、`RST=PB14` 与当前代码一致
- [x] 确认 OLED `IMU:x` 阶段含义
- **Status:** complete

### Phase 2: 识别真实失败点
- [x] 区分 `IMU:1` 与 `IMU:5` 的失败路径
- [x] 判断 `IMU:5` 可能受 `INT` 首包门控影响
- [x] 判断当前启动期存在超长阻塞风险
- **Status:** complete

### Phase 3: 实施鲁棒性修正
- [x] 初始化期加入无 `INT` 轮询首包兜底
- [x] 增加 `failCode` 失败码
- [x] OLED 改为显示 `IMU/ADDR/FAIL`
- [x] 新增串口 `#IMU?!` 诊断命令
- **Status:** complete

### Phase 4: 收缩启动阻塞窗口
- [x] 去掉无效 `0x28/0x29` 地址探测
- [x] 缩短 `present/product-id/first-report` 超时
- [x] 重新编译并烧录
- **Status:** complete

### Phase 5: 板上验证与结论
- [x] 串口验证 `#STAT!` 已恢复响应
- [x] 串口验证 `#IMU?!` 已返回具体失败码
- [ ] 如需继续修复，转入硬件链检查或进一步降级 IMU 依赖
- **Status:** in_progress

## Decisions Made
- `TRACK` 自适应联调暂时暂停，先清除 IMU 启动阻塞。
- 当前最关键的证据来源改为 `#IMU?!`，不再只依赖 OLED 阶段号。
- 已确认不是 `PA15/PB14` 宏定义错误，而是运行时 `BNO` 探测阶段没有收到设备应答。

## Hot Files
- `Hardware/config.h`
- `Hardware/sensor_fusion.h`
- `Hardware/sensor_fusion.c`
- `Hardware/bsp_oled.h`
- `Hardware/bsp_oled.c`
- `User/main.c`
