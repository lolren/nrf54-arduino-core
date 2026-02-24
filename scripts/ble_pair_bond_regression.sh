#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN_DEFAULT="nrf54l15clean:nrf54l15clean:xiao_nrf54l15"
PORT_DEFAULT=""
EXAMPLE_DEFAULT="BleBondPersistenceProbe"
MODE_DEFAULT="pair-bond"
ATTEMPTS_DEFAULT=5
SCAN_TIMEOUT_DEFAULT=12
SERIAL_TIMEOUT_DEFAULT=75
PAIR_TIMEOUT_DEFAULT=20

FQBN="${FQBN_DEFAULT}"
PORT="${PORT_DEFAULT}"
EXAMPLE="${EXAMPLE_DEFAULT}"
MODE="${MODE_DEFAULT}"
ATTEMPTS="${ATTEMPTS_DEFAULT}"
SCAN_TIMEOUT="${SCAN_TIMEOUT_DEFAULT}"
SERIAL_TIMEOUT="${SERIAL_TIMEOUT_DEFAULT}"
PAIR_TIMEOUT="${PAIR_TIMEOUT_DEFAULT}"
USE_SUDO=0
OUTDIR=""
TARGET_ADDR=""
CONTROLLER=""
BTMON_IFACE=""
REMOVE_BEFORE_ATTEMPT=1

usage() {
  cat <<'USAGE'
Usage:
  scripts/ble_pair_bond_regression.sh [options]

Options:
  --port <device>        Serial port (default: auto-detect)
  --fqbn <fqbn>          Board FQBN
  --example <name>       BLE example: BleBondPersistenceProbe or BlePairingEncryptionStatus
  --mode <name>          Regression mode: pair-bond | bonded-reconnect (default: pair-bond)
  --addr <ble_addr>      Override target BLE address (e.g. C0:DE:54:15:00:61)
  --controller <addr>    Bluetooth controller address for bluetoothctl 'select'
  --btmon-iface <hciX>   btmon interface (e.g. hci0, hci1)
  --attempts <n>         Number of attempts (default: 5)
  --scan-timeout <sec>   Scan timeout (default: 12)
  --serial-timeout <sec> Serial capture timeout per attempt (default: 75)
  --pair-timeout <sec>   bluetoothctl pair timeout (default: 20)
  --keep-host-entry      Do not run bluetoothctl remove <addr> during setup
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
    --mode)
      MODE="$2"
      shift 2
      ;;
    --addr)
      TARGET_ADDR="$2"
      shift 2
      ;;
    --controller)
      CONTROLLER="$2"
      shift 2
      ;;
    --btmon-iface)
      BTMON_IFACE="$2"
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
    --keep-host-entry)
      REMOVE_BEFORE_ATTEMPT=0
      shift
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

is_pos_int() {
  local value="$1"
  [[ "${value}" =~ ^[0-9]+$ ]] && [[ "${value}" -ge 1 ]]
}

if ! is_pos_int "${ATTEMPTS}"; then
  echo "Invalid --attempts value: ${ATTEMPTS}" >&2
  exit 1
fi
if ! is_pos_int "${SCAN_TIMEOUT}"; then
  echo "Invalid --scan-timeout value: ${SCAN_TIMEOUT}" >&2
  exit 1
fi
if ! is_pos_int "${SERIAL_TIMEOUT}"; then
  echo "Invalid --serial-timeout value: ${SERIAL_TIMEOUT}" >&2
  exit 1
fi
if ! is_pos_int "${PAIR_TIMEOUT}"; then
  echo "Invalid --pair-timeout value: ${PAIR_TIMEOUT}" >&2
  exit 1
fi

case "${MODE}" in
  pair-bond|bonded-reconnect)
    ;;
  *)
    echo "Unsupported --mode: ${MODE}" >&2
    exit 1
    ;;
esac

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

PYOCD_CMD="$(command -v pyocd || true)"
if [[ -z "${PYOCD_CMD}" && -x "${HOME}/.platformio-venv/bin/pyocd" ]]; then
  PYOCD_CMD="${HOME}/.platformio-venv/bin/pyocd"
fi

if [[ -n "${BTMON_IFACE}" ]]; then
  BTMON_CMD+=(-i "${BTMON_IFACE}")
fi

