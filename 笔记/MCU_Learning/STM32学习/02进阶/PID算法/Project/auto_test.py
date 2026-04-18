"""
STM32 小车自动化测试脚本
通过串口控制小车运行并记录数据
"""

import serial
import serial.tools.list_ports
import time
import struct
from datetime import datetime

# 查找 CH340 串口
def find_ch340_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'CH340' in port.description or 'USB-SERIAL' in port.description:
            return port.device
    return None

# 解析 VOFA+ JustFloat 格式数据
def parse_vofa_frame(data):
    """
    解析 VOFA+ JustFloat 格式
    每个浮点数 4 字节，帧尾 0x00 0x00 0x80 0x7F
    """
    FRAME_TAIL = b'\x00\x00\x80\x7f'

    if len(data) < 8:  # 至少需要 1 个浮点数 + 帧尾
        return None

    # 查找帧尾
    tail_idx = data.find(FRAME_TAIL)
    if tail_idx == -1:
        return None

    # 提取浮点数
    num_floats = tail_idx // 4
    values = []
    for i in range(num_floats):
        try:
            value = struct.unpack('<f', data[i*4:(i+1)*4])[0]
            values.append(value)
        except:
            return None

    return values

# 主测试函数
def run_test(duration=10, target_speed=30):
    """
    运行测试
    duration: 测试时长（秒）
    target_speed: 目标速度
    """

    # 查找串口
    port = find_ch340_port()
    if not port:
        print("[ERROR] 未找到 CH340 串口")
        print("\n可用串口：")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description}")
        return

    print(f"[OK] 找到串口：{port}")

    # 打开串口
    try:
        ser = serial.Serial(
            port=port,
            baudrate=115200,  # VOFA+ 默认波特率
            timeout=0.1
        )
        print(f"[OK] 串口已打开：{port} @ 115200")
    except Exception as e:
        print(f"[ERROR] 无法打开串口：{e}")
        return

    # 清空缓冲区
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.5)

    # 创建日志文件
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = f"test_log_{timestamp}.txt"

    print(f"\n{'='*60}")
    print(f"开始测试")
    print(f"{'='*60}")
    print(f"测试时长：{duration} 秒")
    print(f"目标速度：{target_speed}")
    print(f"日志文件：{log_file}")
    print(f"{'='*60}\n")

    # 发送启动命令（通过 VOFA+ 协议）
    # 格式：#SPEED=30\n
    start_cmd = f"#SPEED={target_speed}\n".encode()
    ser.write(start_cmd)
    print(f"[OK] 已发送启动命令：#SPEED={target_speed}")
    time.sleep(0.5)

    # 记录数据
    start_time = time.time()
    data_buffer = bytearray()
    frame_count = 0

    with open(log_file, 'w', encoding='utf-8') as f:
        f.write(f"# STM32 小车测试数据\n")
        f.write(f"# 开始时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"# 测试时长: {duration} 秒\n")
        f.write(f"# 目标速度: {target_speed}\n")
        f.write(f"# 串口: {port} @ 115200\n")
        f.write(f"#\n")
        f.write(f"# 时间(s), 数据...\n")
        f.write(f"#" + "="*60 + "\n\n")

        print("开始记录数据...")
        print("时间(s) | 帧数 | 数据预览")
        print("-" * 60)

        try:
            while (time.time() - start_time) < duration:
                # 读取串口数据
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting)
                    data_buffer.extend(data)

                    # 尝试解析帧
                    while len(data_buffer) >= 8:
                        values = parse_vofa_frame(data_buffer)
                        if values:
                            frame_count += 1
                            elapsed = time.time() - start_time

                            # 写入日志
                            f.write(f"{elapsed:.3f}")
                            for v in values:
                                f.write(f", {v:.3f}")
                            f.write("\n")

                            # 显示进度（每 0.5 秒显示一次）
                            if frame_count % 25 == 0:  # 假设 50Hz，每 0.5s 约 25 帧
                                preview = ", ".join([f"{v:.1f}" for v in values[:5]])
                                if len(values) > 5:
                                    preview += "..."
                                print(f"{elapsed:6.1f}s | {frame_count:4d} | {preview}")

                            # 移除已解析的数据
                            FRAME_TAIL = b'\x00\x00\x80\x7f'
                            tail_idx = data_buffer.find(FRAME_TAIL)
                            data_buffer = data_buffer[tail_idx + 4:]
                        else:
                            # 没有完整帧，等待更多数据
                            break

                time.sleep(0.01)  # 10ms 采样间隔

        except KeyboardInterrupt:
            print("\n\n[WARNING] 用户中断测试")

        # 发送停止命令
        stop_cmd = b"#STOP\n"
        ser.write(stop_cmd)
        print(f"\n[OK] 已发送停止命令")

    # 关闭串口
    ser.close()

    # 测试总结
    elapsed_total = time.time() - start_time
    print(f"\n{'='*60}")
    print(f"测试完成")
    print(f"{'='*60}")
    print(f"实际运行时间：{elapsed_total:.1f} 秒")
    print(f"接收帧数：{frame_count}")
    print(f"平均帧率：{frame_count/elapsed_total:.1f} Hz")
    print(f"日志文件：{log_file}")
    print(f"{'='*60}\n")

    # 简单数据分析
    if frame_count > 0:
        print("数据分析：")
        print(f"  - 总帧数：{frame_count}")
        print(f"  - 数据完整性：{'良好' if frame_count > duration * 40 else '一般'}")
        print(f"  - 建议：查看日志文件进行详细分析")
    else:
        print("[WARNING] 警告：未接收到任何数据")
        print("可能原因：")
        print("  1. 串口波特率不匹配（当前 115200）")
        print("  2. VOFA+ 协议未启用")
        print("  3. 单片机未发送数据")

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 小车自动化测试")
    print("=" * 60)
    print()

    # 运行测试
    run_test(duration=10, target_speed=30)

    print("\n测试脚本结束")
