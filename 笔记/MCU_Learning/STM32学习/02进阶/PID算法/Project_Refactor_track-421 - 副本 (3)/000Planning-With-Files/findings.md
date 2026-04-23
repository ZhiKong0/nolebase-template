# Findings

## 当前真实硬件边界

- 循迹前端仍然是 `74HC4051 + 12 路循迹模块 A3~A10`。
- `74HC4051` 地址与输出脚为：
  - `S0 -> PA2`
  - `S1 -> PA11`
  - `S2 -> PA10`
  - `Z  -> PA3`
- `DAPlink / 命令串口` 走 `USART1` 重映射：
  - `TX -> PB6`
  - `RX -> PB7`
- `PB14/PB15` 应作为无线串口预留位，不应再被 IMU 复位脚抢占。

## 这轮纠正的关键歧义

- 用户提到“数据帧”时，真实语义不是“外部 UART 传感器持续给 MCU 发串口帧”。
- 当前更准确的理解是：
  - `4051` 逐路扫描 `A3~A10`
  - MCU 内部组合成 `8` 位状态帧
  - `LineSensor_Read()` / `line_track` 消费该状态帧
- 因此此前把 `sensor_fusion` 改成“纯 UART 帧缓存输入”是偏离真实硬件的。

## 本轮代码收口

- `Hardware/config.h`
  - 恢复 `LINE_MUX_S0/S1/S2/Z` 宏
  - 移除“外部 UART 循迹帧”默认配置
  - `BNO085_RST` 从 `PB14` 改到 `PB11`
  - 新增无线串口预留宏：`PB14/PB15`
- `Hardware/sensor_fusion.c`
  - 恢复 `74HC4051` 逐路扫描逻辑
  - `LineSensor_Read()` 回到“扫描 -> bits -> count -> weighted position”
  - 删除串口循迹帧解析状态机
- `Hardware/sensor_fusion.h`
  - 删除 `LineSensor_Tick1ms / FrameRxByte / FrameIsOnline / FrameAgeMs`
- `Hardware/bsp_uart.c`
  - 串口 RX 回到只处理命令帧 `#...!`
  - 不再把串口字节喂给 `LineSensor`
- `User/stm32f10x_it.c`
  - 去掉 `LineSensor_Tick1ms()` 定时调用
- `000/接线总表——END.md`
  - `PB6/PB7` 明确写成 `DAPlink / 命令串口`
  - 把“数据帧”解释为 MCU 内部 `8` 位状态帧
  - `74HC4051` 恢复为当前现行方案
  - 补清 `PB14/PB15` 无线串口接线和 `PA15` 的 JTAG 说明

## 额外发现

- `PA15` 若作为 `BNO085_INT`，才需要释放 `JTAG`；`PB14/PB15` 本身不是 `JTAG` 脚。
- 当前工程里已经在 `BNO085_Init()` 中调用 `GPIO_Remap_SWJ_JTAGDisable`，因此 `PA15` 作为 GPIO 是成立的。
- `PB11` 当前在项目内没有其它功能占用，适合承接 `BNO085_RST`。

## 风险与后续

- 这轮如果上板前不把 `BNO085_RST` 实际改线到 `PB11`，则新固件和旧接线会不一致。
- 由于这次纠正的是硬件边界而不是算法参数，所以优先级应高于继续联调 `PID`。
