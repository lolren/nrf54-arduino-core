#!/usr/bin/env python3
"""Run dual-board BLE PHY transition regressions on XIAO nRF54L15 boards.

This script compiles the local probe sketches for either the `coded` or `2m`
transition path, uploads them to two attached boards, captures serial output
from both boards, and asserts the expected transition markers:

- `CODED -> 1M -> CODED`
- `2M -> 1M -> 2M`

It is intended for local bench validation against the working-tree examples and
library sources in this repo.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FQBN_DEFAULT = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
LIBRARIES_DIR = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "libraries"
)
CONNECTION_EXAMPLES = (
    LIBRARIES_DIR
    / "Nrf54L15-Clean-Implementation"
    / "examples"
    / "BLE"
    / "Connections"
)


@dataclass(frozen=True)
class ModeConfig:
    name: str
    peripheral_example: Path
    central_example: Path
    peripheral_checks: tuple[str, ...]
    central_checks: tuple[str, ...]


MODE_CONFIGS: dict[str, ModeConfig] = {
    "coded": ModeConfig(
        name="coded",
        peripheral_example=CONNECTION_EXAMPLES / "BleCodedPhyProbe",
        central_example=CONNECTION_EXAMPLES / "BleCodedPhyCentralProbe",
        peripheral_checks=(
            "cycle phase: fallback 1M observed",
            "cycle phase: coded return complete",
            "notify tx ll=251",
            "mtu=247 data=251",
        ),
        central_checks=(
            "request phy 1M fallback: queued",
            "cycle phase: 1M long notify confirmed",
            "cycle phase: coded returned",
            "cycle phase: coded long notify reconfirmed",
            "notify rx seq=",
            "mtu=247 data=251",
        ),
    ),
    "2m": ModeConfig(
        name="2m",
        peripheral_example=CONNECTION_EXAMPLES / "Ble2MPhyProbe",
        central_example=CONNECTION_EXAMPLES / "Ble2MPhyCentralProbe",
        peripheral_checks=(
            "cycle phase: fallback 1M observed",
            "cycle phase: 2M return complete",
            "notify tx ll=251",
            "mtu=247 data=251",
        ),
        central_checks=(
            "request phy 1M fallback: queued",
            "cycle phase: 1M long notify confirmed",
            "cycle phase: 2M returned",
            "cycle phase: 2M long notify reconfirmed",
            "notify rx seq=",
            "mtu=247 data=251",
        ),
    ),
}


class SerialCapture:
    def __init__(self, port: str, baud: int) -> None:
        try:
            import serial  # type: ignore
        except Exception as exc:
            raise SystemExit(f"pyserial is required: {exc}") from exc

        last_error: Exception | None = None
        self._serial = None
        for _ in range(50):
            try:
                self._serial = serial.Serial(port, baud, timeout=0.05)
                break
            except Exception as exc:  # pragma: no cover - hardware timing path.
                last_error = exc
                time.sleep(0.1)
        if self._serial is None:
            raise SystemExit(f"Could not open serial port {port}: {last_error}")

        self.port = port
        self._chunks: list[bytes] = []
        self._stop = False
        self._thread = threading.Thread(target=self._reader, daemon=True)
        try:
            self._serial.reset_input_buffer()
        except Exception:
            pass

    def start(self) -> None:
        self._thread.start()

    def _reader(self) -> None:
        while not self._stop:
            data = self._serial.read(4096)
            if data:
                self._chunks.append(data)
            time.sleep(0.02)

    def close(self) -> None:
        self._stop = True
        self._thread.join(timeout=0.5)
        self._serial.close()

    def text(self) -> str:
        return b"".join(self._chunks).decode("utf-8", errors="replace")


def run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("$", " ".join(cmd))
    result = subprocess.run(cmd, text=True, capture_output=True)
    if check and result.returncode != 0:
        if result.stdout:
            print(result.stdout, end="", file=sys.stderr)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        raise SystemExit(result.returncode)
    return result


def clear_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def compile_example(example_dir: Path, fqbn: str, build_dir: Path) -> None:
    clear_dir(build_dir)
    run(
        [
            "arduino-cli",
            "compile",
            "--fqbn",
            fqbn,
            "--libraries",
            str(LIBRARIES_DIR),
            "--build-path",
            str(build_dir),
            str(example_dir),
        ]
    )


def upload_build(build_dir: Path, fqbn: str, port: str) -> None:
    run(
        [
            "arduino-cli",
            "upload",
            "-p",
            port,
            "--fqbn",
            fqbn,
            "--input-dir",
            str(build_dir),
        ]
    )


def save_logs(outdir: Path, peripheral_log: str, central_log: str) -> None:
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "peripheral.log").write_text(peripheral_log, encoding="utf-8")
    (outdir / "central.log").write_text(central_log, encoding="utf-8")


def missing_checks(text: str, checks: tuple[str, ...]) -> list[str]:
    return [needle for needle in checks if needle not in text]


def run_mode(
    config: ModeConfig,
    *,
    fqbn: str,
    peripheral_port: str,
    central_port: str,
    baud: int,
    capture_s: float,
    outdir: Path,
    skip_upload: bool,
) -> None:
    build_root = ROOT / "measurements" / "build_ble_phy_transition_regression" / config.name
    peripheral_build = build_root / "peripheral"
    central_build = build_root / "central"

    if not skip_upload:
        compile_example(config.peripheral_example, fqbn, peripheral_build)
        compile_example(config.central_example, fqbn, central_build)
        upload_build(peripheral_build, fqbn, peripheral_port)
        upload_build(central_build, fqbn, central_port)

    peripheral_cap = SerialCapture(peripheral_port, baud)
    central_cap = SerialCapture(central_port, baud)
    peripheral_cap.start()
    central_cap.start()

    deadline = time.monotonic() + capture_s
    success = False
    try:
        while time.monotonic() < deadline:
            peripheral_log = peripheral_cap.text()
            central_log = central_cap.text()
            if (
                not missing_checks(peripheral_log, config.peripheral_checks)
                and not missing_checks(central_log, config.central_checks)
            ):
                success = True
                break
            time.sleep(0.2)
    finally:
        peripheral_log = peripheral_cap.text()
        central_log = central_cap.text()
        peripheral_cap.close()
        central_cap.close()
        save_logs(outdir, peripheral_log, central_log)

    if not success:
        peripheral_missing = missing_checks(peripheral_log, config.peripheral_checks)
        central_missing = missing_checks(central_log, config.central_checks)
        print(f"{config.name}: validation failed", file=sys.stderr)
        if peripheral_missing:
            print(
                "  peripheral missing: " + ", ".join(peripheral_missing),
                file=sys.stderr,
            )
        if central_missing:
            print(
                "  central missing: " + ", ".join(central_missing),
                file=sys.stderr,
            )
        print(f"  logs: {outdir}", file=sys.stderr)
        raise SystemExit(1)

    print(f"{config.name}: PASS")
    print(f"  logs: {outdir}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode",
        choices=("coded", "2m", "both"),
        default="both",
        help="Which transition regression to run.",
    )
    parser.add_argument(
        "--peripheral-port",
        default="/dev/ttyACM0",
        help="Peripheral board CDC port.",
    )
    parser.add_argument(
        "--central-port",
        default="/dev/ttyACM1",
        help="Central board CDC port.",
    )
    parser.add_argument(
        "--fqbn",
        default=FQBN_DEFAULT,
        help="Board FQBN.",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="CDC baud rate.",
    )
    parser.add_argument(
        "--capture-s",
        type=float,
        default=24.0,
        help="Maximum capture time per mode.",
    )
    parser.add_argument(
        "--skip-upload",
        action="store_true",
        help="Skip compile/upload and only validate what is already flashed.",
    )
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT / "measurements" / "ble_phy_transition_latest",
        help="Directory where per-mode logs are written.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    modes = (
        [MODE_CONFIGS["coded"], MODE_CONFIGS["2m"]]
        if args.mode == "both"
        else [MODE_CONFIGS[args.mode]]
    )
    args.outdir.mkdir(parents=True, exist_ok=True)

    for config in modes:
        run_mode(
            config,
            fqbn=args.fqbn,
            peripheral_port=args.peripheral_port,
            central_port=args.central_port,
            baud=args.baud,
            capture_s=args.capture_s,
            outdir=args.outdir / config.name,
            skip_upload=args.skip_upload,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
