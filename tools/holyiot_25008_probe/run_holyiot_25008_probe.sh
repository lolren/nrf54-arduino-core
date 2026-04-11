#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SKETCH_DIR="${SCRIPT_DIR}/Holyiot25008Probe"
BUILD_DIR="${SCRIPT_DIR}/build"
LOG_DIR="${SCRIPT_DIR}/logs"
FQBN="${FQBN:-nrf54l15clean:nrf54l15clean:generic_nrf54l15_module_36pin}"
PORT="${PICO_PORT:-/dev/ttyACM0}"
PYOCD_UID="${PICO_UID:-}"

mkdir -p "${BUILD_DIR}" "${LOG_DIR}"

CONFIG_FILE="$(mktemp)"
cleanup() {
  rm -f "${CONFIG_FILE}"
}
trap cleanup EXIT

cat >"${CONFIG_FILE}" <<EOF
directories:
  data: ${HOME}/.arduino15
  downloads: ${HOME}/.arduino15/staging
  user: ${REPO_ROOT}
EOF

if [[ -z "${PYOCD_UID}" ]] && [[ -e /dev/serial/by-id ]]; then
  serial_match="$(
    ls /dev/serial/by-id 2>/dev/null \
      | sed -n 's/^.*Raspberry_Pi_Debugprobe_on_Pico__CMSIS-DAP__\([0-9A-Fa-f][0-9A-Fa-f]*\)-if01$/\1/p' \
      | head -n1
  )"
  if [[ -n "${serial_match}" ]]; then
    PYOCD_UID="${serial_match}"
  fi
fi

echo "Repo root : ${REPO_ROOT}"
echo "Sketch    : ${SKETCH_DIR}"
echo "Build dir : ${BUILD_DIR}"
echo "Log dir   : ${LOG_DIR}"
echo "FQBN      : ${FQBN}"
echo "Port      : ${PORT}"
echo "Probe UID : ${PYOCD_UID:-auto}"
echo
echo "HOLYIOT-25008 pins expected from Zephyr board support:"
echo "  UART pads : P1.04 (TX), P1.05 (RX)"
echo "  Button    : P1.13 (active low, pull-up)"
echo "  LED red   : P2.09 (active low)"
echo "  LED green : P1.10 (active low)"
echo "  LED blue  : P2.07 (active low)"
echo
echo "Pico Debugprobe wiring:"
echo "  GP2 -> SWCLK"
echo "  GP3 -> SWDIO"
echo "  GND -> GND"
echo "  VTREF -> target VDD"
echo "  Optional: GP1 -> nRESET"
echo

arduino-cli compile \
  --config-file "${CONFIG_FILE}" \
  --fqbn "${FQBN}" \
  --build-path "${BUILD_DIR}" \
  "${SKETCH_DIR}" \
  2>&1 | tee "${LOG_DIR}/compile.log"

echo
echo "Attempting upload through the Pico Debugprobe..."
upload_args=(
  --config-file "${CONFIG_FILE}"
  --build-path "${BUILD_DIR}"
  -p "${PORT}"
  --fqbn "${FQBN}"
  --board-options clean_upload=pyocd
)
upload_args+=("${SKETCH_DIR}")

set +e
arduino-cli upload \
  "${upload_args[@]}" \
  2>&1 | tee "${LOG_DIR}/arduino_upload.log"
arduino_rc=${PIPESTATUS[0]}
set -e

if [[ ${arduino_rc} -eq 0 ]]; then
  echo
  echo "Upload complete."
  exit 0
fi

echo
echo "Arduino upload failed with rc=${arduino_rc}."

HEX_PATH="${BUILD_DIR}/$(basename "${SKETCH_DIR}").ino.hex"
if [[ ! -f "${HEX_PATH}" ]]; then
  HEX_PATH="$(find "${BUILD_DIR}" -maxdepth 3 -name '*.hex' | head -n1 || true)"
fi

if [[ -n "${HEX_PATH}" ]] && [[ -f "${HEX_PATH}" ]]; then
  echo "Attempting direct pyOCD flash for clearer diagnostics..."
  set +e
  if [[ -n "${PYOCD_UID}" ]]; then
    python3 -m pyocd load -W -t nrf54l -u "${PYOCD_UID}" -M under-reset "${HEX_PATH}" --format hex \
      2>&1 | tee "${LOG_DIR}/pyocd_direct.log"
    pyocd_rc=${PIPESTATUS[0]}
  else
    python3 -m pyocd load -W -t nrf54l -M under-reset "${HEX_PATH}" --format hex \
      2>&1 | tee "${LOG_DIR}/pyocd_direct.log"
    pyocd_rc=${PIPESTATUS[0]}
  fi
  set -e
  echo "Direct pyOCD rc=${pyocd_rc}"
  exit "${pyocd_rc}"
fi

echo "No HEX file found for direct pyOCD retry."
exit "${arduino_rc}"
