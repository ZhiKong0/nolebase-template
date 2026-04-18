"""
简单串口监听工具
直接读取并显示串口原始数据
"""

import serial
import serial.tools.list_ports
import time
from datetime import datetime

# 查找 CH340 串口
def find_ch340_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'CH340' in port.description or 'USB-SERIAL' in port.description:
            return port.device
    return None

def monitor_serial(duration=10):
    """监听串口数据"""

    # 查找串口
    port = find_ch340_port()
    if not port:
        print("[ERROR] 未找到 CH340 串口")
        print("\n可用串口：")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description}")
        return

    print(f"[OK] 找到串口：{port}")

    # 尝试不同波特率
    baudrates = [115200, 921600, 9600]

    for baud in baudrates:
        print(f"\n{'='*60}")
        print(f"尝试波特率：{baud}")
        print(f"{'='*60}")

        try:
            ser = serial.Serial(
                port=port,
                baudrate=baud,
                timeout=0.1
            )
            print(f"[OK] 串口已打开：{port} @ {baud}")

            # 清空缓冲区
            ser.reset_input_buffer()
            time.sleep(0.5)

            # 读取数据
            start_time = time.time()
            byte_count = 0
            last_print = time.time()

            print("\n开始监听（按 Ctrl+C 停止）...")
            print("时间(s) | 字节数 | 数据预览（十六进制）")
            print("-" * 60)

            try:
                while (time.time() - start_time) < 3:  # 每个波特率测试 3 秒
                    if ser.in_waiting > 0:
                        data = ser.read(ser.in_waiting)
                        byte_count += len(data)

                        # 每 0.5 秒显示一次
                        if time.time() - last_print > 0.5:
                            elapsed = time.time() - start_time
                            hex_preview = ' '.join([f'{b:02X}' for b in data[:20]])
                            if len(data) > 20:
                                hex_preview += '...'
                            print(f"{elapsed:6.1f}s | {byte_count:6d} | {hex_preview}")
                            last_print = time.time()

                    time.sleep(0.01)

            except KeyboardInterrupt:
                print("\n[INFO] 用户中断")

            ser.close()

            if byte_count > 0:
                print(f"\n[OK] 接收到 {byte_count} 字节数据")
                print(f"[INFO] 正确的波特率可能是：{baud}")
                return baud
            else:
                print(f"\n[INFO] 未接收到数据")

        except Exception as e:
            print(f"[ERROR] 打开串口失败：{e}")

    print(f"\n{'='*60}")
    print("[WARNING] 所有波特率都未接收到数据")
    print("可能原因：")
    print("  1. 单片机未发送数据（检查 VOFA_Init 是否调用）")
    print("  2. 串口线未连接或接反（TX<->RX）")
    print("  3. 单片机未运行或死机")
    print(f"{'='*60}")

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 串口监听工具")
    print("=" * 60)
    print()

    monitor_serial(duration=10)

    print("\n监听结束")
