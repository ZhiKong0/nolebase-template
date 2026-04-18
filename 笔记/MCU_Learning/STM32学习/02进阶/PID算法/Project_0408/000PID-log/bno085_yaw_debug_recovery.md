# BNO085 从“读不出 yaw”到“成功读出 yaw”的完整排查与实现复盘

## 1. 文档目的

本文记录在 `Project_Refactor` 工程中，将 IMU 从“BNO085 已接入但无法稳定读出 yaw”一步步排查到“已经能够成功读出 yaw，并在串口上看到微弱抖动”的完整过程。

这份文档重点说明以下内容：

- 初始问题是什么
- 我是如何一步步定位根因的
- 具体改了哪些代码
- 关键函数之间是如何调用的
- 为什么这些修改最终能让系统读到 yaw
- 最后是如何验证成功的

本文对应的主要代码文件有：

- `Hardware/ICM42688.c`
- `Hardware/ICM42688.h`
- `Hardware/Control.c`
- `Hardware/BNO085_DebugSerial.c`

虽然文件名仍然叫 `ICM42688`，但这部分代码实际已经承担了 **BNO085 的初始化、SHTP 收包、姿态解析与 yaw 输出** 工作。

---

## 2. 初始问题现象

最初的现象不是单一故障，而是分成两个阶段。

### 2.1 第一阶段：初始化阶段就拿不到有效姿态

最开始系统表现为：

- BNO085 初始化流程经常卡住
- 无法稳定进入 `READY`
- 不能稳定收到 `GAME_ROTATION_VECTOR`
- `yaw` 始终读不出来

也就是说，问题一开始不是“yaw 算法错了”，而是更靠前的链路就已经不通：

- 设备是否存在
- 是否能正常发包
- 是否能正常收包
- 是否能收到第一帧姿态报告

### 2.2 第二阶段：已经进入 READY，但 yaw 还是不变

在后续修复中，系统已经可以进入：

- `who=0x85`
- `st=6`
- `rid=0x08`
- `len=17`

这说明：

- BNO085 已经识别成功
- 初始化阶段已经走到 `READY`
- 也确实收到了 `GAME_ROTATION_VECTOR`

但这时又出现第二类问题：

- 左右晃动板子时，`yaw` 几乎不变
- 串口里 `y=0.000`
- 怀疑是姿态没有持续更新，或者四元数解析有误

因此整条排查链路，其实经历了两个核心问题：

1. **初始化/收包问题**
2. **初始化后主循环不持续更新 yaw 的问题**

---

## 3. 整体技术路线

我采取的不是一次性“盲改”，而是逐层把 BNO085 数据链路拆开。

完整思路如下：

1. 先让初始化链路可观测
2. 再区分失败发生在“发包”还是“收包”
3. 再定位收包失败是“没包到”还是“包到了但解析失败”
4. 再确认第一帧 `GAME_ROTATION_VECTOR` 是否真的到达
5. 再确认四元数是否成功转成 `yaw`
6. 最后确认主循环是否持续调用 IMU 更新

也就是说，排查顺序是：

- **链路通不通**
- **包有没有到**
- **包有没有被正确解析**
- **姿态有没有被持续刷新**

---

## 4. 第一步：增加在线触发初始化能力 `#IMU_INIT!`

### 4.1 为什么要做这一步

如果每次都靠上电自动初始化，不利于调试。因为：

- 上电阶段日志容易错过
- 初始化失败后不容易重试
- 不利于抓取每一次失败时的阶段信息

因此我增加了一个串口命令：

- `#IMU_INIT!`

它的作用是：

- 在线重新触发 `ICM42688_Init()`
- 不必重新上电
- 可以一边看串口一边精确定位失败阶段

### 4.2 相关代码

在 `Hardware/Control.c` 中，命令解析逻辑如下：

