#!/usr/bin/env python3
"""Validate Zigbee join + secure rejoin on two connected nRF54 boards."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial


class SerialCapture:
  def __init__(self, port: str, baudrate: int, prefix: str) -> None:
    self._port = port
    self._baudrate = baudrate
    self._prefix = prefix
    self._serial: serial.Serial | None = None
    self._stop = threading.Event()
    self._lock = threading.Lock()
    self._lines: list[str] = []
    self._thread: threading.Thread | None = None

  def open(self) -> None:
    self._serial = serial.Serial(self._port, self._baudrate, timeout=0.2)
    self._stop.clear()
    self._thread = threading.Thread(target=self._reader, daemon=True)
    self._thread.start()

  def close(self) -> None:
    self._stop.set()
    if self._serial is not None:
      try:
        self._serial.close()
      except Exception:
        pass
    if self._thread is not None:
      self._thread.join(timeout=2.0)
    self._serial = None
    self._thread = None

  def write(self, payload: bytes) -> None:
    if self._serial is None:
      raise RuntimeError(f"{self._prefix} serial port is not open")
    self._serial.write(payload)
    self._serial.flush()

  def text(self) -> str:
    with self._lock:
      return "".join(self._lines)

  def mark(self) -> int:
    return len(self.text())

  def save(self, path: Path) -> None:
    path.write_text(self.text())

  def _reader(self) -> None:
    assert self._serial is not None
    while not self._stop.is_set():
      try:
        raw = self._serial.readline()
      except Exception:
        break
      if not raw:
        continue
      text = raw.decode("utf-8", errors="replace")
      with self._lock:
        self._lines.append(text)
      sys.stdout.write(f"[{self._prefix}] {text}")
      sys.stdout.flush()


def wait_for(getter, pattern: str, timeout_s: float, start_index: int = 0):
  regex = re.compile(pattern, re.MULTILINE)
  deadline = time.time() + timeout_s
  while time.time() < deadline:
    text = getter()
    match = regex.search(text[start_index:])
    if match:
      return True, start_index + match.start()
    time.sleep(0.2)
  return False, -1


def reset_joinable(uid: str) -> None:
  cmd = ["pyocd", "reset", "-u", uid, "-t", "nrf54l"]
  result = subprocess.run(cmd, text=True, capture_output=True, check=False)
  if result.stdout:
    sys.stdout.write(result.stdout)
  if result.stderr:
    sys.stdout.write(result.stderr)
  sys.stdout.flush()
  if result.returncode != 0:
    raise RuntimeError(f"pyocd reset failed with exit code {result.returncode}")


def reopen_capture(port: str, baudrate: int, prefix: str, timeout_s: float) -> SerialCapture:
  deadline = time.time() + timeout_s
  last_error: Exception | None = None
  while time.time() < deadline:
    capture = SerialCapture(port, baudrate, prefix)
    try:
      capture.open()
      return capture
    except Exception as exc:  # pragma: no cover - hardware timing path
      last_error = exc
      time.sleep(0.5)
  if last_error is not None:
    raise last_error
  raise RuntimeError(f"{prefix} port did not return")


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--coord-port", default="/dev/ttyACM1")
  parser.add_argument("--join-port", default="/dev/ttyACM0")
  parser.add_argument("--join-uid", default="E91217E8")
  parser.add_argument("--baudrate", type=int, default=115200)
  parser.add_argument(
      "--output-dir",
      default="/home/lolren/Desktop/Nrf54L15/.build/zigbee_runtime_unified/rejoin_validation_unified",
  )
  args = parser.parse_args()

  out_dir = Path(args.output_dir)
  out_dir.mkdir(parents=True, exist_ok=True)
  summary_path = out_dir / "summary.txt"
  coord_log_path = out_dir / "coordinator.log"
  join_log_path = out_dir / "joinable.log"

  summary: dict[str, str] = {
      "duration_s": "0.0",
      "initial_scan_join_ok": "false",
      "initial_transport_key_ok": "false",
      "initial_simple_desc_ok": "false",
      "first_toggle_ok": "false",
      "rejoin_started": "false",
      "rejoin_ok": "false",
      "update_device_prepared": "false",
      "update_device_seen": "false",
      "second_toggle_ok": "false",
      "fallback_scan_join_after_reset": "unknown",
  }

  start = time.time()
  coord = SerialCapture(args.coord_port, args.baudrate, "coord")
  join = SerialCapture(args.join_port, args.baudrate, "join")
  join_before_reset = ""

  try:
    coord.open()
    join.open()

    time.sleep(0.5)
    coord_mark = coord.mark()
    coord.write(b"c")
    ok, _ = wait_for(lambda: coord.text(), r"nodes cleared", 10.0, start_index=coord_mark)
    if not ok:
      raise RuntimeError("coordinator did not clear nodes")
    coord.write(b"p")
    join_mark = join.mark()
    join.write(b"c")

    ok, _ = wait_for(lambda: join.text(), r"state cleared", 10.0, start_index=join_mark)
    if not ok:
      raise RuntimeError("joinable did not acknowledge clear state")
    join.close()
    reset_joinable(args.join_uid)
    join = reopen_capture(args.join_port, args.baudrate, "join", 20.0)
    time.sleep(1.0)
    coord_mark = coord.mark()
    coord.write(b"c")
    ok, _ = wait_for(lambda: coord.text(), r"nodes cleared", 10.0, start_index=coord_mark)
    if not ok:
      raise RuntimeError("coordinator did not re-clear nodes after joinable reset")
    coord.write(b"p")
    join_mark = join.mark()
    join.write(b"j")
    ok, _ = wait_for(
        lambda: join.text(),
        r"network_steering requested",
        10.0,
        start_index=join_mark,
    )
    if not ok:
      raise RuntimeError("joinable did not request network steering")

    ok, _ = wait_for(lambda: join.text(), r"scan_join start", 25.0, start_index=join_mark)
    if not ok:
      raise RuntimeError("joinable never started scan_join")
    summary["initial_scan_join_ok"] = "true"

    coord_join_mark = coord.mark()
    ok, _ = wait_for(
        lambda: coord.text(),
        r"transport_key OK stage=prepared_tx",
        25.0,
        start_index=coord_join_mark,
    )
    if not ok:
      raise RuntimeError("coordinator never delivered transport key")
    summary["initial_transport_key_ok"] = "true"

    ok, simple_desc_index = wait_for(
        lambda: coord.text(),
        r"simple_desc short=0x[0-9A-Fa-f]+ ep=1 profile=0x104",
        25.0,
    )
    if not ok:
      raise RuntimeError("coordinator never completed simple descriptor interview")
    summary["initial_simple_desc_ok"] = "true"

    ok, _ = wait_for(
        lambda: coord.text(),
        r"alive joined=1 pending=0 permit_join=",
        20.0,
        start_index=simple_desc_index,
    )
    if not ok:
      raise RuntimeError("coordinator never reached idle state after interview")

    coord.write(b"t")
    ok, _ = wait_for(lambda: join.text(), r"zcl cluster=0x6 onoff=ON", 20.0)
    if not ok:
      raise RuntimeError("first toggle did not reach joinable")
    summary["first_toggle_ok"] = "true"

    join_before_reset = join.text()
    join.close()
    reset_joinable(args.join_uid)
    join = reopen_capture(args.join_port, args.baudrate, "join", 20.0)

    ok, _ = wait_for(lambda: join.text(), r"secure_rejoin start", 25.0)
    if not ok:
      raise RuntimeError("secure_rejoin start not observed after reset")
    summary["rejoin_started"] = "true"

    ok, _ = wait_for(
        lambda: coord.text(),
        r"update_device prepared short=0x[0-9A-Fa-f]+ len=",
        25.0,
    )
    if ok:
      summary["update_device_prepared"] = "true"

    ok, _ = wait_for(
        lambda: join.text(),
        r"secure_rejoin OK ch=15 pan=0x1234 short=0x[0-9A-Fa-f]+",
        25.0,
    )
    if not ok:
      raise RuntimeError("secure_rejoin OK not observed")
    summary["rejoin_ok"] = "true"

    ok, _ = wait_for(
        lambda: join.text(),
        r"update_device short=0x[0-9A-Fa-f]+ status=0x0",
        25.0,
    )
    if not ok:
      raise RuntimeError("update_device not observed on joinable")
    summary["update_device_seen"] = "true"

    post_reset_text = join.text()
    summary["fallback_scan_join_after_reset"] = (
        "true" if "scan_join start" in post_reset_text else "false"
    )

    coord.write(b"t")
    ok, _ = wait_for(
        lambda: join.text(),
        r"zcl cluster=0x6 onoff=OFF",
        20.0,
    )
    if not ok:
      raise RuntimeError("second toggle did not reach joinable after rejoin")
    summary["second_toggle_ok"] = "true"
  finally:
    summary["duration_s"] = f"{time.time() - start:.1f}"
    coord.save(coord_log_path)
    join_log_path.write_text(join_before_reset + join.text())
    summary_path.write_text(
        "\n".join(f"{key}={value}" for key, value in summary.items()) + "\n"
    )
    coord.close()
    join.close()

  print("SUMMARY")
  print(summary_path.read_text(), end="")
  failed = any(
      value == "false"
      for key, value in summary.items()
      if key not in {"duration_s", "fallback_scan_join_after_reset"}
  )
  return 1 if failed else 0


if __name__ == "__main__":
  raise SystemExit(main())
