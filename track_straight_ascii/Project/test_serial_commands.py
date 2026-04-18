"""
Test serial commands - send various commands and check responses
"""

import serial
import serial.tools.list_ports
import time

def find_ch340_port():
    """Find CH340 serial port"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'CH340' in port.description or 'USB-SERIAL' in port.description:
            return port.device
    return None

def test_commands():
    """Test various serial commands"""

    # Find serial port
    port = find_ch340_port()
    if not port:
        print("[ERROR] CH340 serial port not found")
        return

    print(f"[OK] Found port: {port}")

    # Open serial port
    try:
        ser = serial.Serial(port=port, baudrate=115200, timeout=0.5)
        print(f"[OK] Port opened: {port} @ 115200\n")
    except Exception as e:
        print(f"[ERROR] Cannot open port: {e}")
        return

    # Clear buffer
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.5)

    # Test commands
    commands = [
        b"#STAT!",  # Get status
        b"#RUN!",   # Start car
        b"#STOP!",  # Stop car
        b"#SPD=50!", # Set speed to 50
    ]

    for cmd in commands:
        print(f"Sending: {cmd.decode('ascii')}")
        ser.write(cmd)
        time.sleep(0.3)

        # Read response
        if ser.in_waiting > 0:
            response = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
            print(f"Response: {response}")
        else:
            print("Response: (no response)")

        print()

    # Monitor data stream for 3 seconds
    print("Monitoring data stream for 3 seconds...")
    start_time = time.time()
    line_count = 0
    run_detected = False

    while (time.time() - start_time) < 3:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
            lines = data.split('\n')
            for line in lines:
                line = line.strip()
                if line and 'run=' in line:
                    line_count += 1
                    if 'run=1' in line and not run_detected:
                        run_detected = True
                        print(f"[OK] Car is RUNNING! Line: {line[:80]}")
                    elif line_count % 10 == 0:
                        print(f"Line {line_count}: {line[:80]}")

        time.sleep(0.01)

    print(f"\nTotal lines received: {line_count}")
    print(f"Car running: {'YES' if run_detected else 'NO'}")

    # Send stop command
    print("\nSending #STOP! command...")
    ser.write(b"#STOP!")
    time.sleep(0.2)

    # Close serial port
    ser.close()
    print("\n[OK] Test complete")

if __name__ == '__main__':
    print("=" * 60)
    print("STM32 Serial Command Test")
    print("=" * 60)
    print()

    test_commands()
