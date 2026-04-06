#!/usr/bin/env python3
"""Quiet serial<->NUS bridge regression with host-side btmon capture.

This harness is meant to answer one specific question: when a NUS bridge looks
unstable, are bytes actually getting lost/corrupted on the connected BLE path,
or is the corruption already happening in the local serial/bridge layer?

It does that by driving two deterministic phases against a quiet probe sketch:

1. host -> BLE RX -> board USB CDC
2. host USB CDC -> board BLE notify -> host

The probe does not print live progress while connected. It only emits a summary
block after disconnect, which keeps the test traffic isolated from debug output.
"""

from __future__ import annotations

import asyncio
import argparse
import json
import re
import signal
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Optional


ROOT = Path(__file__).resolve().parents[1]
NRF_FQBN_DEFAULT = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
ESP32_FQBN_DEFAULT = "esp32:esp32:XIAO_ESP32C6"
DEFAULT_ADDR = ""
DEFAULT_NAME = "X54-QBR"
DEFAULT_ADDR_TYPE = "random"
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
DEFAULT_SCAN_TIMEOUT_S = 10
DEFAULT_INTER_PACKET_S = 0.03
DEFAULT_POST_WRITE_S = 2.0
DEFAULT_ITERATIONS = 64
DEFAULT_FRAME_TAIL = "0123456789"
SUMMARY_BEGIN = "@@SUMMARY_BEGIN@@"
SUMMARY_END = "@@SUMMARY_END@@"
NRF_UPLOAD_SCRIPT = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "tools"
    / "upload.py"
)
NRF_QUIET_EXAMPLE = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
    / "BLE"
    / "NordicUart"
    / "BleNordicUartQuietBridgeProbe"
)
ESP32_QUIET_SKETCH = (
    ROOT / "scripts" / "esp32_nimble_nus_quiet_bridge_probe"
)
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


@dataclass
class TransferResult:
    expected_packets: int
    observed_packets: int
    missing_packets: list[str]


@dataclass
class Summary:
    address: str
    address_type: str
    name: str
    port: str
    fqbn: str
    sketch: str
    scan_found: bool
    connected: bool
    rx_handle: Optional[int]
    tx_handle: Optional[int]
    tx_cccd_handle: Optional[int]
    host_to_device: TransferResult
    device_to_host: TransferResult
    board_summary: dict[str, str]
    board_summary_ok: bool
    disconnect_reason: str
    btmon_enabled: bool
    failure: str


