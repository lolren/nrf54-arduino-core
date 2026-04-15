#!/usr/bin/env python3
"""
Thin wrapper around the vendored Microsoft uf2conv.py utility.

The board package owns the board/family/base-address arguments here so future
boards can override them through build properties without rewriting platform
recipes or patching upstream uf2conv.py.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Emit a UF2 artifact from a HEX image.")
    parser.add_argument("--input", required=True, help="Input Intel HEX file.")
    parser.add_argument("--output", required=True, help="Output UF2 file.")
    parser.add_argument("--family", required=True, help="UF2 family ID, usually hex.")
    parser.add_argument(
        "--base-address",
        default="0x00000000",
        help="Application base address passed through to uf2conv.py.",
    )
    parser.add_argument(
        "--uf2conv",
        default=str(pathlib.Path(__file__).with_name("uf2conv.py")),
        help="Path to the upstream uf2conv.py script.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)
    uf2conv_path = pathlib.Path(args.uf2conv)

    if not input_path.is_file():
        raise FileNotFoundError(f"Missing UF2 input HEX file: {input_path}")
    if not uf2conv_path.is_file():
        raise FileNotFoundError(f"Missing uf2conv.py tool: {uf2conv_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    command = [
        sys.executable,
        str(uf2conv_path),
        str(input_path),
        "-c",
        "-f",
        str(args.family),
        "-b",
        str(args.base_address),
        "-o",
        str(output_path),
    ]

    completed = subprocess.run(command, check=False)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
