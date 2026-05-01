#!/usr/bin/env python3
"""
Continuously monitor Thread status from both XIAO nRF54L15 boards.
Reads serial output every 5 seconds and prints any new data.

Usage:
    python3 scripts/monitor_thread_boards.py
    (Ctrl+C to stop)
"""

import serial
import time
import sys

BOARD1_PORT = "/dev/ttyACM0"
BOARD2_PORT = "/dev/ttyACM1"
UART_BRIDGE_PORT = "/dev/ttyUSB0"

def read_port(port, name):
    try:
        s = serial.Serial(port, 115200, timeout=2)
        time.sleep(1)
        data = s.read_all().decode('utf-8', errors='replace')
        s.close()
        if data:
            print(f"[{name}] {data.strip()}")
            return data
    except:
        pass
    return ""

print("=== Thread Board Monitor (Ctrl+C to stop) ===")
print()

try:
    while True:
        d1 = read_port(UART_BRIDGE_PORT, "Board 1 (UART)")
        d2 = read_port(BOARD2_PORT, "Board 2 (SAMD11)")
        
        if not d1 and not d2:
            print("[.] Waiting for data...")
        
        time.sleep(5)
except KeyboardInterrupt:
    print("\nStopped.")