class SerialCapture:
    def __init__(self, port: str, baud: int) -> None:
        import serial  # type: ignore

        self.port = port
        self._serial = serial.Serial()
        self._serial.port = port
        self._serial.baudrate = baud
        self._serial.timeout = 0.05
        self._serial.write_timeout = 1.0
        self._serial.rtscts = False
        self._serial.dsrdtr = False
        self._serial.open()
        try:
            self._serial.setDTR(True)
            self._serial.setRTS(False)
        except Exception:
            pass
        time.sleep(0.2)
        try:
            self._serial.reset_input_buffer()
            self._serial.reset_output_buffer()
        except Exception:
            pass

        self._chunks: list[bytes] = []
        self._stop = False
        self._thread = threading.Thread(target=self._reader, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def _reader(self) -> None:
        while not self._stop:
            data = self._serial.read(4096)
            if data:
                self._chunks.append(data)
            time.sleep(0.01)

    def write(self, data: bytes) -> None:
        self._serial.write(data)
        self._serial.flush()

    def close(self) -> None:
        self._stop = True
        self._thread.join(timeout=0.5)
        self._serial.close()

    def data(self) -> bytes:
        return b"".join(self._chunks)

    def text(self) -> str:
        return self.data().decode("utf-8", errors="replace")


class BtMonCapture:
    def __init__(self, iface: str, outdir: Path) -> None:
        self.iface = iface
        self.outdir = outdir
        self.snoop_path = outdir / "bt_trace.snoop"
        self.live_log_path = outdir / "btmon_live.log"
        self.text_log_path = outdir / "bt_trace.txt"
        self._proc: Optional[subprocess.Popen[str]] = None
        self.enabled = False

    @staticmethod
    def _command_prefix() -> list[str]:
        try:
            result = subprocess.run(["sudo", "-n", "true"], capture_output=True, check=False)
            if result.returncode == 0:
                return ["sudo", "-n"]
        except FileNotFoundError:
            pass
        return []

    def start(self) -> None:
        cmd = [*self._command_prefix(), "btmon", "-i", self.iface, "-w", str(self.snoop_path)]
        live_log = self.live_log_path.open("w", encoding="utf-8")
        self._proc = subprocess.Popen(
            cmd,
            stdout=live_log,
            stderr=subprocess.STDOUT,
            text=True,
        )
        time.sleep(1.0)
        if self._proc.poll() is not None:
            live_log.close()
            return
        self.enabled = True

    def stop(self) -> None:
        if self._proc is None:
            return
        if self._proc.poll() is None:
            try:
                self._proc.send_signal(signal.SIGINT)
            except ProcessLookupError:
                pass
            try:
                self._proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(timeout=2.0)
        self._proc = None
        if self.enabled and self.snoop_path.is_file():
            rendered = subprocess.run(
                ["btmon", "-r", str(self.snoop_path)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.text_log_path.write_text(rendered.stdout, encoding="utf-8")


class GatttoolSession:
    def __init__(self, addr: str, addr_type: str, log_path: Path) -> None:
        import pexpect

        self._pexpect = pexpect
        self.log_path = log_path
        self.log_file = log_path.open("w", encoding="utf-8", buffering=1)
        self.child = pexpect.spawn(
            "gatttool",
            ["-b", addr, "-t", addr_type, "-I"],
            encoding="utf-8",
            timeout=10,
        )
        self.child.delaybeforesend = 0.05
        self.child.logfile = self.log_file
        self.child.expect(r"\[LE\]>", timeout=5)

    def send(self, command: str) -> None:
        self.child.sendline(command)

    def connect(self, timeout_s: float) -> None:
        deadline = time.time() + timeout_s
        self.send("connect")
        while time.time() < deadline:
            idx = self.child.expect(
                [r"Connection successful", r"Error.*", r"\[LE\]>", self._pexpect.TIMEOUT],
                timeout=min(5.0, max(0.5, deadline - time.time())),
            )
            if idx == 0:
                return
            if idx == 1:
                raise RuntimeError(f"gatttool connect failed: {self.child.after}")
        raise TimeoutError("Timed out waiting for gatttool connection success")

    def write_request(self, handle: int, value_hex: str, timeout_s: float) -> None:
        deadline = time.time() + timeout_s
        self.send(f"char-write-req 0x{handle:04x} {value_hex}")
        while time.time() < deadline:
            idx = self.child.expect(
                [
                    r"Characteristic value was written successfully",
                    r"Command Failed:.*",
                    r"Error:.*",
                    self._pexpect.TIMEOUT,
                ],
                timeout=min(3.0, max(0.5, deadline - time.time())),
            )
            if idx == 0:
                return
            if idx in (1, 2):
                raise RuntimeError(f"gatttool write failed: {self.child.after}")
        raise TimeoutError(f"Timed out waiting for write response on handle 0x{handle:04x}")

    def wait_for_patterns(self, patterns: list[str], timeout_s: float) -> dict[str, bool]:
        deadline = time.time() + timeout_s
        found = {pattern: False for pattern in patterns}
        expect_patterns = [*patterns, r"\[LE\]>", self._pexpect.TIMEOUT]
        while time.time() < deadline and not all(found.values()):
            idx = self.child.expect(
                expect_patterns,
                timeout=min(3.0, max(0.5, deadline - time.time())),
            )
            if idx < len(patterns):
                found[patterns[idx]] = True
        return found

    def close(self) -> None:
        if self.child.isalive():
            try:
                self.child.sendline("disconnect")
                time.sleep(0.2)
            except Exception:
                pass
            try:
                self.child.sendline("exit")
            except Exception:
                pass
            self.child.close(force=True)
        self.log_file.close()

    def log_text(self) -> str:
        self.log_file.flush()
        return self.log_path.read_text(encoding="utf-8", errors="replace")


def run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if check and result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    return result


def detect_uid(port: str) -> str:
    result = run(["udevadm", "info", "-q", "property", "-n", port])
    for line in result.stdout.splitlines():
        if line.startswith("ID_SERIAL_SHORT="):
            return line.split("=", 1)[1].strip()
    raise SystemExit(f"Could not resolve CMSIS-DAP UID for {port}")


def compile_example(example_dir: Path, fqbn: str, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    for entry in output_dir.iterdir():
        if entry.is_file():
            entry.unlink()
    run(
        [
            "arduino-cli",
            "compile",
            "--output-dir",
            str(output_dir),
            "-b",
            fqbn,
            str(example_dir),
        ]
    )
    hex_files = sorted(output_dir.glob("*.hex"))
    if len(hex_files) != 1:
        raise SystemExit(f"Expected exactly one .hex in {output_dir}, found {len(hex_files)}")
    return hex_files[0]


def upload_nrf(hex_path: Path, uid: str) -> None:
    run([sys.executable, str(NRF_UPLOAD_SCRIPT), "--hex", str(hex_path), "--uid", uid])


def compile_and_upload(args: argparse.Namespace) -> None:
    if args.fqbn.startswith("nrf54l15clean:"):
        uid = detect_uid(args.port)
        build_dir = args.outdir / "build"
        hex_path = compile_example(Path(args.sketch), args.fqbn, build_dir)
        upload_nrf(hex_path, uid)
        return

    run(["arduino-cli", "compile", "-b", args.fqbn, str(args.sketch)])
    run(["arduino-cli", "upload", "-p", args.port, "-b", args.fqbn, str(args.sketch)])


def scan_for_device(addr: str, name: str, timeout_s: int, log_path: Path) -> tuple[bool, str]:
    result = run(["bluetoothctl", "--timeout", str(timeout_s), "scan", "on"], check=False)
    stop = run(["bluetoothctl", "scan", "off"], check=False)
    combined = result.stdout + result.stderr + "\n--- scan off ---\n" + stop.stdout + stop.stderr
    log_path.write_text(combined, encoding="utf-8")

    if not name:
        resolved_addr = addr
    else:
        devices = run(["bluetoothctl", "devices"], check=False)
        pattern = re.compile(r"Device ([0-9A-F:]{17}) (.+)$", re.MULTILINE)
        resolved_addr = addr
        matches = pattern.findall(combined)
        for candidate_addr, candidate_name in matches:
            if candidate_name.strip() == name:
                resolved_addr = candidate_addr
        if not resolved_addr:
            for candidate_addr, candidate_name in pattern.findall(devices.stdout + devices.stderr):
                if candidate_name.strip() == name:
                    resolved_addr = candidate_addr
                    break
        if not resolved_addr:
            for candidate_addr, _ in pattern.findall(devices.stdout + devices.stderr):
                info = run(["bluetoothctl", "info", candidate_addr], check=False)
                info_text = info.stdout + info.stderr
                for key in ("Name:", "Alias:"):
                    match = re.search(rf"^{key}\s*(.+)$", info_text, re.MULTILINE)
                    if match and match.group(1).strip() == name:
                        resolved_addr = candidate_addr
                        break
                if resolved_addr:
                    break

    found = False
    if resolved_addr:
        found = resolved_addr in combined
        if not found and name:
            info = run(["bluetoothctl", "info", resolved_addr], check=False)
            info_text = info.stdout + info.stderr
            found = (name in info_text) or ("Connected:" in info_text)
    if not found and name:
        found = name in combined
    return found, resolved_addr


def remove_device(addr: str) -> None:
    if not addr:
        return
    run(["bluetoothctl", "remove", addr], check=False)


def ascii_packet(prefix: str, index: int, tail: str = DEFAULT_FRAME_TAIL) -> bytes:
    payload = f"{prefix}:{index:03d}:{tail}\r\n".encode("ascii")
    if len(payload) > 20:
        raise ValueError(f"ATT write frame exceeds 20-byte value budget: {payload!r}")
    return payload


def parse_handles(gatt_log: str) -> tuple[Optional[int], Optional[int]]:
    rx_handle = None
    tx_handle = None
    pattern = re.compile(
        r"char value handle\s*[:=]\s*0x([0-9a-f]+),\s*uuid\s*[:=]\s*([0-9a-f-]+)",
        re.IGNORECASE,
    )
    for match in pattern.finditer(gatt_log):
        handle = int(match.group(1), 16)
        uuid = match.group(2).lower()
        if uuid == NUS_RX_UUID:
            rx_handle = handle
        elif uuid == NUS_TX_UUID:
            tx_handle = handle
    return rx_handle, tx_handle


def parse_cccd_handle(desc_log: str) -> Optional[int]:
    match = re.search(
        r"handle\s*[:=]\s*0x([0-9a-f]+),\s*uuid\s*[:=]\s*(?:0x2902|00002902-0000-1000-8000-00805f9b34fb)",
        desc_log,
        re.IGNORECASE,
    )
    if match is None:
        return None
    return int(match.group(1), 16)


def parse_notification_stream(gatt_log: str) -> bytes:
    chunks: list[bytes] = []
    for match in re.finditer(
        r"Notification handle = 0x[0-9a-f]+ value: ([0-9a-f ]+)",
        gatt_log,
        re.IGNORECASE,
    ):
        payload_hex = " ".join(match.group(1).split())
        if not payload_hex:
            continue
        chunks.append(bytes.fromhex(payload_hex))
    return b"".join(chunks)


def parse_btmon_notification_stream(btmon_text: str, value_handle: int) -> bytes:
    chunks: list[bytes] = []
    pending_notification = False
    pending_handle: Optional[int] = None

    for raw_line in btmon_text.splitlines():
        line = raw_line.strip()
        if "ATT: Handle Value Notification" in line:
            pending_notification = True
            pending_handle = None
            continue
        if not pending_notification:
            continue
        if line.startswith("Handle: 0x"):
            try:
                pending_handle = int(line.split("0x", 1)[1], 16)
            except ValueError:
                pending_handle = None
            continue
        if line.startswith("Data: ") and pending_handle == value_handle:
            payload_hex = " ".join(line[6:].split())
            if payload_hex:
                chunks.append(bytes.fromhex(payload_hex))
            pending_notification = False
            pending_handle = None
            continue
        if line.startswith("ATT: ") or line.startswith("< ACL Data") or line.startswith("> ACL Data"):
            pending_notification = False
            pending_handle = None

    return b"".join(chunks)


def last_disconnect_reason(btmon_text: str) -> str:
    matches = re.findall(r"Reason:\s+([^\r\n]+)", btmon_text)
    if not matches:
        return ""
    return matches[-1].strip()


def verify_packets(expected: list[bytes], observed_stream: bytes) -> TransferResult:
    missing = [pkt.decode("ascii", errors="replace") for pkt in expected if pkt not in observed_stream]
    return TransferResult(
        expected_packets=len(expected),
        observed_packets=len(expected) - len(missing),
        missing_packets=missing,
    )


def fnv1a_stream(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def parse_summary_block(serial_text: str) -> dict[str, str]:
    matches = re.findall(
        rf"{re.escape(SUMMARY_BEGIN)}\s*(.*?)\s*{re.escape(SUMMARY_END)}",
        serial_text,
        re.DOTALL,
    )
    if not matches:
        return {}
    block = matches[-1]
    parsed: dict[str, str] = {}
    for raw_line in block.splitlines():
        line = raw_line.strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        parsed[key.strip()] = value.strip()
    return parsed


def wait_for_summary(serial_capture: SerialCapture, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        text = serial_capture.text()
        if SUMMARY_BEGIN in text and SUMMARY_END in text:
            return
        time.sleep(0.1)


def summary_matches(board_summary: dict[str, str], host_to_device: list[bytes], device_to_host: list[bytes]) -> bool:
    if not board_summary:
        return False

    ble_to_usb = b"".join(host_to_device)
    usb_to_ble = b"".join(device_to_host)

    expected_pairs = {
        "ble_to_usb_in": str(len(ble_to_usb)),
        "ble_to_usb_out": str(len(ble_to_usb)),
        "ble_to_usb_in_hash": f"0x{fnv1a_stream(ble_to_usb):08X}",
        "ble_to_usb_out_hash": f"0x{fnv1a_stream(ble_to_usb):08X}",
        "usb_to_ble_in": str(len(usb_to_ble)),
        "usb_to_ble_out": str(len(usb_to_ble)),
        "usb_to_ble_in_hash": f"0x{fnv1a_stream(usb_to_ble):08X}",
        "usb_to_ble_out_hash": f"0x{fnv1a_stream(usb_to_ble):08X}",
    }

    for key, expected in expected_pairs.items():
        observed = board_summary.get(key, "").upper()
        if observed != expected.upper():
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help="Board CDC serial port")
    parser.add_argument("--fqbn", default=NRF_FQBN_DEFAULT, help="Board FQBN")
    parser.add_argument("--sketch", type=Path, default=NRF_QUIET_EXAMPLE, help="Sketch directory")
    parser.add_argument("--addr", default=DEFAULT_ADDR, help="BLE advertiser address")
    parser.add_argument("--addr-type", default=DEFAULT_ADDR_TYPE, choices=("public", "random"))
    parser.add_argument("--name", default=DEFAULT_NAME, help="Expected advertiser name")
    parser.add_argument("--iface", default="hci0", help="Host Bluetooth controller interface")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Board CDC baud rate")
    parser.add_argument("--iterations", type=int, default=DEFAULT_ITERATIONS)
    parser.add_argument("--scan-timeout", type=int, default=DEFAULT_SCAN_TIMEOUT_S)
    parser.add_argument("--inter-packet-delay", type=float, default=DEFAULT_INTER_PACKET_S)
    parser.add_argument("--post-write-delay", type=float, default=DEFAULT_POST_WRITE_S)
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT / "measurements" / "ble_nus_quiet_bridge_latest",
    )
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--skip-btmon", action="store_true")
    parser.add_argument(
        "--transport",
        default="bleak",
        choices=("bleak", "gatttool"),
        help="BLE central transport to use for the regression run",
    )
    args = parser.parse_args()

    try:
        import serial  # type: ignore  # noqa: F401
        if args.transport == "gatttool":
            import pexpect  # type: ignore  # noqa: F401
        else:
            from bleak import BleakClient  # type: ignore  # noqa: F401
    except Exception as exc:
        raise SystemExit(f"Missing Python dependency: {exc}") from exc

    args.outdir.mkdir(parents=True, exist_ok=True)

    if not args.skip_upload:
        compile_and_upload(args)
        time.sleep(2.0)

    serial_capture = SerialCapture(args.port, args.baud)
    serial_capture.start()
    btmon = BtMonCapture(args.iface, args.outdir)
    if not args.skip_btmon:
        btmon.start()

    session: Optional[GatttoolSession] = None
    scan_found = False
    connected = False
    resolved_addr = args.addr
    rx_handle: Optional[int] = None
    tx_handle: Optional[int] = None
    tx_cccd_handle: Optional[int] = None
    host_to_device_packets: list[bytes] = []
    device_to_host_packets: list[bytes] = []
    serial_after_host = b""
    notification_stream = b""
    board_summary: dict[str, str] = {}
    failure = ""

    try:
        scan_found, resolved_addr = scan_for_device(
            args.addr, args.name, args.scan_timeout, args.outdir / "scan.log"
        )
        remove_device(resolved_addr)
        time.sleep(0.5)

        serial_phase_start = len(serial_capture.data())
        host_to_device_packets = [ascii_packet("H2D", index) for index in range(args.iterations)]
        device_to_host_packets = [ascii_packet("D2H", index) for index in range(args.iterations)]

        if args.transport == "gatttool":
            session = GatttoolSession(
                addr=resolved_addr,
                addr_type=args.addr_type,
                log_path=args.outdir / "gatttool.log",
            )

            session.connect(25.0)
            connected = True

            session.send("characteristics")
            found = session.wait_for_patterns([NUS_RX_UUID, NUS_TX_UUID], 10.0)
            if not all(found.values()):
                raise RuntimeError(
                    f"Could not resolve NUS characteristics from gatttool output: {found}"
                )
            gatt_log = session.log_text()
            rx_handle, tx_handle = parse_handles(gatt_log)
            if rx_handle is None or tx_handle is None:
                raise RuntimeError("Could not resolve NUS RX/TX handles from gatttool output")

            session.send(f"char-desc 0x{tx_handle:04x} 0x{min(tx_handle + 2, 0xFFFF):04x}")
            found = session.wait_for_patterns(
                [r"uuid\s*[:=]\s*(?:0x2902|00002902-0000-1000-8000-00805f9b34fb)"],
                8.0,
            )
            if not all(found.values()):
                raise RuntimeError("Could not resolve NUS TX CCCD from gatttool output")
            tx_cccd_handle = parse_cccd_handle(session.log_text())
            if tx_cccd_handle is None:
                raise RuntimeError("Could not resolve NUS TX CCCD handle from gatttool output")

            session.write_request(tx_cccd_handle, "0100", 8.0)
            time.sleep(0.5)

            for packet in host_to_device_packets:
                session.write_request(rx_handle, packet.hex(), 8.0)
                time.sleep(args.inter_packet_delay)

            time.sleep(args.post_write_delay)
            serial_after_host = serial_capture.data()[serial_phase_start:]

            for packet in device_to_host_packets:
                serial_capture.write(packet)
                time.sleep(args.inter_packet_delay)

            time.sleep(args.post_write_delay)
            session.send("disconnect")
            time.sleep(0.5)
            notification_stream = parse_notification_stream(session.log_text())
        else:
            async def run_bleak_session() -> tuple[bool, Optional[int], Optional[int], Optional[int], bytes]:
                from bleak import BleakClient

                notifications = bytearray()

                def on_notification(_: object, data: bytearray) -> None:
                    notifications.extend(bytes(data))

                async with BleakClient(resolved_addr, timeout=25.0) as client:
                    services = client.services
                    if services is None:
                        raise RuntimeError("Bleak did not resolve services")

                    rx_char = services.get_characteristic(NUS_RX_UUID)
                    tx_char = services.get_characteristic(NUS_TX_UUID)
                    if rx_char is None or tx_char is None:
                        raise RuntimeError("Could not resolve NUS RX/TX characteristics via Bleak")

                    cccd_handle: Optional[int] = None
                    for descriptor in tx_char.descriptors:
                        if descriptor.uuid.lower() == "00002902-0000-1000-8000-00805f9b34fb":
                            cccd_handle = descriptor.handle
                            break

                    await client.start_notify(tx_char, on_notification)
                    await asyncio.sleep(0.5)

                    for packet in host_to_device_packets:
                        await client.write_gatt_char(rx_char, packet, response=True)
                        await asyncio.sleep(args.inter_packet_delay)

                    await asyncio.sleep(args.post_write_delay)

                    for packet in device_to_host_packets:
                        serial_capture.write(packet)
                        await asyncio.sleep(args.inter_packet_delay)

                    await asyncio.sleep(args.post_write_delay)
                    await client.stop_notify(tx_char)
                    return True, rx_char.handle, tx_char.handle, cccd_handle, bytes(notifications)

            connected, rx_handle, tx_handle, tx_cccd_handle, notification_stream = asyncio.run(
                run_bleak_session()
            )
            serial_after_host = serial_capture.data()[serial_phase_start:]

        wait_for_summary(serial_capture, 4.0)

    except Exception as exc:
        failure = str(exc)

    finally:
        if session is not None:
            session.close()
        serial_capture.close()
        btmon.stop()

    serial_text = serial_capture.text()
    (args.outdir / "serial.log").write_text(serial_text, encoding="utf-8")

    btmon_text = ""
    if btmon.text_log_path.is_file():
        btmon_text = btmon.text_log_path.read_text(encoding="utf-8", errors="replace")
        if tx_handle is not None:
            btmon_notifications = parse_btmon_notification_stream(btmon_text, tx_handle)
            if btmon_notifications:
                notification_stream = btmon_notifications

    board_summary = parse_summary_block(serial_text)
    host_to_device = verify_packets(host_to_device_packets, serial_after_host)
    device_to_host = verify_packets(device_to_host_packets, notification_stream)
    board_summary_ok = summary_matches(board_summary, host_to_device_packets, device_to_host_packets)

    summary = Summary(
        address=resolved_addr,
        address_type=args.addr_type,
        name=args.name,
        port=args.port,
        fqbn=args.fqbn,
        sketch=str(args.sketch),
        scan_found=scan_found,
        connected=connected,
        rx_handle=rx_handle,
        tx_handle=tx_handle,
        tx_cccd_handle=tx_cccd_handle,
        host_to_device=host_to_device,
        device_to_host=device_to_host,
        board_summary=board_summary,
        board_summary_ok=board_summary_ok,
        disconnect_reason=last_disconnect_reason(btmon_text),
        btmon_enabled=btmon.enabled,
        failure=failure,
    )
    (args.outdir / "summary.json").write_text(
        json.dumps(asdict(summary), indent=2, sort_keys=True),
        encoding="utf-8",
    )

    print(json.dumps(asdict(summary), indent=2, sort_keys=True))

    if failure:
        return 4
    if not scan_found:
        return 2
    if host_to_device.missing_packets or device_to_host.missing_packets:
        return 3
    if not board_summary_ok:
        return 5
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
