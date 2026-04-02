#!/usr/bin/env python3
"""Compile/upload background-advertiser diagnostics and run stability checks."""

from __future__ import annotations

import argparse
import asyncio
import json
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    import serial  # type: ignore
except Exception as exc:  # pragma: no cover
    serial = None
    SERIAL_IMPORT_ERROR = exc
else:
    SERIAL_IMPORT_ERROR = None

from bleak import BleakScanner


ROOT = Path(__file__).resolve().parents[1]
LIBRARY_ROOT = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation"
)
DIAG_SKETCH = (
    LIBRARY_ROOT / "examples/BLE/Advertising/BleBackgroundAdvertiser3ChannelDiagnostics"
)
PLAIN_3CH_SKETCH = LIBRARY_ROOT / "examples/BLE/Advertising/BleBackgroundAdvertiser3Channel"
DEFAULT_FQBN = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_power=low"
DEFAULT_PORT = "/dev/ttyACM2"
DEFAULT_PROBE_UID = "E91217E8"
DEFAULT_BAUD = 115200
DEFAULT_CASES = ("3ch-default", "3ch-random", "1ch-default", "1ch-random")


@dataclass(frozen=True)
class CaseConfig:
    name: str
    target_name: str
    build_flags: list[str]
    single_channel: bool
    random_delay: bool
    soak_seconds: float
    reboot_cycles: int


@dataclass
class CycleResult:
    cycle_index: int
    serial_ok: bool
    scan_ok: bool
    line_count: int
    diag_line_count: int
    first_diag_ms: int | None
    last_diag_ms: int | None
    errors: list[str] = field(default_factory=list)
    last_fields: dict[str, int] = field(default_factory=dict)


@dataclass
class CaseResult:
    config: CaseConfig
    compile_build_path: str
    upload_ok: bool = False
    soak_ok: bool = False
    soak_result: CycleResult | None = None
    reboot_results: list[CycleResult] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        if not self.upload_ok or not self.soak_ok:
            return False
        return all(result.serial_ok and result.scan_ok for result in self.reboot_results)


CASE_CONFIGS: dict[str, CaseConfig] = {
    "3ch-default": CaseConfig(
        name="3ch-default",
        target_name="X54-BG-DIAG",
        build_flags=[],
        single_channel=False,
        random_delay=False,
        soak_seconds=40.0,
        reboot_cycles=4,
    ),
    "3ch-random": CaseConfig(
        name="3ch-random",
        target_name="X54-BG-DIAG",
        build_flags=["-DNRF54L15_BG_DIAG_RANDOM_DELAY=1"],
        single_channel=False,
        random_delay=True,
        soak_seconds=30.0,
        reboot_cycles=3,
    ),
    "1ch-default": CaseConfig(
        name="1ch-default",
        target_name="X54-BG-1CH",
        build_flags=["-DNRF54L15_BG_DIAG_SINGLE_CHANNEL=1"],
        single_channel=True,
        random_delay=False,
        soak_seconds=30.0,
        reboot_cycles=3,
    ),
    "1ch-random": CaseConfig(
        name="1ch-random",
        target_name="X54-BG-1CH",
        build_flags=[
            "-DNRF54L15_BG_DIAG_SINGLE_CHANNEL=1",
            "-DNRF54L15_BG_DIAG_RANDOM_DELAY=1",
        ],
        single_channel=True,
        random_delay=True,
        soak_seconds=30.0,
        reboot_cycles=3,
    ),
}


def run_cmd(cmd: list[str], cwd: Path | None = None) -> None:
    print("$", " ".join(cmd))
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, text=True)
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


def parse_build_flags(flags: list[str], build_path: Path) -> str:
    parts = [f'-include "{build_path}/generated/CoreVersionGenerated.h"']
    parts.extend(flags)
    return " ".join(parts)


def compile_case(case: CaseConfig, build_path: Path, fqbn: str) -> None:
    cmd = [
        "arduino-cli",
        "compile",
        "--fqbn",
        fqbn,
        "--library",
        str(LIBRARY_ROOT),
        "--build-path",
        str(build_path),
        "--build-property",
        f"compiler.cpp.extra_flags={parse_build_flags(case.build_flags, build_path)}",
        str(DIAG_SKETCH),
    ]
    run_cmd(cmd, cwd=ROOT)


def upload_build(port: str, fqbn: str, build_path: Path) -> None:
    run_cmd(
        [
            "arduino-cli",
            "upload",
            "-p",
            port,
            "--fqbn",
            fqbn,
            "--input-dir",
            str(build_path),
        ],
        cwd=ROOT,
    )


