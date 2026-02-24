#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN_DEFAULT="nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
PORT_DEFAULT=""
EXAMPLE_DEFAULT="BleBondPersistenceProbe"
ATTEMPTS_DEFAULT=5
SCAN_TIMEOUT_DEFAULT=12
SERIAL_TIMEOUT_DEFAULT=75
PAIR_TIMEOUT_DEFAULT=20

FQBN="${FQBN_DEFAULT}"
PORT="${PORT_DEFAULT}"
EXAMPLE="${EXAMPLE_DEFAULT}"
ATTEMPTS="${ATTEMPTS_DEFAULT}"
SCAN_TIMEOUT="${SCAN_TIMEOUT_DEFAULT}"
SERIAL_TIMEOUT="${SERIAL_TIMEOUT_DEFAULT}"
PAIR_TIMEOUT="${PAIR_TIMEOUT_DEFAULT}"
USE_SUDO=0
OUTDIR=""
TARGET_ADDR=""

usage() {
  cat <<'USAGE'
Usage:
  scripts/ble_pair_bond_regression.sh [options]

Options:
  --port <device>        Serial port (default: auto-detect)
  --fqbn <fqbn>          Board FQBN
  --example <name>       BLE example: BleBondPersistenceProbe or BlePairingEncryptionStatus
  --addr <ble_addr>      Override target BLE address (e.g. C0:DE:54:15:00:61)
  --attempts <n>         Number of attempts (default: 5)
  --scan-timeout <sec>   Scan timeout (default: 12)
  --serial-timeout <sec> Serial capture timeout per attempt (default: 75)
  --pair-timeout <sec>   bluetoothctl pair timeout (default: 20)
  --sudo                 Use sudo -n for btmon/bluetoothctl
  --outdir <path>        Output directory
  --help                 Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="$2"
      shift 2
      ;;
    --fqbn)
      FQBN="$2"
      shift 2
      ;;
    --example)
      EXAMPLE="$2"
      shift 2
      ;;
    --addr)
      TARGET_ADDR="$2"
      shift 2
      ;;
    --attempts)
      ATTEMPTS="$2"
      shift 2
      ;;
    --scan-timeout)
      SCAN_TIMEOUT="$2"
      shift 2
      ;;
    --serial-timeout)
      SERIAL_TIMEOUT="$2"
      shift 2
      ;;
    --pair-timeout)
      PAIR_TIMEOUT="$2"
      shift 2
      ;;
    --sudo)
      USE_SUDO=1
      shift
      ;;
    --outdir)
      OUTDIR="$2"
      shift 2
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if ! [[ "${ATTEMPTS}" =~ ^[0-9]+$ ]] || [[ "${ATTEMPTS}" -lt 1 ]]; then
  echo "Invalid --attempts value: ${ATTEMPTS}" >&2
  exit 1
fi

if [[ -z "${OUTDIR}" ]]; then
  OUTDIR="${ROOT_DIR}/measurements/ble_pair_bond_regression_$(date +%Y%m%d_%H%M%S)"
fi
mkdir -p "${OUTDIR}"

case "${EXAMPLE}" in
  BleBondPersistenceProbe)
    DEFAULT_ADDR="C0:DE:54:15:00:61"
    ;;
  BlePairingEncryptionStatus)
    DEFAULT_ADDR="C0:DE:54:15:00:51"
    ;;
  *)
    echo "Unsupported --example: ${EXAMPLE}" >&2
    exit 1
    ;;
esac

if [[ -z "${TARGET_ADDR}" ]]; then
  TARGET_ADDR="${DEFAULT_ADDR}"
fi

if [[ -z "${PORT}" ]]; then
  PORT="$(arduino-cli board list | awk 'NR > 1 && $1 ~ /^\/dev\// {print $1; exit}')"
fi
if [[ -z "${PORT}" && -e /dev/ttyACM0 ]]; then
  PORT="/dev/ttyACM0"
