#!/usr/bin/env python3
"""Pure-BLE NUS loopback regression with btmon capture.

This script avoids USB CDC entirely. It flashes ``BleNordicUartLoopbackProbe``,
connects with ``gatttool``, enables notifications, writes numbered 20-byte ATT
payloads to the NUS RX characteristic, and verifies that the peripheral echoes
the same bytes back over NUS notifications.

The ``btmon`` capture is host-side HCI traffic only. It is still enough to see
connection/discovery progress, ATT write responses, disconnect reasons, and
whether the echoed notifications were received by the host stack.
"""

from __future__ import annotations

import argparse
import json
import re
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Optional


ROOT = Path(__file__).resolve().parents[1]
FQBN_DEFAULT = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
DEFAULT_ADDR = ""
DEFAULT_NAME = "X54-LB"
DEFAULT_ADDR_TYPE = "random"
DEFAULT_SCAN_TIMEOUT_S = 8
DEFAULT_INTER_PACKET_S = 0.03
DEFAULT_POST_WRITE_S = 2.0
DEFAULT_ITERATIONS = 64
DEFAULT_FRAME_TAIL = "0123456789"
UPLOAD_SCRIPT = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "tools"
    / "upload.py"
)
LOOPBACK_EXAMPLE = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "libraries"
    / "Nrf54L15-Clean-Implementation"
    / "examples"
    / "BLE"
    / "NordicUart"
    / "BleNordicUartLoopbackProbe"
)
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
READY_BANNER = b"X54 NUS loopback ready\r\n"


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
    scan_found: bool
    connected: bool
    rx_handle: Optional[int]
    tx_handle: Optional[int]
    tx_cccd_handle: Optional[int]
    banner_seen: bool
    echo: TransferResult
    disconnect_reason: str
    btmon_enabled: bool
    failure: str


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
                [r"Characteristic value was written successfully", r"Command Failed:.*", r"Error:.*", self._pexpect.TIMEOUT],
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
    result = run(["bluetoothctl", "--timeout", str(timeout_s), "scan", "on"], check=False)
    log_path.write_text(result.stdout + result.stderr, encoding="utf-8")
    combined = result.stdout + result.stderr

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


def ascii_packet(index: int) -> bytes:
    payload = f"LB:{index:03d}:{DEFAULT_FRAME_TAIL}\r\n".encode("ascii")
    if len(payload) > 20:
        raise ValueError(f"Loopback frame exceeds 20-byte value budget: {payload!r}")
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
    return None if match is None else int(match.group(1), 16)


def parse_notification_stream(gatt_log: str) -> bytes:
    chunks: list[bytes] = []
    for match in re.finditer(
        r"Notification handle = 0x[0-9a-f]+ value: ([0-9a-f ]+)",
        gatt_log,
        re.IGNORECASE,
    ):
        payload_hex = " ".join(match.group(1).split())
        if payload_hex:
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


def verify_ordered_packets(expected: list[bytes], observed_stream: bytes) -> TransferResult:
    observed = 0
    offset = 0
    missing: list[str] = []
    for packet in expected:
        pos = observed_stream.find(packet, offset)
        if pos < 0:
            missing.append(packet.decode("ascii", errors="replace"))
            continue
        observed += 1
        offset = pos + len(packet)
    return TransferResult(
        expected_packets=len(expected),
        observed_packets=observed,
        missing_packets=missing,
    )


def last_disconnect_reason(btmon_text: str) -> str:
    matches = re.findall(r"Reason:\s+([^\r\n]+)", btmon_text)
    return matches[-1].strip() if matches else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0", help="Board CDC serial port used for upload UID detection")
    parser.add_argument("--fqbn", default=FQBN_DEFAULT, help="Board FQBN")
    parser.add_argument("--addr", default=DEFAULT_ADDR, help="BLE advertiser address")
    parser.add_argument("--addr-type", default=DEFAULT_ADDR_TYPE, choices=("public", "random"))
    parser.add_argument("--name", default=DEFAULT_NAME, help="Expected advertiser name")
    parser.add_argument("--iface", default="hci0", help="Host Bluetooth controller interface")
    parser.add_argument("--iterations", type=int, default=DEFAULT_ITERATIONS)
    parser.add_argument("--scan-timeout", type=int, default=DEFAULT_SCAN_TIMEOUT_S)
    parser.add_argument("--inter-packet-delay", type=float, default=DEFAULT_INTER_PACKET_S)
    parser.add_argument("--post-write-delay", type=float, default=DEFAULT_POST_WRITE_S)
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT / "measurements" / "ble_nus_loopback_latest",
    )
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--skip-btmon", action="store_true")
    args = parser.parse_args()

    try:
        import pexpect  # type: ignore  # noqa: F401
    except Exception as exc:
        raise SystemExit(f"Missing Python dependency: {exc}") from exc

    args.outdir.mkdir(parents=True, exist_ok=True)

    if not args.skip_upload:
        uid = detect_uid(args.port)
        build_dir = args.outdir / "build"
        hex_path = compile_example(LOOPBACK_EXAMPLE, args.fqbn, build_dir)
        upload_hex(hex_path, uid)
        time.sleep(1.5)

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
    echoed_packets: list[bytes] = []
    failure = ""

    try:
        scan_found, resolved_addr = scan_for_device(
            args.addr, args.name, args.scan_timeout, args.outdir / "scan.log"
        )

        session = GatttoolSession(resolved_addr, args.addr_type, args.outdir / "gatttool.log")
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

        notify_stream_start = len(session.log_text())
        session.write_request(tx_cccd_handle, "0100", 8.0)
        time.sleep(1.0)

        echoed_packets = [ascii_packet(index) for index in range(args.iterations)]
        for packet in echoed_packets:
            session.write_request(rx_handle, packet.hex(), 8.0)
            time.sleep(args.inter_packet_delay)

        time.sleep(args.post_write_delay)
        gatt_log = session.log_text()[notify_stream_start:]
        notify_stream = parse_notification_stream(gatt_log)
        banner_seen = READY_BANNER in notify_stream

    except Exception as exc:
        failure = str(exc)

    finally:
        if session is not None:
            session.close()
        btmon.stop()

    btmon_text = ""
    if btmon.text_log_path.is_file():
        btmon_text = btmon.text_log_path.read_text(encoding="utf-8", errors="replace")
    gatt_log = ""
    gatt_path = args.outdir / "gatttool.log"
    if gatt_path.is_file():
        gatt_log = gatt_path.read_text(encoding="utf-8", errors="replace")
    notify_stream = b""
    if btmon_text and tx_handle is not None:
        notify_stream = parse_btmon_notification_stream(btmon_text, tx_handle)
    if not notify_stream:
        notify_stream = parse_notification_stream(gatt_log)
    echo = verify_ordered_packets(echoed_packets, notify_stream)

    summary = Summary(
        address=resolved_addr,
        address_type=args.addr_type,
        name=args.name,
        scan_found=scan_found,
        connected=connected,
        rx_handle=rx_handle,
        tx_handle=tx_handle,
        tx_cccd_handle=tx_cccd_handle,
        banner_seen=banner_seen,
        echo=echo,
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
    if echo.missing_packets:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
