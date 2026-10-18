#!/usr/bin/env python3
"""Arduino upload helper for XIAO nRF54L15 Zephyr-based core.

This wrapper keeps Arduino upload integration cross-platform while relying on
pyOCD for CMSIS-DAP flashing and explicitly selected experimental J-Link
flashing.
"""

import argparse
import json
import os
import platform
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Optional, List, Dict, Tuple
from xml.parsers.expat import ExpatError

CMSIS_DAP_VENDOR_ID = "2886"
CMSIS_DAP_PRODUCT_IDS = ("0066", "0068")  # L15=0066, LM20A=0068

def _is_cmsis_dap_product(pid_str):
    return pid_str in CMSIS_DAP_PRODUCT_IDS
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
OPEN_NRF_OCD_RELEASE = "v0.3.8"
OPEN_NRF_OCD_RELEASE_BASE_URL = (
    "https://github.com/lolren/open-nrf-ocd/releases/download"
)
NRF_OCD_TRANSPORT_FALLBACK = -2

def run(cmd: List[str], timeout: Optional[float] = None) -> subprocess.CompletedProcess:
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

def print_result(result: subprocess.CompletedProcess) -> None:
    if result.stdout:
        print(result.stdout, end="")
    if result.returncode != 0 and result.stderr:
        print(result.stderr, file=sys.stderr, end="")

def looks_like_nrf_ocd_transport_error(output: str) -> bool:
    text = output.lower()
    return (
        "bulk write failed" in text
        or "bulk read failed" in text
        or "bulk retries exhausted" in text
        or "failed to halt target: bus fault" in text
        or "failed to re-halt after erase" in text
        or "target_init failed: i/o error" in text
        or "target_init failed: timeout" in text
    )

def tool_available(name: str) -> bool:
    return shutil.which(name) is not None

def normalize_tools_path(path: Optional[str]) -> Optional[Path]:
    if not path:
        return None
    if "{" in path and "}" in path:
        return None
    candidate = Path(path)
    return candidate if candidate.exists() else None

def unresolved_property(value: Optional[str]) -> bool:
    if not value:
        return True
    if "{" in value and "}" in value:
        return True
    return value.strip().lower() in {"auto", "default", "none"}

def normalize_tristate(value: Optional[str], default: str = "auto") -> str:
    if unresolved_property(value):
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on", "safe", "vm", "vm-safe"}:
        return "true"
    if normalized in {"0", "false", "no", "off", "normal"}:
        return "false"
    return default

def split_csv(value: Optional[str]) -> List[str]:
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

def resolve_pyocd_safe_mode(requested: Optional[str]) -> bool:
    normalized = normalize_tristate(requested)
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    return running_inside_virtual_machine()

def normalize_token(value: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "", value.upper())

def derived_uf2_path(hex_path: str, requested_uf2: Optional[str]) -> Path:
    if not unresolved_property(requested_uf2):
        return Path(requested_uf2)
    return Path(hex_path).with_suffix(".uf2")

def pyocd_tool_root(host_tools_path: Optional[Path]) -> Path:
    if host_tools_path is not None:
        return host_tools_path
    return Path(__file__).resolve().parent

def bundled_pyocd_site_path(tool_root: Optional[Path]) -> Optional[Path]:
    if tool_root is None:
        return None
    candidate = tool_root / "runtime" / "pyocd-site"
    return candidate if candidate.is_dir() else None

def bundled_pyocd_command(tool_root: Optional[Path]) -> Optional[List[str]]:
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

def bundled_wheelhouse_path(tool_root: Optional[Path]) -> Optional[Path]:
    if tool_root is None:
        return None
    wheelhouse_root = tool_root / "wheelhouse"
    if not wheelhouse_root.is_dir():
        return None
    version_tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
    candidate = wheelhouse_root / version_tag
    return candidate if candidate.is_dir() else None

