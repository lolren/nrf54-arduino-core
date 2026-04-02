#!/usr/bin/env python3
"""Compile/upload BleBackgroundStressProbe and drive a host-side BLE session."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

try:
    import serial  # type: ignore
except Exception as exc:  # pragma: no cover
    serial = None
    SERIAL_IMPORT_ERROR = exc
else:
    SERIAL_IMPORT_ERROR = None

from bleak import BleakClient, BleakScanner


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FQBN = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_SKETCH = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation/examples/BLE/Diagnostics/BleBackgroundStressProbe"
)
TARGET_NAME = "X54-BG-STRESS"
TARGET_ALT_NAME = "X54-BG-STRESS-SCAN"
DEFAULT_ADDRESS = "C0:DE:54:15:00:91"
BATTERY_LEVEL_UUID = "00002a19-0000-1000-8000-00805f9b34fb"
CUSTOM_WRITE_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
CUSTOM_NOTIFY_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
STATS_RE = re.compile(
    r"stats .*rx_to=(?P<rx_to>\d+).*tx_to=(?P<tx_to>\d+).*late_poll=(?P<late>\d+).*"
    r"miss_last=(?P<miss_last>\d+).*miss_max=(?P<miss_max>\d+).*"
    r"defer_ovr=(?P<defer_ovr>\d+).*cb_drop=(?P<cb_drop>\d+).*trace_drop=(?P<trace_drop>\d+)"
)


@dataclass
class SerialCapture:
    port: str
    baud: int
    stop_event: threading.Event = field(default_factory=threading.Event)
    lines: list[str] = field(default_factory=list)
    thread: threading.Thread | None = None
    error: str = ""

    def start(self) -> None:
        if serial is None:
            raise RuntimeError(f"pyserial unavailable: {SERIAL_IMPORT_ERROR}")
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        try:
          with serial.Serial(self.port, self.baud, timeout=0.2) as ser:  # type: ignore[arg-type]
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            while not self.stop_event.is_set():
                data = ser.read(512)
                if not data:
                    continue
                text = data.decode("utf-8", errors="replace")
                self.lines.extend(text.splitlines())
                sys.stdout.write(text)
                sys.stdout.flush()
        except Exception as exc:
            self.error = str(exc)

    def stop(self) -> None:
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=2.0)


def run_cmd(cmd: list[str]) -> None:
    print("$", " ".join(cmd))
    proc = subprocess.run(cmd, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(cmd)}")


def auto_detect_port() -> str:
    proc = subprocess.run(
        ["arduino-cli", "board", "list"], capture_output=True, text=True, check=True
    )
    for line in proc.stdout.splitlines():
        if "nrf54l15clean" not in line:
            continue
        fields = line.split()
        if fields:
            return fields[0]
    return DEFAULT_PORT


def compile_and_upload(fqbn: str, port: str, sketch: Path) -> None:
    run_cmd(["arduino-cli", "compile", "--fqbn", fqbn, str(sketch)])
    run_cmd(["arduino-cli", "upload", "-p", port, "--fqbn", fqbn, str(sketch)])


async def find_device(timeout: float, expected_address: str) -> str:
    deadline = time.monotonic() + timeout
    normalized_expected = expected_address.strip().upper()
    while time.monotonic() < deadline:
        devices = await BleakScanner.discover(timeout=3.0)
        for device in devices:
            name = (device.name or "").strip()
            address = getattr(device, "address", "").strip().upper()
            if address == normalized_expected:
                return device.address
            if name == TARGET_NAME or name == TARGET_ALT_NAME:
                return device.address
        await asyncio.sleep(0.5)
    return expected_address


async def exercise_target(address: str, duration_s: float, write_interval_s: float) -> dict[str, int]:
    counters = {
        "battery_notifications": 0,
        "custom_notifications": 0,
        "writes_ok": 0,
        "reads_ok": 0,
    }

    def battery_handler(_: object, data: bytearray) -> None:
        counters["battery_notifications"] += 1
        print(f"[host] battery notify: {list(data)}")

    def custom_handler(_: object, data: bytearray) -> None:
        counters["custom_notifications"] += 1
        print(f"[host] custom notify: {bytes(data)!r}")

    async with BleakClient(address, timeout=15.0) as client:
        if not client.is_connected:
            raise RuntimeError("BLE client failed to connect")
        print(f"[host] connected to {address}")

        await client.start_notify(BATTERY_LEVEL_UUID, battery_handler)
        await client.start_notify(CUSTOM_NOTIFY_UUID, custom_handler)

        deadline = time.monotonic() + duration_s
        write_index = 0
        next_write = time.monotonic()
        next_read = time.monotonic() + 2.0

        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_write:
                payload = f"ping-{write_index:02d}".encode("ascii")
                response = (write_index % 2) == 0
                await client.write_gatt_char(CUSTOM_WRITE_UUID, payload, response=response)
                counters["writes_ok"] += 1
                print(f"[host] write {write_index} response={response} payload={payload!r}")
                write_index += 1
                next_write = now + write_interval_s
            if now >= next_read:
                value = await client.read_gatt_char(BATTERY_LEVEL_UUID)
                counters["reads_ok"] += 1
                print(f"[host] battery read: {list(value)}")
                next_read = now + 3.0
            await asyncio.sleep(0.1)

        with contextlib.suppress(Exception):
            await client.stop_notify(CUSTOM_NOTIFY_UUID)
        with contextlib.suppress(Exception):
            await client.stop_notify(BATTERY_LEVEL_UUID)

    return counters


def summarize_serial(lines: list[str]) -> dict[str, int]:
    summary = {
        "stats_lines": 0,
        "max_rx_to": 0,
        "max_tx_to": 0,
        "max_late_poll": 0,
        "max_miss_last": 0,
        "max_miss_max": 0,
        "max_defer_ovr": 0,
        "max_cb_drop": 0,
        "max_trace_drop": 0,
    }
    for line in lines:
        match = STATS_RE.search(line)
        if not match:
            continue
        summary["stats_lines"] += 1
        summary["max_rx_to"] = max(summary["max_rx_to"], int(match.group("rx_to")))
        summary["max_tx_to"] = max(summary["max_tx_to"], int(match.group("tx_to")))
        summary["max_late_poll"] = max(summary["max_late_poll"], int(match.group("late")))
        summary["max_miss_last"] = max(summary["max_miss_last"], int(match.group("miss_last")))
        summary["max_miss_max"] = max(summary["max_miss_max"], int(match.group("miss_max")))
        summary["max_defer_ovr"] = max(summary["max_defer_ovr"], int(match.group("defer_ovr")))
        summary["max_cb_drop"] = max(summary["max_cb_drop"], int(match.group("cb_drop")))
        summary["max_trace_drop"] = max(summary["max_trace_drop"], int(match.group("trace_drop")))
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload and exercise BleBackgroundStressProbe on real hardware."
    )
    parser.add_argument("--port", default="", help="Serial port (default: auto-detect).")
    parser.add_argument(
        "--address",
        default=DEFAULT_ADDRESS,
        help="Expected BLE target address; used as connect fallback.",
    )
    parser.add_argument("--fqbn", default=DEFAULT_FQBN, help="Board FQBN.")
    parser.add_argument(
        "--sketch", default=str(DEFAULT_SKETCH), help="Sketch directory to compile/upload."
    )
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate.")
    parser.add_argument(
        "--duration-s", type=float, default=25.0, help="Connected BLE exercise duration."
    )
    parser.add_argument(
        "--write-interval-s",
        type=float,
        default=2.0,
        help="Interval between host writes to 0xFFF1.",
    )
    parser.add_argument(
        "--scan-timeout-s", type=float, default=20.0, help="BLE scan timeout."
    )
    parser.add_argument(
        "--serial-settle-s",
        type=float,
        default=2.5,
        help="Wait after upload before starting BLE scan.",
    )
    parser.add_argument(
        "--skip-upload", action="store_true", help="Skip compile/upload and only run host exercise."
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    port = args.port or auto_detect_port()
    sketch = Path(args.sketch).resolve()

    if not args.skip_upload:
        compile_and_upload(args.fqbn, port, sketch)

    capture = SerialCapture(port=port, baud=args.baud)
    capture.start()

    host_summary: dict[str, int] = {}
    ble_error = ""
    try:
        time.sleep(args.serial_settle_s)
        address = asyncio.run(find_device(args.scan_timeout_s, args.address))
        print(f"[host] found target at {address}")
        host_summary = asyncio.run(
            exercise_target(address, args.duration_s, args.write_interval_s)
        )
    except Exception as exc:
        ble_error = str(exc)
    finally:
        capture.stop()

    if capture.error:
        print(f"[host] serial capture error: {capture.error}", file=sys.stderr)

    serial_summary = summarize_serial(capture.lines)
    print("[host] summary:")
    for key, value in {**host_summary, **serial_summary}.items():
        print(f"[host]   {key}={value}")
    if ble_error:
        print(f"[host] BLE error: {ble_error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