```c
if (strcmp(cmd, "#IMU_INIT") == 0 || strcmp(cmd, "#IMU_INIT!") == 0) {
    memset(&sys->icm, 0, sizeof(sys->icm));
    ICM42688_Init();
    if (ICM42688_GetWhoAmI() == 0x85u) {
        sys->testMode = TEST_MODE_NORMAL;
        (void)ICM42688_ReadAll(&sys->icm);
        control_imu_zero_update(sys);
        ICM42688_ResetAttitude(&sys->icm);
        sys->targetYaw = 0.0f;
        send_ok("IMU_INIT");
    } else {
        send_err();
    }
    return;
}
```

这段代码最后的版本不仅做初始化，还做了：

- 把模式切回 `TEST_MODE_NORMAL`
- 先读一帧 IMU
- 更新 IMU 零点
- 重置姿态基准
- 把目标航向清零

这一步非常关键，后面会解释为什么。

---

## 5. 第二步：给初始化过程加分阶段 trace

### 5.1 为什么要加 trace

只知道“初始化失败”是不够的，必须知道失败发生在哪一步。

所以我在 BNO085 初始化链路里加入了很多简短 trace，例如：

- `BI:PRE`：开始等待设备存在
- `BI:PID`：开始请求 Product ID
- `BI:FTR`：开始使能 Feature
- `BI:WFR`：开始等待第一帧报告
- `BI:RDY`：进入 READY
- `BI:FPID`：Product ID 失败
- `BI:FWFR`：等待第一帧报告失败

### 5.2 相关代码

核心函数在 `Hardware/ICM42688.c`：

```c
static uint8_t BNO085_AttemptInitAtAddr(uint8_t addr, uint8_t doReset, uint16_t presentTimeoutMs, ICM42688_Data_t *tempData) {
    g_icmAddr = addr;
    g_bnoLastProbeAddr = addr;
    g_bnoInitStage = BNO085_INIT_STAGE_PROBE;
    memset(g_bnoSeq, 0, sizeof(g_bnoSeq));
    g_bnoReady = 0u;
    g_icmWho = 0u;

    if (doReset) {
        BNO085_Trace("BI:HR\r\n");
        BNO085_HardwareReset();
        Delay_ms(BNO085_BOOT_DELAY_MS);
    } else {
        BNO085_Trace("BI:WTN\r\n");
        Delay_ms(BNO085_NO_RESET_BOOT_WAIT_MS);
    }

    BNO085_Trace("BI:PRE\r\n");
    if (!BNO085_WaitPresent(presentTimeoutMs)) {
        BNO085_Trace("BI:FPRE\r\n");
        g_icmAddr = 0u;
        return 0u;
    }

    if (!BNO085_WaitPresentStable((uint16_t)(BNO085_PRESENT_STABLE_COUNT * BNO085_PRESENT_STABLE_GAP_MS * 6u),
                                   BNO085_PRESENT_STABLE_COUNT)) {
        BNO085_Trace("BI:FPRS\r\n");
        g_icmAddr = 0u;
        return 0u;
    }

    g_bnoInitStage = BNO085_INIT_STAGE_PRESENT;
    g_bnoProbeWho = 0u;
    g_bnoInitStage = BNO085_INIT_STAGE_PRODUCT_ID;
    BNO085_Trace("BI:PID\r\n");
    if (!BNO085_RequestProductId()) {
        BNO085_Trace("BI:FPID\r\n");
    }

    g_bnoInitStage = BNO085_INIT_STAGE_FEATURE;
    BNO085_Trace("BI:FTR\r\n");
    if (!BNO085_EnableReport(BNO085_SENSOR_GAME_ROTATION_VECTOR, BNO085_REPORT_INTERVAL_US)) {
        BNO085_Trace("BI:FFTR\r\n");
        g_icmAddr = 0u;
        return 0u;
    }

    g_bnoReady = 1u;
    g_bnoInitStage = BNO085_INIT_STAGE_WAIT_REPORT;
    BNO085_Trace("BI:WFR\r\n");
    if (!BNO085_WaitForFirstReport(tempData, BNO085_FIRST_REPORT_TIMEOUT_MS)) {
        BNO085_Trace("BI:FWFR\r\n");
        g_bnoReady = 0u;
        g_icmWho = 0u;
        g_icmAddr = 0u;
        return 0u;
    }

    g_icmWho = 0x85u;
    g_bnoInitStage = BNO085_INIT_STAGE_READY;
    BNO085_Trace("BI:RDY\r\n");
    return 1u;
}
```

