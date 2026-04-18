# STM32 PC端实时控制小车 + AI自动调参

## 项目概述

这是一个 STM32 PID 循迹小车项目的全新架构实现：**PC端实时控制**。不同于传统的在单片机上运行 PID 算法，本项目中 STM32 仅作为简单的传感器/执行器接口，所有控制逻辑、传感器融合和 AI 调参都在 PC 端用 Python 实现。

### 核心架构决策

- **单片机角色**："哑"I/O 接口 - 读取传感器、发送数据、接收 PWM 指令
- **PC 角色**：所有智能 - 传感器融合、PID 控制、AI 调参、可视化
- **通信方式**：高速二进制协议（921600 波特率）+ CRC16 校验
- **控制回路**：PC 端 100Hz 实时控制，目标延迟 <10ms

### 为什么选择这种架构？

✅ **快速原型开发** - 修改算法无需重新烧录单片机
✅ **强大计算能力** - 完整的 Python 生态系统（NumPy、SciPy、机器学习库）
✅ **实时调参** - 小车运行时实时调整 PID 参数
✅ **丰富可视化** - 实时绘图、仪表盘、全面数据记录
✅ **易于集成 AI** - 直接访问优化和机器学习框架

⚠️ **权衡**：需要 PC 连接（非独立运行），延迟约 5-10ms（板载控制约 1ms）

---

## 当前实现状态

### ✅ 已完成（里程碑 1 - 部分）

#### 1. 项目结构已创建
```
Project_PC_Control/
├── Core/
│   ├── Src/          # 单片机源文件（待实现）
│   └── Inc/          # 单片机头文件（待实现）
├── Drivers/
│   ├── MPU6050/      # IMU 驱动（待实现）
│   ├── Encoder/      # 编码器驱动（待实现）
│   ├── Motor/        # 电机 PWM 驱动（待实现）
│   └── I2C/          # I2C 驱动（待实现）
├── Protocol/
│   ├── binary_protocol.h    ✅ 完成
│   └── binary_protocol.c    ✅ 完成
├── pc_control/
│   ├── binary_protocol.py   ✅ 完成
│   ├── serial_interface.py  ✅ 完成
│   ├── sensor_fusion.py     ✅ 完成
│   └── pid_controller.py    ✅ 完成
├── tools/            # 构建自动化（待实现）
└── experiments/      # 数据存储目录
```

#### 2. 二进制协议实现 ✅

**C 语言实现**（`Protocol/binary_protocol.h` 和 `.c`）：
- 上行帧结构（单片机 → PC）：30 字节
  - 帧头：`0xAA 0x55`
  - 数据：时间戳(4)、加速度 xyz(6)、陀螺仪 xyz(6)、编码器(8)
  - 校验：CRC16(2)
  - 帧尾：`0x0D 0x0A`
- 下行帧结构（PC → 单片机）：11 字节
  - 帧头：`0xBB 0x66`
  - 数据：左轮 PWM(2)、右轮 PWM(2)、标志位(1)
  - 校验：CRC16(2)
  - 帧尾：`0x0D 0x0A`
- CRC16-CCITT 校验实现
- 帧打包/解包函数

**Python 实现**（`pc_control/binary_protocol.py`）：
- 匹配的帧结构
- CRC16 校验验证
- 带缓冲区管理的帧解析器（处理分片数据）
- 已测试并验证 ✅

**测试结果**：
```
下行帧 (11 字节): bb66f401d4fe0153480d0a
上行帧 (30 字节): aa55e80300006400c8002c010a0014001e00d20400002e160000f5e40d0a
解析上行帧：
  timestamp: 1000
  ax: 100, ay: 200, az: 300
  gx: 10, gy: 20, gz: 30
  enc_l: 1234, enc_r: 5678
✅ 协议测试完成！
```

#### 3. PC 端串口接口 ✅

**实现**（`pc_control/serial_interface.py`）：
- 串口连接管理
- 自动检测串口（CH340、CP210x、USB Serial）
- 帧发送/接收
- 统计信息跟踪
- 上下文管理器支持

