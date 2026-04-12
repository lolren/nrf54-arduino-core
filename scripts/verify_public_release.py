#!/usr/bin/env python3
"""Verify public release URLs serve the exact bytes described by the package index."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", required=True, type=Path, help="Path to package index JSON")
    parser.add_argument("--version", required=True, help="Platform version to verify")
    parser.add_argument(
        "--include-tools",
        action="store_true",
        help="Also verify host-tool assets referenced by the index",
    )
    parser.add_argument(
        "--tool-name",
        default="nrf54l15hosttools",
        help="Tool name to verify when --include-tools is set",
    )
    parser.add_argument(
        "--tool-version",
        default="1.1.0",
        help="Tool version to verify when --include-tools is set",
    )
    parser.add_argument(
        "--tool-hosts",
        default="",
        help="Comma-separated host filters for tool verification",
    )
    parser.add_argument("--retries", type=int, default=6, help="Retries per URL")
    parser.add_argument("--retry-delay", type=float, default=10.0, help="Seconds between retries")
    return parser.parse_args()


def load_index(path: Path) -> dict:
    if not path.is_file():
        raise SystemExit(f"Index file not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    try:
        package = data["packages"][0]
    except (KeyError, IndexError, TypeError) as exc:
        raise SystemExit(f"Unexpected package index structure: {exc}") from exc
    if not isinstance(package, dict):
        raise SystemExit("Unexpected package index structure: package is not an object")
    return package


def find_platform(package: dict, version: str) -> dict:
    for platform in package.get("platforms", []):
        if isinstance(platform, dict) and str(platform.get("version", "")) == version:
            return platform
    raise SystemExit(f"Platform version {version!r} not found in package index")


def find_tool_systems(package: dict, name: str, version: str, hosts: set[str]) -> list[dict]:
    for tool in package.get("tools", []):
        if not isinstance(tool, dict):
            continue
        if tool.get("name") != name or str(tool.get("version", "")) != version:
            continue
        systems = []
        for system in tool.get("systems", []):
            if not isinstance(system, dict):
                continue
            host = str(system.get("host", ""))
            if hosts and host not in hosts:
                continue
            systems.append(system)
        if systems:
            return systems
    raise SystemExit(f"Tool {name!r}@{version!r} not found in package index")


def download_with_retries(url: str, retries: int, retry_delay: float) -> bytes:
    headers = {"User-Agent": "nrf54-arduino-core-release-verifier/1.0"}
    last_exc: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            request = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError, OSError, http.client.IncompleteRead) as exc:
            last_exc = exc
            if attempt == retries:
                break
            time.sleep(retry_delay)
    raise SystemExit(f"Failed to download {url!r}: {last_exc}")


def verify_entry(label: str, entry: dict, retries: int, retry_delay: float) -> None:
    url = str(entry.get("url", ""))
    expected_checksum = str(entry.get("checksum", ""))
    expected_size = str(entry.get("size", ""))
    archive_name = str(entry.get("archiveFileName", ""))

    if not url or not expected_checksum or not expected_size or not archive_name:
        raise SystemExit(f"Incomplete entry for {label}: {entry}")

    payload = download_with_retries(url, retries=retries, retry_delay=retry_delay)
    actual_size = str(len(payload))
    if actual_size != expected_size:
        raise SystemExit(f"{label}: size mismatch for {archive_name}: url={actual_size}, index={expected_size}")

    actual_checksum = f"SHA-256:{sha256_bytes(payload)}"
    if actual_checksum != expected_checksum:
        raise SystemExit(
            f"{label}: checksum mismatch for {archive_name}: url={actual_checksum}, index={expected_checksum}"
        )

    print(f"{label} OK")
    print(f"  archive: {archive_name}")
    print(f"  size:    {actual_size}")
    print(f"  sha256:  {actual_checksum.removeprefix('SHA-256:')}")


def main() -> int:
    args = parse_args()
    package = load_index(args.index.resolve())
    platform = find_platform(package, args.version)
    verify_entry(
        f"platform {args.version}",
        platform,
        retries=args.retries,
        retry_delay=args.retry_delay,
    )

    if args.include_tools:
        hosts = {item.strip() for item in args.tool_hosts.split(",") if item.strip()}
        for system in find_tool_systems(package, args.tool_name, args.tool_version, hosts):
            host = str(system.get("host", "unknown"))
            verify_entry(
                f"tool {args.tool_name}@{args.tool_version} [{host}]",
                system,
                retries=args.retries,
                retry_delay=args.retry_delay,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