这让我们不再只能看到“失败”，而是可以知道：

- 失败是在等待设备存在
- 还是在 Product ID
- 还是在 Feature 使能
- 还是在等待第一帧姿态

---

## 6. 第三步：给串口状态输出增加更细的诊断字段

### 6.1 为什么要做

仅有 trace 还不够。还需要看到一些“连续状态量”，例如：

- 当前初始化阶段 `st`
- 最近收到的通道 `ch`
- 最近报告 ID `rid`
- 最近 payload 长度 `len`
- 最近发送通道 `txch`
- 最近发送包长度 `txlen`
- 最近写失败字节索引 `wfail`
- `INT` 引脚状态
- 主循环中 IMU 读取成功次数 `iok`
- 主循环中 IMU 读取失败次数 `if`

于是我把 `BNO085_DebugSerial_Send()` 扩展成更详细的诊断输出。

### 6.2 相关代码

在 `Hardware/BNO085_DebugSerial.c` 中：

```c
void BNO085_DebugSerial_Send(uint32_t tickCount, uint8_t isRunning, const ICM42688_Data_t *data)
{
    static char out[384];
    const ICM42688_Data_t *p = data;

    if (p == 0) {
        return;
    }

    snprintf(out, sizeof(out),
             "BNO tick=%lu run=%u y=%.3f yr=%.3f q0=%.5f q1=%.5f q2=%.5f q3=%.5f who=0x%02X addr=0x%02X paddr=0x%02X st=%u pwho=0x%02X sfa=0x%02X sla=0x%02X shc=%u ch=%u rid=0x%02X len=%u txch=%u txlen=%u wfail=%u scl=%u sda=%u rst=%u int=%u iok=%lu if=%lu rx=%lu txd=%lu\r\n",
             (unsigned long)tickCount,
             (unsigned)isRunning,
             (double)p->yaw,
             (double)p->yawRate,
             (double)p->q0,
             (double)p->q1,
             (double)p->q2,
             (double)p->q3,
             (unsigned)ICM42688_GetWhoAmI(),
             (unsigned)ICM42688_GetI2CAddr(),
             (unsigned)ICM42688_GetLastProbeAddr(),
             (unsigned)ICM42688_GetInitStage(),
             ...);
    VOFA_SendString(out);
}
```

这一步后，我在串口里就能同时看见：

- `yaw`
- 四元数 `q0/q1/q2/q3`
- 初始化状态
- 最近包信息
- 主循环是否真的在持续读 IMU

---

## 7. 第四步：定位收包链路问题——原始实现不适合当前 SHTP 收包方式

### 7.1 观察到的问题

在等待第一帧报告时，经常看到：

- `WFRA`
- 或 `WFNP`

为了进一步细分，我又在 `BNO085_WaitForFirstReport()` 里把失败情况拆成了三类：

- `BI:WFNO`：根本没有发生读包尝试
- `BI:WFRA`：发生了读包尝试，但读包失败
- `BI:WFNP`：已经收到包，但不是我们要的可解析姿态包

### 7.2 相关代码

