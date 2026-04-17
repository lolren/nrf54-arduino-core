#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RULE_SRC="${SCRIPT_DIR}/60-seeed-xiao-nrf54-cmsis-dap.rules"
RULE_DST="${RULE_DST:-/etc/udev/rules.d/60-seeed-xiao-nrf54-cmsis-dap.rules}"
UDEVADM_BIN="${UDEVADM_BIN:-udevadm}"

usage() {
  cat <<'EOF'
Usage:
  install_linux_host_deps.sh           Install Python pyOCD host dependencies
  install_linux_host_deps.sh --python  Install Python pyOCD host dependencies
  install_linux_host_deps.sh --udev    Install Linux udev rules only
  install_linux_host_deps.sh --all     Install both Python deps and udev rules
EOF
}

install_python_deps() {
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required for pyOCD installation." >&2
    exit 1
  fi

  python3 -m pip install --user --upgrade pip pyocd
}

install_udev_rules() {
  local rule_dir

  if ! command -v "${UDEVADM_BIN}" >/dev/null 2>&1; then
    echo "${UDEVADM_BIN} is required for --udev." >&2
    exit 1
  fi

  rule_dir="$(dirname -- "${RULE_DST}")"
  if [[ ! -d "${rule_dir}" ]]; then
    echo "udev rules directory does not exist: ${rule_dir}" >&2
    exit 1
  fi

  if [[ -w "${rule_dir}" ]]; then
    install -m 0644 "${RULE_SRC}" "${RULE_DST}"
    "${UDEVADM_BIN}" control --reload-rules
    "${UDEVADM_BIN}" trigger --attr-match=idVendor=2886 --attr-match=idProduct=0066
  elif command -v sudo >/dev/null 2>&1; then
    sudo install -m 0644 "${RULE_SRC}" "${RULE_DST}"
    sudo "${UDEVADM_BIN}" control --reload-rules
    sudo "${UDEVADM_BIN}" trigger --attr-match=idVendor=2886 --attr-match=idProduct=0066
  else
    echo "Need write access to ${rule_dir}. Re-run with sudo available or as root." >&2
    exit 1
  fi
}

want_python=0
want_udev=0

case "${1:-}" in
  "")
    want_python=1
    ;;
  --python)
    want_python=1
    ;;
  --udev)
    want_udev=1
    ;;
  --all)
    want_python=1
    want_udev=1
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac

if (( want_python )); then
  install_python_deps
fi

if (( want_udev )); then
  install_udev_rules
fi

echo "Host upload dependencies are ready."
echo "Restart the Arduino IDE if it was already open."