def upload_plain_3ch(port: str, fqbn: str, build_path: Path) -> None:
    run_cmd(
        [
            "arduino-cli",
            "compile",
            "--fqbn",
            fqbn,
            "--library",
            str(LIBRARY_ROOT),
            "--build-path",
            str(build_path),
            str(PLAIN_3CH_SKETCH),
        ],
        cwd=ROOT,
    )
    upload_build(port, fqbn, build_path)


def pyocd_reset(probe_uid: str) -> None:
    run_cmd(["pyocd", "cmd", "-u", probe_uid, "-t", "nrf54l", "-c", "reset"], cwd=ROOT)


def wait_for_serial_port(port: str, timeout_s: float = 10.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with serial.Serial(port, DEFAULT_BAUD, timeout=0.2):  # type: ignore[arg-type]
                return
        except Exception:
            time.sleep(0.05)
    raise RuntimeError(f"serial port did not appear: {port}")


def read_serial_lines(port: str, baud: int, duration_s: float) -> list[str]:
    if serial is None:
        raise RuntimeError(f"pyserial unavailable: {SERIAL_IMPORT_ERROR}")
    deadline = time.monotonic() + duration_s
    lines: list[str] = []
    with serial.Serial(port, baud, timeout=0.25) as ser:  # type: ignore[arg-type]
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            if text:
                print(text)
                lines.append(text)
    return lines


def parse_diag_line(line: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            out[key] = int(value, 10)
        except ValueError:
            continue
    return out


async def scan_for_target(target_name: str, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        devices = await BleakScanner.discover(timeout=4.0)
        for device in devices:
            if (device.name or "").strip() == target_name:
                print(f"[scan] FOUND {device.address} {device.name}")
                return True
        await asyncio.sleep(0.5)
    print(f"[scan] NOT_FOUND {target_name}")
    return False


def validate_cycle(case: CaseConfig, cycle_index: int, lines: list[str], scan_ok: bool) -> CycleResult:
    diag_lines = [parse_diag_line(line) for line in lines if line.startswith("ms=")]
    diag_lines = [line for line in diag_lines if line]
    result = CycleResult(
        cycle_index=cycle_index,
        serial_ok=False,
        scan_ok=scan_ok,
        line_count=len(lines),
        diag_line_count=len(diag_lines),
        first_diag_ms=diag_lines[0].get("ms") if diag_lines else None,
        last_diag_ms=diag_lines[-1].get("ms") if diag_lines else None,
        last_fields=diag_lines[-1] if diag_lines else {},
    )

    if not diag_lines:
        result.errors.append("no_diagnostics_lines")
        return result

    last = diag_lines[-1]
    required_positive = (
        "arm",
        "complete",
        "irq",
        "service",
        "tx_ready",
        "tx_phyend",
        "tx_disabled",
        "clk_irq",
        "clk_tuned",
    )
    for key in required_positive:
        if last.get(key, 0) <= 0:
            result.errors.append(f"{key}_not_positive")

    if last.get("enabled") != 1 or last.get("live_enabled") != 1:
        result.errors.append("advertiser_not_enabled")
    if last.get("stop_reason") != 0:
        result.errors.append(f"stop_reason_{last.get('stop_reason')}")
    if last.get("idle") != 0:
        result.errors.append(f"idle_fallback_{last.get('idle')}")
    if last.get("tx_settle_to") != 0:
        result.errors.append(f"tx_settle_timeout_{last.get('tx_settle_to')}")
    if last.get("tx_kick_fallback") != 0:
        result.errors.append(f"unexpected_tx_kick_fallback_{last.get('tx_kick_fallback')}")
    if last.get("clk_fail") != 0:
        result.errors.append(f"clk_fail_{last.get('clk_fail')}")
    if last.get("clk_err") != 0:
        result.errors.append(f"clk_err_{last.get('clk_err')}")
    if last.get("xo_running") != 0:
        result.errors.append(f"xo_running_{last.get('xo_running')}")

    three_ch_expected = 0 if case.single_channel else 1
    if last.get("three_ch") != three_ch_expected:
        result.errors.append(f"three_ch_mismatch_{last.get('three_ch')}")

    if case.random_delay:
        if last.get("rng_hw", 0) <= 0:
            result.errors.append("rng_hw_not_positive")
        if last.get("rng_sw", 0) != 0:
            result.errors.append(f"rng_sw_nonzero_{last.get('rng_sw')}")
    else:
        if last.get("rng_hw", 0) != 0:
            result.errors.append(f"rng_hw_nonzero_{last.get('rng_hw')}")
        if last.get("rng_sw", 0) != 0:
            result.errors.append(f"rng_sw_nonzero_{last.get('rng_sw')}")

    result.serial_ok = not result.errors
    if not result.scan_ok:
        result.errors.append("scan_not_found")
    return result


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def save_cycle_log(output_dir: Path, case_name: str, cycle_index: int, lines: list[str]) -> None:
    write_text(output_dir / f"{case_name}_cycle{cycle_index:02d}.log", "\n".join(lines) + "\n")


async def run_case(
    case: CaseConfig,
    fqbn: str,
    port: str,
    probe_uid: str,
    output_dir: Path,
    baud: int,
) -> CaseResult:
    build_path = output_dir / f"build_{case.name}"
    result = CaseResult(config=case, compile_build_path=str(build_path))
    compile_case(case, build_path, fqbn)
    upload_build(port, fqbn, build_path)
    result.upload_ok = True

    pyocd_reset(probe_uid)
    wait_for_serial_port(port)
    soak_lines = read_serial_lines(port, baud, case.soak_seconds)
    soak_scan_ok = await scan_for_target(case.target_name, 8.0)
    save_cycle_log(output_dir, case.name, 0, soak_lines)
    result.soak_result = validate_cycle(case, 0, soak_lines, soak_scan_ok)
    result.soak_ok = result.soak_result.serial_ok and result.soak_result.scan_ok

    for cycle in range(1, case.reboot_cycles + 1):
        pyocd_reset(probe_uid)
        wait_for_serial_port(port)
        lines = read_serial_lines(port, baud, 6.0)
        scan_ok = await scan_for_target(case.target_name, 6.0)
        save_cycle_log(output_dir, case.name, cycle, lines)
        result.reboot_results.append(validate_cycle(case, cycle, lines, scan_ok))

    return result


def summarize_results(results: list[CaseResult]) -> dict[str, Any]:
    return {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "cases": [
            {
                "config": asdict(result.config),
                "compile_build_path": result.compile_build_path,
                "upload_ok": result.upload_ok,
                "soak_ok": result.soak_ok,
                "soak_result": asdict(result.soak_result) if result.soak_result else None,
                "reboot_results": [asdict(item) for item in result.reboot_results],
                "passed": result.passed,
            }
            for result in results
        ],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run stability/reboot regression on background BLE advertiser diagnostics."
    )
    parser.add_argument("--fqbn", default=DEFAULT_FQBN)
    parser.add_argument("--port", default="", help="Serial/upload port. Defaults to auto-detect.")
    parser.add_argument("--probe-uid", default=DEFAULT_PROBE_UID)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--cases",
        nargs="+",
        default=list(DEFAULT_CASES),
        choices=sorted(CASE_CONFIGS.keys()),
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Directory for logs and summary. Defaults to measurements/background_advertiser_regression_<timestamp>.",
    )
    return parser.parse_args()


def default_output_dir() -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return ROOT / "measurements" / f"background_advertiser_regression_{stamp}"


async def async_main() -> int:
    args = parse_args()
    port = args.port or auto_detect_port()
    output_dir = Path(args.output_dir).resolve() if args.output_dir else default_output_dir()
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"[info] port={port}")
    print(f"[info] output_dir={output_dir}")

    results: list[CaseResult] = []
    for case_name in args.cases:
        case = CASE_CONFIGS[case_name]
        print(f"[case] begin {case.name}")
        case_result = await run_case(case, args.fqbn, port, args.probe_uid, output_dir, args.baud)
        results.append(case_result)
        print(
            f"[case] end {case.name} passed={case_result.passed} "
            f"soak_ok={case_result.soak_ok} reboot_passes="
            f"{sum(1 for item in case_result.reboot_results if item.serial_ok and item.scan_ok)}/"
            f"{len(case_result.reboot_results)}"
        )

    summary = summarize_results(results)
    summary_path = output_dir / "summary.json"
    write_text(summary_path, json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(f"[summary] wrote {summary_path}")

    failures = [result.config.name for result in results if not result.passed]
    if failures:
        print(f"[summary] FAIL {', '.join(failures)}")
        return 1

    print("[summary] PASS")
    return 0


def main() -> int:
    try:
        return asyncio.run(async_main())
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