```c
static uint8_t BNO085_WaitForFirstReport(ICM42688_Data_t *data, uint16_t timeoutMs) {
    uint16_t t;
    uint32_t startAttemptCount = g_bnoRxAttemptCount;
    uint32_t startRxCount = g_bnoRxPacketCount;
    uint8_t sawPacket = 0u;
    uint8_t sawAttempt = 0u;
    char traceBuf[48];

    for (t = 0u; t < timeoutMs; t++) {
        if (ICM42688_ReadAll(data) && data->ahrsInited) {
            BNO085_Trace("BI:RPT\r\n");
            return 1u;
        }
        if (!sawAttempt && g_bnoRxAttemptCount != startAttemptCount) {
            sawAttempt = 1u;
        }
        if (!sawPacket && g_bnoRxPacketCount != startRxCount) {
            sawPacket = 1u;
            BNO085_Trace("BI:WFPK\r\n");
        }
        Delay_ms(1);
    }

    if (sawPacket) {
        snprintf(traceBuf, sizeof(traceBuf), "BI:WFNP ch=%u rid=0x%02X len=%u\r\n",
                 (unsigned)g_bnoLastChannel,
                 (unsigned)g_bnoLastReportId,
                 (unsigned)g_bnoLastPayloadLen);
        BNO085_Trace(traceBuf);
    } else if (sawAttempt) {
        snprintf(traceBuf, sizeof(traceBuf), "BI:WFRA c=%u h=%02X%02X%02X%02X\r\n",
                 (unsigned)g_bnoLastRxFailCode,
                 (unsigned)g_bnoHeader[0],
                 (unsigned)g_bnoHeader[1],
                 (unsigned)g_bnoHeader[2],
                 (unsigned)g_bnoHeader[3]);
        BNO085_Trace(traceBuf);
    } else {
        BNO085_Trace("BI:WFNO\r\n");
    }
    return 0u;
}
```

这一步的意义是：

- 不是笼统地说“第一帧失败”
- 而是明确知道失败发生在“没尝试”“读失败”“收到非目标包”中的哪一种

---

## 8. 第五步：按 SparkFun 参考库改造收包方式为两段式读取

### 8.1 根因分析

参考 SparkFun BNO080/BNO085 库后发现，I2C 读 SHTP 包时，常见的方式不是“单次读完整包”，而是：

1. 先读取 **4 字节 header**
2. 根据 header 得出总长度
3. 再读取 **4 + payload**
4. 再丢弃前 4 字节重复 header，只保留 payload

这正是当前代码后来采用的方式。

### 8.2 相关代码

```c
static uint8_t BNO085_ReceivePacket(uint8_t *channel, uint16_t *payloadLen) {
    uint16_t totalLen;
    uint16_t len;
    uint16_t i;
    uint8_t raw[BNO085_PACKET_MAX + 4u];

    if (GPIO_ReadInputDataBit(BNO085_INT_PORT, BNO085_INT_PIN) != Bit_RESET) {
        return 0u;
    }

    g_bnoRxAttemptCount++;
    g_bnoLastRxFailCode = 0u;
    memset(g_bnoHeader, 0, sizeof(g_bnoHeader));

    if (!BNO085_ReadBlock(g_bnoHeader, 4u)) {
        g_bnoLastRxFailCode = 1u;
        return 0u;
    }

    totalLen = (uint16_t)(((uint16_t)g_bnoHeader[1] << 8) | g_bnoHeader[0]);
    totalLen &= (uint16_t)~0x8000u;
    if (totalLen < 4u) {
        g_bnoLastRxFailCode = 2u;
        return 0u;
    }

    len = (uint16_t)(totalLen - 4u);
    if (len == 0u || len > BNO085_PACKET_MAX) {
        g_bnoLastRxFailCode = 3u;
        return 0u;
    }

    if (!BNO085_ReadBlock(raw, (uint16_t)(len + 4u))) {
        g_bnoLastRxFailCode = 4u;
        return 0u;
    }

    for (i = 0; i < len; i++) {
        g_bnoData[i] = raw[i + 4u];
    }
    g_bnoLastRxFailCode = 0u;
    g_bnoRxPacketCount++;

    if (channel) *channel = g_bnoHeader[2];
    if (payloadLen) *payloadLen = len;
    return 1u;
}
```

### 8.3 这一步解决了什么

这一步解决的是：

- 原来收包方式不稳
- header/payload 边界处理不对
- 读包失败时看不清是哪个阶段失败

