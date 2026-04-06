#!/usr/bin/env python3
"""Host-side Nordic UART Service regression with btmon capture.

This script compiles/uploads a native NUS sketch, captures the board's CDC
serial log, records a host-side HCI trace with ``btmon``, drives a BLE session
through ``gatttool``, and checks both directions:

- host -> XIAO: write to the NUS RX characteristic, confirm the bytes arrive on
  the board's USB CDC stream
- XIAO -> host: write to the board's USB CDC stream, confirm the host receives
  NUS notifications

It does not sniff over-the-air traffic. The ``btmon`` trace is host-side HCI
traffic only, which is still enough to see connection/discovery stalls,
disconnect reasons, MTU/ATT progress, and whether notifications were received.
For this sketch, btmon-backed verification is the authoritative path; the
reduced ``--skip-btmon`` mode can under-report notifications because gatttool
does not always surface every incoming notification in its interactive log.
"""

from __future__ import annotations

import argparse
import json
import os
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
FQBN_DEFAULT = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
DEFAULT_ADDR = ""
DEFAULT_NAME = "X54-NUS"
DEFAULT_ADDR_TYPE = "random"
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
DEFAULT_SCAN_TIMEOUT_S = 8
DEFAULT_INTER_PACKET_S = 0.05
DEFAULT_POST_WRITE_S = 1.8
DEFAULT_ITERATIONS = 12
DEFAULT_FRAME_TAIL = "0123456789"
UPLOAD_SCRIPT = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "tools"
    / "upload.py"
)
NUS_EXAMPLE = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
    / "BLE"
    / "NordicUart"
    / "BleNordicUartBridge"
)
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
NUS_READY_BANNER = b"X54 NUS ready\r\n"


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
    scan_found: bool
    connected: bool
    rx_handle: Optional[int]
    tx_handle: Optional[int]
    tx_cccd_handle: Optional[int]
    notify_enabled: bool
    banner_seen: bool
    host_to_xiao: TransferResult
    xiao_to_host: TransferResult
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
            time.sleep(0.02)

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
            result = subprocess.run(
                ["sudo", "-n", "true"], capture_output=True, check=False
            )
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
        self.addr = addr
        self.addr_type = addr_type
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
                [
                    r"Connection successful",
                    r"Error.*",
                    r"\[LE\]>",
                    self._pexpect.TIMEOUT,
                ],
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
                continue
            if idx == len(patterns):
                continue
        return found

    def close(self) -> None:
        if self.child.isalive():
            try:
                self.child.sendline("disconnect")
                time.sleep(0.3)
            except Exception:
                pass
            try:
                self.child.sendline("exit")
                self.child.close(force=True)
            except Exception:
                self.child.close(force=True)
        self.log_file.close()

    def log_text(self) -> str:
        self.log_file.flush()
        return self.log_path.read_text(encoding="utf-8", errors="replace")

    def wait_for_regex(self, pattern: str, timeout_s: float, start_at: int = 0) -> re.Match[str]:
        compiled = re.compile(pattern, re.IGNORECASE | re.MULTILINE | re.DOTALL)
        deadline = time.time() + timeout_s
        last_text = ""
        while time.time() < deadline:
            last_text = self.log_text()
            match = compiled.search(last_text, pos=start_at)
            if match is not None:
                return match
            time.sleep(0.1)
        raise TimeoutError(f"Timed out waiting for pattern: {pattern}\n{last_text[-2000:]}")


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


def upload_hex(hex_path: Path, uid: str) -> None:
    run([sys.executable, str(UPLOAD_SCRIPT), "--hex", str(hex_path), "--uid", uid])