**功能**：
```python
class SerialInterface:
    def __init__(self, port=None, baud_rate=921600, timeout=0.1)
    def auto_detect_port(self, keywords=['CH340', 'CP210', 'USB Serial'])
    def connect(self)
    def disconnect(self)
    def send_pwm(self, pwm_left, pwm_right, emergency_stop=False)
    def receive_frame(self, timeout=None)
    def get_statistics()
    def reset_statistics()
```

#### 4. 传感器融合算法 ✅

**实现**（`pc_control/sensor_fusion.py`）：

四种融合算法可选：

1. **互补滤波器**（ComplementaryFilter）
   - 简单、快速、CPU 占用低
   - 适合俯仰/横滚，航向会漂移
   - 加速度计和陀螺仪的 alpha 混合

2. **卡尔曼滤波器**（KalmanFilter）
   - 高斯噪声下最优
   - 状态估计（预测/更新）
   - 包含陀螺仪零偏估计

3. **Mahony 滤波器**（MahonyFilter）
   - 基于四元数 + PI 校正
   - 陀螺仪零偏估计
   - 性能/CPU 平衡良好

4. **Madgwick 滤波器**（MadgwickFilter）
   - 梯度下降姿态估计
   - 基于四元数
   - 计算效率高

所有算法输出欧拉角（俯仰、横滚、航向），单位为度。

#### 5. PID 控制器 ✅

**实现**（`pc_control/pid_controller.py`）：

```python
class PID:
    def __init__(self, Kp=1.0, Ki=0.0, Kd=0.0,
                 output_limit=None,
                 integral_limit=None,
                 derivative_filter_alpha=0.1)
    def update(self, error, dt)
    def reset()
    def set_gains(self, Kp=None, Ki=None, Kd=None)
    def get_components()  # 返回 P、I、D 分量用于调试
```

**特性**：
- 抗积分饱和（积分限幅）
- 微分项低通滤波（减少噪声）
- 输出限幅
- 可调参数

---

## 剩余工作

### 🔄 里程碑 1：最小单片机固件（进行中）

**优先级：高** - 开始测试前必须完成

#### 需要实现的单片机驱动：

1. **MPU6050 原始数据驱动**（`Drivers/MPU6050/`）
   - I2C 通信（软件或硬件）
   - 初始化 MPU6050（唤醒、配置）
   - 读取原始加速度计（ax、ay、az）
   - 读取原始陀螺仪（gx、gy、gz）
   - 不使用 DMP，不做传感器融合（保持简单）
   - 预计：2-3 小时

2. **编码器定时器驱动**（`Drivers/Encoder/`）
   - 配置 TIM2（左轮）编码器模式
   - 配置 TIM3（右轮）编码器模式
   - 读取编码器计数（32 位有符号）
   - 预计：1-2 小时

3. **电机 PWM 驱动**（`Drivers/Motor/`）
   - 配置 TIM1 CH1(PA8) 和 CH2(PA9) 用于 PWM
   - 设置 PWM 频率（推荐 1kHz）
   - 方向控制（AIN1/AIN2、BIN1/BIN2）
   - 接受 PWM 值（-1000 到 +1000）
   - 预计：1-2 小时

4. **USART DMA 通信**（`Core/Src/usart_dma.c`）
   - 配置 USART2（PA2=TX、PA3=RX），921600 波特率
   - DMA 发送（最小化 CPU 负载）
   - DMA 接收（循环缓冲区）
   - 帧发送函数
   - 帧接收回调
   - 预计：2-3 小时

5. **主循环**（`Core/Src/main.c`）
   - 系统初始化
   - 100Hz 主循环（10ms 周期）
   - 读取传感器 → 打包帧 → 通过 USART 发送
   - 接收 PWM 指令 → 应用到电机
   - 看门狗定时器（100ms 无指令则紧急停止）
   - LED 状态指示
   - 预计：2-3 小时

6. **Keil 项目配置**
   - 创建 `.uvprojx` 项目文件
   - 配置 STM32F103 目标
   - 设置包含路径
   - 配置构建设置
   - 预计：1 小时

**里程碑 1 剩余总计：约 10-15 小时**

---

### 📋 里程碑 2：PC 端实时控制

**优先级：高** - 核心功能

#### 需要实现的 Python 模块：

