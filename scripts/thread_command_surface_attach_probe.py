#!/usr/bin/env python3
"""
Drive the staged Thread command-surface example from a host serial port.

This is intended for real external-network attach validation once
`ThreadExperimentalCommandSurface` is flashed to a board.

Typical use:
  python3 scripts/thread_command_surface_attach_probe.py \
      --port /dev/ttyACM0 \
      --dataset-hex <ACTIVE_DATASET_TLV_HEX>
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import serial


@dataclass
class ThreadCommandState:
    values: Dict[str, str] = field(default_factory=dict)
    history: List[str] = field(default_factory=list)

    def update_from_line(self, line: str) -> None:
        self.history.append(line)
        prefix = "thread_cmd "
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

    def attached(self) -> bool:
        return self.get_int("attached") == 1

    def role(self) -> str:
        return self.get_str("role") or "unknown"

    def attach_state(self) -> str:
        return self.get_str("attach_state") or "unknown"

    def attach_mode(self) -> str:
        return self.get_str("attach_mode") or "unknown"

    def summary_lines(self) -> List[str]:
        lines = [
            f"role={self.role()}",
            f"attached={1 if self.attached() else 0}",
            f"dataset={self.get_str('dataset') or 'n/a'}",
            f"restored={self.get_str('restored') or 'n/a'}",
            f"rloc16={self.get_str('rloc16') or 'n/a'}",
            f"attach_state={self.attach_state()}",
            f"attach_mode={self.attach_mode()}",
            f"reattach_mode={self.get_str('reattach_mode') or 'n/a'}",
            f"parent_candidate_state={self.get_str('parent_candidate_state') or 'n/a'}",
            f"attach_in_progress={self.get_str('attach_in_progress') or 'n/a'}",
            f"attach_timer_remaining_ms={self.get_str('attach_timer_remaining_ms') or 'n/a'}",
            f"attach_attempts={self.get_str('attach_attempts') or 'n/a'}",
            f"better_parent_attach_attempts={self.get_str('better_parent_attach_attempts') or 'n/a'}",
            f"better_partition_attach_attempts={self.get_str('better_partition_attach_attempts') or 'n/a'}",
            f"parent_changes={self.get_str('parent_changes') or 'n/a'}",
            f"last_flags={self.get_str('last_flags') or 'n/a'}",
        ]
        return lines


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Drive ThreadExperimentalCommandSurface from the host."
    )
    parser.add_argument("--port", required=True, help="Serial port for the board.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate.")
    parser.add_argument(
        "--dataset-hex",
        default="",
        help="Active Operational Dataset TLV hex to apply before waiting for attach.",
    )
    parser.add_argument(
        "--wipe-settings",
        action="store_true",
        help="Send wipe-settings before applying the dataset.",
    )
    parser.add_argument(
        "--timeout-s",
        type=float,
        default=45.0,
        help="Maximum seconds to wait for attach.",
    )
    parser.add_argument(
        "--status-interval-s",
        type=float,
        default=3.0,
        help="Seconds between state/stats polls.",
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
    state: ThreadCommandState,
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


def print_summary(state: ThreadCommandState) -> None:
    print("thread_attach_probe summary:")
    for line in state.summary_lines():
        print(f"  {line}")


def main() -> int:
    args = parse_args()
    state = ThreadCommandState()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as port:
            time.sleep(1.0)
            port.reset_input_buffer()
            port.reset_output_buffer()

            send_command(port, "help")
            read_lines(port, state, 1.0, args.dump_lines)

            if args.wipe_settings:
                send_command(port, "wipe-settings")
                read_lines(port, state, 1.5, args.dump_lines)

            if args.dataset_hex:
                send_command(port, f"dataset-hex {args.dataset_hex}")
                read_lines(port, state, 2.0, args.dump_lines)

            deadline = time.monotonic() + args.timeout_s
            while time.monotonic() < deadline:
                send_command(port, "state")
                send_command(port, "stats")
                read_lines(port, state, args.status_interval_s, args.dump_lines)
                if state.attached():
                    print("thread_attach_probe result=attached")
                    print_summary(state)
                    return 0

            print("thread_attach_probe result=timeout")
            print_summary(state)
            return 1
    except serial.SerialException as exc:
        print(f"thread_attach_probe serial_error={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