def detect_pyocd_command(host_tools_path: Optional[Path] = None) -> Optional[List[str]]:
    # First try bundled wrapper (system pyocd 0.44.1 for AppImage IDE)
    import pathlib
    _here = pathlib.Path(__file__).resolve().parent
    _wrapper = _here / "pyocd_bundled.py"
    if _wrapper.is_file():
        wrapper_cmd = [sys.executable, str(_wrapper)]
        try:
            result = subprocess.run(wrapper_cmd + ["--version"], capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                return wrapper_cmd
        except Exception:
            pass

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

def host_setup_hint(host_tools_path: Optional[Path] = None, purpose: str = "python") -> str:
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

def install_pyocd(host_tools_path: Optional[Path] = None) -> bool:
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

def normalize_uid(requested_uid: Optional[str]) -> Optional[str]:
    if requested_uid is None:
        return None

    cleaned = requested_uid.strip()
    if not cleaned:
        return None
    return cleaned

def normalize_pyocd_probe_type(requested: Optional[str]) -> Optional[str]:
    if unresolved_property(requested):
        return None

    normalized = re.sub(r"[^a-z0-9]+", "", requested.strip().lower())
    aliases = {
        "cmsis": "cmsisdap",
        "cmsisdap": "cmsisdap",
        "jlink": "jlink",
    }
    probe_type = aliases.get(normalized)
    if probe_type is None:
        raise ValueError(
            f"unsupported pyOCD probe type '{requested}'; expected cmsisdap or jlink"
        )
    return probe_type

def qualify_pyocd_probe_uid(
    requested_uid: Optional[str], probe_type: Optional[str]
) -> Optional[str]:
    """Bind pyOCD selection to one probe plug-in without changing raw IDs."""
    probe_type = normalize_pyocd_probe_type(probe_type)
    uid = normalize_uid(requested_uid)

    if uid is not None and ":" in uid:
        prefix, suffix = uid.split(":", 1)
        normalized_prefix = normalize_pyocd_probe_type(prefix)
        if probe_type is not None and normalized_prefix != probe_type:
            raise ValueError(
                f"probe UID '{uid}' conflicts with selected {probe_type} transport"
            )
        probe_type = normalized_prefix
        uid = normalize_uid(suffix)

    # The SEGGER API and pyOCD represent J-Link serial numbers without the
    # leading zeroes sometimes exposed by the associated VCOM USB descriptor.
    if probe_type == "jlink" and uid is not None and uid.isdecimal():
        uid = uid.lstrip("0") or "0"

    if probe_type is None:
        return uid
    return f"{probe_type}:{uid or ''}"

def bundled_import_path(host_tools_path: Optional[Path]) -> Optional[Path]:
    site_dir = bundled_pyocd_site_path(pyocd_tool_root(host_tools_path))
    return site_dir if site_dir is not None and site_dir.is_dir() else None

def import_serial_list_ports(host_tools_path: Optional[Path] = None):
    try:
        from serial.tools import list_ports

        return list_ports
    except ModuleNotFoundError:
        pass

    site_dir = bundled_import_path(host_tools_path)
    if site_dir is None:
        return None

    site_dir_str = str(site_dir)
    if site_dir_str not in sys.path:
        sys.path.insert(0, site_dir_str)

    try:
        from serial.tools import list_ports

        return list_ports
    except ModuleNotFoundError:
        return None

def normalized_port_name(port: Optional[str]) -> str:
    if not port:
        return ""
    cleaned = port.strip()
    if cleaned.startswith("\\\\.\\"):
        cleaned = cleaned[4:]
    return cleaned.casefold()

def serial_from_hwid(hwid: Optional[str]) -> Optional[str]:
    if not hwid:
        return None

    match = re.search(r"\bSER=([A-Za-z0-9._-]+)\b", hwid)
    if not match:
        return None
    return normalize_uid(match.group(1))

def port_uid_from_wmic(port: Optional[str]) -> Optional[str]:
    """Windows-only: resolve COM port -> USB serial via wmic."""
    if not port or not sys.platform.startswith("win"):
        return None
    import subprocess
    try:
        result = subprocess.run(
            ["wmic", "path", "Win32_PnPEntity", "get", "DeviceID,PNPClass,Name",
             "/format:csv"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode != 0:
            return None
        wanted = normalized_port_name(port)
        for line in result.stdout.splitlines():
            if "COM" not in line or "USB" not in line:
                continue
            if normalized_port_name(line.split(",")[-1] if "," in line else "") == wanted:
                continue
            # Extract serial from DeviceID: USB\VID_2886&PID_0068\3377B9D6
            parts = line.split(",")
            for p in parts:
                m = re.search(r"([A-F0-9]{8})$", p.strip())
                if m:
                    return m.group(1)
    except Exception:
        pass
    return None

def port_uid_from_list_ports(port: Optional[str], host_tools_path: Optional[Path] = None) -> Optional[str]:
    if not port:
        return None

    list_ports = import_serial_list_ports(host_tools_path)
    if list_ports is None:
        return None

    wanted = normalized_port_name(port)
    for info in list_ports.comports():
        devices = [
            normalized_port_name(getattr(info, "device", None)),
            normalized_port_name(getattr(info, "name", None)),
        ]
        if wanted not in devices:
            continue

        serial_value = normalize_uid(getattr(info, "serial_number", None))
        if serial_value is not None:
            return serial_value

        serial_value = serial_from_hwid(getattr(info, "hwid", None))
        if serial_value is not None:
            return serial_value

    return None

def port_uid_from_macos_registry(port: Optional[str]) -> Optional[str]:
    """Resolve a selected serial device without requiring pyserial in this Python."""
    if sys.platform != "darwin" or not port:
        return None

    # Keep the full provider ancestry: serial-only subtrees omit the USB UID.
    try:
        registry = run(
            ["/usr/sbin/ioreg", "-a", "-l", "-i", "-p", "IOService"],
            timeout=10.0,
        )
        if registry.returncode != 0:
            return None
        tree = plistlib.loads((registry.stdout or "").encode("utf-8"))
    except (OSError, UnicodeError, ValueError, plistlib.InvalidFileException, ExpatError):
        return None

    roots = tree if isinstance(tree, list) else [tree]
    pending = [(node, None) for node in roots]
    wanted = port.strip()
    matches = []
    usb_classes = {"IOUSBHostDevice", "IOUSBDevice"}
    while pending:
        node, usb_serial = pending.pop()
        if not isinstance(node, dict):
            continue

        node_class = node.get("IOObjectClass")
        inheritance = node.get("IOObjectInheritance", [])
        is_usb_device = isinstance(node_class, str) and node_class in usb_classes
        if isinstance(inheritance, list):
            is_usb_device = is_usb_device or any(
                isinstance(name, str) and name in usb_classes for name in inheritance
            )
        if is_usb_device:
            # A child USB device must never inherit a parent hub's identity.
            serial = node.get("USB Serial Number")
            usb_serial = normalize_uid(serial) if isinstance(serial, str) else None

        if wanted in (node.get("IOCalloutDevice"), node.get("IODialinDevice")):
            matches.append(usb_serial)

        children = node.get("IORegistryEntryChildren", [])
        if isinstance(children, list):
            pending.extend((child, usb_serial) for child in children)

    return matches[0] if len(matches) == 1 else None

def matching_cmsis_dap_serial_ports(host_tools_path: Optional[Path] = None) -> List[str]:
    list_ports = import_serial_list_ports(host_tools_path)
    if list_ports is None:
        return []

    matches: List[str] = []
    vid = int(CMSIS_DAP_VENDOR_ID, 16)
    for info in list_ports.comports():
        info_vid = getattr(info, "vid", None)
        info_pid = getattr(info, "pid", None)
        if info_vid == vid and _is_cmsis_dap_product(f"{info_pid:04x}"):
            device = getattr(info, "device", None)
            if device:
                matches.append(str(device))
            continue

        hwid = (getattr(info, "hwid", None) or "").lower()
        if any(f"vid:pid={CMSIS_DAP_VENDOR_ID.lower()}:{pid}" in hwid for pid in CMSIS_DAP_PRODUCT_IDS):
            device = getattr(info, "device", None)
            if device:
                matches.append(str(device))

    return matches

def infer_uid_from_port(port: Optional[str], host_tools_path: Optional[Path] = None) -> Optional[str]:
    if not port:
        return None
    # If port is an 8-char hex UID, return directly
    if len(port) == 8 and all(c in '0123456789ABCDEF' for c in port.upper()):
        return port
    # Try to resolve via serial port lookup (works on all platforms)
    inferred = port_uid_from_list_ports(port, host_tools_path)
    if inferred is not None:
        return inferred

    if sys.platform == "darwin":
        return port_uid_from_macos_registry(port)

    # Windows fallback: parse wmic output
    if sys.platform.startswith("win"):
        inferred = port_uid_from_wmic(port)
        if inferred is not None:
            return inferred
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

def explicit_uf2_drive(path: Optional[str]) -> Optional[Path]:
    if unresolved_property(path):
        env_path = os.environ.get("NRF54L15_UF2_DRIVE", "")
        if unresolved_property(env_path):
            return None
        path = env_path
    candidate = Path(path).expanduser()
    return candidate if candidate.is_dir() else None

def mounted_volume_candidates() -> List[Path]:
    candidates: List[Path] = []

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

def looks_like_uf2_drive(path: Path, labels: List[str]) -> bool:
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

def find_uf2_drives(drive: Optional[str], labels: List[str]) -> List[Path]:
    if not unresolved_property(drive):
        candidate = Path(drive).expanduser()
        return [candidate] if candidate.is_dir() else []

    explicit = explicit_uf2_drive(None)
    if explicit is not None:
        return [explicit]
    return [path for path in mounted_volume_candidates() if looks_like_uf2_drive(path, labels)]

def print_uf2_bootloader_hint(labels: List[str]) -> None:
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
    drive: Optional[str],
    labels: List[str],
    timeout: float,
    *,
    quiet_missing: bool = False,
) -> int:
    if not uf2_path.is_file():
        print(f"ERROR: UF2 file not found: {uf2_path}", file=sys.stderr)
        return 2

    deadline = time.monotonic() + max(0.0, timeout)
    drives: List[Path] = []
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
) -> Tuple[Optional[str], Optional[str]]:
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

def _sysfs_usb_identity_for_hidraw(node: Path) -> Tuple[Optional[str], Optional[str]]:
    return _sysfs_usb_identity_for_class_node("hidraw", node.name)

def _sysfs_usb_identity_for_tty(node: Path) -> Tuple[Optional[str], Optional[str]]:
    return _sysfs_usb_identity_for_class_node("tty", node.name)

def matching_probe_hidraw_nodes() -> List[Path]:
    if not sys.platform.startswith("linux"):
        return []

    matches: List[Path] = []
    for node in sorted(Path("/dev").glob("hidraw*")):
        vendor, product = _sysfs_usb_identity_for_hidraw(node)
        if vendor == CMSIS_DAP_VENDOR_ID and f"{int(product, 16):04x}" in CMSIS_DAP_PRODUCT_IDS:
            matches.append(node)
    return matches

def probe_hidraw_nodes_accessible(nodes: List[Path]) -> bool:
    return any(os.access(node, os.R_OK | os.W_OK) for node in nodes)

def probe_usb_ids_for_hidraw_nodes(nodes: List[Path]) -> List[str]:
    identities = set()
    for node in nodes:
        vendor, product = _sysfs_usb_identity_for_hidraw(node)
        if vendor == CMSIS_DAP_VENDOR_ID and product in CMSIS_DAP_PRODUCT_IDS:
            identities.add(f"{vendor}:{product}")
    return sorted(identities)

def cmsis_dap_usb_ids_in_lsusb(output: str) -> List[str]:
    normalized = output.lower()
    return [
        f"{CMSIS_DAP_VENDOR_ID}:{product}"
        for product in CMSIS_DAP_PRODUCT_IDS
        if f"{CMSIS_DAP_VENDOR_ID}:{product}" in normalized
    ]

def format_probe_usb_ids(identities: List[str]) -> str:
    if identities:
        return " and ".join(identities)
    return "2886:0066 or 2886:0068"

def port_looks_like_probe_serial(port: Optional[str]) -> bool:
    if not port:
        return False
    # Probe unique ID: 8 hex chars (e.g., "3377B9D6")
    if len(port) == 8 and all(c in '0123456789ABCDEF' for c in port.upper()):
        return True
    if not sys.platform.startswith("linux"):
        return False
    port_name = Path(port).name
    if not port_name.startswith("tty"):
        return False
    vendor, product = _sysfs_usb_identity_for_tty(Path(port_name))
    return vendor == CMSIS_DAP_VENDOR_ID and f"{int(product, 16):04x}" in CMSIS_DAP_PRODUCT_IDS

def looks_like_locked_target(result: subprocess.CompletedProcess) -> bool:
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

def looks_like_nrf54l_mass_erase_timeout(result: subprocess.CompletedProcess) -> bool:
    details = ((result.stdout or "") + "\n" + (result.stderr or "")).lower()
    indicators = (
        "mass erase timeout waiting for eraseallstatus",
        "no cores were discovered",
    )
    return any(token in details for token in indicators)

def looks_like_no_probe_error(result: subprocess.CompletedProcess) -> bool:
    details = ((result.stdout or "") + "\n" + (result.stderr or "")).lower()
    indicators = (
        "no connected debug probes",
        "no connected debug probe matches unique id",
        "no available debug probes",
        "unable to open cmsis-dap device",
        "unable to find a matching cmsis-dap device",
        "unable to open jlink dll",
        "could not find jlink probe",
        "failed to open dll",
    )
    return any(token in details for token in indicators)

def force_nrf54l_unlock_workaround(
    pyocd_cmd: List[str], target: str, uid: Optional[str], safe_mode: bool = False,
    probe_type: Optional[str] = None,
) -> subprocess.CompletedProcess:
    if target.strip().lower() not in ("nrf54l", "nrf54lm20a"):
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
        cmd = [*pyocd_cmd, "commander"]
        cmd = append_pyocd_target_script(cmd, target)
        cmd.extend(["-N", "-O", "auto_unlock=false", "-x", script_path])
        cmd = append_uid(cmd, uid)
        cmd = append_pyocd_safe_options(cmd, safe_mode)
        cmd = append_pyocd_probe_options(cmd, probe_type)
        result = run(cmd, timeout=pyocd_timeout_seconds())
        print_result(result)
        return result
    finally:
        try:
            os.unlink(script_path)
        except OSError:
            pass

def print_linux_probe_permission_hint(
    result: subprocess.CompletedProcess, host_tools_path: Optional[Path] = None
) -> None:
    if not looks_like_no_probe_error(result):
        return
    if not sys.platform.startswith("linux"):
        return
    if not tool_available("lsusb"):
        return

    lsusb_result = run(["lsusb"])
    probe_ids = cmsis_dap_usb_ids_in_lsusb(lsusb_result.stdout or "")
    if not probe_ids:
        return

    hidraw_nodes = matching_probe_hidraw_nodes()
    if not hidraw_nodes:
        return
    if probe_hidraw_nodes_accessible(hidraw_nodes):
        return

    print(
        f"HINT: CMSIS-DAP probe {format_probe_usb_ids(probe_ids)} is present but "
        "hidraw access is denied. "
        "Access must be granted on /dev/hidraw*, not only on /dev/ttyACM*.",
        file=sys.stderr,
    )
    print(
        f"HINT: {host_setup_hint(host_tools_path, purpose='udev')}",
        file=sys.stderr,
    )

def print_jlink_probe_hint(result: subprocess.CompletedProcess) -> None:
    if not looks_like_no_probe_error(result):
        return
    print(
        "HINT: Install the official SEGGER J-Link Software and Documentation Pack, "
        "reconnect the probe, and confirm that `pyocd list --probes` shows it.",
        file=sys.stderr,
    )
    print(
        "HINT: J-Link upload is experimental and has not yet been validated on "
        "maintainer hardware.",
        file=sys.stderr,
    )

def discover_jlink_probe_uids(pyocd_cmd: List[str]) -> List[str]:
    """Return J-Link serials from pyOCD's machine-readable probe inventory."""
    inventory = run([*pyocd_cmd, "json", "--probes"], timeout=15.0)
    if inventory.returncode != 0:
        return []
    try:
        payload = json.loads(inventory.stdout or "{}")
    except (TypeError, ValueError):
        return []
    if payload.get("status") != 0:
        return []

    discovered: List[str] = []
    for probe in payload.get("boards", []):
        vendor = str(probe.get("vendor_name") or "").strip().casefold()
        product = str(probe.get("product_name") or probe.get("info") or "")
        if vendor != "segger" or "j-link" not in product.casefold():
            continue
        uid = normalize_uid(str(probe.get("unique_id") or ""))
        if uid is not None and uid not in discovered:
            discovered.append(uid)
    return discovered

def select_jlink_probe_uid(pyocd_cmd: List[str]) -> Tuple[Optional[str], bool]:
    """Select one J-Link without allowing pyOCD's interactive probe picker."""
    probes = discover_jlink_probe_uids(pyocd_cmd)
    if len(probes) <= 1:
        return (probes[0] if probes else None), False

    print("ERROR: Multiple J-Link probes are connected:", file=sys.stderr)
    for uid in probes:
        print(f"  {uid}", file=sys.stderr)
    print(
        "HINT: Set NRF54L15_JLINK_UID to the intended J-Link serial number and retry.",
        file=sys.stderr,
    )
    return None, True

def report_missing_jlink_probe() -> None:
    print("ERROR: No J-Link probe was found by pyOCD.", file=sys.stderr)
    result = subprocess.CompletedProcess(
        ["pyocd", "json", "--probes"],
        1,
        "",
        "No connected debug probe matches unique ID 'jlink:'",
    )
    print_jlink_probe_hint(result)

def preflight_linux_probe_access(
    port: Optional[str], host_tools_path: Optional[Path] = None
) -> bool:
    if not sys.platform.startswith("linux"):
        return False

    hidraw_nodes = matching_probe_hidraw_nodes()
    if hidraw_nodes:
        if probe_hidraw_nodes_accessible(hidraw_nodes):
            return False
        probe_ids = probe_usb_ids_for_hidraw_nodes(hidraw_nodes)
        print(
            f"ERROR: CMSIS-DAP probe {format_probe_usb_ids(probe_ids)} is present "
            "but hidraw access is denied.",
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

def append_uid(cmd: List[str], uid: Optional[str]) -> List[str]:
    if uid:
        cmd.extend(["-u", uid])
    return cmd

def append_pyocd_probe_options(
    cmd: List[str], probe_type: Optional[str]
) -> List[str]:
    if normalize_pyocd_probe_type(probe_type) == "jlink":
        # Do not let pyOCD turn the J-Link target supply on and then off. The
        # board or user-provided target supply remains authoritative.
        cmd.extend(
            [
                "-O",
                "jlink.power=false",
                "-O",
                "jlink.non_interactive=true",
            ]
        )
    return cmd

def append_connect_mode(cmd: List[str], connect_mode: Optional[str]) -> List[str]:
    if connect_mode:
        cmd.extend(["-M", connect_mode])
    return cmd

def append_pyocd_target_script(cmd: List[str], target: str) -> List[str]:
    if target.strip().lower() != "nrf54lm20a":
        return cmd

    script = Path(__file__).resolve().parent / "pyocd_register_lm20b.py"
    if script.is_file():
        cmd.extend(["--script", str(script)])
    return cmd

def append_pyocd_safe_options(cmd: List[str], safe_mode: bool) -> List[str]:
    if safe_mode:
        cmd.extend(
            [
                "-f",
                "1000000",
                "-O",
                "cmsis_dap.prefer_v1=true",
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

def retry_connect_mode(target: str, attempt: int, safe_mode: bool = False) -> Optional[str]:
    if target.strip().lower() in ("nrf54l", "nrf54lm20a"):
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
    pyocd_cmd: List[str], target: str, uid: Optional[str], hex_path: str,
    *, auto_unlock: bool = True, connect_mode: Optional[str] = None,
    safe_mode: bool = False, probe_type: Optional[str] = None
) -> subprocess.CompletedProcess:
    cmd = [*pyocd_cmd, "load"]
    cmd = append_pyocd_target_script(cmd, target)
    cmd.extend(["-W", "-t", target])
    cmd = append_uid(cmd, uid)
    cmd = append_connect_mode(cmd, connect_mode)
    cmd = append_pyocd_safe_options(cmd, safe_mode)
    cmd = append_pyocd_probe_options(cmd, probe_type)
    if not auto_unlock:
        cmd.extend(["-O", "auto_unlock=false"])
    cmd.extend([hex_path, "--format", "hex"])
    # Existing CMSIS-DAP recovery performs a separate, transport-aware reset.
    # The J-Link path must remain inside pyOCD, so let a successful load reset
    # the target before pyOCD closes the J-Link session.
    if normalize_pyocd_probe_type(probe_type) != "jlink":
        cmd.append("--no-reset")
    result = run(cmd, timeout=pyocd_timeout_seconds())
    print_result(result)
    return result

def recover_target(
    pyocd_cmd: List[str], target: str, uid: Optional[str], *,
    connect_mode: Optional[str] = None, safe_mode: bool = False,
    probe_type: Optional[str] = None
) -> subprocess.CompletedProcess:
    print("Detected protected target; attempting chip erase and retry...")
    cmd = [*pyocd_cmd, "erase"]
    cmd = append_pyocd_target_script(cmd, target)
    cmd.extend(["-W", "--chip", "-t", target])
    cmd = append_uid(cmd, uid)
    cmd = append_connect_mode(cmd, connect_mode)
    cmd = append_pyocd_safe_options(cmd, safe_mode)
    cmd = append_pyocd_probe_options(cmd, probe_type)
    result = run(cmd, timeout=pyocd_timeout_seconds())
    print_result(result)
    return result

def recover_target_with_retries(
    pyocd_cmd: List[str], target: str, uid: Optional[str], *,
    connect_mode: Optional[str] = None, safe_mode: bool = False,
    attempts: int = 2, retry_delay: float = 1.0,
    probe_type: Optional[str] = None
) -> subprocess.CompletedProcess:
    """Retry CTRL-AP recovery because the first erase can drop the probe link."""
    attempts = max(1, attempts)
    result = subprocess.CompletedProcess(
        args=[*pyocd_cmd, "erase"], returncode=1, stdout="", stderr=""
    )
    for attempt in range(1, attempts + 1):
        print(f"Protected-target recovery attempt {attempt}/{attempts}")
        result = recover_target(
            pyocd_cmd,
            target,
            uid,
            connect_mode=connect_mode,
            safe_mode=safe_mode,
            probe_type=probe_type,
        )
        if result.returncode == 0:
            break
        maybe_wait_before_retry(attempt, attempts, retry_delay)
    return result

def choose_runner(requested: str, openocd_bin: str, host_tools_path: Optional[Path]) -> str:
    normalized = requested.strip().lower()
    if normalized == "openocd":
        raise RuntimeError(
            "OpenOCD does not provide a verified nRF54 target in this package; "
            "use pyocd or nrf_ocd"
        )
    if normalized != "auto":
        return normalized

    return choose_recovery_runner(openocd_bin, host_tools_path)

def choose_recovery_runner(openocd_bin: str, host_tools_path: Optional[Path]) -> str:
    del openocd_bin
    if detect_pyocd_command(host_tools_path) is not None or host_tools_path is not None:
        return "pyocd"
    raise RuntimeError("No supported recovery uploader found (pyOCD is required)")

def upload_pyocd(
    hex_path: str,
    target: str,
    requested_uid: Optional[str],
    retries: int,
    retry_delay: float,
    allow_uid_fallback: bool = False,
    pyocd_cmd: Optional[List[str]] = None,
    host_tools_path: Optional[Path] = None,
    safe_mode: bool = False,
    port: Optional[str] = None,
    probe_type: Optional[str] = None,
) -> int:
    # Keep the legacy argument accepted, but never broaden a selected probe to
    # auto-select after an error. The selected board may have been unplugged.
    del allow_uid_fallback
    pyocd_cmd = pyocd_cmd if pyocd_cmd is not None else detect_pyocd_command(host_tools_path)
    if pyocd_cmd is None:
        print("ERROR: pyocd is not installed or not available in PATH", file=sys.stderr)
        print(f"HINT: {host_setup_hint(host_tools_path, purpose='python')}", file=sys.stderr)
        return 3

    try:
        probe_type = normalize_pyocd_probe_type(probe_type)
        raw_uid = normalize_uid(requested_uid)
        uid = qualify_pyocd_probe_uid(raw_uid, probe_type)
        if uid is not None and ":" in uid:
            raw_uid = normalize_uid(uid.split(":", 1)[1])
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 4

    if probe_type == "jlink":
        # VM safe mode contains CMSIS-DAP-only options and intentionally skips
        # reset. Neither behavior belongs on the explicitly selected J-Link
        # transport.
        safe_mode = False
        if raw_uid is None:
            discovered_uid, ambiguous = select_jlink_probe_uid(pyocd_cmd)
            if ambiguous:
                return 8
            if discovered_uid is None:
                report_missing_jlink_probe()
                return 6
            raw_uid = discovered_uid
            uid = qualify_pyocd_probe_uid(raw_uid, probe_type)

    print(f"Flashing {hex_path}")
    print(
        "Runner: pyocd"
        + (" (J-Link experimental)" if probe_type == "jlink" else "")
    )
    print(f"Probe type: {probe_type or 'auto'}")
    print(f"Probe UID: {uid or 'auto-select'}")
    print(f"Retries: {retries}")
    if safe_mode:
        print("pyOCD safe transport: enabled (CMSIS-DAP v1 HID, 1 MHz)")

    load_result = subprocess.CompletedProcess(args=[*pyocd_cmd, "load"], returncode=1)
    retries = max(1, retries)
    last_connect_mode: Optional[str] = None

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
            probe_type=probe_type,
        )
        if load_result.returncode != 0 and looks_like_locked_target(load_result):
            erase_result = recover_target_with_retries(
                pyocd_cmd,
                target,
                uid,
                connect_mode=connect_mode,
                safe_mode=safe_mode,
                retry_delay=retry_delay,
                probe_type=probe_type,
            )
            if erase_result.returncode == 0:
                load_result = flash_hex(
                    pyocd_cmd,
                    target,
                    uid,
                    hex_path,
                    connect_mode=connect_mode,
                    safe_mode=safe_mode,
                    probe_type=probe_type,
                )
            elif looks_like_nrf54l_mass_erase_timeout(erase_result):
                workaround_result = force_nrf54l_unlock_workaround(
                    pyocd_cmd,
                    target,
                    uid,
                    safe_mode=safe_mode,
                    probe_type=probe_type,
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
                        probe_type=probe_type,
                    )

        if load_result.returncode == 0:
            break
        maybe_wait_before_retry(attempt, retries, retry_delay)

    if load_result.returncode != 0:
        if probe_type == "jlink":
            print_jlink_probe_hint(load_result)
        else:
            print_linux_probe_permission_hint(load_result, host_tools_path)
        return load_result.returncode

    if probe_type == "jlink":
        print("J-Link target reset: completed by pyOCD")
        return 0

    if safe_mode:
        print(
            "pyOCD safe transport: skipping post-upload reset to avoid VM USB re-enumeration."
        )
        print("If the sketch does not start, press RESET or power-cycle the board.")
        return 0

    # Keep reset on the same UID-restricted transport. The bundled native
    # tool can ignore an unmatched UID and reset a different connected board.
    reset_cmd = [*pyocd_cmd, "reset", "-W", "-t", target]
    reset_cmd = append_pyocd_target_script(reset_cmd, target)
    reset_cmd = append_uid(reset_cmd, uid)
    reset_cmd = append_pyocd_probe_options(reset_cmd, probe_type)
    reset_cmd.extend(["-O", "auto_unlock=false"])
    reset_result = run(reset_cmd, timeout=15.0)
    print_result(reset_result)
    if reset_result.returncode != 0:
        print("ERROR: Firmware was written, but resetting the selected probe failed.", file=sys.stderr)
    return reset_result.returncode

def open_nrf_ocd_asset_for_host() -> Optional[tuple]:
    system = platform.system().lower()
    machine = platform.machine().lower()

    if system == "windows":
        if machine in {"amd64", "x86_64"}:
            return "nrf_ocd-windows-amd64.exe", "nrf_ocd.exe"
        if machine in {"x86", "i386", "i686"}:
            return "nrf_ocd-windows-i386.exe", "nrf_ocd.exe"
        return None

    if system == "linux":
        if machine in {"x86_64", "amd64"}:
            return "nrf_ocd-linux-amd64", "nrf_ocd"
        if machine in {"aarch64", "arm64"}:
            return "nrf_ocd-linux-arm64", "nrf_ocd"
        if machine.startswith("arm") or machine in {"armv7l", "armhf"}:
            return "nrf_ocd-linux-armhf", "nrf_ocd"

    return None

def open_nrf_ocd_candidate_names() -> Tuple[str, ...]:
    if sys.platform.startswith("win"):
        return (
            "nrf_ocd.exe",
            "nrf_ocd-windows-amd64.exe",
            "nrf_ocd-windows-i386.exe",
            "nrf_ocd-win64.exe",
            "nrf_ocd",
        )
    return (
        "nrf_ocd",
        "nrf_ocd-linux-amd64",
        "nrf_ocd-linux-x64",
        "nrf_ocd-linux-arm64",
        "nrf_ocd-linux-armhf",
    )

def open_nrf_ocd_cache_roots(host_tools_path: Optional[Path] = None) -> List[Path]:
    roots: List[Path] = []
    if host_tools_path is not None:
        roots.append(host_tools_path / "runtime" / "open-nrf-ocd" / OPEN_NRF_OCD_RELEASE)

    script_dir = Path(__file__).resolve().parent
    roots.append(script_dir / "runtime" / "open-nrf-ocd" / OPEN_NRF_OCD_RELEASE)
    roots.append(
        Path.home()
        / ".arduino15"
        / "packages"
        / "nrf54l15clean"
        / "tools"
        / "open-nrf-ocd"
        / OPEN_NRF_OCD_RELEASE
    )
    roots.append(Path.home() / ".cache" / "nrf54l15clean" / "open-nrf-ocd" / OPEN_NRF_OCD_RELEASE)
    return roots

def make_executable_if_needed(path: Path) -> None:
    if sys.platform.startswith("win"):
        return
    try:
        mode = path.stat().st_mode
        path.chmod(mode | 0o755)
    except OSError:
        pass

def ensure_open_nrf_ocd_release_binary(host_tools_path: Optional[Path] = None) -> Optional[List[str]]:
    asset = open_nrf_ocd_asset_for_host()
    if asset is None:
        print(
            "open-nrf-ocd fallback is not available for this host OS/CPU; "
            "use pyOCD instead.",
            file=sys.stderr,
        )
        return None

    asset_name, executable_name = asset
    url = f"{OPEN_NRF_OCD_RELEASE_BASE_URL}/{OPEN_NRF_OCD_RELEASE}/{asset_name}"

    for root in open_nrf_ocd_cache_roots(host_tools_path):
        destination = root / executable_name
        if destination.is_file():
            make_executable_if_needed(destination)
            return [str(destination)]

        try:
            root.mkdir(parents=True, exist_ok=True)
        except OSError:
            continue

        temp_path = destination.with_suffix(destination.suffix + ".tmp")
        try:
            print(f"Downloading open-nrf-ocd {asset_name}...")
            with urllib.request.urlopen(url, timeout=60) as response, temp_path.open("wb") as out:
                shutil.copyfileobj(response, out, length=1024 * 1024)
            os.replace(temp_path, destination)
            make_executable_if_needed(destination)
            return [str(destination)]
        except (OSError, urllib.error.URLError) as exc:
            try:
                temp_path.unlink()
            except OSError:
                pass
            print(f"open-nrf-ocd download/cache failed at {root}: {exc}", file=sys.stderr)

    return None

def detect_nrf_ocd_command(
    host_tools_path: Optional[Path] = None,
    allow_download: bool = False,
) -> Optional[List[str]]:
    override = os.environ.get("NRF54_NRF_OCD") or os.environ.get("OPEN_NRF_OCD")
    if override:
        return shlex.split(override)

    # The platform archive ships nrf_ocd beside this wrapper. Select that exact
    # release binary before looking at caches so an older installed core cannot
    # win merely because its path is shorter.
    search_roots = [Path(__file__).resolve().parent]
    search_roots.extend(open_nrf_ocd_cache_roots(host_tools_path))
    if host_tools_path is not None:
        search_roots.append(host_tools_path)

    seen = set()
    for root in search_roots:
        root_key = str(root)
        if root_key in seen:
            continue
        seen.add(root_key)
        for name in open_nrf_ocd_candidate_names():
            candidate = root / name
            if not candidate.is_file():
                continue
            make_executable_if_needed(candidate)
            return [str(candidate)]

    if allow_download:
        return ensure_open_nrf_ocd_release_binary(host_tools_path)

    return None

def upload_nrf_ocd(
    hex_path,
    target,
    uid,
    host_tools_path: Optional[Path] = None,
    nrf_ocd_cmd=None,
):
    # nrf_ocd 0.3.8 falls back to the only probe if its UID lookup fails.
    # A preflight inventory cannot close the unplug/replug race; selected
    # probes must stay on pyOCD until the native tool supports strict UIDs.
    if normalize_uid(uid) is not None:
        print("ERROR: UID-selected uploads require pyOCD; refusing native probe fallback.", file=sys.stderr)
        return 4
    nrf_ocd_cmd = (
        nrf_ocd_cmd
        if nrf_ocd_cmd is not None
        else detect_nrf_ocd_command(host_tools_path, allow_download=True)
    )
    if nrf_ocd_cmd is None:
        return -1  # not found
    inventory = run([*nrf_ocd_cmd, "list"], timeout=15.0)
    probe_rows = [
        line for line in (inventory.stdout or "").splitlines()
        if re.match(r"^\s*\d+\s+", line)
    ]
    valid_single_probe = len(probe_rows) == 1 and re.fullmatch(
        r"\s*\d+\s+.+\s+nrf54(?:l15|lm20a)\s*", probe_rows[0]
    )
    if inventory.returncode != 0 or not valid_single_probe:
        print_result(inventory)
        print(
            "ERROR: Native auto-select requires exactly one CMSIS-DAP probe. "
            "Select a probe UID or serial port to use pyOCD safely.",
            file=sys.stderr,
        )
        return 4
    target_map = {
        "nrf54l": "nrf54l15",
    }
    ocd_target = target.strip().lower()
    if ocd_target in target_map:
        ocd_target = target_map[ocd_target]
    args = [*nrf_ocd_cmd, "-t", ocd_target]
    args.extend(["-e", "chip", "-R", "--no-verify", "load", hex_path])
    print(f"Flashing {hex_path}")
    print(f"Runner: nrf_ocd")
    print("Probe UID: auto-select")
    captured_output: List[str] = []
    try:
        # Run nrf_ocd, streaming all output in real-time
        import subprocess as _sp
        proc = _sp.Popen(args, stdout=_sp.PIPE, stderr=_sp.STDOUT, bufsize=0)
        for line in iter(proc.stdout.readline, b''):
            if line:
                captured_output.append(line.decode(errors="replace"))
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
        proc.stdout.close()
        rc = proc.wait()
        # Some nrf_ocd versions report a CMSIS-DAP teardown failure while
        # still exiting successfully. Do not turn that transport error into a
        # clean Arduino upload result; let the caller run its pyOCD fallback.
        if looks_like_nrf_ocd_transport_error("".join(captured_output)):
            return NRF_OCD_TRANSPORT_FALLBACK
        return rc
    except subprocess.TimeoutExpired:
        proc.kill()
        print("nrf_ocd timed out", file=sys.stderr)
    except Exception as e:
        print(f"nrf_ocd error: {e}", file=sys.stderr)
    return 1

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
        help="Upload runner: uf2, auto, pyocd, or nrf_ocd",
    )
    parser.add_argument(
        "--probe-type",
        default="auto",
        help="Restrict pyOCD to cmsisdap or experimental jlink probes",
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

    try:
        pyocd_probe_type = normalize_pyocd_probe_type(args.probe_type)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 4

    explicit_uid = normalize_uid(args.uid)
    if pyocd_probe_type == "jlink" and explicit_uid is None:
        explicit_uid = normalize_uid(os.environ.get("NRF54L15_JLINK_UID"))
    inferred_uid = infer_uid_from_port(args.port, host_tools_path) if explicit_uid is None else None
    selected_uid = explicit_uid if explicit_uid is not None else inferred_uid
    if requested_runner != "uf2" and not unresolved_property(args.port) and selected_uid is None:
        print(
            "ERROR: Cannot identify the debug probe for the selected serial port. "
            "Specify its probe UID; refusing to auto-select a different board.",
            file=sys.stderr,
        )
        return 8
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

    if runner == "nrf_ocd" and selected_uid is not None:
        print("Using pyOCD to keep the selected probe UID strict across upload and reset.")
        runner = "pyocd"

    rc = 1
    tried_nrf_ocd = False
    if runner == "uf2":
        rc = upload_uf2(
            uf2_path,
            args.uf2_drive,
            uf2_labels,
            args.uf2_timeout,
        )
    if runner == "pyocd":
        if (
            pyocd_probe_type != "jlink"
            and sys.platform.startswith("win")
            and explicit_uid is None
            and selected_uid is None
            and len(matching_cmsis_dap_serial_ports(host_tools_path)) > 1
        ):
            print(
                "ERROR: Multiple CMSIS-DAP probes are connected, but the selected COM "
                "port did not expose a unique probe serial number.",
                file=sys.stderr,
            )
            print(
                "HINT: Replug the board or switch to UF2 upload once, then retry. "
                "The uploader needs the board's unique USB serial to avoid pyocd's "
                "interactive probe picker.",
                file=sys.stderr,
            )
            return 8
        if (
            pyocd_probe_type != "jlink"
            and preflight_linux_probe_access(args.port, host_tools_path)
        ):
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
            allow_uid_fallback=False,
            retries=args.retries,
            retry_delay=args.retry_delay,
            host_tools_path=host_tools_path,
            safe_mode=pyocd_safe_mode,
            port=args.port,
            probe_type=pyocd_probe_type,
        )

    elif runner == "nrf_ocd":
        tried_nrf_ocd = True
        nrf_rc = upload_nrf_ocd(
            args.hex,
            args.target,
            selected_uid,
            host_tools_path=host_tools_path,
        )
        if nrf_rc == 0:
            rc = 0
        elif nrf_rc == NRF_OCD_TRANSPORT_FALLBACK:
            print(
                "nrf_ocd CMSIS-DAP transport failed; retrying with pyOCD safe mode...",
                file=sys.stderr,
            )
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
                allow_uid_fallback=False,
                retries=args.retries,
                retry_delay=args.retry_delay,
                host_tools_path=host_tools_path,
                safe_mode=True,
                port=args.port,
                probe_type=pyocd_probe_type,
            )
        elif nrf_rc == -1:
            print("nrf_ocd not found (please install or choose pyOCD)", file=sys.stderr)
    elif runner != "uf2":
        print(f"ERROR: Unsupported runner: {runner}", file=sys.stderr)
        return 4

    if runner == "pyocd" and rc != 0 and not tried_nrf_ocd and pyocd_probe_type != "jlink" and selected_uid is None:
        # Final fallback: try nrf_ocd
        nrf_rc = upload_nrf_ocd(
            args.hex,
            args.target,
            selected_uid,
            host_tools_path=host_tools_path,
        )
        if nrf_rc == 0:
            rc = 0
        elif nrf_rc == -1:
            print("nrf_ocd not found (optional native flash tool)", file=sys.stderr)

    if rc != 0:
        return rc

    print("Upload complete")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