EXAMPLE_PATH="${ROOT_DIR}/hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation/examples/${EXAMPLE}"
if [[ ! -d "${EXAMPLE_PATH}" ]]; then
  echo "Example not found: ${EXAMPLE_PATH}" >&2
  exit 1
fi

if [[ "${MODE}" == "bonded-reconnect" && "${EXAMPLE}" != "BleBondPersistenceProbe" ]]; then
  echo "--mode bonded-reconnect requires --example BleBondPersistenceProbe" >&2
  exit 1
fi

BTCTL_SELECT_LINE=""
if [[ -n "${CONTROLLER}" ]]; then
  BTCTL_SELECT_LINE="select ${CONTROLLER}"
fi

run_btctl_sequence() {
  local process_timeout="$1"
  local cli_timeout="$2"
  local logfile="$3"
  local commands="$4"
  local line=""

  (
    if [[ -n "${BTCTL_SELECT_LINE}" ]]; then
      echo "${BTCTL_SELECT_LINE}"
    fi
    while IFS= read -r line; do
      if [[ "${line}" =~ ^__SLEEP__:([0-9]+)$ ]]; then
        sleep "${BASH_REMATCH[1]}"
      elif [[ -n "${line}" ]]; then
        echo "${line}"
      fi
    done <<< "${commands}"
  ) | timeout "${process_timeout}s" "${BTCTL_CMD[@]}" --timeout "${cli_timeout}" >"${logfile}" 2>&1 || true
}

link_log_aliases() {
  local source_log="$1"
  shift
  local source_name
  source_name="$(basename "${source_log}")"
  local alias_path=""
  for alias_path in "$@"; do
    ln -sf "${source_name}" "${alias_path}" || true
  done
}

detect_host_instability_signature() {
  local files=()
  local one_file=""
  for one_file in "$@"; do
    if [[ -f "${one_file}" ]]; then
      files+=("${one_file}")
    fi
  done

  if [[ "${#files[@]}" -eq 0 ]]; then
    echo "no"
    return
  fi

  if rg -qi "org\\.bluez\\.Error\\.(InProgress|NotReady|NotAvailable)|le-connection-abort-by-local|Operation already in progress|No default controller available|Connection Failed to be Established|Software caused connection abort" "${files[@]}"; then
    echo "yes"
  else
    echo "no"
  fi
}

clear_target_bond_storage() {
  local logfile="$1"
  if [[ -z "${PYOCD_CMD}" ]]; then
    echo "pyocd not available; skipping bond-sector erase" >"${logfile}"
    return
  fi
  "${PYOCD_CMD}" erase -t nrf54l --sector 0x7f000-0x80000 >"${logfile}" 2>&1 || true
}

check_connect_hit() {
  local logfile="$1"
  if rg -q "Connection successful|Connected:\\s+yes|already connected|AlreadyConnected" "${logfile}"; then
    echo "yes"
  else
    echo "no"
  fi
}

check_info_connected() {
  local logfile="$1"
  if rg -q "Connected:\\s+yes" "${logfile}"; then
    echo "yes"
  else
    echo "no"
  fi
}

echo "[ble-regression] outdir=${OUTDIR}"
echo "[ble-regression] port=${PORT} fqbn=${FQBN} example=${EXAMPLE} mode=${MODE} addr=${TARGET_ADDR} attempts=${ATTEMPTS}"
if [[ -n "${CONTROLLER}" ]]; then
  echo "[ble-regression] controller=${CONTROLLER}"
fi
if [[ -n "${BTMON_IFACE}" ]]; then
  echo "[ble-regression] btmon_iface=${BTMON_IFACE}"
fi

echo "[ble-regression] compiling example..."
arduino-cli compile --fqbn "${FQBN}" "${EXAMPLE_PATH}" >"${OUTDIR}/compile.log" 2>&1

CSV="${OUTDIR}/attempts.csv"
{
  echo "attempt,mode,pair_ok,paired,bonded,enc_change_success,enc_change_success_count,reconnect_connected,reconnect_bonded,reconnect_enc_seen,target_trace_error,mic_failure,host_crash,host_unstable,target_verdict,overall_verdict,disconnect_reason"
} > "${CSV}"

