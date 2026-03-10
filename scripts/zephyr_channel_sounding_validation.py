#!/usr/bin/env python3
"""Build and flash the official Zephyr connected-CS sample on XIAO nRF54L15."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOARD = "xiao_nrf54l15/nrf54l15/cpuapp"
DEFAULT_SAMPLE = "connected_cs"
DEFAULT_TOOLS_CANDIDATES = (
    REPO_ROOT / "hardware/nrf54l15clean/nrf54l15clean/tools",
    Path.home()
    / ".local/share/Trash/files/here/xiao-nrf54l15-arduino-core/hardware/seeed/nrf54l15/tools",
)
ROLE_TO_SAMPLE_PATH = {
    "initiator": Path("zephyr/samples/bluetooth/channel_sounding/connected_cs/initiator"),
    "reflector": Path("zephyr/samples/bluetooth/channel_sounding/connected_cs/reflector"),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate controller-backed BLE Channel Sounding on XIAO nRF54L15."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    build = subparsers.add_parser("build", help="Build Zephyr connected_cs role(s).")
    add_common_build_args(build)
    build.add_argument(
        "--role",
        choices=("initiator", "reflector", "both"),
        default="both",
        help="Which role to build.",
    )

    flash = subparsers.add_parser("flash", help="Flash a previously built role.")
    add_common_build_args(flash)
    flash.add_argument("--role", choices=("initiator", "reflector"), required=True)
    add_probe_args(flash)

    reset = subparsers.add_parser("reset", help="Reset a board through pyOCD.")
    add_common_build_args(reset)
    add_probe_args(reset)

    pair = subparsers.add_parser(
        "pair-demo",
        help="Build, flash, and reset both connected_cs roles on two boards.",
    )
    add_common_build_args(pair)
    pair.add_argument("--skip-build", action="store_true", help="Reuse existing build outputs.")
    pair.add_argument(
        "--initiator-port",
        default="/dev/ttyACM0",
        help="Serial port whose attached probe should receive the initiator image.",
    )
    pair.add_argument(
        "--reflector-port",
        default="/dev/ttyACM1",
        help="Serial port whose attached probe should receive the reflector image.",
    )

    return parser.parse_args()


def add_common_build_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--tools-dir",
        default="",
        help="Path to a tools dir containing ncs/, pydeps/, and zephyr-sdk/.",
    )
    parser.add_argument(
        "--board",
        default=DEFAULT_BOARD,
        help=f"Zephyr board target. Default: {DEFAULT_BOARD}",
    )
    parser.add_argument(
        "--build-root",
        default=str(REPO_ROOT / "dist" / "zephyr_channel_sounding"),
        help="Root directory for west build outputs.",
    )


def add_probe_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--port",
        default="",
        help="Serial port used to infer the matching CMSIS-DAP UID on Linux.",
    )
    parser.add_argument(
        "--uid",
        default="",
        help="Explicit CMSIS-DAP UID. Overrides --port inference.",
    )


def normalize_uid(uid: str) -> str | None:
    cleaned = uid.strip()
    return cleaned or None


def infer_uid_from_port(port: str) -> str | None:
    if not port or not sys.platform.startswith("linux"):
        return None

    by_id_dir = Path("/dev/serial/by-id")
    if not by_id_dir.is_dir():
        return None

    try:
        target = Path(port).resolve(strict=True)
    except OSError:
        return None

    for entry in by_id_dir.iterdir():
        try:
            if entry.resolve(strict=True) != target:
                continue
        except OSError:
            continue

        match = re.search(r"_([0-9A-Fa-f]+)-if\d+$", entry.name)
        if match:
            return match.group(1)
    return None


def require_uid(port: str, uid: str) -> str:
    explicit = normalize_uid(uid)
    if explicit:
        return explicit

    inferred = infer_uid_from_port(port)
    if inferred:
        return inferred

    raise RuntimeError("Unable to resolve CMSIS-DAP UID. Pass --uid or a Linux --port.")


def detect_tools_dir(explicit: str) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())

    env_tools = os.environ.get("XIAO_NRF54L15_TOOLS_DIR", "").strip()
    if env_tools:
        candidates.append(Path(env_tools).expanduser())

    for candidate in DEFAULT_TOOLS_CANDIDATES:
        candidates.append(candidate)

    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if not resolved.is_dir():
            continue
        if (
            (resolved / "ncs" / "zephyr").is_dir()
            and (resolved / "pydeps").is_dir()
            and (resolved / "zephyr-sdk").is_dir()
        ):
            return resolved

    raise RuntimeError(
        "Could not find an NCS tools directory. Pass --tools-dir or set "
        "XIAO_NRF54L15_TOOLS_DIR."
    )


def run(cmd: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    print("$", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=cwd, env=env, check=False, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed with exit code {proc.returncode}")


def zephyr_env(tools_dir: Path) -> dict[str, str]:
    env = dict(os.environ)
    sdk_dir = tools_dir / "zephyr-sdk"
    env["ZEPHYR_BASE"] = str(tools_dir / "ncs" / "zephyr")
    env["ZEPHYR_SDK_INSTALL_DIR"] = str(sdk_dir)
    pydeps_dir = str(tools_dir / "pydeps")
    existing_pythonpath = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = (
        pydeps_dir
        if not existing_pythonpath
        else os.pathsep.join([pydeps_dir, existing_pythonpath])
    )
    path_entries = [
        sdk_dir / "arm-zephyr-eabi" / "bin",
        sdk_dir / "hosttools" / "bin",
        sdk_dir / "hosttools" / "usr" / "bin",
    ]
    existing_path = env.get("PATH", "")
    env["PATH"] = os.pathsep.join([str(p) for p in path_entries] + [existing_path])
    return env


def detect_python() -> str:
    python = shutil.which("python3")
    if python:
        return python
    python = shutil.which("python")
    if python:
        return python
    raise RuntimeError("python3 is required")


def detect_pyocd() -> list[str]:
    exe = shutil.which("pyocd")
    if exe:
        return [exe]

    python = detect_python()
    probe = subprocess.run(
        [python, "-m", "pyocd", "--version"],
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode == 0:
        return [python, "-m", "pyocd"]
    raise RuntimeError("pyOCD is required")


def west_cmd(tools_dir: Path) -> list[str]:
    return [detect_python(), "-m", "west"]


def build_dir_for(build_root: Path, role: str) -> Path:
    return build_root / DEFAULT_SAMPLE / role


def app_dir_for(tools_dir: Path, role: str) -> Path:
    return tools_dir / "ncs" / ROLE_TO_SAMPLE_PATH[role]


def build_role(tools_dir: Path, build_root: Path, board: str, role: str) -> Path:
    env = zephyr_env(tools_dir)
    build_dir = build_dir_for(build_root, role)
    app_dir = app_dir_for(tools_dir, role)
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    run(
        west_cmd(tools_dir)
        + ["build", "-p", "always", "-b", board, str(app_dir), "-d", str(build_dir)],
        cwd=tools_dir / "ncs",
        env=env,
    )
    return build_dir


def flash_role(tools_dir: Path, build_root: Path, role: str, uid: str) -> None:
    _ = tools_dir
    build_dir = build_dir_for(build_root, role)
    merged_hex = build_dir / "merged.hex"
    if not merged_hex.is_file():
        raise RuntimeError(f"Missing build artifact: {merged_hex}")

    cmd = [*detect_pyocd(), "load", "-W", "-t", "nrf54l", "-u", uid, str(merged_hex), "--format", "hex"]
    run(cmd, cwd=REPO_ROOT, env=dict(os.environ))


def reset_probe(uid: str) -> None:
    cmd = [*detect_pyocd(), "commander", "-t", "nrf54l", "-u", uid, "-c", "reset"]
    run(cmd, cwd=REPO_ROOT, env=dict(os.environ))


def print_monitor_hint(initiator_port: str, reflector_port: str) -> None:
    print()
    print("Open serial before reset to catch Zephyr boot logs:")
    print(f"  stty -F {initiator_port} 115200 raw -echo && stdbuf -oL cat {initiator_port}")
    print(f"  stty -F {reflector_port} 115200 raw -echo && stdbuf -oL cat {reflector_port}")
    print("Then run this command again with `reset` or `pair-demo` to retrigger boot output.")


def main() -> int:
    args = parse_args()
    tools_dir = detect_tools_dir(args.tools_dir)
    build_root = Path(args.build_root).expanduser().resolve()

    if args.command == "build":
        roles = ("initiator", "reflector") if args.role == "both" else (args.role,)
        for role in roles:
            build_path = build_role(tools_dir, build_root, args.board, role)
            print(f"Built {role}: {build_path}")
        return 0

    if args.command == "flash":
        uid = require_uid(args.port, args.uid)
        flash_role(tools_dir, build_root, args.role, uid)
        print(f"Flashed {args.role} using UID {uid}")
        return 0

    if args.command == "reset":
        uid = require_uid(args.port, args.uid)
        reset_probe(uid)
        print(f"Reset board UID {uid}")
        return 0

    if args.command == "pair-demo":
        if not args.skip_build:
            for role in ("initiator", "reflector"):
                build_path = build_role(tools_dir, build_root, args.board, role)
                print(f"Built {role}: {build_path}")

        initiator_uid = require_uid(args.initiator_port, "")
        reflector_uid = require_uid(args.reflector_port, "")
        flash_role(tools_dir, build_root, "initiator", initiator_uid)
        flash_role(tools_dir, build_root, "reflector", reflector_uid)
        reset_probe(initiator_uid)
        reset_probe(reflector_uid)
        print(f"Initiator flashed/reset on {args.initiator_port} ({initiator_uid})")
        print(f"Reflector flashed/reset on {args.reflector_port} ({reflector_uid})")
        print_monitor_hint(args.initiator_port, args.reflector_port)
        return 0

    raise RuntimeError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
