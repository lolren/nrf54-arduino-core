#!/usr/bin/env python3
"""
Automated Matter commissioning test between two XIAO nRF54L15 boards.

Flashes Matter On/Off Light to both boards and monitors for Thread network formation.
Matter commissioning requires additional setup (manual pairing/QR code) to complete.

Usage:
    python3 scripts/test_matter_between_boards.py
"""

import subprocess
import time
import serial
import sys

BOARD1_PORT = "/dev/ttyACM0"
BOARD2_PORT = "/dev/ttyACM1"
UART_BRIDGE_PORT = "/dev/ttyUSB0"
MATTER_TEST_EXAMPLE = "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightNodeDemo"

def flash_example(port):
    print(f"Flashing Matter On/Off Light to {port}...")
    try:
        result = subprocess.run(
            ["arduino-cli", "upload", "-p", port, "-b", "nrf54l15clean:nrf54l15clean:xiao_nrf54l15",
             MATTER_TEST_EXAMPLE],
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
        if 'thread_started=' in line:
            m = re.search(r'thread_started=(\d)', line)
            if m: results['thread_started'] = int(m.group(1))
        if 'thread_attached=' in line:
            m = re.search(r'thread_attached=(\d)', line)
            if m: results['thread_attached'] = int(m.group(1))
        if 'thread_role=' in line:
            m = re.search(r'thread_role=(\w+)', line)
            if m: results['thread_role'] = m.group(1)
        if 'dataset_source=' in line:
            m = re.search(r'dataset_source=(\w+)', line)
            if m: results['dataset_source'] = m.group(1)
    return results

def test():
    print("=== Matter Commissioning Test ===")
    print()
    
    if not flash_example(BOARD1_PORT):
        return False
    if not flash_example(BOARD2_PORT):
        return False
    
    print()
    print("Waiting for Thread network formation...")
    
    for i in range(18):
        time.sleep(10)
        
        d1 = read_serial_data(UART_BRIDGE_PORT)
        d2 = read_serial_data(BOARD2_PORT)
        
        s1 = parse_status(d1)
        s2 = parse_status(d2)
        
        if s1.get('thread_attached') == 1 and s2.get('thread_attached') == 1:
            print()
            print("  ✓ Both boards joined Thread network!")
            print()
            print("  Board 1 (UART bridge):")
            for k, v in s1.items():
                print(f"    {k}={v}")
            print()
            print("  Board 2 (SAMD11):")
            for k, v in s2.items():
                print(f"    {k}={v}")
            print()
            print("=== Matter commissioning test PASSED (Thread layer) ===")
            print("Note: Full Matter commissioning requires manual pairing/QR code scanning.")
            return True
    
    print()
    print("=== Matter commissioning test TIMED OUT ===")
    return False

if __name__ == "__main__":
    success = test()
    sys.exit(0 if success else 1)
