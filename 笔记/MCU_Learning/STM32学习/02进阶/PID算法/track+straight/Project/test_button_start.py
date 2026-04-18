"""
Test with button start - monitor data after user presses button
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

def analyze_car_behavior(data_list):
    """Analyze car running behavior"""
    if not data_list:
        return "No data to analyze"

    analysis = []
    analysis.append("\n" + "="*60)
    analysis.append("CAR BEHAVIOR ANALYSIS")
    analysis.append("="*60)

    # Check if car is running
    run_count = sum(1 for d in data_list if d.get('run', 0) == 1)
    if run_count == 0:
        analysis.append("\n[CRITICAL] Car is NOT running (run=0 throughout)")
        return "\n".join(analysis)

    analysis.append(f"\n[OK] Car is RUNNING ({run_count}/{len(data_list)} samples)")

    # Analyze PWM values
    left_pwm_values = [d.get('L', 0) for d in data_list if d.get('run', 0) == 1]
    right_pwm_values = [d.get('R', 0) for d in data_list if d.get('run', 0) == 1]

    if left_pwm_values and right_pwm_values:
        avg_left = sum(left_pwm_values) / len(left_pwm_values)
        avg_right = sum(right_pwm_values) / len(right_pwm_values)
        max_left = max(left_pwm_values, key=abs)
        max_right = max(right_pwm_values, key=abs)

        analysis.append(f"\nPWM Analysis:")
        analysis.append(f"  Left motor:  avg={avg_left:.1f}, max={max_left}")
        analysis.append(f"  Right motor: avg={avg_right:.1f}, max={max_right}")

        # Check for left/right drift
        pwm_diff = avg_left - avg_right
        if abs(pwm_diff) > 10:
            if pwm_diff > 0:
                analysis.append(f"\n[WARNING] Car drifting RIGHT (left PWM > right PWM by {pwm_diff:.1f})")
            else:
                analysis.append(f"\n[WARNING] Car drifting LEFT (right PWM > left PWM by {-pwm_diff:.1f})")
        else:
            analysis.append(f"\n[OK] Car going relatively straight (PWM diff = {pwm_diff:.1f})")

        # Check for jerky motion (high PWM variance)
        if len(left_pwm_values) > 1:
            left_variance = sum((x - avg_left)**2 for x in left_pwm_values) / len(left_pwm_values)
            right_variance = sum((x - avg_right)**2 for x in right_pwm_values) / len(right_pwm_values)

            analysis.append(f"\nMotion Smoothness:")
            analysis.append(f"  Left variance:  {left_variance:.1f}")
            analysis.append(f"  Right variance: {right_variance:.1f}")

            if left_variance > 100 or right_variance > 100:
                analysis.append(f"\n[WARNING] JERKY motion detected (high PWM variance)")
            else:
                analysis.append(f"\n[OK] Motion is relatively smooth")

    # Analyze encoder values
    left_enc_values = [d.get('el', 0) for d in data_list if d.get('run', 0) == 1]
    right_enc_values = [d.get('er', 0) for d in data_list if d.get('run', 0) == 1]

    if left_enc_values and right_enc_values:
        # Check if encoders are changing
        left_enc_changing = len(set(left_enc_values)) > 1
        right_enc_changing = len(set(right_enc_values)) > 1

        analysis.append(f"\nEncoder Status:")
        analysis.append(f"  Left encoder:  {'WORKING' if left_enc_changing else 'STUCK'} (range: {min(left_enc_values)} to {max(left_enc_values)})")
        analysis.append(f"  Right encoder: {'WORKING' if right_enc_changing else 'STUCK'} (range: {min(right_enc_values)} to {max(right_enc_values)})")

        if not left_enc_changing or not right_enc_changing:
            analysis.append(f"\n[WARNING] Encoder not changing - wheels may not be moving!")

        # Average speed
        avg_left_speed = sum(left_enc_values) / len(left_enc_values)
        avg_right_speed = sum(right_enc_values) / len(right_enc_values)
        analysis.append(f"\nAverage Speed:")
        analysis.append(f"  Left:  {avg_left_speed:.1f} counts/period")
        analysis.append(f"  Right: {avg_right_speed:.1f} counts/period")

    analysis.append("\n" + "="*60)
    return "\n".join(analysis)

def run_button_test(duration=10):
    """Test with button start"""

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
    time.sleep(0.5)

    # Create log file
    data_dir = r"F:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project-GPT\STM32project\000Data"
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = f"{data_dir}\\test_log_{timestamp}.csv"

    print(f"\n{'='*60}")
    print(f"Button Start Test")
    print(f"{'='*60}")
    print(f"Test duration: {duration} seconds")
    print(f"Log file: {log_file}")
    print(f"{'='*60}\n")

    print("[INSTRUCTION] Please press PB5 button NOW to start the car!")
    print("[INSTRUCTION] Test will record for 10 seconds...")
    print()

    # Record data
    start_time = time.time()
    line_buffer = ""
    line_count = 0
    data_list = []
    run_detected = False

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
                                data_list.append(parsed)

                                # Detect start
                                if parsed.get('run', 0) == 1 and not run_detected:
                                    run_detected = True
                                    print(f"\n[OK] Car started! (time: {elapsed:.1f}s)\n")

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
    print(f"Log file: {log_file}")

    # Analyze behavior
    analysis = analyze_car_behavior(data_list)
    print(analysis)

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 Car Button Start Test")
    print("=" * 60)
    print()

    run_button_test(duration=10)

    print("\nTest script ended")