改成现在这个版本后，至少能稳定地区分：

- 读 header 失败
- 长度非法
- 第二阶段读 payload 失败
- 成功拿到 payload

---

## 9. 第六步：发现并修复接收缓冲区太小的问题

### 9.1 关键现象

在初始化过程中，串口诊断暴露出一个重要现象：

- 初始化阶段会收到一个 **远大于 64 字节** 的 SHTP 包
- 之前的包长度上限太小，导致把本来合法的大包直接当成非法包丢弃

当前代码中的定义是：

```c
#define BNO085_PACKET_MAX             384u
```

之前这个值较小，后面被提升到了 `384`。

### 9.2 为什么这一步关键

如果接收缓冲区太小，`BNO085_ReceivePacket()` 里这段判断会直接失败：

```c
len = (uint16_t)(totalLen - 4u);
if (len == 0u || len > BNO085_PACKET_MAX) {
    g_bnoLastRxFailCode = 3u;
    return 0u;
}
```

也就是说：

- 包实际上已经来了
- 但因为本地缓存太小，被我们自己丢掉了
- 后面当然拿不到第一帧姿态，也就不可能有 `yaw`

因此，把 `BNO085_PACKET_MAX` 增大，是初始化能继续往下走的关键修改之一。

---

## 10. 第七步：发现 `GAME_ROTATION_VECTOR` 实际长度是 17，而不是原先假设的 19

### 10.1 关键现象

后续串口输出显示，等待第一帧报告时已经收到了：

- `ch=3`
- `rid=0x08`
- `len=17`

其中：

- `rid=0x08` 就是 `GAME_ROTATION_VECTOR`
- 但我们的解析逻辑原来要求 `len >= 19`

这意味着：

- 目标姿态包其实已经到了
- 但是被解析层错误地拒绝了

### 10.2 相关代码

现在的解析逻辑是：

```c
static uint8_t BNO085_ParseSensorReport(ICM42688_Data_t *data, uint16_t len) {
    uint8_t reportId;

    if (len < 9u) return 0u;
    if (g_bnoData[0] != BNO085_REPORT_BASE_TIMESTAMP) return 0u;

    reportId = g_bnoData[5];
    g_bnoLastReportId = reportId;

    if (reportId == BNO085_SENSOR_ACCELEROMETER && len >= 15u) {
        ...
        return 1u;
    } else if (reportId == BNO085_SENSOR_GYROSCOPE && len >= 15u) {
        ...
        return 1u;
    } else if ((reportId == BNO085_SENSOR_ROTATION_VECTOR || reportId == BNO085_SENSOR_GAME_ROTATION_VECTOR) && len >= 17u) {
        data->q1 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[9]), BNO085_Q_ROTATION);
        data->q2 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[11]), BNO085_Q_ROTATION);
        data->q3 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[13]), BNO085_Q_ROTATION);
        data->q0 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[15]), BNO085_Q_ROTATION);
        data->ahrsInited = 1u;
        BNO085_UpdateEuler(data);
        return 1u;
    }

    return 0u;
}
```

这里最关键的修复点就是：

- 把 `GAME_ROTATION_VECTOR` 的判断门槛改成 **`len >= 17u`**

### 10.3 为什么这一步能让 READY 成立

因为 `BNO085_WaitForFirstReport()` 的成功条件是：

```c
if (ICM42688_ReadAll(data) && data->ahrsInited) {
    BNO085_Trace("BI:RPT\r\n");
    return 1u;
}
```

而 `data->ahrsInited` 正是在上面的四元数分支里置为 `1u`。

所以只要：

- 收到 `rid=0x08`
- 且 `len=17`
- 且我们允许 `len >= 17`

那么：

- 四元数就会被成功解析
- `ahrsInited` 会变成 `1`
- 初始化就能真正跨过 `WAIT_REPORT`
- 最终进入 `READY`

---

## 11. 第八步：把四元数真正转成 yaw

一旦四元数成功解析，yaw/pitch/roll 的计算就在 `BNO085_UpdateEuler()` 中完成。

