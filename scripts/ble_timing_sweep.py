#!/usr/bin/env python3
"""Automate BLE timing/tx-power sweeps for BleConnectionTimingMetrics example."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import re
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


WINDOW_RE = re.compile(
    r"window_ms=(?P<window_ms>\d+)\s+adv=(?P<adv>\d+)\s+link=(?P<link>\d+)\s+"
    r"rx_ok=(?P<rx_ok>\d+)\s+crc_fail=(?P<crc_fail>\d+)\s+"
    r"rx_timeout=(?P<rx_timeout>\d+)\s+tx_timeout=(?P<tx_timeout>\d+)\s+"
    r"ll=(?P<ll>\d+)\s+att=(?P<att>\d+)\s+enc=(?P<enc>\d+)"
)

DEFAULT_TIMING = ("interop", "balanced", "aggressive")
DEFAULT_TX = ("n20", "n8", "p0", "p8")
DEFAULT_CPU = "cpu64"
DEFAULT_POWER = "low"
DEFAULT_SKETCH = (
    "hardware/nrf54l15clean/nrf54l15clean/libraries/"
    "Nrf54L15-Clean-Implementation/examples/BleConnectionTimingMetrics"
)
DEFAULT_FQBN_BASE = "nrf54l15clean:nrf54l15clean:xiao_nrf54l15"


@dataclass
class RunMetrics:
    timing: str
    tx: str
    run_index: int
    raw_log_path: Path
    window_count: int
    window_s: float
    adv_total: int
    link_total: int
    rx_ok_total: int
    crc_fail_total: int
    rx_timeout_total: int
    tx_timeout_total: int
    ll_total: int
    att_total: int
    enc_total: int
    adv_hz: float
    link_hz: float
    rx_ok_per_link: float
    crc_fail_per_link: float
    rx_timeout_per_link: float
    tx_timeout_per_link: float
    monitor_backend: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sweep BLE timing profile and TX power options on hardware."
    )
    parser.add_argument(
        "--port",
        default="",
        help="Serial port for upload/monitor (default: auto-detect via arduino-cli board list).",
    )
    parser.add_argument(
        "--duration-s",
        type=float,
        default=20.0,
        help="Monitor capture duration per run (seconds).",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=1,
        help="Runs per timing/tx combination.",
    )
    parser.add_argument(
        "--timing",
        nargs="+",
        default=list(DEFAULT_TIMING),
        choices=list(DEFAULT_TIMING),
        help="Timing profiles to test.",
    )
    parser.add_argument(
        "--tx",
        nargs="+",
        default=list(DEFAULT_TX),
        choices=list(DEFAULT_TX),
        help="TX power options to test.",
    )
    parser.add_argument(
        "--cpu",
        default=DEFAULT_CPU,
        choices=("cpu64", "cpu128"),
        help="clean_cpu option.",
    )
    parser.add_argument(
        "--power",
        default=DEFAULT_POWER,
        choices=("low", "balanced"),
        help="clean_power option.",
    )
    parser.add_argument(
        "--skip-compile",
        action="store_true",
        help="Skip compile step and upload cached build only.",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baudrate for monitor capture.",
    )
    parser.add_argument(
        "--serial-settle-s",
        type=float,
        default=2.0,
        help="Delay after upload before monitor capture starts.",
    )
    parser.add_argument(
        "--require-windows",
        action="store_true",
        help="Fail with non-zero exit if no timing windows are captured in any run.",
    )
    parser.add_argument(
        "--fqbn-base",
        default=DEFAULT_FQBN_BASE,
        help="FQBN base without options.",
    )
    parser.add_argument(
        "--sketch",
        default=DEFAULT_SKETCH,
        help="Sketch path to compile/upload.",
    )
    parser.add_argument(
        "--outdir",
        default="",
        help="Output directory for logs/reports. Default: measurements/ble_timing_sweep_<timestamp>.",
    )
    return parser.parse_args()


def run_cmd(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    print("$", " ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if check and proc.returncode != 0:
        if proc.stdout:
            print(proc.stdout, end="", file=sys.stderr)
        if proc.stderr:
            print(proc.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed: {' '.join(cmd)}")
    return proc


def auto_detect_port() -> str:
    proc = run_cmd(["arduino-cli", "board", "list"], check=True)
    lines = [ln for ln in proc.stdout.splitlines() if "nrf54l15clean" in ln]
    if not lines:
        raise RuntimeError("unable to auto-detect nrf54l15clean board port")
    first = lines[0].split()
    if not first:
        raise RuntimeError("unexpected board list format")
    return first[0]


def fqbn_with_options(base: str, timing: str, tx: str, cpu: str, power: str) -> str:
    opts = (
        f"clean_ble=on,clean_ble_tx={tx},clean_ble_timing={timing},"
        f"clean_cpu={cpu},clean_power={power}"
    )
    return f"{base}:{opts}"


def compile_sketch(fqbn: str, sketch: str) -> None:
    run_cmd(["arduino-cli", "compile", "--fqbn", fqbn, sketch], check=True)


def upload_sketch(fqbn: str, sketch: str, port: str) -> None:
    run_cmd(["arduino-cli", "upload", "-p", port, "--fqbn", fqbn, sketch], check=True)


def capture_with_pyserial(port: str, baud: int, duration_s: float) -> str:
    try:
        import serial  # type: ignore
    except Exception as exc:  # pragma: no cover - fallback path.
        raise RuntimeError(f"pyserial unavailable: {exc}") from exc

    out_chunks: list[str] = []
    deadline = time.monotonic() + duration_s
    with serial.Serial(port=port, baudrate=baud, timeout=0.2) as ser:
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        while time.monotonic() < deadline:
            data = ser.read(512)
            if not data:
                continue
            out_chunks.append(data.decode("utf-8", errors="replace"))
    return "".join(out_chunks)


def capture_with_arduino_monitor(port: str, baud: int, duration_s: float) -> str:
    cmd = ["arduino-cli", "monitor", "-p", port, "--config", f"baudrate={baud}"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        time.sleep(duration_s)
    finally:
        proc.terminate()
    try:
        stdout, _ = proc.communicate(timeout=2.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, _ = proc.communicate()
    return stdout or ""


def capture_serial(port: str, baud: int, duration_s: float) -> tuple[str, str]:
    try:
        text = capture_with_pyserial(port, baud, duration_s)
        if WINDOW_RE.search(text):
            return text, "pyserial"
    except Exception:
        pass

    text = capture_with_arduino_monitor(port, baud, duration_s)
    return text, "arduino-cli-monitor"


def parse_windows(text: str) -> list[dict[str, int]]:
    out: list[dict[str, int]] = []
    for raw in text.splitlines():
        match = WINDOW_RE.search(raw.strip())
        if not match:
            continue
        row: dict[str, int] = {}
        for key, value in match.groupdict().items():
            row[key] = int(value)
        out.append(row)
    return out


def safe_ratio(num: float, den: float) -> float:
    return (num / den) if den > 0.0 else 0.0


def aggregate_run(
    timing: str,
    tx: str,
    run_index: int,
    raw_log_path: Path,
    windows: Iterable[dict[str, int]],
    monitor_backend: str,
) -> RunMetrics:
    rows = list(windows)
    window_ms = sum(r["window_ms"] for r in rows)
    window_s = window_ms / 1000.0

    adv_total = sum(r["adv"] for r in rows)
    link_total = sum(r["link"] for r in rows)
    rx_ok_total = sum(r["rx_ok"] for r in rows)
    crc_fail_total = sum(r["crc_fail"] for r in rows)
    rx_timeout_total = sum(r["rx_timeout"] for r in rows)
    tx_timeout_total = sum(r["tx_timeout"] for r in rows)
    ll_total = sum(r["ll"] for r in rows)
    att_total = sum(r["att"] for r in rows)
    enc_total = sum(r["enc"] for r in rows)

    adv_hz = safe_ratio(float(adv_total), window_s)
    link_hz = safe_ratio(float(link_total), window_s)
    rx_ok_per_link = safe_ratio(float(rx_ok_total), float(link_total))
    crc_fail_per_link = safe_ratio(float(crc_fail_total), float(link_total))
    rx_timeout_per_link = safe_ratio(float(rx_timeout_total), float(link_total))
    tx_timeout_per_link = safe_ratio(float(tx_timeout_total), float(link_total))

    return RunMetrics(
        timing=timing,
        tx=tx,
        run_index=run_index,
        raw_log_path=raw_log_path,
        window_count=len(rows),
        window_s=window_s,
        adv_total=adv_total,
        link_total=link_total,
        rx_ok_total=rx_ok_total,
        crc_fail_total=crc_fail_total,
        rx_timeout_total=rx_timeout_total,
        tx_timeout_total=tx_timeout_total,
        ll_total=ll_total,
        att_total=att_total,
        enc_total=enc_total,
        adv_hz=adv_hz,
        link_hz=link_hz,
        rx_ok_per_link=rx_ok_per_link,
        crc_fail_per_link=crc_fail_per_link,
        rx_timeout_per_link=rx_timeout_per_link,
        tx_timeout_per_link=tx_timeout_per_link,
        monitor_backend=monitor_backend,
    )


def write_run_csv(path: Path, rows: list[RunMetrics]) -> None:
    fields = [
        "timing",
        "tx",
        "run_index",
        "window_count",
        "window_s",
        "adv_total",
        "link_total",
        "rx_ok_total",
        "crc_fail_total",
        "rx_timeout_total",
        "tx_timeout_total",
        "ll_total",
        "att_total",
        "enc_total",
        "adv_hz",
        "link_hz",
        "rx_ok_per_link",
        "crc_fail_per_link",
        "rx_timeout_per_link",
        "tx_timeout_per_link",
        "monitor_backend",
        "raw_log_path",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    "timing": row.timing,
                    "tx": row.tx,
                    "run_index": row.run_index,
                    "window_count": row.window_count,
                    "window_s": f"{row.window_s:.3f}",
                    "adv_total": row.adv_total,
                    "link_total": row.link_total,
                    "rx_ok_total": row.rx_ok_total,
                    "crc_fail_total": row.crc_fail_total,
                    "rx_timeout_total": row.rx_timeout_total,
                    "tx_timeout_total": row.tx_timeout_total,
                    "ll_total": row.ll_total,
                    "att_total": row.att_total,
                    "enc_total": row.enc_total,
                    "adv_hz": f"{row.adv_hz:.4f}",
                    "link_hz": f"{row.link_hz:.4f}",
                    "rx_ok_per_link": f"{row.rx_ok_per_link:.6f}",
                    "crc_fail_per_link": f"{row.crc_fail_per_link:.6f}",
                    "rx_timeout_per_link": f"{row.rx_timeout_per_link:.6f}",
                    "tx_timeout_per_link": f"{row.tx_timeout_per_link:.6f}",
                    "monitor_backend": row.monitor_backend,
                    "raw_log_path": str(row.raw_log_path),
                }
            )


def write_summary_md(path: Path, rows: list[RunMetrics], args: argparse.Namespace) -> None:
    grouped: dict[tuple[str, str], list[RunMetrics]] = {}
    for row in rows:
        grouped.setdefault((row.timing, row.tx), []).append(row)

    lines: list[str] = []
    lines.append("# BLE Timing Sweep Summary")
    lines.append("")
    lines.append(f"Generated: {dt.datetime.now().isoformat(timespec='seconds')}")
    lines.append(f"Port: `{args.port}`")
    lines.append(f"Sketch: `{args.sketch}`")
    lines.append(f"Duration per run: `{args.duration_s}` s, runs: `{args.runs}`")
    lines.append("")
    lines.append(
        "| Timing | TX | Runs | Win Count Mean | Link Hz Mean | RX Timeout/Link Mean | TX Timeout/Link Mean | CRC Fail/Link Mean |"
    )
    lines.append(
        "|---|---|---:|---:|---:|---:|---:|---:|"
    )

    for timing in args.timing:
        for tx in args.tx:
            combo_rows = grouped.get((timing, tx), [])
            if not combo_rows:
                lines.append(f"| {timing} | {tx} | 0 | 0.00 | 0.0000 | 0.000000 | 0.000000 | 0.000000 |")
                continue
            lines.append(
                "| "
                + f"{timing} | {tx} | {len(combo_rows)} | "
                + f"{statistics.mean(r.window_count for r in combo_rows):.2f} | "
                + f"{statistics.mean(r.link_hz for r in combo_rows):.4f} | "
                + f"{statistics.mean(r.rx_timeout_per_link for r in combo_rows):.6f} | "
                + f"{statistics.mean(r.tx_timeout_per_link for r in combo_rows):.6f} | "
                + f"{statistics.mean(r.crc_fail_per_link for r in combo_rows):.6f} |"
            )

    lines.append("")
    lines.append("## Notes")
    lines.append("")
    lines.append("- `Link Hz Mean` near `0` indicates no active BLE connection during capture.")
    lines.append("- Use run-level CSV for detailed per-run totals and raw-log file paths.")
    lines.append("")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.runs < 1:
        print("--runs must be >= 1", file=sys.stderr)
        return 2
    if args.duration_s <= 0.0:
        print("--duration-s must be > 0", file=sys.stderr)
        return 2

    if not args.port:
        args.port = auto_detect_port()
        print(f"auto-detected port: {args.port}")

    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    outdir = (
        Path(args.outdir)
        if args.outdir
        else Path("measurements") / f"ble_timing_sweep_{timestamp}"
    )
    outdir.mkdir(parents=True, exist_ok=True)
    raw_dir = outdir / "raw_logs"
    raw_dir.mkdir(parents=True, exist_ok=True)

    all_rows: list[RunMetrics] = []

    for timing in args.timing:
        for tx in args.tx:
            fqbn = fqbn_with_options(args.fqbn_base, timing, tx, args.cpu, args.power)
            print(f"\n=== combo timing={timing} tx={tx} ===")
            if not args.skip_compile:
                compile_sketch(fqbn, args.sketch)
            for run_index in range(1, args.runs + 1):
                print(f"-- run {run_index}/{args.runs}")
                upload_sketch(fqbn, args.sketch, args.port)
                time.sleep(args.serial_settle_s)
                capture_text, monitor_backend = capture_serial(
                    args.port, args.baud, args.duration_s
                )
                raw_log_path = raw_dir / f"{timing}_{tx}_run{run_index}.log"
                raw_log_path.write_text(capture_text, encoding="utf-8", errors="replace")
                windows = parse_windows(capture_text)
                row = aggregate_run(
                    timing=timing,
                    tx=tx,
                    run_index=run_index,
                    raw_log_path=raw_log_path,
                    windows=windows,
                    monitor_backend=monitor_backend,
                )
                print(
                    f"captured windows={row.window_count} link_total={row.link_total} "
                    f"link_hz={row.link_hz:.4f} rx_timeout/link={row.rx_timeout_per_link:.6f}"
                )
                all_rows.append(row)

    run_csv = outdir / "ble_timing_sweep_runs.csv"
    summary_md = outdir / "ble_timing_sweep_summary.md"
    write_run_csv(run_csv, all_rows)
    write_summary_md(summary_md, all_rows, args)

    total_windows = sum(r.window_count for r in all_rows)
    if total_windows == 0:
        print(
            "warning: no timing windows captured. Check serial routing, monitor port, "
            "and increase --serial-settle-s/--duration-s.",
            file=sys.stderr,
        )
        if args.require_windows:
            return 1

    print("\nSweep complete.")
    print(f"run csv: {run_csv}")
    print(f"summary: {summary_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
