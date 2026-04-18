"""
自动测试脚本（无需交互）
自动运行 10 秒并记录数据
"""

import serial
import serial.tools.list_ports
import time
from datetime import datetime
import re

def find_ch340_port():
    """查找 CH340 串口"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'CH340' in port.description or 'USB-SERIAL' in port.description:
            return port.device
    return None

def parse_data_line(line):
    """解析数据行"""
    data = {}
    pattern = r'(\w+)=([-+]?\d*\.?\d+)'
    matches = re.findall(pattern, line)
    for key, value in matches:
        try:
            if '.' in value:
                data[key] = float(value)
            else:
                data[key] = int(value)
        except:
            data[key] = value
    return data

def run_auto_test(duration=10):
    """自动运行测试"""

    # 查找串口
    port = find_ch340_port()
    if not port:
        print("[ERROR] 未找到 CH340 串口")
        return

    print(f"[OK] 找到串口：{port}")

    # 打开串口
    try:
        ser = serial.Serial(port=port, baudrate=115200, timeout=0.1)
        print(f"[OK] 串口已打开：{port} @ 115200")
    except Exception as e:
        print(f"[ERROR] 无法打开串口：{e}")
        return

    # 清空缓冲区
    ser.reset_input_buffer()
    time.sleep(0.5)

    # 创建日志文件
    data_dir = r"F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project-GPT\STM32project\000Data"
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = f"{data_dir}\\test_log_{timestamp}.csv"

    print(f"\n{'='*60}")
    print(f"自动测试")
    print(f"{'='*60}")
    print(f"测试时长：{duration} 秒")
    print(f"日志文件：{log_file}")
    print(f"{'='*60}\n")

    print("[提示] 测试将在 3 秒后开始，请准备按 PB5 按键...")
    for i in range(3, 0, -1):
        print(f"倒计时：{i}...")
        time.sleep(1)
    print("开始记录！\n")

    # 记录数据
    start_time = time.time()
    line_buffer = ""
    line_count = 0
    run_detected = False
    max_pwm_left = 0
    max_pwm_right = 0

    with open(log_file, 'w', encoding='utf-8') as f:
        # 写入CSV头
        f.write("时间(s),tick,run,spd,leftPWM,rightPWM,leftEnc,rightEnc,yaw,yawErr,headingCorr,原始数据\n")

        print("时间(s) | 行数 | RUN | SPD | L_PWM | R_PWM | L_ENC | R_ENC")
        print("-" * 75)

        try:
            while (time.time() - start_time) < duration:
                # 读取串口数据
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting)
                    try:
                        text = data.decode('ascii', errors='ignore')
                        line_buffer += text

                        # 按行处理
                        while '\n' in line_buffer:
                            line, line_buffer = line_buffer.split('\n', 1)
                            line = line.strip()

                            if line:
                                line_count += 1
                                elapsed = time.time() - start_time

                                # 解析数据
                                parsed = parse_data_line(line)

                                # 检测启动
                                if parsed.get('run', 0) == 1 and not run_detected:
                                    run_detected = True
                                    print(f"\n[OK] 检测到小车启动！(时间: {elapsed:.1f}s)\n")

                                # 记录最大 PWM
                                max_pwm_left = max(max_pwm_left, abs(parsed.get('L', 0)))
                                max_pwm_right = max(max_pwm_right, abs(parsed.get('R', 0)))

                                # 写入CSV
                                f.write(f"{elapsed:.3f},")
                                f.write(f"{parsed.get('tick', 0)},")
                                f.write(f"{parsed.get('run', 0)},")
                                f.write(f"{parsed.get('spd', 0)},")
                                f.write(f"{parsed.get('L', 0)},")
                                f.write(f"{parsed.get('R', 0)},")
                                f.write(f"{parsed.get('el', 0)},")
                                f.write(f"{parsed.get('er', 0)},")
                                f.write(f"{parsed.get('y', 0)},")
                                f.write(f"{parsed.get('e', 0)},")
                                f.write(f"{parsed.get('c', 0)},")
                                f.write(f'"{line}"\n')

                                # 显示进度
                                if line_count % 10 == 0:
                                    print(f"{elapsed:6.1f}s | {line_count:4d} | "
                                          f"{parsed.get('run', 0):3d} | "
                                          f"{parsed.get('spd', 0):3d} | "
                                          f"{parsed.get('L', 0):5d} | "
                                          f"{parsed.get('R', 0):5d} | "
                                          f"{parsed.get('el', 0):5d} | "
                                          f"{parsed.get('er', 0):5d}")

                    except Exception as e:
                        pass

                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n\n[INFO] 用户中断测试")

    # 关闭串口
    ser.close()

    # 测试总结
    elapsed_total = time.time() - start_time
    print(f"\n{'='*60}")
    print(f"测试完成")
    print(f"{'='*60}")
    print(f"实际运行时间：{elapsed_total:.1f} 秒")
    print(f"接收行数：{line_count}")
    print(f"平均数据率：{line_count/elapsed_total:.1f} 行/秒")
    print(f"小车启动：{'是' if run_detected else '否'}")
    print(f"最大 PWM（左）：{max_pwm_left}")
    print(f"最大 PWM（右）：{max_pwm_right}")
    print(f"日志文件：{log_file}")
    print(f"{'='*60}\n")

    if not run_detected:
        print("[WARNING] 未检测到小车启动")
        print("可能原因：")
        print("  1. 未按 PB5 按键")
        print("  2. 按键未响应（检查接线）")
    else:
        print("[OK] 测试成功！")
        print(f"建议：用 Excel 打开日志文件查看详细数据")

        # 简单诊断
        if max_pwm_left == 0 and max_pwm_right == 0:
            print("\n[WARNING] PWM 输出为 0")
            print("可能原因：")
            print("  1. 目标速度为 0")
            print("  2. 编码器未工作（速度反馈为 0）")
            print("  3. PID 参数不合理")

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 小车自动测试")
    print("=" * 60)
    print()

    run_auto_test(duration=10)

    print("\n测试脚本结束")