def resolve_cached_address_by_name(name: str) -> str:
    if not name:
        return ""

    devices = run(["bluetoothctl", "devices"], check=False)
    pattern = re.compile(r"Device ([0-9A-F:]{17}) (.+)$", re.MULTILINE)
    for candidate_addr, candidate_name in pattern.findall(devices.stdout + devices.stderr):
        if candidate_name.strip() == name:
            return candidate_addr

    for candidate_addr, _ in pattern.findall(devices.stdout + devices.stderr):
        info = run(["bluetoothctl", "info", candidate_addr], check=False)
        info_text = info.stdout + info.stderr
        for key in ("Name:", "Alias:"):
            match = re.search(rf"^{key}\s*(.+)$", info_text, re.MULTILINE)
            if match and match.group(1).strip() == name:
                return candidate_addr

    return ""


def scan_for_device(addr: str, name: str, timeout_s: int, log_path: Path) -> tuple[bool, str]:
    result = run(
        ["bluetoothctl", "--timeout", str(timeout_s), "scan", "on"],
        check=False,
    )
    stop = run(["bluetoothctl", "scan", "off"], check=False)
    combined = result.stdout + result.stderr + "\n--- scan off ---\n" + stop.stdout + stop.stderr
    log_path.write_text(combined, encoding="utf-8")

    resolved_addr = addr
    if name:
        pattern = re.compile(r"Device ([0-9A-F:]{17}) (.+)$", re.MULTILINE)
        matches = pattern.findall(combined)
        for candidate_addr, candidate_name in matches:
            if candidate_name.strip() == name:
                resolved_addr = candidate_addr
        if not resolved_addr:
            resolved_addr = resolve_cached_address_by_name(name)

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


