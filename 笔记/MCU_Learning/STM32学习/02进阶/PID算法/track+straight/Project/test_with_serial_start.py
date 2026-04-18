"""
Automated test with serial command to start car
Sends #RUN command to start the car automatically
"""

import serial
import serial.tools.list_ports
import time
from datetime import datetime
import re

def find_ch340_port():
    """Find CH340 serial port"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'CH340' in port.description or 'USB-SERIAL' in port.description:
            return port.device
    return None

def parse_data_line(line):
    """Parse data line"""
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

def run_serial_start_test(duration=10):
    """Test with serial command start"""

    # Find serial port
    port = find_ch340_port()
    if not port:
        print("[ERROR] CH340 serial port not found")
        return

    print(f"[OK] Found port: {port}")

    # Open serial port
    try:
        ser = serial.Serial(port=port, baudrate=115200, timeout=0.1)
        print(f"[OK] Port opened: {port} @ 115200")
    except Exception as e:
        print(f"[ERROR] Cannot open port: {e}")
        return

    # Clear buffer
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.5)

    # Create log file
    data_dir = r"F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project-GPT\STM32project\000Data"
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = f"{data_dir}\\test_log_{timestamp}.csv"

    print(f"\n{'='*60}")
    print(f"Serial Command Start Test")
    print(f"{'='*60}")
    print(f"Test duration: {duration} seconds")
    print(f"Log file: {log_file}")
    print(f"{'='*60}\n")

    # Send #RUN! command to start the car (VOFA protocol uses ! as frame tail)
    print("[INFO] Sending #RUN! command to start car...")
    ser.write(b"#RUN!")
    time.sleep(0.5)

    # Check for response
    if ser.in_waiting > 0:
        response = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
        print(f"[RESPONSE] {response.strip()}")

    print("[OK] Car should be running now!")
    print("[INFO] Starting data recording...\n")

    # Record data
    start_time = time.time()
    line_buffer = ""
    line_count = 0
    run_detected = False
    max_pwm_left = 0
    max_pwm_right = 0

    with open(log_file, 'w', encoding='utf-8') as f:
        # Write CSV header
        f.write("Time(s),tick,run,spd,leftPWM,rightPWM,leftEnc,rightEnc,yaw,yawErr,headingCorr,RawData\n")

        print("Time(s) | Lines | RUN | SPD | L_PWM | R_PWM | L_ENC | R_ENC")
        print("-" * 75)

        try:
            while (time.time() - start_time) < duration:
                # Read serial data
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting)
                    try:
                        text = data.decode('ascii', errors='ignore')
                        line_buffer += text

                        # Process by line
                        while '\n' in line_buffer:
                            line, line_buffer = line_buffer.split('\n', 1)
                            line = line.strip()

                            if line:
                                line_count += 1
                                elapsed = time.time() - start_time

                                # Parse data
                                parsed = parse_data_line(line)

                                # Detect start
                                if parsed.get('run', 0) == 1 and not run_detected:
                                    run_detected = True
                                    print(f"\n[OK] Car running confirmed! (time: {elapsed:.1f}s)\n")

                                # Record max PWM
                                max_pwm_left = max(max_pwm_left, abs(parsed.get('L', 0)))
                                max_pwm_right = max(max_pwm_right, abs(parsed.get('R', 0)))

                                # Write to CSV
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

                                # Display progress
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
            print("\n\n[INFO] User interrupted test")

    # Send #STOP! command (VOFA protocol uses ! as frame tail)
    print("\n[INFO] Sending #STOP! command...")
    ser.write(b"#STOP!")
    time.sleep(0.2)

    # Close serial port
    ser.close()

    # Test summary
    elapsed_total = time.time() - start_time
    print(f"\n{'='*60}")
    print(f"Test Complete")
    print(f"{'='*60}")
    print(f"Actual runtime: {elapsed_total:.1f} seconds")
    print(f"Lines received: {line_count}")
    print(f"Average data rate: {line_count/elapsed_total:.1f} lines/sec")
    print(f"Car started: {'YES' if run_detected else 'NO'}")
    print(f"Max PWM (left): {max_pwm_left}")
    print(f"Max PWM (right): {max_pwm_right}")
    print(f"Log file: {log_file}")
    print(f"{'='*60}\n")

    if not run_detected:
        print("[WARNING] Car start not detected")
        print("Possible reasons:")
        print("  1. #RUN command not recognized")
        print("  2. Control_ParseVOFA not being called")
        print("  3. Car in error state")
    else:
        print("[OK] Test successful!")
        print(f"Suggestion: Open log file in Excel to view detailed data")

        # Simple diagnostics
        if max_pwm_left == 0 and max_pwm_right == 0:
            print("\n[WARNING] PWM output is 0")
            print("Possible reasons:")
            print("  1. Target speed is 0")
            print("  2. Encoders not working (speed feedback is 0)")
            print("  3. PID parameters unreasonable")
        elif max_pwm_left > 0 or max_pwm_right > 0:
            print(f"\n[OK] Motors are active!")
            print(f"     Left motor max PWM: {max_pwm_left}")
            print(f"     Right motor max PWM: {max_pwm_right}")

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 Car Serial Command Start Test")
    print("=" * 60)
    print()

    run_serial_start_test(duration=10)

    print("\nTest script ended")
