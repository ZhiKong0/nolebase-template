"""
STM32 按键和电机诊断工具
用于排查按键无法启动电机的问题
"""

import sys
import time

print("=" * 60)
print("STM32 按键和电机诊断工具")
print("=" * 60)

# 检查点 1: OLED 显示
print("\n[检查点 1] OLED 显示")
print("✅ OLED 显示正常（用户已确认）")
print("   说明：单片机运行正常，主循环在执行")

# 检查点 2: 按键硬件
print("\n[检查点 2] 按键硬件连接")
print("请检查：")
print("  1. 按键一端是否连接到 PB5")
print("  2. 按键另一端是否连接到 GND")
print("  3. 按键是否正常（用万用表测试通断）")
input("按回车继续...")

# 检查点 3: 按键响应
print("\n[检查点 3] 按键响应测试")
print("观察 OLED 显示：")
print("  - 第 1 行应该显示 'DIAG T0000 OK000'")
print("  - 按下按键时，第 1 行应该变为 'Starting...' 然后 'Running...'")
print("  - 第 4 行的 'RUN=0' 应该变为 'RUN=1'")
print("\n请按下 PB5 按键（短按 <1秒）")
response = input("OLED 显示是否有变化？(y/n): ").lower()

if response == 'n':
    print("\n❌ 按键没有响应")
    print("\n可能原因：")
    print("  1. 按键接线错误（检查 PB5 和 GND）")
    print("  2. 按键损坏（用万用表测试）")
    print("  3. 按键消抖时间过长（代码中是 20ms）")
    print("  4. 主循环被阻塞（检查 Control_Tick 函数）")
    print("\n建议操作：")
    print("  1. 用万用表测试按键按下时 PB5 是否接地")
    print("  2. 检查按键是否为常开型（按下导通）")
    print("  3. 尝试直接用导线短接 PB5 和 GND")
    sys.exit(1)
else:
    print("✅ 按键响应正常")

# 检查点 4: 运行状态
print("\n[检查点 4] 运行状态检查")
print("观察 OLED 第 4 行：")
run_status = input("RUN= 后面的数字是多少？(0/1): ")

if run_status == '0':
    print("\n❌ 系统未进入运行状态")
    print("\n可能原因：")
    print("  1. Control_Start() 函数没有正确设置 isRunning 标志")
    print("  2. Control_Tick() 函数中有逻辑错误")
    print("  3. MPU6050 初始化失败导致控制系统无法启动")
    print("\n建议操作：")
    print("  1. 检查 OLED 第 1 行的 'OK' 计数是否增加（MPU6050 读取成功次数）")
    print("  2. 检查 OLED 第 2 行的 'F' 计数（MPU6050 读取失败次数）")
    print("  3. 如果 F 计数很高，说明 MPU6050 通信有问题")

    mpu_ok = input("\nOLED 第 1 行 'OK' 后面的数字是否在增加？(y/n): ").lower()
    if mpu_ok == 'n':
        print("\n❌ MPU6050 读取失败")
        print("\n可能原因：")
        print("  1. MPU6050 未连接或接线错误")
        print("  2. I2C 通信问题（检查 PB12/PB13 和上拉电阻）")
        print("  3. MPU6050 地址错误（应该是 0x68）")
        print("\n建议操作：")
        print("  1. 检查 MPU6050 的 VCC、GND、SCL(PB12)、SDA(PB13) 接线")
        print("  2. 检查 I2C 上拉电阻（4.7kΩ 到 3.3V）")
        print("  3. 用 I2C 扫描工具确认 MPU6050 地址")
    sys.exit(1)
else:
    print("✅ 系统已进入运行状态")

# 检查点 5: 电机驱动
print("\n[检查点 5] 电机驱动检查")
print("观察 OLED 第 3 行：")
print("  - 'L' 后面是左轮 PWM 值")
print("  - 'R' 后面是右轮 PWM 值")
pwm_left = input("左轮 PWM 值是多少？: ")
pwm_right = input("右轮 PWM 值是多少？: ")

if pwm_left == '0' and pwm_right == '0':
    print("\n❌ PWM 输出为 0")
    print("\n可能原因：")
    print("  1. 目标速度设置为 0（代码中设置为 5）")
    print("  2. PID 控制器输出为 0")
    print("  3. 速度环没有正常工作")
    print("\n建议操作：")
    print("  1. 检查 g_controlSys.targetSpeed 是否为 5")
    print("  2. 检查编码器是否正常工作")
    print("  3. 检查速度 PID 参数是否合理")
    sys.exit(1)
else:
    print(f"✅ PWM 输出正常（左: {pwm_left}, 右: {pwm_right}）")

# 检查点 6: 电机硬件
print("\n[检查点 6] 电机硬件连接")
print("请检查：")
print("  1. TB6612 的 STBY 引脚（PB0）是否为高电平")
print("  2. TB6612 的 VM 引脚是否有电池电压（7.4V）")
print("  3. TB6612 的 VCC 引脚是否有 3.3V")
print("  4. 电机是否正确连接到 TB6612 的 AO1/AO2 和 BO1/BO2")
print("  5. 电池是否有电")
print("\n建议操作：")
print("  1. 用万用表测量 PB0 电压（应该是 3.3V）")
print("  2. 用万用表测量 TB6612 VM 引脚电压（应该是 7.4V）")
print("  3. 用示波器测量 PA8/PA9 的 PWM 波形")
print("  4. 检查电机是否损坏（直接接电池测试）")

# 检查点 7: 目标速度
print("\n[检查点 7] 目标速度设置")
print("代码中设置的目标速度：g_controlSys.targetSpeed = 5")
print("这个值可能太小，导致 PWM 输出不足以驱动电机")
print("\n建议操作：")
print("  1. 尝试增加目标速度到 20-30")
print("  2. 修改 main.c 第 30 行：g_controlSys.targetSpeed = 30;")
print("  3. 重新编译烧录")

print("\n" + "=" * 60)
print("诊断完成")
print("=" * 60)
print("\n总结：")
print("如果所有检查点都通过，但电机仍不转，最可能的原因是：")
print("  1. 目标速度太小（5 可能不够）")
print("  2. TB6612 STBY 引脚未使能（PB0 应该为高）")
print("  3. 电池电压不足或未连接")
print("  4. 电机或 TB6612 损坏")
