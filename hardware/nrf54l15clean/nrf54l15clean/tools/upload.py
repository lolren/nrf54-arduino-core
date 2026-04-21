#!/usr/bin/env python3
"""Arduino upload helper for XIAO nRF54L15 Zephyr-based core.

This wrapper keeps Arduino upload integration cross-platform while relying on
pyOCD for CMSIS-DAP flashing.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

CMSIS_DAP_VENDOR_ID = "2886"
CMSIS_DAP_PRODUCT_ID = "0066"
DEFAULT_PYOCD_TIMEOUT_SECONDS = 120.0
DEFAULT_PYOCD_INSTALL_TIMEOUT_SECONDS = 300.0
DEFAULT_UF2_LABELS = (
    "UF2BOOT",
    "XIAO-NRF54L15",
    "XIAO-NRF54",
    "XIAO-SENSE",
    "NRF54L15",
    "NRF54BOOT",
    "DAPLINK",
)
UF2_MARKER_FILES = ("INFO_UF2.TXT", "CURRENT.UF2", "INDEX.HTM")


def run(cmd: list[str], timeout: float | None = None) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            cmd,
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        stderr = exc.stderr or ""
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        stdout = exc.stdout or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        stderr += f"\nERROR: command timed out after {timeout:.0f}s: {' '.join(cmd)}\n"
        return subprocess.CompletedProcess(
            args=cmd,
            returncode=124,
            stdout=stdout,
            stderr=stderr,
        )


def print_result(result: subprocess.CompletedProcess[str]) -> None:
    if result.stdout:
        print(result.stdout, end="")
    if result.returncode != 0 and result.stderr:
        print(result.stderr, file=sys.stderr, end="")


def tool_available(name: str) -> bool:
    return shutil.which(name) is not None


def normalize_tools_path(path: str | None) -> Path | None:
    if not path:
        return None
    if "{" in path and "}" in path:
        return None
    candidate = Path(path)
    return candidate if candidate.exists() else None


def unresolved_property(value: str | None) -> bool:
    if not value:
        return True
    if "{" in value and "}" in value:
        return True
    return value.strip().lower() in {"auto", "default", "none"}


def normalize_tristate(value: str | None, default: str = "auto") -> str:
    if unresolved_property(value):
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on", "safe", "vm", "vm-safe"}:
        return "true"
    if normalized in {"0", "false", "no", "off", "normal"}:
        return "false"
    return default


def split_csv(value: str | None) -> list[str]:
    if unresolved_property(value):
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def running_inside_virtual_machine() -> bool:
    env_override = normalize_tristate(os.environ.get("NRF54L15_PYOCD_SAFE"), default="")
    if env_override == "true":
        return True
    if env_override == "false":
        return False

    markers = (
        "virtualbox",
        "oracle",
        "vmware",
        "qemu",
        "kvm",
        "hyper-v",
        "parallels",
        "bhyve",
        "xen",
    )

    if sys.platform.startswith("linux") and tool_available("systemd-detect-virt"):
        result = run(["systemd-detect-virt", "--vm"])
        if result.returncode == 0 and (result.stdout or "").strip():
            return True

    if sys.platform.startswith("linux"):
        for path in (
            Path("/sys/class/dmi/id/product_name"),
            Path("/sys/class/dmi/id/sys_vendor"),
            Path("/sys/class/dmi/id/board_vendor"),
        ):
            try:
                text = path.read_text(encoding="utf-8", errors="ignore").lower()
            except OSError:
                continue
            if any(marker in text for marker in markers):
                return True

    return False


def resolve_pyocd_safe_mode(requested: str | None) -> bool:
    normalized = normalize_tristate(requested)
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    return running_inside_virtual_machine()


def normalize_token(value: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "", value.upper())


def derived_uf2_path(hex_path: str, requested_uf2: str | None) -> Path:
    if not unresolved_property(requested_uf2):
        return Path(requested_uf2)
    return Path(hex_path).with_suffix(".uf2")


def pyocd_tool_root(host_tools_path: Path | None) -> Path:
    if host_tools_path is not None:
        return host_tools_path
    return Path(__file__).resolve().parent


def bundled_pyocd_site_path(tool_root: Path | None) -> Path | None:
    if tool_root is None:
        return None
    candidate = tool_root / "runtime" / "pyocd-site"
    return candidate if candidate.is_dir() else None


def bundled_pyocd_command(tool_root: Path | None) -> list[str] | None:
    if tool_root is None:
        return None

    site_dir = bundled_pyocd_site_path(tool_root)
    shim = tool_root / "pyocd_shim.py"
    if site_dir is not None and shim.is_file():
        return [sys.executable, str(shim)]

    candidates = []
    if sys.platform.startswith("win"):
        candidates.extend(
            [
                tool_root / "runtime" / "pyocd-venv" / "Scripts" / "pyocd.exe",
                tool_root / "runtime" / "pyocd-venv" / "Scripts" / "python.exe",
            ]
        )
    else:
        candidates.extend(
            [
                tool_root / "runtime" / "pyocd-venv" / "bin" / "pyocd",
                tool_root / "runtime" / "pyocd-venv" / "bin" / "python",
            ]
        )

    for candidate in candidates:
        if not candidate.is_file():
            continue
        if candidate.name.startswith("pyocd"):
            return [str(candidate)]
        return [str(candidate), "-m", "pyocd"]
    return None


def bundled_wheelhouse_path(tool_root: Path | None) -> Path | None:
    if tool_root is None:
        return None
    wheelhouse_root = tool_root / "wheelhouse"
    if not wheelhouse_root.is_dir():
        return None
    version_tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
    candidate = wheelhouse_root / version_tag
    return candidate if candidate.is_dir() else None


def detect_pyocd_command(host_tools_path: Path | None = None) -> list[str] | None:
    tool_root = pyocd_tool_root(host_tools_path)

    bundled = bundled_pyocd_command(tool_root)
    if bundled is not None:
        return bundled

    pyocd_exe = shutil.which("pyocd")
    if pyocd_exe:
        return [pyocd_exe]

    module_probe = run([sys.executable, "-m", "pyocd", "--version"])
    if module_probe.returncode == 0:
        return [sys.executable, "-m", "pyocd"]

    return None


def host_setup_hint(host_tools_path: Path | None = None, purpose: str = "python") -> str:
    tool_root = pyocd_tool_root(host_tools_path)
    tools_dir = tool_root / "setup"
    if sys.platform.startswith("linux"):
        script = str(tools_dir / "install_linux_host_deps.sh")
        if purpose == "udev":
            return f"Run {script} --udev"
        if purpose == "all":
            return f"Run {script} --all"
        return f"Run {script} --python (or --all on first-time setup)"
    if sys.platform.startswith("win"):
        return (
            "Run PowerShell -ExecutionPolicy Bypass -File "
            + str(tools_dir / "install_windows_host_deps.ps1")
        )
    return "Install Python 3 and pyocd, then retry"


def install_pyocd(host_tools_path: Path | None = None) -> bool:
    print("Attempting to install pyocd for automatic target recovery...")
    tool_root = pyocd_tool_root(host_tools_path)
    runtime_dir = tool_root / "runtime"
    site_dir = runtime_dir / "pyocd-site"
    requirements = tool_root / "requirements-pyocd.txt"
    wheelhouse = bundled_wheelhouse_path(tool_root)
    runtime_dir.mkdir(parents=True, exist_ok=True)
    if site_dir.exists():
        shutil.rmtree(site_dir)
    site_dir.mkdir(parents=True, exist_ok=True)

    pip_check = run([sys.executable, "-m", "pip", "--version"])
    if pip_check.returncode != 0:
        ensurepip = run([sys.executable, "-m", "ensurepip", "--upgrade"])
        print_result(ensurepip)
        if ensurepip.returncode != 0:
            return False

    install_cmd = [
        sys.executable,
        "-m",
        "pip",
        "install",
        "--upgrade",
        "--disable-pip-version-check",
        "--ignore-installed",
        "--target",
        str(site_dir),
    ]
    if wheelhouse is not None:
        print(f"Using bundled offline wheelhouse: {wheelhouse}")
        install_cmd.extend(["--no-index", "--find-links", str(wheelhouse)])
    if requirements.is_file():
        install_cmd.extend(["-r", str(requirements)])
    else:
        install_cmd.append("pyocd")

    install = run(install_cmd, timeout=pyocd_install_timeout_seconds())
    print_result(install)
    if install.returncode != 0 and wheelhouse is not None:
        print("Bundled wheelhouse install failed; retrying with online indexes...")
        online_cmd = [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--upgrade",
            "--disable-pip-version-check",
            "--ignore-installed",
            "--target",
            str(site_dir),
        ]
        if requirements.is_file():
            online_cmd.extend(["-r", str(requirements)])
        else:
            online_cmd.append("pyocd")
        install = run(online_cmd, timeout=pyocd_install_timeout_seconds())
        print_result(install)
    if install.returncode != 0:
        return False

    bundled = bundled_pyocd_command(tool_root)
    if bundled is None:
        return False
    verify = run([*bundled, "--version"])
    print_result(verify)
    return verify.returncode == 0


def resolve_tool(path_or_name: str) -> str | None:
    if not path_or_name:
        return None

    if "{" in path_or_name and "}" in path_or_name:
        # Unresolved Arduino property placeholder, fall back to PATH lookup.
        return shutil.which("openocd")

    candidate = Path(path_or_name)
    if candidate.is_file():
        return str(candidate)

    return shutil.which(path_or_name)


def normalize_uid(requested_uid: str | None) -> str | None:
    if requested_uid is None:
        return None

    cleaned = requested_uid.strip()
    if not cleaned:
        return None
    return cleaned


def infer_uid_from_port(port: str | None) -> str | None:
    if not port:
        return None
    if not sys.platform.startswith("linux"):
        return None

    if tool_available("udevadm"):
        info = run(["udevadm", "info", "-q", "property", "-n", port])
        if info.returncode == 0:
            for line in (info.stdout or "").splitlines():
                if line.startswith("ID_SERIAL_SHORT="):
                    value = line.split("=", 1)[1].strip()
                    if value:
                        return value

    try:
        target_path = Path(port).resolve(strict=True)
    except OSError:
        return None

    by_id_dir = Path("/dev/serial/by-id")
    if not by_id_dir.is_dir():
        return None

    for entry in by_id_dir.iterdir():
        try:
            if entry.resolve(strict=True) != target_path:
                continue
        except OSError:
            continue

        match = re.search(r"_([0-9A-Fa-f]+)-if\d+$", entry.name)
        if match:
            return match.group(1)

    return None


def explicit_uf2_drive(path: str | None) -> Path | None:
    if unresolved_property(path):
        env_path = os.environ.get("NRF54L15_UF2_DRIVE", "")
        if unresolved_property(env_path):
            return None
        path = env_path
    candidate = Path(path).expanduser()
    return candidate if candidate.is_dir() else None


def mounted_volume_candidates() -> list[Path]:
    candidates: list[Path] = []

    if sys.platform.startswith("win"):
        for letter in "DEFGHIJKLMNOPQRSTUVWXYZ":
            root = Path(f"{letter}:\\")
            if root.is_dir():
                candidates.append(root)
        return candidates

    if sys.platform == "darwin":
        volumes = Path("/Volumes")
        if volumes.is_dir():
            candidates.extend(path for path in volumes.iterdir() if path.is_dir())
        return candidates

    user = os.environ.get("USER") or os.environ.get("USERNAME") or ""
    roots = []
    if user:
        roots.extend([Path("/media") / user, Path("/run/media") / user])
    roots.extend([Path("/media"), Path("/run/media"), Path("/mnt")])

    seen: set[Path] = set()
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.iterdir():
            try:
                resolved = path.resolve()
            except OSError:
                resolved = path
            try:
                is_dir = path.is_dir()
            except OSError:
                continue
            if resolved in seen or not is_dir:
                continue
            seen.add(resolved)
            candidates.append(path)
    return candidates


def safe_exists(path: Path) -> bool:
    try:
        return path.exists()
    except OSError:
        return False


def uf2_marker_text(path: Path) -> str:
    info_path = path / "INFO_UF2.TXT"
    if not info_path.is_file():
        return ""
    try:
        return info_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ""


def contains_uf2_marker(path: Path) -> bool:
    return any(safe_exists(path / marker) for marker in UF2_MARKER_FILES)


def looks_like_uf2_drive(path: Path, labels: list[str]) -> bool:
    try:
        if not path.is_dir():
            return False
    except OSError:
        return False

    label_tokens = [normalize_token(label) for label in (labels or list(DEFAULT_UF2_LABELS))]
    volume_token = normalize_token(path.name)
    strong_label_match = any(
        token and (token == volume_token or token in volume_token or volume_token in token)
        for token in label_tokens
    )

    if strong_label_match and os.access(path, os.W_OK):
        return True

    if not contains_uf2_marker(path):
        return False

    marker_text = normalize_token(uf2_marker_text(path))
    marker_tokens = ("UF2", "BOOTLOADER", "DAPLINK", "NRF54", "XIAO")
    if any(token in marker_text for token in marker_tokens):
        return os.access(path, os.W_OK)

    # A CURRENT.UF2 plus bootloader web page is a common UF2/DAPLink shape even
    # when INFO_UF2.TXT is minimal.
    if safe_exists(path / "CURRENT.UF2") and safe_exists(path / "INDEX.HTM"):
        return os.access(path, os.W_OK)
    return False


def find_uf2_drives(drive: str | None, labels: list[str]) -> list[Path]:
    if not unresolved_property(drive):
        candidate = Path(drive).expanduser()
        return [candidate] if candidate.is_dir() else []

    explicit = explicit_uf2_drive(None)
    if explicit is not None:
        return [explicit]
    return [path for path in mounted_volume_candidates() if looks_like_uf2_drive(path, labels)]


def print_uf2_bootloader_hint(labels: list[str]) -> None:
    label_text = ", ".join(labels or list(DEFAULT_UF2_LABELS))
    print("ERROR: No writable UF2 bootloader drive was found.", file=sys.stderr)
    print(
        "HINT: Put the board into UF2 bootloader mode, wait for the USB mass-storage "
        "drive to appear, then upload again.",
        file=sys.stderr,
    )
    print(
        "HINT: If auto reset does not work, double-tap RESET or use the board's "
        "documented BOOT/RESET sequence.",
        file=sys.stderr,
    )
    print(
        f"HINT: Searched labels/markers: {label_text}. "
        "You can set NRF54L15_UF2_DRIVE to the mounted drive path.",
        file=sys.stderr,
    )
    print(
        "HINT: Select pyOCD Recovery (CMSIS-DAP) for blank, locked, or non-bootloader targets.",
        file=sys.stderr,
    )


def copy_uf2_to_drive(uf2_path: Path, drive: Path) -> int:
    destination = drive / uf2_path.name.upper()
    print(f"Copying {uf2_path} to {destination}")
    try:
        with uf2_path.open("rb") as src, destination.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=1024 * 1024)
            dst.flush()
            os.fsync(dst.fileno())
    except OSError as exc:
        if not drive.exists():
            print(
                "UF2 drive disappeared during copy; the bootloader may have accepted "
                "the image and rebooted.",
                file=sys.stderr,
            )
            return 0
        print(f"ERROR: UF2 copy failed: {exc}", file=sys.stderr)
        return 5
    print("UF2 copy complete")
    return 0


def upload_uf2(
    uf2_path: Path,
    drive: str | None,
    labels: list[str],
    timeout: float,
    *,
    quiet_missing: bool = False,
) -> int:
    if not uf2_path.is_file():
        print(f"ERROR: UF2 file not found: {uf2_path}", file=sys.stderr)
        return 2

    deadline = time.monotonic() + max(0.0, timeout)
    drives: list[Path] = []
    while True:
        drives = find_uf2_drives(drive, labels)
        if drives or time.monotonic() >= deadline:
            break
        time.sleep(0.25)

    if not drives:
        if not quiet_missing:
            print_uf2_bootloader_hint(labels)
        return 6
    if len(drives) > 1:
        print("ERROR: Multiple UF2 bootloader drives matched:", file=sys.stderr)
        for candidate in drives:
            print(f"  {candidate}", file=sys.stderr)
        print("HINT: Set NRF54L15_UF2_DRIVE to the exact mount path.", file=sys.stderr)
        return 6

    print(f"Runner: uf2")
    print(f"UF2 drive: {drives[0]}")
    return copy_uf2_to_drive(uf2_path, drives[0])


def _sysfs_usb_identity_for_class_node(
    class_name: str, node_name: str
) -> tuple[str | None, str | None]:
    sys_device = Path("/sys/class") / class_name / node_name / "device"
    try:
        resolved = sys_device.resolve(strict=True)
    except OSError:
        return None, None

    for parent in (resolved, *resolved.parents):
        vid_file = parent / "idVendor"
        pid_file = parent / "idProduct"
        if not vid_file.is_file() or not pid_file.is_file():
            continue
        try:
            vendor = vid_file.read_text(encoding="utf-8").strip().lower()
            product = pid_file.read_text(encoding="utf-8").strip().lower()
        except OSError:
            return None, None
        return vendor, product

    return None, None


def _sysfs_usb_identity_for_hidraw(node: Path) -> tuple[str | None, str | None]:
    return _sysfs_usb_identity_for_class_node("hidraw", node.name)


def _sysfs_usb_identity_for_tty(node: Path) -> tuple[str | None, str | None]:
    return _sysfs_usb_identity_for_class_node("tty", node.name)


def matching_probe_hidraw_nodes() -> list[Path]:
    if not sys.platform.startswith("linux"):
        return []

    matches: list[Path] = []
    for node in sorted(Path("/dev").glob("hidraw*")):
        vendor, product = _sysfs_usb_identity_for_hidraw(node)
        if vendor == CMSIS_DAP_VENDOR_ID and product == CMSIS_DAP_PRODUCT_ID:
            matches.append(node)
    return matches


def probe_hidraw_nodes_accessible(nodes: list[Path]) -> bool:
    return any(os.access(node, os.R_OK | os.W_OK) for node in nodes)


def port_looks_like_probe_serial(port: str | None) -> bool:
    if not sys.platform.startswith("linux") or not port:
        return False

    port_name = Path(port).name
    if not port_name.startswith("tty"):
        return False

    vendor, product = _sysfs_usb_identity_for_tty(Path(port_name))
    return vendor == CMSIS_DAP_VENDOR_ID and product == CMSIS_DAP_PRODUCT_ID


def looks_like_locked_target(result: subprocess.CompletedProcess[str]) -> bool:
    details = ((result.stdout or "") + "\n" + (result.stderr or "")).lower()
    indicators = (
        "approtect",
        "memory transfer fault",
        "fault ack",
        "failed to read memory",
        "dp initialisation failed",
        "ap write error",
        "locked",
    )
    return any(token in details for token in indicators)


def looks_like_nrf54l_mass_erase_timeout(result: subprocess.CompletedProcess[str]) -> bool:
    details = ((result.stdout or "") + "\n" + (result.stderr or "")).lower()
    indicators = (
        "mass erase timeout waiting for eraseallstatus",
        "no cores were discovered",
    )
    return any(token in details for token in indicators)


def looks_like_no_probe_error(result: subprocess.CompletedProcess[str]) -> bool:
    details = ((result.stdout or "") + "\n" + (result.stderr or "")).lower()
    indicators = (
        "no connected debug probes",
        "no connected debug probe matches unique id",
        "no available debug probes",
        "unable to open cmsis-dap device",
        "unable to find a matching cmsis-dap device",
    )
    return any(token in details for token in indicators)


def force_nrf54l_unlock_workaround(
    pyocd_cmd: list[str], target: str, uid: str | None, safe_mode: bool = False
) -> subprocess.CompletedProcess[str]:
    if target.strip().lower() != "nrf54l":
        return subprocess.CompletedProcess(
            args=[*pyocd_cmd, "commander"], returncode=2, stdout="", stderr=""
        )

    print("pyocd nRF54L unlock workaround: forcing CTRL-AP erase/reset sequence...")

    script_lines = [
        "initdp",
        "writeap 2 0x04 1",
        "sleep 500",
        "readap 2 0x08",
        "sleep 500",
        "readap 2 0x08",
        "writeap 2 0x00 2",
        "writeap 2 0x00 0",
        "sleep 500",
        "readap 2 0x08",
        "readap 0 0x00",
        "readap 1 0x00",
    ]

    import tempfile

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as script_file:
        script_file.write("\n".join(script_lines))
        script_path = script_file.name

    try:
        cmd = append_uid(
            [*pyocd_cmd, "commander", "-N", "-O", "auto_unlock=false", "-x", script_path],
            uid,
        )
        cmd = append_pyocd_safe_options(cmd, safe_mode)
        result = run(cmd, timeout=pyocd_timeout_seconds())
        print_result(result)
        return result
    finally:
        try:
            os.unlink(script_path)
        except OSError:
            pass


def print_linux_probe_permission_hint(
    result: subprocess.CompletedProcess[str], host_tools_path: Path | None = None
) -> None:
    if not looks_like_no_probe_error(result):
        return
    if not sys.platform.startswith("linux"):
        return
    if not tool_available("lsusb"):
        return

    lsusb_result = run(["lsusb"])
    probe_id = f"{CMSIS_DAP_VENDOR_ID}:{CMSIS_DAP_PRODUCT_ID}"
    if probe_id not in (lsusb_result.stdout or "").lower():
        return

    hidraw_nodes = matching_probe_hidraw_nodes()
    if not hidraw_nodes:
        return
    if probe_hidraw_nodes_accessible(hidraw_nodes):
        return

    print(
        "HINT: CMSIS-DAP probe 2886:0066 is present but hidraw access is denied. "
        "Access must be granted on /dev/hidraw*, not only on /dev/ttyACM*.",
        file=sys.stderr,
    )
    print(
        f"HINT: {host_setup_hint(host_tools_path, purpose='udev')}",
        file=sys.stderr,
    )


def preflight_linux_probe_access(
    port: str | None, host_tools_path: Path | None = None
) -> bool:
    if not sys.platform.startswith("linux"):
        return False

    hidraw_nodes = matching_probe_hidraw_nodes()
    if hidraw_nodes:
        if probe_hidraw_nodes_accessible(hidraw_nodes):
            return False
        print(
            "ERROR: CMSIS-DAP probe 2886:0066 is present but hidraw access is denied.",
            file=sys.stderr,
        )
        print(
            "HINT: Access must be granted on /dev/hidraw*, not only on /dev/ttyACM*.",
            file=sys.stderr,
        )
        print(f"HINT: {host_setup_hint(host_tools_path, purpose='udev')}", file=sys.stderr)
        print("HINT: Replug the board after installing the rule.", file=sys.stderr)
        return True

    if port_looks_like_probe_serial(port):
        print(
            "ERROR: The Arduino serial port is present, but the CMSIS-DAP hidraw "
            "interface is missing.",
            file=sys.stderr,
        )
        print(
            "HINT: On Linux VMs, pass through the whole USB device, not only the "
            "ttyACM interface.",
            file=sys.stderr,
        )
        print("HINT: Replug the board after the VM USB filter is active.", file=sys.stderr)
        return True

    return False


def append_uid(cmd: list[str], uid: str | None) -> list[str]:
    if uid:
        cmd.extend(["-u", uid])
    return cmd


def append_connect_mode(cmd: list[str], connect_mode: str | None) -> list[str]:
    if connect_mode:
        cmd.extend(["-M", connect_mode])
    return cmd


def append_pyocd_safe_options(cmd: list[str], safe_mode: bool) -> list[str]:
    if safe_mode:
        cmd.extend(
            [
                "-f",
                "1000000",
                "-O",
                "cmsis_dap.limit_packets=true",
                "-O",
                "cmsis_dap.deferred_transfers=false",
            ]
        )
    return cmd


def pyocd_timeout_seconds() -> float:
    value = os.environ.get("NRF54L15_PYOCD_TIMEOUT", "").strip()
    if not value:
        return DEFAULT_PYOCD_TIMEOUT_SECONDS
    try:
        return max(10.0, float(value))
    except ValueError:
        return DEFAULT_PYOCD_TIMEOUT_SECONDS


def pyocd_install_timeout_seconds() -> float:
    value = os.environ.get("NRF54L15_PYOCD_INSTALL_TIMEOUT", "").strip()
    if not value:
        return DEFAULT_PYOCD_INSTALL_TIMEOUT_SECONDS
    try:
        return max(30.0, float(value))
    except ValueError:
        return DEFAULT_PYOCD_INSTALL_TIMEOUT_SECONDS


def retry_connect_mode(target: str, attempt: int, safe_mode: bool = False) -> str | None:
    if target.strip().lower() == "nrf54l":
        if safe_mode:
            return None
        if attempt <= 1:
            return None
        if attempt == 2:
            return "halt"
        return "under-reset"
    if attempt <= 1:
        return None
    return "halt"


def maybe_wait_before_retry(attempt: int, retries: int, retry_delay: float) -> None:
    if attempt >= retries:
        return
    delay = max(0.0, retry_delay)
    print(
        f"Attempt {attempt}/{retries} failed; retrying in {delay:.1f}s...",
        file=sys.stderr,
    )
    time.sleep(delay)


def flash_hex(
    pyocd_cmd: list[str], target: str, uid: str | None, hex_path: str,
    *, auto_unlock: bool = True, connect_mode: str | None = None, safe_mode: bool = False
) -> subprocess.CompletedProcess[str]:
    cmd = append_uid([*pyocd_cmd, "load", "-W", "-t", target], uid)
    cmd = append_connect_mode(cmd, connect_mode)
    cmd = append_pyocd_safe_options(cmd, safe_mode)
    if not auto_unlock:
        cmd.extend(["-O", "auto_unlock=false"])
    cmd.extend([hex_path, "--format", "hex", "--no-reset"])
    result = run(cmd, timeout=pyocd_timeout_seconds())
    print_result(result)
    return result


def recover_target(
    pyocd_cmd: list[str], target: str, uid: str | None, *,
    connect_mode: str | None = None, safe_mode: bool = False
) -> subprocess.CompletedProcess[str]:
    print("Detected protected target; attempting chip erase and retry...")
    cmd = append_uid([*pyocd_cmd, "erase", "-W", "--chip", "-t", target], uid)
    cmd = append_connect_mode(cmd, connect_mode)
    cmd = append_pyocd_safe_options(cmd, safe_mode)
    result = run(cmd, timeout=pyocd_timeout_seconds())
    print_result(result)
    return result


def choose_runner(requested: str, openocd_bin: str, host_tools_path: Path | None) -> str:
    normalized = requested.strip().lower()
    if normalized != "auto":
        return normalized

    return choose_recovery_runner(openocd_bin, host_tools_path)


def choose_recovery_runner(openocd_bin: str, host_tools_path: Path | None) -> str:
    if detect_pyocd_command(host_tools_path) is not None or host_tools_path is not None:
        return "pyocd"
    if resolve_tool(openocd_bin):
        return "openocd"

    raise RuntimeError("No supported uploader found (need pyocd or openocd in PATH)")


def upload_pyocd(
    hex_path: str,
    target: str,
    requested_uid: str | None,
    retries: int,
    retry_delay: float,
    allow_uid_fallback: bool = False,
    pyocd_cmd: list[str] | None = None,
    host_tools_path: Path | None = None,
    safe_mode: bool = False,
) -> int:
    pyocd_cmd = pyocd_cmd if pyocd_cmd is not None else detect_pyocd_command(host_tools_path)
    if pyocd_cmd is None:
        print("ERROR: pyocd is not installed or not available in PATH", file=sys.stderr)
        print(f"HINT: {host_setup_hint(host_tools_path, purpose='python')}", file=sys.stderr)
        return 3

    uid = normalize_uid(requested_uid)

    print(f"Flashing {hex_path}")
    print(f"Runner: pyocd")
    print(f"Probe UID: {uid or 'auto-select'}")
    print(f"Retries: {retries}")
    if safe_mode:
        print("pyOCD safe transport: enabled")

    load_result = subprocess.CompletedProcess(args=[*pyocd_cmd, "load"], returncode=1)
    retries = max(1, retries)
    last_connect_mode: str | None = None

    for attempt in range(1, retries + 1):
        connect_mode = retry_connect_mode(target, attempt, safe_mode=safe_mode)
        last_connect_mode = connect_mode
        print(
            f"Upload attempt {attempt}/{retries}"
            + (
                f" (pyocd, connect={connect_mode})"
                if connect_mode
                else " (pyocd)"
            )
        )

        load_result = flash_hex(
            pyocd_cmd,
            target,
            uid,
            hex_path,
            connect_mode=connect_mode,
            safe_mode=safe_mode,
        )
        if load_result.returncode != 0 and looks_like_locked_target(load_result):
            erase_result = recover_target(
                pyocd_cmd,
                target,
                uid,
                connect_mode=connect_mode,
                safe_mode=safe_mode,
            )
            if erase_result.returncode == 0:
                load_result = flash_hex(
                    pyocd_cmd,
                    target,
                    uid,
                    hex_path,
                    connect_mode=connect_mode,
                    safe_mode=safe_mode,
                )
            elif looks_like_nrf54l_mass_erase_timeout(erase_result):
                workaround_result = force_nrf54l_unlock_workaround(
                    pyocd_cmd,
                    target,
                    uid,
                    safe_mode=safe_mode,
                )
                if workaround_result.returncode == 0:
                    load_result = flash_hex(
                        pyocd_cmd,
                        target,
                        uid,
                        hex_path,
                        auto_unlock=False,
                        connect_mode=connect_mode,
                        safe_mode=safe_mode,
                    )

        if (
            load_result.returncode != 0
            and allow_uid_fallback
            and uid is not None
            and looks_like_no_probe_error(load_result)
        ):
            print(
                f"Inferred probe UID '{uid}' did not match an accessible debug probe; "
                "retrying with auto-select...",
                file=sys.stderr,
            )
            uid = None
            allow_uid_fallback = False
            load_result = flash_hex(
                pyocd_cmd,
                target,
                uid,
                hex_path,
                connect_mode=connect_mode,
                safe_mode=safe_mode,
            )

        if load_result.returncode == 0:
            break
        maybe_wait_before_retry(attempt, retries, retry_delay)

    if load_result.returncode != 0:
        print_linux_probe_permission_hint(load_result, host_tools_path)
        return load_result.returncode

    reset_cmd = append_uid([*pyocd_cmd, "reset", "-W", "-t", target], uid)
    reset_cmd = append_connect_mode(reset_cmd, last_connect_mode)
    reset_cmd = append_pyocd_safe_options(reset_cmd, safe_mode)
    if target.strip().lower() == "nrf54l":
        reset_cmd.extend(["-m", "sysresetreq"])
        reset_cmd.extend(["-O", "auto_unlock=false"])
    reset_result = run(reset_cmd, timeout=pyocd_timeout_seconds())
    print_result(reset_result)
    return 0


def upload_openocd(
    hex_path: str,
    openocd_script: str,
    openocd_speed: int,
    openocd_bin: str,
    retries: int,
    retry_delay: float,
    host_tools_path: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    openocd_exe = resolve_tool(openocd_bin)
    if not openocd_exe:
        print(f"ERROR: openocd binary not found: {openocd_bin}", file=sys.stderr)
        return subprocess.CompletedProcess(args=[openocd_bin], returncode=4, stdout="", stderr="")

    if not os.path.isfile(openocd_script):
        print(f"ERROR: OpenOCD config not found: {openocd_script}", file=sys.stderr)
        return subprocess.CompletedProcess(args=[openocd_script], returncode=2, stdout="", stderr="")

    print(f"Flashing {hex_path}")
    print("Runner: openocd")
    print(f"Retries: {retries}")

    retries = max(1, retries)
    result = subprocess.CompletedProcess(args=[openocd_exe], returncode=1, stdout="", stderr="")
    for attempt in range(1, retries + 1):
        print(f"Upload attempt {attempt}/{retries} (openocd)")
        cmd = [
            openocd_exe,
            "-f",
            openocd_script,
            "-c",
            f"adapter speed {openocd_speed}",
            "-c",
            f'program "{hex_path}" verify reset exit',
        ]
        result = run(cmd)
        print_result(result)
        if result.returncode == 0:
            return result
        maybe_wait_before_retry(attempt, retries, retry_delay)

    if result.returncode != 0:
        print_linux_probe_permission_hint(result, host_tools_path)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hex", required=True, help="Path to firmware hex file")
    parser.add_argument("--uf2", default="", help="Path to firmware UF2 file")
    parser.add_argument("--port", default="", help="Arduino serial port")
    parser.add_argument("--target", default="nrf54l", help="pyOCD target name")
    parser.add_argument(
        "--uid",
        nargs="?",
        default=None,
        const="",
        help="Optional pyOCD probe UID",
    )
    parser.add_argument(
        "--runner",
        default="auto",
        help="Upload runner: uf2, auto, pyocd, openocd",
    )
    parser.add_argument(
        "--openocd-script",
        default="",
        help="OpenOCD target config script path",
    )
    parser.add_argument(
        "--openocd-speed",
        type=int,
        default=4000,
        help="OpenOCD adapter speed in kHz",
    )
    parser.add_argument(
        "--openocd-bin",
        default="openocd",
        help="OpenOCD executable path or command name",
    )
    parser.add_argument(
        "--host-tools-path",
        default="",
        help="Optional bundled host-tools package path",
    )
    parser.add_argument(
        "--uf2-drive",
        default="",
        help="Optional mounted UF2 bootloader drive path",
    )
    parser.add_argument(
        "--uf2-labels",
        default=",".join(DEFAULT_UF2_LABELS),
        help="Comma-separated UF2 bootloader volume labels to auto-detect",
    )
    parser.add_argument(
        "--uf2-timeout",
        type=float,
        default=12.0,
        help="Seconds to wait for a UF2 bootloader drive to appear",
    )
    parser.add_argument(
        "--pyocd-safe",
        default="auto",
        help="pyOCD transport mode: auto, true, or false",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=4,
        help="Number of upload attempts before failing (default: 4)",
    )
    parser.add_argument(
        "--retry-delay",
        type=float,
        default=0.6,
        help="Delay in seconds between upload attempts (default: 0.6)",
    )
    args = parser.parse_args()
    requested_runner = args.runner.strip().lower()
    host_tools_path = normalize_tools_path(args.host_tools_path)
    explicit_uid = normalize_uid(args.uid)
    inferred_uid = infer_uid_from_port(args.port) if explicit_uid is None else None
    selected_uid = explicit_uid if explicit_uid is not None else inferred_uid
    allow_inferred_uid_fallback = explicit_uid is None and inferred_uid is not None
    uf2_path = derived_uf2_path(args.hex, args.uf2)
    uf2_labels = split_csv(args.uf2_labels) or list(DEFAULT_UF2_LABELS)
    pyocd_safe_mode = resolve_pyocd_safe_mode(args.pyocd_safe)

    if not os.path.isfile(args.hex):
        print(f"ERROR: HEX file not found: {args.hex}", file=sys.stderr)
        return 2

    try:
        runner = choose_runner(requested_runner, args.openocd_bin, host_tools_path)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 4

    rc = 1
    if runner == "uf2":
        rc = upload_uf2(
            uf2_path,
            args.uf2_drive,
            uf2_labels,
            args.uf2_timeout,
        )
    if runner == "pyocd":
        if preflight_linux_probe_access(args.port, host_tools_path):
            return 7
        if detect_pyocd_command(host_tools_path) is None:
            if install_pyocd(host_tools_path):
                print("pyocd installation succeeded.")
            else:
                print("pyocd installation failed.", file=sys.stderr)
                print(f"HINT: {host_setup_hint(host_tools_path, purpose='python')}", file=sys.stderr)
        rc = upload_pyocd(
            args.hex,
            args.target,
            selected_uid,
            allow_uid_fallback=allow_inferred_uid_fallback,
            retries=args.retries,
            retry_delay=args.retry_delay,
            host_tools_path=host_tools_path,
            safe_mode=pyocd_safe_mode,
        )
        if rc != 0 and requested_runner == "auto":
            print("pyocd upload failed in auto mode; trying openocd...")
            rc = upload_openocd(
                args.hex,
                args.openocd_script,
                args.openocd_speed,
                args.openocd_bin,
                retries=args.retries,
                retry_delay=args.retry_delay,
                host_tools_path=host_tools_path,
            ).returncode
    elif runner == "openocd":
        if preflight_linux_probe_access(args.port, host_tools_path):
            return 7
        openocd_result = upload_openocd(
            args.hex,
            args.openocd_script,
            args.openocd_speed,
            args.openocd_bin,
            retries=args.retries,
            retry_delay=args.retry_delay,
            host_tools_path=host_tools_path,
        )
        rc = openocd_result.returncode

        if rc != 0 and looks_like_locked_target(openocd_result):
            pyocd_cmd = detect_pyocd_command(host_tools_path)
            if pyocd_cmd is None and requested_runner == "auto":
                if install_pyocd(host_tools_path):
                    pyocd_cmd = detect_pyocd_command(host_tools_path)

            if pyocd_cmd is not None:
                print("OpenOCD indicates protected target; attempting pyocd recover/flash...")
                rc = upload_pyocd(
                    args.hex,
                    args.target,
                    selected_uid,
                    allow_uid_fallback=allow_inferred_uid_fallback,
                    retries=args.retries,
                    retry_delay=args.retry_delay,
                    pyocd_cmd=pyocd_cmd,
                    host_tools_path=host_tools_path,
                    safe_mode=pyocd_safe_mode,
                )
            elif requested_runner == "auto":
                print(
                    "ERROR: Target appears protected and OpenOCD cannot recover it. "
                    "Install pyocd and retry (or select pyOCD upload method).",
                    file=sys.stderr,
                )
                print(f"HINT: {host_setup_hint(host_tools_path, purpose='python')}", file=sys.stderr)
        elif rc != 0 and detect_pyocd_command(host_tools_path) is not None:
            print("OpenOCD upload failed; falling back to pyocd...")
            rc = upload_pyocd(
                args.hex,
                args.target,
                selected_uid,
                allow_uid_fallback=allow_inferred_uid_fallback,
                retries=args.retries,
                retry_delay=args.retry_delay,
                host_tools_path=host_tools_path,
                safe_mode=pyocd_safe_mode,
            )
    elif runner != "uf2":
        print(f"ERROR: Unsupported runner: {runner}", file=sys.stderr)
        return 4

    if rc != 0:
        return rc

    print("Upload complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
