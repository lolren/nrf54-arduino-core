#!/usr/bin/env python3
"""
Drive the staged Matter command-surface example from a host serial port.

This is intended for staged on-network commissioning validation once
`MatterOnNetworkOnOffLightCommandSurfaceDemo` is flashed to a board.

Typical use:
  python3 scripts/matter_command_surface_probe.py \
      --port /dev/ttyACM0 \
      --dataset-hex <ACTIVE_DATASET_TLV_HEX> \
      --open-window 180
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import serial


@dataclass
class MatterCommandState:
    values: Dict[str, str] = field(default_factory=dict)
    history: List[str] = field(default_factory=list)

    def update_from_line(self, line: str) -> None:
        self.history.append(line)
        prefix = "matter_cmd_demo "
        if not line.startswith(prefix):
            return
        payload = line[len(prefix) :].strip()
        if "=" not in payload:
            return
        key, value = payload.split("=", 1)
        self.values[key.strip()] = value.strip()

    def get_int(self, key: str) -> Optional[int]:
        raw = self.values.get(key)
        if raw is None:
            return None
        try:
            if raw.startswith(("0x", "0X")):
                return int(raw, 16)
            return int(raw)
        except ValueError:
            return None

    def get_str(self, key: str) -> Optional[str]:
        return self.values.get(key)

    def ready(self) -> bool:
        return self.get_int("ready") == 1

    def bundle_ready(self) -> bool:
        return self.get_int("bundle_ready") == 1

    def thread_attached(self) -> bool:
        return self.get_int("thread_attach_summary_attached") == 1

    def window_state(self) -> str:
        return self.get_str("window") or self.get_str("bundle_window") or ""

    def window_open(self) -> bool:
        return self.window_state() == "open"

    def blocked_by(self) -> str:
        return (
            self.get_str("readiness_blocker")
            or self.get_str("thread_attach_blocker")
            or "n/a"
        )

    def target_reached(self, require_window: bool) -> bool:
        if not self.ready():
            return False
        if require_window:
            return self.bundle_ready() and self.window_open()
        return True

    def summary_lines(self) -> List[str]:
        return [
            f"thread_role={self.get_str('thread_role') or 'n/a'}",
            f"ready={1 if self.ready() else 0}",
            f"bundle_ready={1 if self.bundle_ready() else 0}",
            f"dataset_source={self.get_str('dataset_source') or self.get_str('bundle_dataset_source') or 'n/a'}",
            f"window={self.window_state() or 'n/a'}",
            f"window_seconds={self.get_str('window_seconds') or self.get_str('bundle_window_seconds') or 'n/a'}",
            f"readiness_phase={self.get_str('readiness_phase') or 'n/a'}",
            f"readiness_blocker={self.get_str('readiness_blocker') or 'n/a'}",
            f"readiness_storage={self.get_str('readiness_storage') or 'n/a'}",
            f"readiness_light={self.get_str('readiness_light') or 'n/a'}",
            f"readiness_foundation={self.get_str('readiness_foundation') or 'n/a'}",
            f"readiness_thread_started={self.get_str('readiness_thread_started') or 'n/a'}",
            f"readiness_thread_attached={self.get_str('readiness_thread_attached') or 'n/a'}",
            f"readiness_manual={self.get_str('readiness_manual') or 'n/a'}",
            f"readiness_qr={self.get_str('readiness_qr') or 'n/a'}",
            f"readiness_dataset_exportable={self.get_str('readiness_dataset_exportable') or 'n/a'}",
            f"blocked_by={self.blocked_by()}",
            f"thread_attach_phase={self.get_str('thread_attach_phase') or 'n/a'}",
            f"thread_attach_blocker={self.get_str('thread_attach_blocker') or 'n/a'}",
            f"thread_attach_state={self.get_str('thread_attach_state') or 'n/a'}",
            f"thread_attach_mode={self.get_str('thread_attach_mode') or 'n/a'}",
            f"thread_attach_attempts={self.get_str('thread_attach_attempts') or 'n/a'}",
            f"thread_parent_changes={self.get_str('thread_parent_changes') or 'n/a'}",
            f"manual={self.get_str('bundle_manual') or self.get_str('manual') or 'n/a'}",
            f"qr={self.get_str('bundle_qr') or self.get_str('qr') or 'n/a'}",
            f"dataset_hex={self.get_str('bundle_ot_tlv_hex') or 'n/a'}",
        ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Drive MatterOnNetworkOnOffLightCommandSurfaceDemo from the host."
    )
    parser.add_argument("--port", required=True, help="Serial port for the board.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate.")
    parser.add_argument(
        "--dataset-hex",
        default="",
        help="Active Operational Dataset TLV hex to apply before waiting for readiness.",
    )
    parser.add_argument(
        "--open-window",
        type=int,
        default=0,
        help="If non-zero, send open-window <seconds> after the node becomes ready.",
    )
    parser.add_argument(
        "--timeout-s",
        type=float,
        default=60.0,
        help="Maximum seconds to wait for ready state.",
    )
    parser.add_argument(
        "--status-interval-s",
        type=float,
        default=3.0,
        help="Seconds between state/bundle polls.",
    )
    parser.add_argument(
        "--dump-lines",
        action="store_true",
        help="Print every serial line while the probe runs.",
    )
    return parser.parse_args()


def send_command(port: serial.Serial, command: str) -> None:
    port.write((command + "\n").encode("utf-8"))
    port.flush()


def read_lines(
    port: serial.Serial,
    state: MatterCommandState,
    duration_s: float,
    dump_lines: bool,
) -> None:
    deadline = time.monotonic() + duration_s
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        state.update_from_line(line)
        if dump_lines:
            print(line)


def print_summary(state: MatterCommandState) -> None:
    print("matter_command_probe summary:")
    for line in state.summary_lines():
        print(f"  {line}")


def main() -> int:
    args = parse_args()
    state = MatterCommandState()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as port:
            time.sleep(1.0)
            port.reset_input_buffer()
            port.reset_output_buffer()

            send_command(port, "help")
            read_lines(port, state, 1.0, args.dump_lines)

            if args.dataset_hex:
                send_command(port, f"dataset-hex {args.dataset_hex}")
                read_lines(port, state, 2.0, args.dump_lines)

            deadline = time.monotonic() + args.timeout_s
            opened_window = False
            while time.monotonic() < deadline:
                send_command(port, "state")
                send_command(port, "thread-stats")
                send_command(port, "bundle")
                read_lines(port, state, args.status_interval_s, args.dump_lines)

                if state.ready() and args.open_window > 0 and not opened_window:
                    send_command(port, f"open-window {args.open_window}")
                    read_lines(port, state, 1.0, args.dump_lines)
                    send_command(port, "bundle")
                    read_lines(port, state, 1.0, args.dump_lines)
                    opened_window = True

                if state.target_reached(require_window=args.open_window > 0):
                    print("matter_command_probe result=ready")
                    print_summary(state)
                    return 0

            print("matter_command_probe result=timeout")
            print_summary(state)
            return 1
    except serial.SerialException as exc:
        print(f"matter_command_probe serial_error={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
