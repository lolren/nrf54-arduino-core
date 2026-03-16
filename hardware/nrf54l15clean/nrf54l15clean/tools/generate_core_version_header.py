#!/usr/bin/env python3
"""Generate CoreVersionGenerated.h from the package version string."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Package version string, e.g. 0.1.86")
    parser.add_argument("--out", required=True, type=Path, help="Output header path")
    return parser.parse_args()


def parse_version(version: str) -> tuple[int, int, int]:
    match = re.fullmatch(r"\s*(\d+)\.(\d+)\.(\d+)\s*", version)
    if not match:
        raise SystemExit(f"Unsupported version format {version!r}; expected major.minor.patch")
    return tuple(int(group) for group in match.groups())


def render_header(version: str, major: int, minor: int, patch: int) -> str:
    return f"""#ifndef NRF54L15_CLEAN_CORE_VERSION_GENERATED_H
#define NRF54L15_CLEAN_CORE_VERSION_GENERATED_H

#define ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR {major}
#define ARDUINO_NRF54L15_CLEAN_VERSION_MINOR {minor}
#define ARDUINO_NRF54L15_CLEAN_VERSION_PATCH {patch}

#define ARDUINO_NRF54L15_CLEAN_VERSION_ENCODE(major, minor, patch) \\
    (((major) * 10000UL) + ((minor) * 100UL) + (patch))

#define ARDUINO_NRF54L15_CLEAN_VERSION \\
    ARDUINO_NRF54L15_CLEAN_VERSION_ENCODE( \\
        ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR, \\
        ARDUINO_NRF54L15_CLEAN_VERSION_MINOR, \\
        ARDUINO_NRF54L15_CLEAN_VERSION_PATCH)

#define ARDUINO_NRF54L15_CLEAN_VERSION_STRING "{version}"

#endif  // NRF54L15_CLEAN_CORE_VERSION_GENERATED_H
"""


def main() -> int:
    args = parse_args()
    major, minor, patch = parse_version(args.version)
    output_path = args.out.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        render_header(args.version, major, minor, patch),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