### 11.1 相关代码

```c
static void BNO085_UpdateEuler(ICM42688_Data_t *data) {
    float q0 = data->q0;
    float q1 = data->q1;
    float q2 = data->q2;
    float q3 = data->q3;

    g_bnoRawYaw = -atan2f(2.0f * q1 * q2 + 2.0f * q0 * q3,
                          -2.0f * q2 * q2 - 2.0f * q3 * q3 + 1.0f) * BNO085_RAD2DEG;
    data->yaw = BNO085_WrapDeg(g_bnoRawYaw - g_bnoYawBase);
    data->pitch = -asinf(ICM42688_ClampFloat(-2.0f * q1 * q3 + 2.0f * q0 * q2, -1.0f, 1.0f)) * BNO085_RAD2DEG;
    data->roll = atan2f(2.0f * q2 * q3 + 2.0f * q0 * q1,
                        -2.0f * q1 * q1 - 2.0f * q2 * q2 + 1.0f) * BNO085_RAD2DEG;
}
```

### 11.2 数据结构

四元数和欧拉角最终都存到 `ICM42688_Data_t` 中：

```c
typedef struct {
    ...
    float yawRate;
    float yaw;
    float pitch;
    float roll;

    float q0;
    float q1;
    float q2;
    float q3;
    ...
    uint8_t ahrsInited;
} ICM42688_Data_t;
```

因此，只要四元数解析成功：

- `q0/q1/q2/q3` 会更新
- `yaw/pitch/roll` 会更新
- 后续控制器也能使用这些值

---

## 12. 第九步：定位“初始化成功但 yaw 仍不变化”的根因

### 12.1 现象

到这里为止，系统已经能进入 `READY`，串口也能看到：

- `who=0x85`
- `st=6`
- `rid=0x08`
- `len=17`

但用户实际左右晃动板子时发现：

- `yaw` 还是几乎不动

### 12.2 真正根因

这时我发现问题已经不在 BNO085 初始化，而在 **主循环是否持续更新 IMU**。

主循环中的 IMU 更新在 `Hardware/Control.c` 里：

```c
static void control_imu_update(ControlSystem_t *sys) {
    if (ICM42688_ReadAll(&sys->icm)) {
        g_icmOk++;
        g_icmReadOkCount = g_icmOk;
        ICM42688_UpdateYaw(&sys->icm, 0.005f);
    } else {
        g_icmFail++;
        g_icmReadFailCount = g_icmFail;
    }

    sys->yawErr = ICM42688_GetYawError(sys->targetYaw, sys->icm.yaw);
    ...
}
```

而 `Control_Tick()` 中只有在 `TEST_MODE_NORMAL` 下才会调用它：

```c
if ((sys->tickCount % 5) == 0) {
    if (sys->testMode == TEST_MODE_NORMAL) {
        control_imu_update(sys);
    }
}
```

问题在于：

- 为了便于调试，之前临时跳过了开机自动 IMU 初始化
- 这样系统可能停留在 `TEST_MODE_SPEED_ONLY`
- 在这个模式下，`control_imu_update()` 根本不会被持续调用

结果就是：

- 初始化阶段虽然拿到过第一帧姿态
- 但主循环不持续读 IMU
- `yaw` 就像“冻结”了一样

### 12.3 修复方法

因此我把 `#IMU_INIT!` 的成功分支改成：

```c
if (ICM42688_GetWhoAmI() == 0x85u) {
    sys->testMode = TEST_MODE_NORMAL;
    (void)ICM42688_ReadAll(&sys->icm);
    control_imu_zero_update(sys);
    ICM42688_ResetAttitude(&sys->icm);
    sys->targetYaw = 0.0f;
    send_ok("IMU_INIT");
}
```

这一步的作用是：

- 让系统从 `SPEED_ONLY` 回到 `NORMAL`
- 之后 `Control_Tick()` 才会周期性调用 `control_imu_update()`
- 主循环才会持续刷新 `yaw`