fi
if [[ -z "${PORT}" ]]; then
  echo "Could not auto-detect serial port. Use --port." >&2
  exit 1
fi

if [[ "${USE_SUDO}" -eq 1 ]]; then
  BTMON_CMD=(sudo -n btmon)
  BTCTL_CMD=(sudo -n bluetoothctl --agent NoInputNoOutput)
else
  BTMON_CMD=(btmon)
  BTCTL_CMD=(bluetoothctl --agent NoInputNoOutput)
fi

EXAMPLE_PATH="${ROOT_DIR}/hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation/examples/${EXAMPLE}"
if [[ ! -d "${EXAMPLE_PATH}" ]]; then
  echo "Example not found: ${EXAMPLE_PATH}" >&2
  exit 1
fi

echo "[ble-regression] outdir=${OUTDIR}"
echo "[ble-regression] port=${PORT} fqbn=${FQBN} example=${EXAMPLE} addr=${TARGET_ADDR} attempts=${ATTEMPTS}"

echo "[ble-regression] compiling example..."
arduino-cli compile --fqbn "${FQBN}" "${EXAMPLE_PATH}" >"${OUTDIR}/compile.log" 2>&1

CSV="${OUTDIR}/attempts.csv"
{
  echo "attempt,pair_ok,paired,bonded,enc_change_success,mic_failure,host_crash,disconnect_reason"
} > "${CSV}"

pass_count=0
pair_ok_count=0
paired_count=0
bonded_count=0
enc_ok_count=0
mic_fail_count=0
host_crash_count=0

for attempt in $(seq 1 "${ATTEMPTS}"); do
  A_DIR="${OUTDIR}/attempt_$(printf '%02d' "${attempt}")"
  mkdir -p "${A_DIR}"
  echo "[ble-regression] attempt ${attempt}/${ATTEMPTS}"

  arduino-cli upload -p "${PORT}" --fqbn "${FQBN}" "${EXAMPLE_PATH}" >"${A_DIR}/upload.log" 2>&1 || true

  stty -F "${PORT}" 115200 raw -echo -echoe -echok -echoctl -echoke || true
  (timeout "${SERIAL_TIMEOUT}s" cat "${PORT}" >"${A_DIR}/serial.log") &
  SERIAL_PID=$!

  ("${BTMON_CMD[@]}" >"${A_DIR}/btmon.log" 2>&1) &
  BTMON_PID=$!

  sleep 2

  timeout 16s "${BTCTL_CMD[@]}" --timeout 12 <<EOF >"${A_DIR}/setup.log" 2>&1 || true
