#!/usr/bin/env python3
"""
Thin wrapper around the vendored Microsoft uf2conv.py utility.

The board package owns the board/family/base-address arguments here so future
boards can override them through build properties without rewriting platform
recipes or patching upstream uf2conv.py.
"""

import argparse
import os
import subprocess
import sys


def parse_args():
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
        default=os.path.join(os.path.dirname(__file__), "uf2conv.py"),
        help="Path to the upstream uf2conv.py script.",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    input_path = os.path.abspath(args.input)
    output_path = os.path.abspath(args.output)
    uf2conv_path = os.path.abspath(args.uf2conv)

    if not os.path.isfile(input_path):
        sys.stderr.write("Missing UF2 input HEX file: {0}\n".format(input_path))
        return 1
    if not os.path.isfile(uf2conv_path):
        sys.stderr.write("Missing uf2conv.py tool: {0}\n".format(uf2conv_path))
        return 1

    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.isdir(output_dir):
        os.makedirs(output_dir)

    command = [
        sys.executable,
        uf2conv_path,
        input_path,
        "-c",
        "-f",
        args.family,
        "-b",
        args.base_address,
        "-o",
        output_path,
    ]

    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main())
