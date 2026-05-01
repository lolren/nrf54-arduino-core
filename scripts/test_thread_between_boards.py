#!/usr/bin/env python3
"""
Automated Thread communication test between two XIAO nRF54L15 boards.

Flashes Thread UART test to both boards and monitors for UDP ping/pong communication.
Uses UART bridge (CP2102) for board 1 and SAMD11 for board 2.

Usage:
    python3 scripts/test_thread_between_boards.py
"""

import subprocess
import time
import serial
import sys

BOARD1_PORT = "/dev/ttyACM0"
BOARD2_PORT = "/dev/ttyACM1"
UART_BRIDGE_PORT = "/dev/ttyUSB0"
THREAD_TEST_EXAMPLE = "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadUartTest"

def flash_example(port):
    print(f"Flashing Thread UART test to {port}...")
    try:
        result = subprocess.run(
            ["arduino-cli", "upload", "-p", port, "-b", "nrf54l15clean:nrf54l15clean:xiao_nrf54l15",
             THREAD_TEST_EXAMPLE],
            capture_output=True, text=True, timeout=120
        )
        if "Upload complete" in result.stdout:
            print(f"  ✓ Flash successful to {port}")
            return True
        else:
            print(f"  ✗ Flash failed: {result.stderr}")
            return False
    except subprocess.TimeoutExpired:
        print(f"  ✗ Flash timed out for {port}")
        return False

def read_serial_data(port, timeout=5):
    try:
        s = serial.Serial(port, 115200, timeout=timeout)
        time.sleep(2)
        data = s.read_all().decode('utf-8', errors='replace')
        s.close()
        return data
    except:
        return ""

def parse_status(data):
    results = {}
    import re
    for line in data.split('\n'):
        if 'role=' in line:
            m = re.search(r'role=(\w+)', line)
            if m: results['role'] = m.group(1)
            m = re.search(r'ping_tx=(\d+)', line)
            if m: results['ping_tx'] = int(m.group(1))
            m = re.search(r'ping_rx=(\d+)', line)
            if m: results['ping_rx'] = int(m.group(1))
            m = re.search(r'pong_tx=(\d+)', line)
            if m: results['pong_tx'] = int(m.group(1))
            m = re.search(r'pong_rx=(\d+)', line)
            if m: results['pong_rx'] = int(m.group(1))
            m = re.search(r'pong_seen=(\d)', line)
            if m: results['pong_seen'] = int(m.group(1))
    return results

def test():
    print("=== Thread Communication Test ===")
    print()
    
    if not flash_example(BOARD1_PORT):
        return False
    if not flash_example(BOARD2_PORT):
        return False
    
    print()
    print("Waiting for Thread network formation and UDP ping/pong...")
    
    for i in range(18):
        time.sleep(10)
        
        d1 = read_serial_data(UART_BRIDGE_PORT)
        d2 = read_serial_data(BOARD2_PORT)
        
        s1 = parse_status(d1)
        s2 = parse_status(d2)
        
        if s1.get('pong_seen') == 1 and s2.get('pong_seen') == 1:
            print()
            print("  ✓ Both boards see pong responses!")
            print()
            print("  Board 1 (UART bridge):")
            for k, v in s1.items():
                print(f"    {k}={v}")
            print()
            print("  Board 2 (SAMD11):")
            for k, v in s2.items():
                print(f"    {k}={v}")
            print()
            print("=== Thread communication test PASSED ===")
            return True
    
    print()
    print("=== Thread communication test TIMED OUT ===")
    return False

if __name__ == "__main__":
    success = test()
    sys.exit(0 if success else 1)
