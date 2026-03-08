#!/usr/bin/env python3
"""Run two-board nrf_to_nrf regression checks on XIAO nRF54L15.

This script compiles one of the checked-in compatibility examples, uploads it to
both boards, drives the serial-node selection flow, and asserts that the two
boards exchange packets as expected.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List


ROOT = Path(__file__).resolve().parents[1]
FQBN_DEFAULT = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
UPLOAD_SCRIPT = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "tools"
    / "upload.py"
)
EXAMPLES = {
    "getting-started": ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "examples"
    / "Peripherals"
    / "nrf_to_nrfGettingStarted",
    "ack-payloads": ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "examples"
    / "Peripherals"
    / "nrf_to_nrfAcknowledgementPayloads",
}


@dataclass
class PortInfo:
    port: str
    uid: str


class SerialCapture:
    def __init__(self, port: str, baud: int) -> None:
        import serial  # type: ignore

        self.port = port
        self._serial = serial.Serial(port, baud, timeout=0.05)
        self._chunks: List[bytes] = []
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

    def text(self) -> str:
        return b"".join(self._chunks).decode("utf-8", errors="replace")


def run(cmd: List[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(cmd, text=True, capture_output=True)
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
    raise SystemExit(f"Could not resolve probe UID for {port}")


def compile_example(example_dir: Path, fqbn: str, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    for existing in output_dir.iterdir():
        if existing.is_file():
            existing.unlink()
    run([
        "arduino-cli",
        "compile",
        "--output-dir",
        str(output_dir),
        "-b",
        fqbn,
        str(example_dir),
    ])
    hex_files = sorted(output_dir.glob("*.hex"))
    if len(hex_files) != 1:
        raise SystemExit(f"Expected one hex in {output_dir}, found {len(hex_files)}")
    return hex_files[0]


def upload_hex(hex_path: Path, uid: str) -> None:
    run([
        sys.executable,
        str(UPLOAD_SCRIPT),
        "--hex",
        str(hex_path),
        "--uid",
        uid,
    ])


def evaluate_ack(rx_log: str, tx_log: str) -> None:
    if "Received 8 bytes on pipe 1: Hello" not in rx_log:
        raise SystemExit("RX side did not log expected Hello payload reception")
    if "Transmission successful!" not in tx_log:
        raise SystemExit("TX side did not report any successful transmission")
    if "Received 8 bytes on pipe 1: World" not in tx_log:
        raise SystemExit("TX side did not log expected ACK payload reception")


def evaluate_getting_started(rx_log: str, tx_log: str) -> None:
    if "Received 4 bytes on pipe 1:" not in rx_log:
        raise SystemExit("RX side did not log float payload reception")
    if "Transmission successful!" not in tx_log:
        raise SystemExit("TX side did not report any successful transmission")


def save_logs(outdir: Path, rx_log: str, tx_log: str) -> None:
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "rx.log").write_text(rx_log, encoding="utf-8")
    (outdir / "tx.log").write_text(tx_log, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--example",
        choices=sorted(EXAMPLES.keys()),
        default="ack-payloads",
        help="Which checked-in nrf_to_nrf compatibility example to validate",
    )
    parser.add_argument("--rx-port", default="/dev/ttyACM0", help="RX board CDC port")
    parser.add_argument("--tx-port", default="/dev/ttyACM1", help="TX board CDC port")
    parser.add_argument("--fqbn", default=FQBN_DEFAULT, help="Board FQBN")
    parser.add_argument("--baud", type=int, default=115200, help="CDC baud rate")
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT / "measurements" / "nrf_to_nrf_dual_board_latest",
        help="Directory where RX/TX logs are written",
    )
    parser.add_argument(
        "--skip-upload",
        action="store_true",
        help="Skip compile/upload and only drive/capture the current firmware",
    )
    args = parser.parse_args()

    try:
        import serial  # type: ignore  # noqa: F401
    except Exception as exc:
        raise SystemExit(f"pyserial is required: {exc}") from exc

    rx = PortInfo(port=args.rx_port, uid=detect_uid(args.rx_port))
    tx = PortInfo(port=args.tx_port, uid=detect_uid(args.tx_port))

    if not args.skip_upload:
        build_dir = ROOT / "measurements" / f"build_nrf_to_nrf_regression_{args.example}"
        hex_path = compile_example(EXAMPLES[args.example], args.fqbn, build_dir)
        upload_hex(hex_path, rx.uid)
        upload_hex(hex_path, tx.uid)

    rx_cap = SerialCapture(rx.port, args.baud)
    tx_cap = SerialCapture(tx.port, args.baud)
    rx_cap.start()
    tx_cap.start()
    try:
        time.sleep(0.35)
        tx_cap.write(b"1\n")
        time.sleep(2.8)
        for _ in range(3):
            tx_cap.write(b"T\n")
            time.sleep(0.7)
        time.sleep(6.0)
    finally:
        rx_cap.close()
        tx_cap.close()

    rx_log = rx_cap.text()
    tx_log = tx_cap.text()
    save_logs(args.outdir, rx_log, tx_log)

    if args.example == "ack-payloads":
        evaluate_ack(rx_log, tx_log)
    else:
        evaluate_getting_started(rx_log, tx_log)

    print("nrf_to_nrf dual-board regression OK")
    print(f"example: {args.example}")
    print(f"rx port: {rx.port} uid={rx.uid}")
    print(f"tx port: {tx.port} uid={tx.uid}")
    print(f"logs: {args.outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
