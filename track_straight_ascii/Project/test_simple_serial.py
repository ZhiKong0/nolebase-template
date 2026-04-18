"""
Simple serial test - just send and receive raw bytes
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

ser.reset_input_buffer()
ser.reset_output_buffer()
time.sleep(1)

print("=" * 60)
print("Simple Serial Test")
print("=" * 60)
print()

# Read initial data
print("[INFO] Reading initial data for 2 seconds...")
start_time = time.time()
byte_count = 0
while (time.time() - start_time) < 2.0:
    if ser.in_waiting > 0:
        data = ser.read(ser.in_waiting)
        byte_count += len(data)
    time.sleep(0.01)

print(f"[INFO] Received {byte_count} bytes in 2 seconds")
print(f"[INFO] Data rate: {byte_count/2:.1f} bytes/sec")
print()

# Send command byte by byte
print("[INFO] Sending #RUN! byte by byte...")
cmd = b"#RUN!"
for i, byte in enumerate(cmd):
    print(f"  Sending byte {i}: {chr(byte)} (0x{byte:02X})")
    ser.write(bytes([byte]))
    time.sleep(0.1)  # 100ms between bytes

print()
print("[INFO] Waiting for response (3 seconds)...")
time.sleep(3)

# Read response
if ser.in_waiting > 0:
    data = ser.read(ser.in_waiting)
    text = data.decode('ascii', errors='ignore')
    print(f"[INFO] Response ({len(data)} bytes):")
    for line in text.split('\n')[:10]:
        if line.strip():
            print(f"  {line.strip()}")

    if "OK RUN" in text:
        print("\n[OK] Command acknowledged!")
    elif "CMD_RX" in text:
        print("\n[OK] Command received (debug output detected)")
    else:
        print("\n[WARNING] No command acknowledgment")
else:
    print("[WARNING] No response received")

ser.close()
print("\n" + "=" * 60)
print("Test complete")
print("=" * 60)