这是后期非常关键的一次修复。

---

## 13. 第十步：直接输出四元数，确认姿态真的在变化

### 13.1 为什么要输出四元数

当用户说“正常来说 yaw/W 值应该抖动”时，最有效的判断方法不是继续猜，而是直接把四元数输出出来。

原因很简单：

- 如果 `q0/q1/q2/q3` 在变化，而 `yaw` 不变，那么是姿态换算问题
- 如果 `q0/q1/q2/q3` 也不变，那么是数据源没有持续更新

### 13.2 实施方法

因此我在 `BNO085_DebugSerial_Send()` 中增加了：

- `q0`
- `q1`
- `q2`
- `q3`

这样就能在串口中直接看到四元数本体。

---

## 14. 关键函数调用链总结

下面用一条“从初始化到 yaw 输出”的调用链说明整个系统是如何工作的。

### 14.1 初始化链路

1. 串口收到 `#IMU_INIT!`
2. `Control.c` 中命令解析调用 `ICM42688_Init()`
3. `ICM42688_Init()` 内部会尝试多个 BNO085 地址
4. 调用 `BNO085_AttemptInitAtAddr()`
5. 依次执行：
   - `BNO085_WaitPresent()`
   - `BNO085_WaitPresentStable()`
   - `BNO085_RequestProductId()`
   - `BNO085_EnableReport(BNO085_SENSOR_GAME_ROTATION_VECTOR, ...)`
   - `BNO085_WaitForFirstReport()`
6. 如果第一帧姿态成功收到，则进入 `READY`

### 14.2 收包与解析链路

1. `ICM42688_ReadAll()` 调用 `BNO085_ReceivePacket()`
2. `BNO085_ReceivePacket()`：
   - 先检查 `INT`
   - 先读 4 字节 header
   - 再读完整 `4 + payload`
   - 去掉重复 header，把 payload 放到 `g_bnoData`
3. `ICM42688_ReadAll()` 根据 `channel` 判断是否为 reports
4. 若是报告通道，则调用 `BNO085_ParseSensorReport()`
5. 若 `reportId == BNO085_SENSOR_GAME_ROTATION_VECTOR` 且长度满足，则解析：
   - `q1`
   - `q2`
   - `q3`
   - `q0`
6. 调用 `BNO085_UpdateEuler()` 计算：
   - `yaw`
   - `pitch`
   - `roll`

### 14.3 主循环持续更新链路

1. `Control_Tick()` 每 5 个 tick 检查一次
2. 若 `sys->testMode == TEST_MODE_NORMAL`
3. 则调用 `control_imu_update()`
4. `control_imu_update()` 内部：
   - 调用 `ICM42688_ReadAll(&sys->icm)`
   - 调用 `ICM42688_UpdateYaw(&sys->icm, 0.005f)`
5. 于是 `sys->icm.yaw` 会持续刷新

---

## 15. 最终验证方法

最终验证不是只看“初始化成功”四个字，而是看多项指标是否同时成立。

### 15.1 初始化成功的标志

串口中看到：

- `who=0x85`
- `addr=0x4A`
- `st=6`
- `rid=0x08`
- `len=17`

这说明：

- BNO085 地址识别正确
- 已进入 `READY`
- 已收到 `GAME_ROTATION_VECTOR`

### 15.2 主循环持续读 IMU 的标志

串口中看到：

- `iok` 持续增加

例如：

- `iok=8`
- `iok=48`
- `iok=88`
- `iok=128`
- ...

这说明主循环已经在持续调用：

- `ICM42688_ReadAll()`
- `ICM42688_UpdateYaw()`

### 15.3 yaw 已开始变化的标志

最后的串口监看结果中，已经看到：

```text
BNO tick=35000 run=0 y=-0.000 ...
BNO tick=35200 run=0 y=-0.007 ...
```

同时四元数也有变化，例如：

```text
q0=0.00006
q1 从 -0.00006 变化到 0.00012
q2 / q3 也有变化
```