def ascii_packet(prefix: str, index: int, tail: str = DEFAULT_FRAME_TAIL) -> bytes:
    payload = f"{prefix}:{index:03d}:{tail}\r\n".encode("ascii")
    if len(payload) > 20:
        raise ValueError(
            f"ATT write frame exceeds 20-byte value budget: {payload!r}"
        )
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help="Board CDC serial port")
    parser.add_argument("--fqbn", default=FQBN_DEFAULT, help="Board FQBN")
    parser.add_argument("--addr", default=DEFAULT_ADDR, help="BLE advertiser address")
    parser.add_argument(
        "--addr-type",
        default=DEFAULT_ADDR_TYPE,
        choices=("public", "random"),
        help="BLE advertiser address type for gatttool",
    )
    parser.add_argument("--name", default=DEFAULT_NAME, help="Expected advertiser name")
    parser.add_argument("--iface", default="hci0", help="Host Bluetooth controller interface")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Board CDC baud rate")
    parser.add_argument(
        "--iterations",
        type=int,
        default=DEFAULT_ITERATIONS,
        help="Packets to send in each direction",
    )
    parser.add_argument(
        "--scan-timeout",
        type=int,
        default=DEFAULT_SCAN_TIMEOUT_S,
        help="bluetoothctl scan duration in seconds",
    )
    parser.add_argument(
        "--inter-packet-delay",
        type=float,
        default=DEFAULT_INTER_PACKET_S,
        help="Delay between test packets in seconds",
    )
    parser.add_argument(
        "--post-write-delay",
        type=float,
        default=DEFAULT_POST_WRITE_S,
        help="Drain delay after each transfer phase in seconds",
    )
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT / "measurements" / "ble_nus_btmon_latest",
        help="Artifact output directory",
    )
    parser.add_argument(
        "--example",
        type=Path,
        default=NUS_EXAMPLE,
        help="Example sketch directory to compile/upload",
    )
    parser.add_argument(
        "--ready-banner",
        default=NUS_READY_BANNER.decode("utf-8"),
        help="Expected notification banner",
    )
    parser.add_argument(
        "--skip-upload",
        action="store_true",
        help="Skip compile/upload and use the current firmware on the board",
    )
    parser.add_argument(
        "--skip-btmon",
        action="store_true",
        help="Skip btmon capture if only the GATT/serial path matters",
    )
    args = parser.parse_args()

    try:
        import serial  # type: ignore  # noqa: F401
        import pexpect  # type: ignore  # noqa: F401
    except Exception as exc:
        raise SystemExit(f"Missing Python dependency: {exc}") from exc

    args.outdir.mkdir(parents=True, exist_ok=True)
    ready_banner = args.ready_banner.encode("utf-8")

    if not args.skip_upload:
        uid = detect_uid(args.port)
        build_dir = args.outdir / "build"
        hex_path = compile_example(args.example, args.fqbn, build_dir)
        upload_hex(hex_path, uid)
        time.sleep(1.5)

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
    banner_seen = False
    serial_after_host = b""
    notification_stream = b""
    host_to_xiao_packets: list[bytes] = []
    xiao_to_host_packets: list[bytes] = []
    failure = ""

    try:
        scan_found, resolved_addr = scan_for_device(
            args.addr, args.name, args.scan_timeout, args.outdir / "scan.log"
        )
        time.sleep(0.5)

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
            raise RuntimeError(f"Could not resolve NUS characteristics from gatttool output: {found}")
        gatt_log = session.log_text()
        rx_handle, tx_handle = parse_handles(gatt_log)
        if rx_handle is None or tx_handle is None:
            raise RuntimeError("Could not resolve NUS RX/TX handles from gatttool output")

        session.send(
            f"char-desc 0x{tx_handle:04x} 0x{min(tx_handle + 2, 0xFFFF):04x}"
        )
        found = session.wait_for_patterns(
            [r"uuid\s*[:=]\s*(?:0x2902|00002902-0000-1000-8000-00805f9b34fb)"],
            8.0,
        )
        if not all(found.values()):
            raise RuntimeError("Could not resolve NUS TX CCCD from gatttool output")
        desc_log = session.log_text()
        tx_cccd_handle = parse_cccd_handle(desc_log)
        if tx_cccd_handle is None:
            raise RuntimeError("Could not resolve NUS TX CCCD handle from gatttool output")

        session.write_request(tx_cccd_handle, "0100", 8.0)
        time.sleep(1.0)

        host_to_xiao_packets = [
            ascii_packet("H2D", index)
            for index in range(args.iterations)
        ]
        for packet in host_to_xiao_packets:
            session.write_request(rx_handle, packet.hex(), 8.0)
            time.sleep(args.inter_packet_delay)

        time.sleep(args.post_write_delay)
        serial_after_host = serial_capture.data()

        xiao_to_host_packets = [
            ascii_packet("D2H", index)
            for index in range(args.iterations)
        ]
        for packet in xiao_to_host_packets:
            serial_capture.write(packet)
            time.sleep(args.inter_packet_delay)

        time.sleep(args.post_write_delay)

        session.send("disconnect")
        time.sleep(0.5)

        gatt_log = session.log_text()
        notification_stream = parse_notification_stream(gatt_log)
        banner_seen = ready_banner in notification_stream

    except Exception as exc:
        failure = str(exc)

    finally:
        if session is not None:
            session.close()
        serial_capture.close()
        btmon.stop()

    serial_log_path = args.outdir / "serial.log"
    serial_log_path.write_text(serial_capture.text(), encoding="utf-8")

    btmon_text = ""
    if btmon.text_log_path.is_file():
        btmon_text = btmon.text_log_path.read_text(encoding="utf-8", errors="replace")
        if tx_handle is not None:
            btmon_notifications = parse_btmon_notification_stream(btmon_text, tx_handle)
            if btmon_notifications:
                notification_stream = btmon_notifications
                banner_seen = ready_banner in notification_stream

    host_to_xiao = verify_packets(host_to_xiao_packets, serial_after_host)
    xiao_to_host = verify_packets(xiao_to_host_packets, notification_stream)

    summary = Summary(
        address=resolved_addr,
        address_type=args.addr_type,
        name=args.name,
        port=args.port,
        scan_found=scan_found,
        connected=connected,
        rx_handle=rx_handle,
        tx_handle=tx_handle,
        tx_cccd_handle=tx_cccd_handle,
        notify_enabled=True,
        banner_seen=banner_seen,
        host_to_xiao=host_to_xiao,
        xiao_to_host=xiao_to_host,
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
    if host_to_xiao.missing_packets or xiao_to_host.missing_packets:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
