"""
Debug serial communication - check if commands are being received
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

# Find port
port = find_ch340_port()
if not port:
    print("[ERROR] CH340 port not found")
    exit()

print(f"[OK] Found port: {port}")

# Open serial
ser = serial.Serial(port=port, baudrate=115200, timeout=0.1)
print(f"[OK] Port opened\n")

# Clear buffer
ser.reset_input_buffer()
ser.reset_output_buffer()
time.sleep(0.5)

print("=" * 60)
print("Serial Command Debug Test")
print("=" * 60)
print()

# Test 1: Send #RUN! and wait for response
print("[TEST 1] Sending #RUN! command...")
ser.write(b"#RUN!")
time.sleep(0.5)

# Read response
response_lines = []
start_time = time.time()
while (time.time() - start_time) < 2.0:
    if ser.in_waiting > 0:
        data = ser.read(ser.in_waiting)
        try:
            text = data.decode('ascii', errors='ignore')
            response_lines.append(text)
        except:
            pass
    time.sleep(0.01)

print(f"Response received ({len(''.join(response_lines))} bytes):")
full_response = ''.join(response_lines)
for line in full_response.split('\n')[:10]:  # Show first 10 lines
    if line.strip():
        print(f"  {line.strip()}")

# Check if "OK RUN" is in response
if "OK RUN" in full_response:
    print("\n[OK] Command acknowledged!")
else:
    print("\n[WARNING] No 'OK RUN' acknowledgment found")

# Check if run=1 appears
if "run=1" in full_response:
    print("[OK] Car is running (run=1 detected)")
elif "run=0" in full_response:
    print("[FAIL] Car is NOT running (run=0)")
else:
    print("[WARNING] No run status found in response")

print()

# Test 2: Send #STAT command to check status
print("[TEST 2] Sending #STAT! command...")
ser.write(b"#STAT!")
time.sleep(0.3)

response_lines = []
start_time = time.time()
while (time.time() - start_time) < 1.0:
    if ser.in_waiting > 0:
        data = ser.read(ser.in_waiting)
        try:
            text = data.decode('ascii', errors='ignore')
            response_lines.append(text)
        except:
            pass
    time.sleep(0.01)

print(f"Status response:")
full_response = ''.join(response_lines)
for line in full_response.split('\n')[:5]:
    if line.strip():
        print(f"  {line.strip()}")

print()

# Test 3: Send #STOP!
print("[TEST 3] Sending #STOP! command...")
ser.write(b"#STOP!")
time.sleep(0.3)

response_lines = []
start_time = time.time()
while (time.time() - start_time) < 1.0:
    if ser.in_waiting > 0:
        data = ser.read(ser.in_waiting)
        try:
            text = data.decode('ascii', errors='ignore')
            response_lines.append(text)
        except:
            pass
    time.sleep(0.01)

print(f"Stop response:")
full_response = ''.join(response_lines)
for line in full_response.split('\n')[:5]:
    if line.strip():
        print(f"  {line.strip()}")

if "OK STOP" in full_response:
    print("\n[OK] Stop command acknowledged")
else:
    print("\n[WARNING] No 'OK STOP' acknowledgment")

ser.close()
print("\n" + "=" * 60)
print("Debug test complete")
print("=" * 60)