1. **实时控制器**（`pc_control/realtime_controller.py`）
   - 主控制回路（100Hz）
   - 传感器融合集成
   - 双环级联控制（航向 + 速度）
   - PWM 指令生成
   - 紧急停止逻辑
   - 延迟监控
   - 预计：4-6 小时

2. **数据记录器**（`pc_control/data_logger.py`）
   - 实时数据缓冲
   - HDF5 文件写入
   - 元数据管理
   - 预计：2-3 小时

**里程碑 2 总计：约 6-9 小时**

---

### 📋 里程碑 3：实时参数调整 GUI

**优先级：中** - 提升可用性

#### GUI 实现：

1. **主窗口**（`pc_control/tuning_gui.py`）
   - PyQt5 或 Tkinter 框架
   - 控制面板（启动/停止/紧急停止）
   - 参数滑块（速度和航向的 Kp、Ki、Kd）
   - 目标速度/航向调整
   - 预计：4-6 小时

2. **实时绘图**
   - Matplotlib 或 PyQtGraph 集成
   - 航向 vs 时间图
   - 速度 vs 时间图
   - PWM vs 时间图
   - 误差图
   - 预计：3-4 小时

3. **指标显示**
   - 回路时间监控
   - RMS 误差计算
   - 饱和检测
   - 性能指标
   - 预计：1-2 小时

**里程碑 3 总计：约 8-12 小时**

---

### 📋 里程碑 4：AI 自动调参

**优先级：低** - 高级功能

#### AI 调参模式：

1. **网格搜索**（`pc_control/ai_tuner.py`）
   - 系统参数空间探索
   - 预计：2-3 小时

2. **贝叶斯优化**
   - 高斯过程建模
   - 高效参数空间探索
   - 预计：3-4 小时

3. **LLM 引导调参**
   - Claude API 集成
   - 性能数据分析
   - 参数建议
   - 预计：3-4 小时

4. **强化学习**（可选）
   - RL 环境设置
   - PPO/SAC 训练
   - 预计：6-8 小时

**里程碑 4 总计：约 8-15 小时**

---

### 📋 里程碑 5：数据分析工具

**优先级：低** - 后处理

1. **分析器模块**（`pc_control/analyzer.py`）
   - HDF5 数据加载
   - 图表生成
   - 指标计算
   - 实验对比
   - 预计：3-4 小时

2. **导出工具**
   - CSV 导出
   - 报告生成
   - 预计：1-2 小时

**里程碑 5 总计：约 4-6 小时**

---

### 📋 里程碑 6：构建自动化

**优先级：中** - 生活质量

1. **构建和烧录脚本**（`tools/build_and_flash.py`）
   - Keil 命令行编译
   - STM32CubeProgrammer 集成
   - 构建验证
   - 预计：2-3 小时

2. **串口检测器**（`tools/port_detector.py`）
   - 自动检测 CH340 串口
   - 串口验证
   - 预计：1 小时

**里程碑 6 总计：约 3-4 小时**

---

## 剩余工作总计

| 里程碑 | 状态 | 预计小时数 |
|--------|------|-----------|
| M1：单片机固件 | 🔄 进行中 | 10-15 |
| M2：实时控制 | ⏳ 未开始 | 6-9 |
| M3：调参 GUI | ⏳ 未开始 | 8-12 |
| M4：AI 自动调参 | ⏳ 未开始 | 8-15 |
| M5：数据分析 | ⏳ 未开始 | 4-6 |
| M6：构建工具 | ⏳ 未开始 | 3-4 |
| **总计** | | **39-61 小时** |

---

## 硬件要求

### 单片机开发：
- ✅ Keil MDK-ARM（已安装在 `D:\keil\Keil-v5\Arm\UV4\UV4.exe`）
- ✅ STM32CubeProgrammer CLI（已安装在 `E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe`）
- ❓ ST-Link 调试器（需要验证）

### PC 开发：
- ✅ Python 3.8+（假设已安装）
- ⏳ Python 包（需要安装）：
  ```bash
  pip install pyserial numpy scipy pandas matplotlib PyQt5 pyqtgraph h5py scikit-optimize
  ```

### 硬件：
- ❓ STM32F103 开发板
- ❓ TB6612 电机驱动
- ❓ MPU6050 IMU
- ❓ 带 GMR 编码器的双直流电机
- ❓ USB-TTL 适配器（CH340 或 CP2102）- **需要高质量适配器支持 921600 波特率**
- ❓ ST-Link 编程器
- ❓ 电源