pass_count=0
fail_target_count=0
inconclusive_host_count=0
pair_ok_count=0
paired_count=0
bonded_count=0
enc_ok_count=0
reconnect_connected_count=0
reconnect_bonded_count=0
reconnect_enc_seen_count=0
target_trace_error_count=0
mic_fail_count=0
host_crash_count=0
host_unstable_count=0

clear_target_bond_before_attempt=0
if [[ "${MODE}" == "pair-bond" && "${EXAMPLE}" == "BleBondPersistenceProbe" && "${REMOVE_BEFORE_ATTEMPT}" -eq 1 ]]; then
  clear_target_bond_before_attempt=1
fi

for attempt in $(seq 1 "${ATTEMPTS}"); do
  A_DIR="${OUTDIR}/attempt_$(printf '%02d' "${attempt}")"
  mkdir -p "${A_DIR}"
  echo "[ble-regression] attempt ${attempt}/${ATTEMPTS}"

  if [[ "${clear_target_bond_before_attempt}" -eq 1 ]]; then
    clear_target_bond_storage "${A_DIR}/target_bond_clear.log"
  fi

  arduino-cli upload -p "${PORT}" --fqbn "${FQBN}" "${EXAMPLE_PATH}" >"${A_DIR}/upload.log" 2>&1 || true

  stty -F "${PORT}" 115200 raw -echo -echoe -echok -echoctl -echoke || true
  (timeout "${SERIAL_TIMEOUT}s" cat "${PORT}" >"${A_DIR}/serial.log") &
  SERIAL_PID=$!

  ("${BTMON_CMD[@]}" >"${A_DIR}/btmon.log" 2>&1) &
  BTMON_PID=$!

  sleep 2

  btctl_main_log="${A_DIR}/btctl_main.log"
  btctl_main_commands=$'scan off\npower on\npairable on\ndiscoverable off'
  if [[ "${REMOVE_BEFORE_ATTEMPT}" -eq 1 ]]; then
    btctl_main_commands+=$'\n'"remove ${TARGET_ADDR}"
  fi
  btctl_main_commands+=$'\n'"__SLEEP__:1"
  btctl_main_commands+=$'\n'"default-agent"
  btctl_main_commands+=$'\n'"scan off"
  btctl_main_commands+=$'\n'"scan on"
  btctl_main_commands+=$'\n'"__SLEEP__:${SCAN_TIMEOUT}"
  btctl_main_commands+=$'\n'"devices"
  btctl_main_commands+=$'\n'"scan off"
  btctl_main_commands+=$'\n'"trust ${TARGET_ADDR}"
  btctl_main_commands+=$'\n'"info ${TARGET_ADDR}"
  btctl_main_commands+=$'\n'"pair ${TARGET_ADDR}"
  btctl_main_commands+=$'\n'"__SLEEP__:${PAIR_TIMEOUT}"
  btctl_main_commands+=$'\n'"info ${TARGET_ADDR}"
  btctl_main_commands+=$'\n'"disconnect ${TARGET_ADDR}"
  btctl_main_commands+=$'\n'"info ${TARGET_ADDR}"
  btctl_main_commands+=$'\n'"quit"
  btctl_main_commands+=$'\n'"__SLEEP__:1"

  btctl_main_timeout="$((PAIR_TIMEOUT + SCAN_TIMEOUT + 42))"
  run_btctl_sequence "$((btctl_main_timeout + 6))" "${btctl_main_timeout}" \
    "${btctl_main_log}" "${btctl_main_commands}"

  link_log_aliases "${btctl_main_log}" \
    "${A_DIR}/setup.log" \
    "${A_DIR}/scan.log" \
    "${A_DIR}/devices.log" \
    "${A_DIR}/trust.log" \
    "${A_DIR}/connect.log" \
    "${A_DIR}/pair.log" \
    "${A_DIR}/info.log" \
    "${A_DIR}/disconnect.log" \
    "${A_DIR}/disconnect_info.log"

  btctl_reconnect_log="${A_DIR}/btctl_reconnect.log"
  if [[ "${MODE}" == "bonded-reconnect" ]]; then
    btctl_reconnect_commands=$'scan off'
    btctl_reconnect_commands+=$'\n'"disconnect ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"info ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"disconnect ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"info ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"connect ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"__SLEEP__:4"
    btctl_reconnect_commands+=$'\n'"info ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"disconnect ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"info ${TARGET_ADDR}"
    btctl_reconnect_commands+=$'\n'"quit"
    btctl_reconnect_commands+=$'\n'"__SLEEP__:1"

    run_btctl_sequence 34 30 "${btctl_reconnect_log}" "${btctl_reconnect_commands}"
    link_log_aliases "${btctl_reconnect_log}" \
      "${A_DIR}/disconnect_retry.log" \
      "${A_DIR}/disconnect_retry_info.log" \
      "${A_DIR}/reconnect_connect.log" \
      "${A_DIR}/reconnect_info.log" \
      "${A_DIR}/reconnect_disconnect.log"
  fi

  sleep 2
  kill "${BTMON_PID}" >/dev/null 2>&1 || true
  wait "${BTMON_PID}" >/dev/null 2>&1 || true
  wait "${SERIAL_PID}" >/dev/null 2>&1 || true

  pair_ok="no"
  btctl_pair_logs=("${btctl_main_log}")
  if [[ "${MODE}" == "bonded-reconnect" ]]; then
    btctl_pair_logs+=("${btctl_reconnect_log}")
  fi
  if rg -q "Pairing successful|Status: Success|Paired: yes|Bonded: yes" "${btctl_pair_logs[@]}"; then
    pair_ok="yes"
  fi

  paired="no"
  if rg -q "Paired:\\s+yes" "${btctl_pair_logs[@]}"; then
    paired="yes"
  fi

  bonded="no"
  if rg -q "Bonded:\\s+yes" "${btctl_pair_logs[@]}"; then
    bonded="yes"
  fi

  enc_ok_count_for_attempt="$((
    $(awk '
      /Encryption Change \(0x08\)/ {window=4; next}
      window > 0 && /Status: Success \(0x00\)/ {count++; window=0; next}
      window > 0 {window--}
      END {print count+0}
    ' "${A_DIR}/btmon.log")
  ))"
  enc_on_count="$(rg -c "encryption=ON" "${A_DIR}/serial.log" || true)"
  enc_ok="no"
  if [[ "${enc_ok_count_for_attempt}" -gt 0 || "${enc_on_count}" -gt 0 ]]; then
    enc_ok="yes"
  fi

  reconnect_connected="n/a"
  reconnect_bonded="n/a"
  reconnect_enc_seen="n/a"
  if [[ "${MODE}" == "bonded-reconnect" ]]; then
    reconnect_connected="$(check_connect_hit "${A_DIR}/reconnect_connect.log")"
    if [[ "${reconnect_connected}" == "no" && "$(check_info_connected "${A_DIR}/reconnect_info.log")" == "yes" ]]; then
      reconnect_connected="yes"
    fi

    reconnect_bonded="no"
    if rg -q "Bonded:\\s+yes|Paired:\\s+yes" "${A_DIR}/reconnect_info.log"; then
      reconnect_bonded="yes"
    fi

    reconnect_enc_seen="no"
    if [[ "${enc_ok_count_for_attempt}" -ge 2 || "${enc_on_count}" -ge 2 ]]; then
      reconnect_enc_seen="yes"
    fi
  fi

  target_trace_error="no"
  if rg -q "ENC_RX_SHORT_PDU|ENC_RX_MIC_FAIL|LL_ENC_REQ_EDIV_RAND_MISMATCH|LL_ENC_REQ_REJECTED|LL_START_ENC_REQ_REJECTED" "${A_DIR}/serial.log"; then
    target_trace_error="yes"
  fi

  mic_fail="no"
  if rg -qi "MIC Failure \\(0x3d\\)|Reason:\\s+MIC Failure \\(0x3d\\)" "${A_DIR}/btmon.log"; then
    mic_fail="yes"
  fi

  host_crash="no"
  if rg -q "Hardware Error \\(0x10\\).*0x0c|Intel Bootup|Reset reason: System exception" "${A_DIR}/btmon.log"; then
    host_crash="yes"
  fi

  host_unstable="no"
  if [[ "${host_crash}" == "yes" ]]; then
    host_unstable="yes"
  elif [[ "$(detect_host_instability_signature "${btctl_pair_logs[@]}")" == "yes" ]]; then
    host_unstable="yes"
  fi

  target_verdict="fail"
  if [[ "${mic_fail}" == "yes" || "${target_trace_error}" == "yes" ]]; then
    target_verdict="fail"
  elif [[ "${MODE}" == "pair-bond" ]]; then
    if [[ "${pair_ok}" == "yes" && "${bonded}" == "yes" && "${enc_ok}" == "yes" ]]; then
      target_verdict="pass"
    elif [[ "${host_unstable}" == "yes" ]]; then
      target_verdict="unknown_host"
    else
      target_verdict="fail"
    fi
  else
    if [[ "${pair_ok}" == "yes" && "${bonded}" == "yes" && "${reconnect_connected}" == "yes" &&
          "${reconnect_bonded}" == "yes" && "${reconnect_enc_seen}" == "yes" ]]; then
      target_verdict="pass"
    elif [[ "${host_unstable}" == "yes" ]]; then
      target_verdict="unknown_host"
    else
      target_verdict="fail"
    fi
  fi

  overall_verdict="fail_target"
  if [[ "${target_verdict}" == "pass" && "${host_unstable}" == "no" ]]; then
    overall_verdict="pass"
  elif [[ "${host_unstable}" == "yes" && "${target_verdict}" != "fail" ]]; then
    overall_verdict="inconclusive_host"
  elif [[ "${host_unstable}" == "yes" && "${mic_fail}" == "no" && "${target_trace_error}" == "no" ]]; then
    overall_verdict="inconclusive_host"
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
  if [[ "${MODE}" == "bonded-reconnect" && "${reconnect_connected}" == "yes" ]]; then
    reconnect_connected_count=$((reconnect_connected_count + 1))
  fi
  if [[ "${MODE}" == "bonded-reconnect" && "${reconnect_bonded}" == "yes" ]]; then
    reconnect_bonded_count=$((reconnect_bonded_count + 1))
  fi
  if [[ "${MODE}" == "bonded-reconnect" && "${reconnect_enc_seen}" == "yes" ]]; then
    reconnect_enc_seen_count=$((reconnect_enc_seen_count + 1))
  fi
  if [[ "${target_trace_error}" == "yes" ]]; then target_trace_error_count=$((target_trace_error_count + 1)); fi
  if [[ "${mic_fail}" == "yes" ]]; then mic_fail_count=$((mic_fail_count + 1)); fi
  if [[ "${host_crash}" == "yes" ]]; then host_crash_count=$((host_crash_count + 1)); fi
  if [[ "${host_unstable}" == "yes" ]]; then host_unstable_count=$((host_unstable_count + 1)); fi

  case "${overall_verdict}" in
    pass)
      pass_count=$((pass_count + 1))
      ;;
    inconclusive_host)
      inconclusive_host_count=$((inconclusive_host_count + 1))
      ;;
    *)
      fail_target_count=$((fail_target_count + 1))
      ;;
  esac

  echo "${attempt},${MODE},${pair_ok},${paired},${bonded},${enc_ok},${enc_ok_count_for_attempt},${reconnect_connected},${reconnect_bonded},${reconnect_enc_seen},${target_trace_error},${mic_fail},${host_crash},${host_unstable},${target_verdict},${overall_verdict},${disconnect_reason}" >> "${CSV}"
done

SUMMARY="${OUTDIR}/summary.txt"
{
  echo "BLE Pair/Bond Regression Summary"
  echo "attempts=${ATTEMPTS}"
  echo "mode=${MODE}"
  echo "pass_count=${pass_count}"
  echo "fail_target_count=${fail_target_count}"
  echo "inconclusive_host_count=${inconclusive_host_count}"
  echo "pair_ok_count=${pair_ok_count}"
  echo "paired_count=${paired_count}"
  echo "bonded_count=${bonded_count}"
  echo "enc_change_success_count=${enc_ok_count}"
  echo "reconnect_connected_count=${reconnect_connected_count}"
  echo "reconnect_bonded_count=${reconnect_bonded_count}"
  echo "reconnect_enc_seen_count=${reconnect_enc_seen_count}"
  echo "target_trace_error_count=${target_trace_error_count}"
  echo "mic_failure_count=${mic_fail_count}"
  echo "host_crash_count=${host_crash_count}"
  echo "host_unstable_count=${host_unstable_count}"
  echo "csv=${CSV}"
} > "${SUMMARY}"

echo "[ble-regression] done"
cat "${SUMMARY}"
