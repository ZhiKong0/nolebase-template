"""
STM32 小车完整测试脚本
解析文本格式数据并记录
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
    """
    解析数据行
    格式示例：HB tick=87769 run=0 trim=0 ta=0.00 at=1.00 ...
    """
    data = {}

    # 提取所有 key=value 对
    pattern = r'(\w+)=([-+]?\d*\.?\d+)'
    matches = re.findall(pattern, line)

    for key, value in matches:
        try:
            # 尝试转换为浮点数
            if '.' in value:
                data[key] = float(value)
            else:
                data[key] = int(value)
        except:
            data[key] = value

    return data

def run_test(duration=10):
    """运行测试"""

    # 查找串口
    port = find_ch340_port()
    if not port:
        print("[ERROR] 未找到 CH340 串口")
        return

    print(f"[OK] 找到串口：{port}")

    # 打开串口
    try:
        ser = serial.Serial(
            port=port,
            baudrate=115200,
            timeout=0.1
        )
        print(f"[OK] 串口已打开：{port} @ 115200")
    except Exception as e:
        print(f"[ERROR] 无法打开串口：{e}")
        return

    # 清空缓冲区
    ser.reset_input_buffer()
    time.sleep(0.5)

    # 创建日志文件
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = f"test_log_{timestamp}.csv"

    print(f"\n{'='*60}")
    print(f"开始测试")
    print(f"{'='*60}")
    print(f"测试时长：{duration} 秒")
    print(f"日志文件：{log_file}")
    print(f"{'='*60}\n")

    # 记录数据
    start_time = time.time()
    line_buffer = ""
    line_count = 0

    with open(log_file, 'w', encoding='utf-8') as f:
        # 写入CSV头
        f.write("时间(s),tick,run,trim,leftPWM,rightPWM,leftSpeed,rightSpeed,yaw,yawErr,headingCorr,原始数据\n")

        print("开始记录数据...")
        print("时间(s) | 行数 | RUN | L_PWM | R_PWM | L_SPD | R_SPD")
        print("-" * 70)

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

                                # 写入CSV
                                f.write(f"{elapsed:.3f},")
                                f.write(f"{parsed.get('tick', 0)},")
                                f.write(f"{parsed.get('run', 0)},")
                                f.write(f"{parsed.get('trim', 0)},")
                                f.write(f"{parsed.get('L', 0)},")
                                f.write(f"{parsed.get('R', 0)},")
                                f.write(f"{parsed.get('ls', 0)},")
                                f.write(f"{parsed.get('rs', 0)},")
                                f.write(f"{parsed.get('yaw', 0)},")
                                f.write(f"{parsed.get('ye', 0)},")
                                f.write(f"{parsed.get('hc', 0)},")
                                f.write(f'"{line}"\n')

                                # 显示进度（每 0.5 秒显示一次）
                                if line_count % 25 == 0:
                                    print(f"{elapsed:6.1f}s | {line_count:4d} | "
                                          f"{parsed.get('run', 0):3d} | "
                                          f"{parsed.get('L', 0):5d} | "
                                          f"{parsed.get('R', 0):5d} | "
                                          f"{parsed.get('ls', 0):5d} | "
                                          f"{parsed.get('rs', 0):5d}")

                    except Exception as e:
                        print(f"[WARNING] 解析错误：{e}")

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
    print(f"日志文件：{log_file}")
    print(f"{'='*60}\n")

    # 数据分析
    if line_count > 0:
        print("数据分析：")
        print(f"  - 总行数：{line_count}")
        print(f"  - 数据完整性：{'良好' if line_count > duration * 40 else '一般'}")
        print(f"  - 建议：用 Excel 或 Python 打开 CSV 文件进行详细分析")
        print(f"\n提示：")
        print(f"  - 如果 RUN=0，说明小车未启动，请按 PB5 按键启动")
        print(f"  - 如果 L_PWM 和 R_PWM 都是 0，说明目标速度为 0 或 PID 未工作")
        print(f"  - 如果 L_SPD 和 R_SPD 都是 0，说明编码器未连接或未工作")
    else:
        print("[WARNING] 未接收到任何数据")

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 小车完整测试")
    print("=" * 60)
    print()

    # 运行测试
    run_test(duration=10)

    print("\n测试脚本结束")
