"""
Read boot messages - capture data immediately after reset
"""

import serial
import serial.tools.list_ports
import time

def find_ch340_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'CH340' in port.description or 'USB-SERIAL' in port.description:
            return port.device
    return None

port = find_ch340_port()
if not port:
    print("[ERROR] CH340 port not found")
    exit()

print(f"[OK] Found port: {port}")

ser = serial.Serial(port=port, baudrate=115200, timeout=0.1)
print(f"[OK] Port opened\n")

print("=" * 60)
print("Boot Message Capture")
print("=" * 60)
print("Please reset the STM32 board now (press reset button)")
print("Listening for boot messages...")
print("=" * 60)
print()

# Read for 5 seconds
start_time = time.time()
all_data = []
while (time.time() - start_time) < 5.0:
    if ser.in_waiting > 0:
        data = ser.read(ser.in_waiting)
        all_data.append(data)
    time.sleep(0.01)

# Combine and decode
full_data = b''.join(all_data)
text = full_data.decode('ascii', errors='ignore')

print(f"Received {len(full_data)} bytes total\n")
print("All messages:")
print("-" * 60)
for line in text.split('\n'):
    if line.strip():
        print(line.strip())

print("-" * 60)

# Check for specific messages
if "BOOT" in text:
    print("\n[OK] BOOT message found")
if "VOFA_INIT_OK" in text:
    print("[OK] VOFA_INIT_OK message found")
if "USART2_RX_INT_ENABLED" in text:
    print("[OK] USART2 RX interrupt enabled")

ser.close()
print("\n" + "=" * 60)
print("Capture complete")
print("=" * 60)