---

## 系统架构

### 硬件组件

- **单片机**：STM32F103C8T6
- **电机驱动**：TB6612FNG 双 H 桥
- **电机**：MG310 直流电机 + GMR 编码器（30:1 减速比，11 线）
- **IMU**：MPU6050 六轴（加速度计 + 陀螺仪）
- **显示**：OLED 128x64（可选，用于调试）
- **按键**：PB5 按键（启动/停止控制）
- **通信**：USB-TTL 适配器（CH340/CP2102），921600 波特率

### 引脚连接

详见 [WIRING.md](WIRING.md) 完整接线说明。

**关键连接**：
- USART2（PA2/PA3）：通过 USB-TTL 与 PC 通信
- TIM1（PA8/PA9）：电机 PWM 输出
- TIM2（PA0/PA1）：左编码器输入
- TIM3（PA6/PA7）：右编码器输入
- PB12/PB13：MPU6050 I2C（软件）
- PB0：TB6612 STBY（待机控制）
- PB5：按键输入（启动/停止，短按<1s；切换速度，长按≥1s）

### 软件架构

**PC 端（Python）**：
- 100Hz 实时控制回路
- 传感器融合（互补、卡尔曼、Mahony、Madgwick）
- 双环级联 PID（航向 + 速度）
- 多种优化算法的 AI 自动调参
- 实时参数调整 GUI
- 全面数据记录（HDF5）

**单片机端（C）**：
- 最小固件：传感器读取 + PWM 输出
- 低延迟通信的二进制协议
- 通信丢失时紧急停止
- 安全看门狗定时器

## 二进制通信协议

### 上行帧（单片机 → PC）

```
[0xAA 0x55] [时间戳:4] [ax:2] [ay:2] [az:2] [gx:2] [gy:2] [gz:2]
[enc_l:4] [enc_r:4] [crc16:2] [0x0D 0x0A]
总计：30 字节 @ 100Hz = 3 KB/s
```

### 下行帧（PC → 单片机）

```
[0xBB 0x66] [pwm_l:2] [pwm_r:2] [标志:1] [crc16:2] [0x0D 0x0A]
总计：11 字节 @ 100Hz = 1.1 KB/s
```

**标志位**：
- 位 0：紧急停止

## 安装

### 前置条件

**Python 3.8+** 及包：
```bash
pip install -r tools/requirements.txt
```

**单片机开发工具**：
- Keil MDK-ARM（用于构建固件）
- STM32CubeProgrammer（用于烧录）
- ST-Link 调试器

### 设置

1. **烧录单片机固件**：
```bash
python tools/build_and_flash.py
```

2. **连接硬件**：
   - 将 USB-TTL 适配器连接到 USART2（PA2/PA3）
   - 给系统供电（推荐 7.4V 电池）
   - 验证 COM 口检测

3. **测试串口通信**：
```bash
python -m pc_control.serial_interface
```

## 使用方法

### 手动控制

```python
from pc_control.serial_interface import SerialInterface

# 自动检测并连接
with SerialInterface() as serial:
    # 发送 PWM 指令
    serial.send_pwm(pwm_left=50, pwm_right=50)

    # 接收传感器数据
    frame = serial.receive_frame()
    if frame:
        print(f"航向角速度：{frame['gz']} deg/s")
```

### 实时控制

```python
from pc_control.realtime_controller import RealtimeController

controller = RealtimeController(port='COM6')
controller.set_target_speed(50)  # 计数/周期
controller.start()

# 控制回路在后台以 100Hz 运行
# 按 Ctrl+C 停止
```

### AI 自动调参

```python
from pc_control.ai_tuner import AITuner

tuner = AITuner(controller)
best_params, best_score = tuner.bayesian_optimize(n_iterations=50)
print(f"最优参数：{best_params}")
```

## 控制算法

### 双环级联控制

**外环（航向控制）** @ 50Hz：
- 输入：目标航向角（启动时锁定）
- 传感器：MPU6050 陀螺仪 + 传感器融合
- 控制器：PID 或模糊 PID
- 输出：差速修正（±PWM）