这说明：

- BNO085 已经在持续输出姿态
- 四元数不是死值
- `yaw` 已经出现微弱抖动

也就是说，从工程验证角度，可以判定：

- **BNO085 已经从“读不出 yaw”修复到“可以读出 yaw”**

---

## 16. 本次修复中最关键的几项结论

### 16.1 结论一：问题并不只有一个

这次不是单点故障，而是多层问题叠加：

- 收包方式问题
- 缓冲区过小问题
- `GAME_ROTATION_VECTOR` 长度门槛错误
- 初始化后主循环不持续更新的问题

### 16.2 结论二：最核心的代码修复有四项

最关键的修复包括：

- **按两段式方式重写 `BNO085_ReceivePacket()`**
- **将 `BNO085_PACKET_MAX` 提高到 `384`**
- **将 `GAME_ROTATION_VECTOR` 解析门槛改为 `len >= 17u`**
- **在 `#IMU_INIT!` 成功后切回 `TEST_MODE_NORMAL`，恢复主循环持续更新 IMU**

### 16.3 结论三：调试能力本身也是修复的一部分

这次能成功，不只是因为改了业务代码，还因为增加了大量“可观测性”：

- 初始化阶段 trace
- 首帧等待失败细分
- 最近包信息输出
- 四元数输出
- 主循环 IMU 读成功计数输出

没有这些可观测性，很难快速确认问题发生在哪一层。

---

## 17. 给后续维护者的建议

如果以后 BNO085 再次出现“读不到 yaw”或“初始化失败”，建议按下面顺序排查：

### 17.1 先看是否进入 READY

优先看串口字段：

- `who`
- `st`
- `rid`
- `len`

如果：

- `who != 0x85`
- 或 `st != 6`

那么先排初始化链路。

### 17.2 再看 `rid` 和 `len`

如果已经能看到：

- `rid=0x08`
- `len=17`

说明目标姿态包已经到了。

### 17.3 再看 `iok` 是否增长

如果 `iok` 不增长，说明主循环没有持续更新 IMU，需要检查：

- `sys->testMode`
- `Control_Tick()`
- `control_imu_update()` 是否被调到

### 17.4 最后看四元数

如果 `yaw` 看起来不对，再看：

- `q0`
- `q1`
- `q2`
- `q3`

先判断是四元数本身不动，还是 `yaw` 换算有问题。

---

## 18. 最终结论

这次把 BNO085 从“读不出 yaw”修到“能够读出 yaw”，本质上完成了以下几件事：

1. **把初始化过程做成可在线触发、可观测**
2. **把收包流程改成更符合 BNO085/SHTP 行为的两段式读取**
3. **修复了缓冲区过小导致的大包误判问题**
4. **修复了 `GAME_ROTATION_VECTOR` 长度判断过严的问题**
5. **修复了初始化后主循环没有持续读 IMU 的状态机问题**
6. **最终通过串口直接验证四元数和 yaw 已经开始变化**

因此，到当前这一步可以明确认为：

- **BNO085 的 yaw 读取功能已经被成功打通**
- **后续工作重点可以从“能不能读到 yaw”切换到“yaw 稳定性、零点、控制效果优化”**

---

## 19. 当前与本问题直接相关的关键函数清单

为方便后续查阅，这里列出本次修复过程中最重要的函数：

- `ICM42688_Init()`
- `BNO085_AttemptInitAtAddr()`
- `BNO085_RequestProductId()`
- `BNO085_EnableReport()`
- `BNO085_WaitForFirstReport()`
- `BNO085_ReceivePacket()`
- `ICM42688_ReadAll()`
- `BNO085_ParseSensorReport()`
- `BNO085_UpdateEuler()`
- `ICM42688_UpdateYaw()`
- `control_imu_update()`
- `Control_Tick()`
- `BNO085_DebugSerial_Send()`

这些函数基本构成了从“BNO085 收包”到“yaw 输出给控制器”的完整链路。