power on
pairable on
discoverable off
agent NoInputNoOutput
default-agent
remove ${TARGET_ADDR}
EOF

  timeout "$((SCAN_TIMEOUT + 2))s" "${BTCTL_CMD[@]}" --timeout "${SCAN_TIMEOUT}" scan on >"${A_DIR}/scan.log" 2>&1 || true
  timeout 8s "${BTCTL_CMD[@]}" --timeout 6 devices >"${A_DIR}/devices.log" 2>&1 || true
  timeout 10s "${BTCTL_CMD[@]}" --timeout 8 trust "${TARGET_ADDR}" >"${A_DIR}/trust.log" 2>&1 || true
  timeout 12s "${BTCTL_CMD[@]}" --timeout 10 connect "${TARGET_ADDR}" >"${A_DIR}/connect.log" 2>&1 || true
  timeout "$((PAIR_TIMEOUT + 2))s" "${BTCTL_CMD[@]}" --timeout "${PAIR_TIMEOUT}" pair "${TARGET_ADDR}" >"${A_DIR}/pair.log" 2>&1 || true
  timeout 10s "${BTCTL_CMD[@]}" --timeout 8 info "${TARGET_ADDR}" >"${A_DIR}/info.log" 2>&1 || true
  timeout 8s "${BTCTL_CMD[@]}" --timeout 6 disconnect "${TARGET_ADDR}" >"${A_DIR}/disconnect.log" 2>&1 || true

  sleep 2
  kill "${BTMON_PID}" >/dev/null 2>&1 || true
  wait "${BTMON_PID}" >/dev/null 2>&1 || true
  wait "${SERIAL_PID}" >/dev/null 2>&1 || true

  pair_ok="no"
  if rg -q "Pairing successful|Status: Success|Paired: yes|Bonded: yes" "${A_DIR}/pair.log" "${A_DIR}/info.log"; then
    pair_ok="yes"
  fi

  paired="no"
  if rg -q "Paired:\\s+yes" "${A_DIR}/info.log" "${A_DIR}/pair.log"; then
    paired="yes"
  fi

  bonded="no"
  if rg -q "Bonded:\\s+yes" "${A_DIR}/info.log" "${A_DIR}/pair.log"; then
    bonded="yes"
  fi

  enc_ok="no"
  if awk '
      /Encryption Change \(0x08\)/ {window=4; next}
      window > 0 && /Status: Success \(0x00\)/ {ok=1; exit}
      window > 0 {window--}
      END {exit(ok ? 0 : 1)}
    ' "${A_DIR}/btmon.log"; then
    enc_ok="yes"
  fi

  mic_fail="no"
  if rg -qi "MIC Failure \\(0x3d\\)|Reason:\\s+MIC Failure \\(0x3d\\)" "${A_DIR}/btmon.log"; then
    mic_fail="yes"
  fi

  host_crash="no"
  if rg -q "Hardware Error \\(0x10\\).*0x0c|Intel Bootup|Reset reason: System exception" "${A_DIR}/btmon.log"; then
    host_crash="yes"
  fi

  disconnect_reason="$( (rg -n 'Reason:' "${A_DIR}/btmon.log" || true) | tail -n 1 | sed 's/^[0-9]*://; s/[[:space:]]\\+/ /g' | sed 's/^ //')"
  if [[ -z "${disconnect_reason}" ]]; then
    disconnect_reason="n/a"
  fi
  disconnect_reason="${disconnect_reason//,/;}"

  if [[ "${pair_ok}" == "yes" ]]; then pair_ok_count=$((pair_ok_count + 1)); fi
  if [[ "${paired}" == "yes" ]]; then paired_count=$((paired_count + 1)); fi
  if [[ "${bonded}" == "yes" ]]; then bonded_count=$((bonded_count + 1)); fi
  if [[ "${enc_ok}" == "yes" ]]; then enc_ok_count=$((enc_ok_count + 1)); fi
  if [[ "${mic_fail}" == "yes" ]]; then mic_fail_count=$((mic_fail_count + 1)); fi
  if [[ "${host_crash}" == "yes" ]]; then host_crash_count=$((host_crash_count + 1)); fi

  if [[ "${pair_ok}" == "yes" && "${bonded}" == "yes" && "${mic_fail}" == "no" ]]; then
    pass_count=$((pass_count + 1))
  fi

  echo "${attempt},${pair_ok},${paired},${bonded},${enc_ok},${mic_fail},${host_crash},${disconnect_reason}" >> "${CSV}"
done

SUMMARY="${OUTDIR}/summary.txt"
{
  echo "BLE Pair/Bond Regression Summary"
  echo "attempts=${ATTEMPTS}"
  echo "pass_count=${pass_count}"
  echo "pair_ok_count=${pair_ok_count}"
  echo "paired_count=${paired_count}"
  echo "bonded_count=${bonded_count}"
  echo "enc_change_success_count=${enc_ok_count}"
  echo "mic_failure_count=${mic_fail_count}"
  echo "host_crash_count=${host_crash_count}"
  echo "csv=${CSV}"
} > "${SUMMARY}"

echo "[ble-regression] done"
cat "${SUMMARY}"