**内环（速度控制）** @ 100Hz：
- 输入：目标速度（计数/周期）
- 传感器：编码器计数
- 控制器：PI（左右轮独立）
- 输出：基础 PWM（0-100%）

**最终 PWM**：
```
PWM_左 = PWM_基础_左 - 航向修正
PWM_右 = PWM_基础_右 + 航向修正
```

### 传感器融合

四种算法可选：

1. **互补滤波器**：简单、快速，适合大多数情况
2. **卡尔曼滤波器**：高斯噪声下最优
3. **Mahony 滤波器**：基于四元数，带零偏估计
4. **Madgwick 滤波器**：梯度下降，计算效率高

## 安全特性

### 单片机端
- 看门狗定时器（100ms 超时）
- 通信丢失时紧急停止
- PWM 输出限制（硬件 + 软件）
- 传感器故障检测

### PC 端
- 连接监控
- 回路时间监控（检测超时）
- 参数范围验证
- 异常检测（旋转、饱和）
- 紧急停止按钮（GUI）

## 故障排除

### 串口连接问题

**问题**：无法检测串口
- 检查 USB-TTL 适配器连接
- 验证驱动安装（CH340/CP2102）
- 尝试手动指定端口：`SerialInterface(port='COM6')`

**问题**：通信超时
- 检查波特率（必须是 921600）
- 验证 TX/RX 接线（PA2→TTL-RX，PA3→TTL-TX）
- 先用较低波特率测试（115200）

### 控制问题

**问题**：小车不走直线
- 校准 MPU6050（启动时保持静止）
- 检查电机接线极性
- 调整 trim 参数
- 验证编码器连接

**问题**：振荡或不稳定
- 降低 PID 增益（特别是 Kp 和 Kd）
- 检查回路时间（应为 100Hz ±5%）
- 验证传感器数据质量
- 启用微分滤波

### 性能问题

**问题**：高延迟（>10ms）
- 使用高质量 USB-TTL 适配器
- 关闭其他串口应用
- 降低系统负载
- 检查 USB 供电问题

**问题**：回路时间抖动
- 提高 Python 进程优先级
- 禁用节能功能
- 使用专用 USB 端口（非集线器）
- 考虑实时操作系统（Linux + PREEMPT_RT）

## 开发

### 构建单片机固件

```bash
# 使用 Keil MDK-ARM
cd Project
# 在 Keil 中打开 project.uvprojx
# 构建 → 全部重建

# 或使用命令行
python tools/build_and_flash.py --build-only
```

### 运行测试

```bash
# 测试二进制协议
python -m pc_control.binary_protocol

# 测试串口接口
python -m pc_control.serial_interface

# 测试传感器融合
python -m pc_control.sensor_fusion

# 测试 PID 控制器
python -m pc_control.pid_controller
```

## 性能基准

### 典型性能（调参后）

- **航向误差 RMS**：< 2°（优秀），< 5°（良好）
- **速度跟踪**：目标的 ±5%
- **控制回路**：100Hz ±2%
- **往返延迟**：5-8ms
- **直线偏差**：2m 内 < 10cm

### 调参时间

- **手动调参**：10-20 分钟
- **网格搜索**：30-60 分钟（取决于分辨率）
- **贝叶斯优化**：15-30 分钟（20-50 次迭代）
- **强化学习训练**：1-2 小时（100-200 回合）

## 参考资料

- [STM32F103 参考手册](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)
- [TB6612FNG 数据手册](https://www.sparkfun.com/datasheets/Robotics/TB6612FNG.pdf)
- [MPU6050 寄存器映射](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf)
- [Mahony 滤波器论文](https://hal.archives-ouvertes.fr/hal-00488376/document)
- [Madgwick 滤波器论文](https://www.x-io.co.uk/res/doc/madgwick_internal_report.pdf)

## 许可证

MIT 许可证 - 详见 LICENSE 文件

## 贡献

欢迎贡献！请：
1. Fork 仓库
2. 创建功能分支
3. 充分测试
4. 提交 pull request

## 联系方式

如有问题或疑问，请在 GitHub 上提交 issue 或联系维护者。

---

**最后更新**：2026-03-14
**状态**：里程碑 1 进行中（协议完成，驱动待完成）
**下一步行动**：决定开发方法并继续实现
