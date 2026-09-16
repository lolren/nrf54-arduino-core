#!/usr/bin/env python3
"""Run host utility sanitizers and validate nRF54 IRQ vector contracts."""

from __future__ import annotations

import hashlib
import os
import re
import runpy
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
TESTS = ROOT / "tests/core_io"

CHIPS = {
    "nrf54l15": {
        "last_irq": 269,
        "vectors": {
            28: "SWI00_IRQHandler",
            70: "AAR00_CCM00_IRQHandler",
            71: "ECB00_IRQHandler",
            74: "SPIM00_IRQHandler",
            133: "TIMER10_IRQHandler",
            135: "EGU10_IRQHandler",
            138: "RADIO_0_IRQHandler",
            139: "RADIO_1_IRQHandler",
            198: "SPIM20_IRQHandler",
            199: "SPIM21_IRQHandler",
            200: "SPIM22_IRQHandler",
            201: "EGU20_IRQHandler",
            202: "TIMER20_IRQHandler",
            203: "TIMER21_IRQHandler",
            204: "TIMER22_IRQHandler",
            205: "TIMER23_IRQHandler",
            206: "TIMER24_IRQHandler",
            208: "PDM20_IRQHandler",
            209: "PDM21_IRQHandler",
            210: "PWM20_IRQHandler",
            211: "PWM21_IRQHandler",
            212: "PWM22_IRQHandler",
            213: "SAADC_IRQHandler",
            214: "NFCT_IRQHandler",
            215: "TEMP_IRQHandler",
            218: "GPIOTE20_0_IRQHandler",
            219: "GPIOTE20_1_IRQHandler",
            221: "I2S20_IRQHandler",
            224: "QDEC20_IRQHandler",
            225: "QDEC21_IRQHandler",
            226: "GRTC_0_IRQHandler",
            227: "GRTC_1_IRQHandler",
            228: "GRTC_2_IRQHandler",
            229: "GRTC_3_IRQHandler",
            260: "SPIM30_IRQHandler",
            261: "CLOCK_POWER_IRQHandler",
            262: "LPCOMP_IRQHandler",
            264: "WDT30_IRQHandler",
            265: "WDT31_IRQHandler",
            268: "GPIOTE30_0_IRQHandler",
            269: "GPIOTE30_1_IRQHandler",
        },
        "irqs": {
            "SWI00": 28,
            "AAR00_CCM00": 70,
            "ECB00": 71,
            "SPIM00": 74,
            "TIMER10": 133,
            "SPIM20": 198,
            "SPIM21": 199,
            "SPIM22": 200,
            "SAADC": 213,
            "NFCT": 214,
            "GPIOTE20_0": 218,
            "GPIOTE20_1": 219,
            "I2S20": 221,
            "SPIM30": 260,
            "CLOCK_POWER": 261,
            "LPCOMP": 262,
            "GPIOTE30_0": 268,
            "GPIOTE30_1": 269,
        },
    },
    "nrf54lm20b": {
        "last_irq": 270,
        "vectors": {
            28: "SWI00_IRQHandler",
            74: "AAR00_CCM00_IRQHandler",
            75: "ECB00_IRQHandler",
            77: "SPIM00_IRQHandler",
            133: "TIMER10_IRQHandler",
            135: "EGU10_IRQHandler",
            138: "RADIO_0_IRQHandler",
            139: "RADIO_1_IRQHandler",
            198: "SPIM20_IRQHandler",
            199: "SPIM21_IRQHandler",
            200: "SPIM22_IRQHandler",
            201: "EGU20_IRQHandler",
            202: "TIMER20_IRQHandler",
            203: "TIMER21_IRQHandler",
            204: "TIMER22_IRQHandler",
            205: "TIMER23_IRQHandler",
            206: "TIMER24_IRQHandler",
            208: "PDM20_IRQHandler",
            209: "PDM21_IRQHandler",
            210: "PWM20_IRQHandler",
            211: "PWM21_IRQHandler",
            212: "PWM22_IRQHandler",
            213: "SAADC_IRQHandler",
            214: "NFCT_IRQHandler",
            215: "TEMP_IRQHandler",
            218: "GPIOTE20_0_IRQHandler",
            219: "GPIOTE20_1_IRQHandler",
            224: "QDEC20_IRQHandler",
            225: "QDEC21_IRQHandler",
            226: "GRTC_0_IRQHandler",
            227: "GRTC_1_IRQHandler",
            228: "GRTC_2_IRQHandler",
            229: "GRTC_3_IRQHandler",
            237: "SPIM23_IRQHandler",
            260: "SPIM30_IRQHandler",
            262: "LPCOMP_IRQHandler",
            264: "WDT30_IRQHandler",
            265: "WDT31_IRQHandler",
            268: "GPIOTE30_0_IRQHandler",
            269: "GPIOTE30_1_IRQHandler",
            270: "CLOCK_POWER_IRQHandler",
        },
        "irqs": {
            "SWI00": 28,
            "AAR00_CCM00": 74,
            "ECB00": 75,
            "SPIM00": 77,
            "TIMER10": 133,
            "SPIM20": 198,
            "SPIM21": 199,
            "SPIM22": 200,
            "SAADC": 213,
            "NFCT": 214,
            "GPIOTE20_0": 218,
            "GPIOTE20_1": 219,
            "SPIM23": 237,
            "SPIM30": 260,
            "LPCOMP": 262,
            "GPIOTE30_0": 268,
            "GPIOTE30_1": 269,
            "CLOCK_POWER": 270,
        },
    },
}


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def vector_entries(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    label = next(i for i, line in enumerate(lines) if line.strip() == "g_pfnVectors:")
    size = next(
        i for i, line in enumerate(lines[label + 1 :], label + 1)
        if line.strip().startswith(".size g_pfnVectors")
    )
    entries: list[str] = []
    index = label + 1
    while index < size:
        code = lines[index].split("/*", 1)[0].strip()
        if code.startswith(".word"):
            entries.append(code.split()[1])
        elif code.startswith(".rept"):
            count = int(code.split()[1], 0)
            index += 1
            while index < size and not lines[index].strip().startswith(".word"):
                index += 1
            if index >= size:
                raise AssertionError(f"unterminated .rept in {path}")
            word = lines[index].split("/*", 1)[0].strip().split()[1]
            entries.extend([word] * count)
            while index < size and not lines[index].strip().startswith(".endr"):
                index += 1
            if index >= size:
                raise AssertionError(f"missing .endr in {path}")
        index += 1
    return entries


def validate_vectors() -> None:
    for chip, contract in CHIPS.items():
        core = PLATFORM / "cores" / chip
        startup = core / f"startup_{chip}.S"
        cmsis = core / "cmsis.h"
        entries = vector_entries(startup)
        expected_count = 16 + int(contract["last_irq"]) + 1
        assert len(entries) == expected_count, (
            f"{chip}: {len(entries)} vector entries, expected {expected_count}"
        )
        for irq, handler in contract["vectors"].items():
            actual = entries[16 + irq]
            assert actual == handler, f"{chip} IRQ {irq}: {actual}, expected {handler}"

        cmsis_text = cmsis.read_text(encoding="utf-8")
        for name, irq in contract["irqs"].items():
            match = re.search(rf"\b{name}_IRQn\s*=\s*(-?\d+)", cmsis_text)
            assert match is not None, f"{chip}: missing {name}_IRQn"
            assert int(match.group(1)) == irq, (
                f"{chip} {name}_IRQn={match.group(1)}, expected {irq}"
            )

        if chip == "nrf54lm20b":
            assert entries[16 + 221] == "Default_Handler"
            assert "I2S20_IRQn" not in cmsis_text
            assert all("I2S20" not in entry for entry in entries)
        print(f"PASS {chip} vectors: {len(entries)} entries")


def validate_cmsis_priority_contracts() -> None:
    for chip in CHIPS:
        cmsis_text = (PLATFORM / "cores" / chip / "cmsis.h").read_text(
            encoding="utf-8"
        )
        assert "__NVIC_SystemPriorityByte" in cmsis_text
        set_body = function_body(cmsis_text, "static inline void __NVIC_SetPriority(")
        get_body = function_body(cmsis_text, "static inline uint32_t __NVIC_GetPriority(")
        assert "0xE000ED18UL" in cmsis_text
        assert "__NVIC_SystemPriorityByte(IRQn)" in set_body
        assert "__NVIC_SystemPriorityByte(IRQn)" in get_body
        print(f"PASS {chip} CMSIS system-exception priority contract")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def validate_hardware_serial_contracts() -> None:
    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "HardwareSerial.cpp").read_text(
            encoding="utf-8"
        )
        baud_body = function_body(source, "static uint32_t baud_to_reg(")
        assert "static_cast<uint64_t>(baud) << 32U" in baud_body
        assert "kPresets[bestIndex].baud) << 32U" not in baud_body

        end_body = function_body(source, "void HardwareSerial::end()")
        assert end_body.index("drainTxForShutdown()") < end_body.index(
            "U_TASKS_DMA_TX_STOP"
        )
        drain_body = function_body(source, "bool HardwareSerial::drainTxForShutdown()")
        assert "serial_byte_timeout_us" in drain_body
        assert "serviceTxDma()" in drain_body

        route_body = function_body(source, "static bool uart_route_valid(")
        assert "tx == 2U && rx == 0U" in route_body
        assert "tx == 8U && rx == 7U" in route_body
        nfc_release_body = function_body(
            source, "static void release_nfc_pads_for_gpio("
        )
        nfc_pad_body = function_body(source, "static bool is_nfc_pad(")
        expected_nfc_pins = (
            "pin == 2U || pin == 3U"
            if chip == "nrf54l15"
            else "pin == 1U || pin == 2U"
        )
        assert expected_nfc_pins in nfc_pad_body
        assert "is_nfc_pad(txPort, tx) || is_nfc_pad(rxPort, rx)" in nfc_release_body
        assert "NRF_NFCT->PADCONFIG" in nfc_release_body
        assert "NFCT_PADCONFIG_ENABLE_Disabled" in nfc_release_body
        assert source.count(
            "release_nfc_pads_for_gpio(txPort, tx, rxPort, rx);"
        ) == 2
        digital_source = (PLATFORM / "cores" / chip / "wiring_digital.c").read_text(
            encoding="utf-8"
        )
        if chip == "nrf54l15":
            assert "NRF54L15_CLEAN_SERIAL_BRIDGE_AUTO" in source
            assert "bridge_uart_is_present" in source
            assert "configure_pin_high_z(txPort, tx);" in source
            assert "_bridgeDeferred" in source
        digital_nfc_body = function_body(
            digital_source, "static void release_nfc_pad_for_gpio("
        )
        assert expected_nfc_pins.replace("pin", "d->pin") in digital_nfc_body
        assert "NRF_NFCT->PADCONFIG" in digital_nfc_body
        assert "NFCT_PADCONFIG_ENABLE_Disabled" in digital_nfc_body
        pin_mode_body = function_body(digital_source, "void pinMode(")
        assert "release_nfc_pad_for_gpio(&d);" in pin_mode_body
        interrupt_body = function_body(
            digital_source, "static void configure_pin_for_interrupt("
        )
        assert "release_nfc_pad_for_gpio(d);" in interrupt_body
        print(
            f"PASS {chip} serial shutdown, high-baud, route, and NFC-pad contracts"
        )


def validate_pca10156_serial_route_contracts() -> None:
    variant = (
        PLATFORM / "variants/nrf54l15dk_pca10156/pins_arduino.h"
    ).read_text(encoding="utf-8")
    assert "#define PIN_SERIAL_TX  PIN_P1_04" in variant
    assert "#define PIN_SERIAL_RX  PIN_P1_05" in variant
    assert "#define PIN_SERIAL1_TX PIN_P1_02" in variant
    assert "#define PIN_SERIAL1_RX PIN_P1_03" in variant
    assert "#define PIN_SERIAL1_TX PIN_SERIAL_TX" not in variant
    assert "#define PIN_SERIAL1_RX PIN_SERIAL_RX" not in variant

    serial_source = (PLATFORM / "cores/nrf54l15/HardwareSerial.cpp").read_text(
        encoding="utf-8"
    )
    assert "HardwareSerial Serial(NRF_UARTE20, PIN_SERIAL_TX, PIN_SERIAL_RX);" in serial_source
    assert "HardwareSerial Serial1(NRF_UARTE21, PIN_SERIAL1_TX, PIN_SERIAL1_RX);" in serial_source
    route_body = function_body(serial_source, "static bool uart_route_valid(")
    assert "if (txPort == 1U) {\n        return true;\n    }" in route_body
    print("PASS PCA10156 Serial and Serial1 default routes are independent")


def validate_thread_crypto_build_contracts() -> None:
    source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/matter_pbkdf2.cpp"
    ).read_text(encoding="utf-8")
    guard = source[: source.index('#include "matter_pbkdf2.h"')]
    assert "NRF54L15_CLEAN_MATTER_CORE_ENABLE" in guard
    assert "NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE" in guard
    assert "||" in guard
    print("PASS PBKDF2 implementation is available to Matter and Thread builds")


def validate_ble_disconnect_reason_contracts() -> None:
    source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_central_event.inc"
    ).read_text(encoding="utf-8")
    body = function_body(
        source, "bool BleRadio::pollCentralConnectionEvent(BleConnectionEvent* event,"
    )
    assert "bool terminateMicFailure = false;" in body
    assert body.count("terminateMicFailure = true;") == 4
    assert "terminateMicFailure\n            ? BleDisconnectReason::kMicFailure" in body
    assert "peerTerminateIndReceived\n                   ? BleDisconnectReason::kPeerTerminate" in body
    assert "localTerminateSent ? BleDisconnectReason::kApi" in body
    assert ": BleDisconnectReason::kInternalTerminate" in body
    assert "localTerminateSent ? localTerminateErrorCode" in body
    assert ": peerTerminateErrorCode" in body
    print("PASS BLE central MIC, peer, and internal disconnect classification")


def validate_ble_connection_parameter_contracts() -> None:
    parts = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
    )
    connection_api = (parts / "nrf54l15_hal_ble_connection_api.inc").read_text(
        encoding="utf-8"
    )
    crypto_service = (
        parts / "nrf54l15_hal_internal_crypto_service.inc"
    ).read_text(encoding="utf-8")
    access_address_validator = function_body(
        crypto_service, "bool isValidBleConnectionAccessAddress("
    )
    for invalid_contract in (
        "accessAddress == kBleAccessAddress",
        "accessAddress == 0U",
        "accessAddress == 0xFFFFFFFFUL",
        "static_cast<uint8_t>(accessAddress >> 8U) == octet0",
        "diffBits <= 1U",
        "maxRunLength > 6U",
        "transitions > 24U",
        "__builtin_popcount(accessAddress & 0xFFU) < 3",
        "low16Transitions > 11U",
        "topTransitions >= 2U",
    ):
        assert invalid_contract in access_address_validator

    def host_access_address_valid(access_address: int) -> bool:
        access_address &= 0xFFFFFFFF
        if access_address in (0x8E89BED6, 0x00000000, 0xFFFFFFFF):
            return False
        octet0 = access_address & 0xFF
        if all(((access_address >> shift) & 0xFF) == octet0 for shift in (8, 16, 24)):
            return False
        if ((access_address ^ 0x8E89BED6) & 0xFFFFFFFF).bit_count() <= 1:
            return False

        bits = [(access_address >> bit) & 1 for bit in range(32)]
        transitions = sum(bits[bit] != bits[bit - 1] for bit in range(1, 32))
        max_run_length = 1
        run_length = 1
        for bit in range(1, 32):
            if bits[bit] == bits[bit - 1]:
                run_length += 1
                max_run_length = max(max_run_length, run_length)
            else:
                run_length = 1
        if max_run_length > 6 or transitions > 24:
            return False
        if (access_address & 0xFF).bit_count() < 3:
            return False
        if sum(bits[bit] != bits[bit - 1] for bit in range(1, 16)) > 11:
            return False
        return sum(bits[bit] != bits[bit - 1] for bit in range(27, 32)) >= 2

    for access_address in (0xAF9A8C69, 0xD7A46C5B, 0xC59A3B6D):
        assert host_access_address_valid(access_address)
    for access_address in (
        0x8E89BED6,
        0x00000000,
        0xFFFFFFFF,
        0x703A0788,  # only two ones in the least-significant octet
        0x87985AA5,  # thirteen transitions in the least-significant 16 bits
    ):
        assert not host_access_address_valid(access_address)

    access_address_generator = function_body(
        crypto_service, "uint32_t generateBleConnectionAccessAddress("
    )
    assert access_address_generator.count("isValidBleConnectionAccessAddress(") == 1
    assert "kFallbackCandidates" not in access_address_generator
    assert "return 0U;" in access_address_generator
    pseudo_random_word = function_body(
        crypto_service, "uint32_t nextPseudoRandomWord("
    )
    assert "0xA5A55A5AUL" not in pseudo_random_word
    for signature in (
        "bool BleRadio::initiateConnection(",
        "bool BleRadio::initiateConnectionBudgeted(",
    ):
        body = function_body(connection_api, signature)
        assert "connParamsAreValid(intervalUnits, intervalUnits, 0U," in body
        assert "hopIncrement < 5U || hopIncrement > 16U" in body

    scanning = (
        parts / "nrf54l15_hal_ble_scanning_connections.inc"
    ).read_text(encoding="utf-8")
    for signature in (
        "bool BleRadio::initiateConnectionOnce(",
        "bool BleRadio::initiateConnectionOnceBudgeted(",
    ):
        body = function_body(scanning, signature)
        assert "connParamsAreValid(intervalUnits, intervalUnits, 0U," in body
        assert body.index("connParamsAreValid(") < body.index(
            "setAdvertisingChannel(channel)"
        )
        assert body.count("fillBleSecurityRandomBytes(") == 1
        assert "fillPseudoRandomBytes(" not in body
        assert "CENTRAL_CONNECT_ENTROPY_UNAVAILABLE" in body
        assert body.index("fillBleSecurityRandomBytes(") < body.index(
            "setAdvertisingChannel(channel)"
        )
        assert body.index("fillBleSecurityRandomBytes(") < body.index(
            "receivePacketOnCurrentChannel"
        )
        assert "generateBleConnectionAccessAddress(accessAddressSeed)" in body
        assert "if (accessAddress == 0U)" in body
        assert "CENTRAL_CONNECT_AA_GENERATION_FAILED" in body
        assert body.index("generateBleConnectionAccessAddress(") < body.index(
            "writeLe32(&llData[0], accessAddress)"
        )
        assert body.index("receivePacketOnCurrentChannel") < body.index(
            "generateBleConnectionAccessAddress("
        )
        assert "readLe24(&connectionEntropy[4])" in body
        assert "crcInit ^=" not in body

    peripheral_start = function_body(
        scanning, "bool BleRadio::startConnectionFromConnectInd("
    )
    first_state_mutation = peripheral_start.index("memcpy(connectionPeerAddress_")
    required_precommit_checks = (
        "length != 34U",
        "connParamsAreValid(intervalUnits, intervalUnits, latency,",
        "winSize < 1U || winSize > maxWinSize",
        "winOffset > intervalUnits",
        "(channelMap[4] & 0xE0U) != 0U",
        "bitCount37(channelMap) < 2U",
        "hopIncrement < 5U || hopIncrement > 16U",
        "!isValidBleConnectionAccessAddress(accessAddress)",
    )
    for check in required_precommit_checks:
        assert check in peripheral_start
        assert peripheral_start.index(check) < first_state_mutation
    assert "minU16(8U" in peripheral_start
    assert "intervalUnits - 1U" in peripheral_start
    assert "winOffset >= intervalUnits" not in peripheral_start
    assert peripheral_start.index("winOffset > intervalUnits") < peripheral_start.index(
        "g_ble_periph_connect_win_offset = winOffset;"
    )

    central_start = function_body(
        scanning, "bool BleRadio::startCentralConnection("
    )
    assert "connParamsAreValid(intervalUnits, intervalUnits, 0U," in central_start
    assert "!isValidBleConnectionAccessAddress(accessAddress)" in central_start
    assert central_start.index("connParamsAreValid(") < central_start.index(
        "memcpy(connectionPeerAddress_"
    )

    ll_security = (
        parts / "nrf54l15_hal_ble_ll_security.inc"
    ).read_text(encoding="utf-8")
    ll_control = function_body(
        ll_security, "bool BleRadio::buildLlControlResponse("
    )
    conn_update = ll_control[ll_control.index("case kBleLlCtrlConnectionUpdateInd:") :]
    conn_update = conn_update[: conn_update.index("case kBleLlCtrlChannelMapInd:")]
    assert "winSize < 1U || winSize > maxWinSize" in conn_update
    assert "winOffset > interval" in conn_update
    assert "winOffset >= interval" not in conn_update
    assert "minU16(8U" in conn_update
    assert conn_update.index("winOffset > interval") < conn_update.index(
        "connectionPendingWinOffset_ = winOffset;"
    )
    channel_map = ll_control[ll_control.index("case kBleLlCtrlChannelMapInd:") :]
    channel_map = channel_map[: channel_map.index("case kBleLlCtrlEncReq:")]
    assert "reservedBitsClear = (map[4] & 0xE0U) == 0U" in channel_map
    assert "map[4] &= 0x1FU" not in channel_map
    print(
        "PASS BLE access addresses and connection windows validate before "
        "transmit/state mutation"
    )


def validate_ble_persistence_layout_contracts() -> None:
    source_root = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
    bond_store = (
        source_root / "nrf54l15_hal_parts/nrf54l15_hal_internal_gatt_bond.inc"
    ).read_text(encoding="utf-8")

    for contract in (
        "struct BleBondFlashBlob",
        "struct BleBondSigningBlob",
        "sizeof(BleBondFlashBlob) == 80U",
        "sizeof(BleBondSigningState) == 44U",
        "sizeof(BleBondSigningBlob) == 56U",
        'section(".bond_base_storage")',
        'section(".bond_cccd_storage")',
        'section(".bond_signing_storage")',
    ):
        assert contract in bond_store

    signing_write = function_body(
        bond_store, "bool writeFlashBondSigningState("
    )
    assert "base.magic != kBleBondRetentionMagic" in signing_write
    assert "base.version != 2U" in signing_write
    assert "base.crc32" in signing_write
    assert "memcmp(&base.record, &expectedBase" in signing_write
    assert "writeFlashSigningBlob(signing)" in signing_write

    flash_read = function_body(bond_store, "bool readFlashBondRecord(")
    assert "signing.baseRecordCrc32 == base.crc32" in flash_read
    assert "signing.signingStateCrc32" in flash_read
    assert "signingStateLooksSane(signing.state)" in flash_read
    assert "applySigningState(signing.state, outRecord);" in flash_read

    slot_layout = (
        (".bond_storage ORIGIN(FLASH_BOND) (NOLOAD)", 0x000, 0x050),
        (".bond_cccd_storage ORIGIN(FLASH_BOND) + 0x50 (NOLOAD)", 0x050, 0x08C),
        (".prefs_storage ORIGIN(FLASH_BOND) + 0xDC (NOLOAD)", 0x0DC, 0xAE0),
        (".eeprom_storage ORIGIN(FLASH_BOND) + 0xBBC (NOLOAD)", 0xBBC, 0x40C),
        (".bond_signing_storage ORIGIN(FLASH_BOND) + 0xFC8 (NOLOAD)", 0xFC8, 0x038),
    )
    assert all(
        offset + size == slot_layout[index + 1][1]
        for index, (_, offset, size) in enumerate(slot_layout[:-1])
    )
    assert slot_layout[-1][1] + slot_layout[-1][2] == 0x1000
    linker_paths = (
        PLATFORM / "cores/nrf54l15/nrf54l15_linker_script.ld",
        PLATFORM / "cores/nrf54l15/nrf54l15_linker_script_no_vpr.ld",
        PLATFORM / "cores/nrf54l15/nrf54lm20b_linker_script.ld",
        PLATFORM / "cores/nrf54lm20b/nrf54lm20b_linker_script.ld",
    )
    for linker_path in linker_paths:
        linker = linker_path.read_text(encoding="utf-8")
        assert re.search(r"FLASH_BOND\s*\([^)]*\).*LENGTH\s*=\s*0x1000", linker)
        previous = -1
        for declaration, _, size in slot_layout:
            position = linker.index(declaration)
            assert position > previous
            previous = position
            section_name = declaration.split()[0]
            assert f"ASSERT(SIZEOF({section_name}) <= 0x{size:X}" in linker
        assert "KEEP(*(.bond_base_storage))" in linker
        assert "KEEP(*(.bond_cccd_storage))" in linker
        assert "KEEP(*(.bond_signing_storage))" in linker

    print("PASS fixed BLE bond/CCCD/preferences/EEPROM/signing flash layout")


def validate_custom_gatt_initial_value_capacity_contracts() -> None:
    source_root = (
        PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
    )
    header = (source_root / "nrf54l15_hal.h").read_text(encoding="utf-8")
    implementation = (
        source_root / "nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc"
    ).read_text(encoding="utf-8")

    signature_pattern = re.compile(
        r"\bbool\s+(?:BleRadio::)?"
        r"(addCustomGattCharacteristic[A-Za-z0-9_]*)"
        r"\s*\(([^;{}]*)\)\s*(?:;|\{)",
        re.DOTALL,
    )
    expected_header = {
        "addCustomGattCharacteristic": 2,
        "addCustomGattCharacteristicWithDescriptors": 1,
        "addCustomGattCharacteristic128": 2,
        "addCustomGattCharacteristic128WithDescriptors": 1,
        "addCustomGattCharacteristicCommon": 1,
        "addCustomGattCharacteristicUuid": 1,
    }
    expected_implementation = expected_header | {
        "addCustomGattCharacteristicUuid": 0,
    }

    for label, source, expected in (
        ("header", header, expected_header),
        ("implementation", implementation, expected_implementation),
    ):
        signatures = signature_pattern.findall(source)
        actual = {
            name: sum(1 for signature_name, _ in signatures if signature_name == name)
            for name in expected
        }
        assert actual == expected, f"custom GATT {label} signatures changed: {actual}"
        assert len(signatures) == sum(expected.values())
        for name, parameters in signatures:
            normalized = " ".join(parameters.split())
            assert "uint16_t initialValueLength" in normalized, (
                f"{label} {name} narrows the initial value length: {normalized}"
            )
            assert "uint8_t initialValueLength" not in normalized

    max_length_match = re.search(
        r"static constexpr uint16_t\s+kCustomGattMaxValueLength\s*=\s*(\d+)U;",
        header,
    )
    assert max_length_match is not None
    max_length = int(max_length_match.group(1))
    assert max_length == 512
    assert max_length > 0xFF

    common_body = function_body(
        implementation, "bool BleRadio::addCustomGattCharacteristicCommon("
    )
    assert "maxValueLength > kCustomGattMaxValueLength" in common_body
    assert "initialValueLength > maxValueLength" in common_body
    assert "fixedValueLength && initialValueLength != 0U" in common_body
    assert "initialValueLength != maxValueLength" in common_body
    assert "characteristic.maxValueLength = maxValueLength;" in common_body
    assert "characteristic.fixedValueLength = fixedValueLength;" in common_body
    assert "memcpy(characteristic.value, initialValue, initialValueLength);" in common_body
    assert "uint16_t maxValueLength;" in header
    assert "bool fixedValueLength;" in header
    assert "uint16_t valueLength;" in header
    assert "uint8_t value[kCustomGattMaxValueLength];" in header

    set_value_body = function_body(
        implementation, "bool BleRadio::setCustomGattCharacteristicValue("
    )
    assert "characteristic->fixedValueLength" in set_value_body
    assert "valueLength == characteristic->maxValueLength" in set_value_body
    assert "valueLength <= characteristic->maxValueLength" in set_value_body
    peer_write_body = function_body(
        implementation, "bool BleRadio::applyCustomGattCharacteristicValueWrite("
    )
    assert "valueTarget->fixedValueLength" in peer_write_body
    assert "valueLength == valueTarget->maxValueLength" in peer_write_body
    assert "valueLength <= valueTarget->maxValueLength" in peer_write_body

    permission_check = function_body(
        implementation, "bool BleRadio::customGattPermissionSatisfied("
    )
    lesc_permission = permission_check[
        permission_check.index("case kBleGattPermEncWithLescMitm:") :
    ]
    lesc_permission = lesc_permission[
        : lesc_permission.index("case kBleGattPermSignedNoMitm:")
    ]
    assert "!isConnectionEncrypted()" in lesc_permission
    assert (
        "isConnectionAuthenticated() && connectionEncSecureConnections_"
        in lesc_permission
    )
    assert "kAttErrInsufficientAuthentication" in lesc_permission

    att = (
        source_root / "nrf54l15_hal_parts/nrf54l15_hal_ble_att_l2cap.inc"
    ).read_text(encoding="utf-8")
    prepare_write = att[att.index("case kAttOpPrepareWriteReq:") :]
    prepare_write = prepare_write[: prepare_write.index("case kAttOpExecuteWriteReq:")]
    assert "? customValueTarget->maxValueLength" in prepare_write
    enqueue_signature = re.search(
        r"enqueueCustomGattNotification\s*\([^;]*uint16_t\s+valueLength\s*\);",
        header,
        re.DOTALL,
    )
    assert enqueue_signature is not None
    enqueue_body = function_body(
        implementation, "bool BleRadio::enqueueCustomGattNotification("
    )
    capacity_guard = "valueLength > maxNotificationValueLength()"
    narrowing_store = "slot.valueLength = static_cast<uint8_t>(valueLength);"
    assert capacity_guard in enqueue_body
    assert narrowing_store in enqueue_body
    assert enqueue_body.index(capacity_guard) < enqueue_body.index(narrowing_store)
    notify_body = function_body(
        implementation, "bool BleRadio::notifyCustomGattCharacteristic("
    )
    assert "customGattHvxPermissionSatisfied(characteristic)" in notify_body
    assert notify_body.index(
        "customGattHvxPermissionSatisfied(characteristic)"
    ) < notify_body.index("enqueueCustomGattNotification(")
    assert "customGattHvxPermissionSatisfied(&pendingCharacteristic)" in notify_body
    hvx_permission = function_body(
        implementation, "bool BleRadio::customGattHvxPermissionSatisfied("
    )
    assert "readPermission == kBleGattPermNoAccess" in hvx_permission
    assert "customGattReadPermissionSatisfied(characteristic, nullptr)" in hvx_permission
    peripheral_tx = (
        source_root / "nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc"
    ).read_text(encoding="utf-8")
    peripheral_tail = (
        source_root / "nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tail.inc"
    ).read_text(encoding="utf-8")
    assert "customGattHvxPermissionSatisfied(&custom)" in peripheral_tx
    assert "customGattHvxPermissionSatisfied(&custom)" in peripheral_tail
    bluefruit = (
        PLATFORM / "libraries/Bluefruit52Lib/src/bluefruit.cpp"
    ).read_text(encoding="utf-8")
    bluefruit_begin = function_body(bluefruit, "err_t BLECharacteristic::begin()")
    assert "inheritServiceSecurity(_read_perm, _service->_read_perm)" in bluefruit_begin
    assert "inheritServiceSecurity(_write_perm, _service->_write_perm)" in bluefruit_begin
    assert "!securityModesCanInherit(_read_perm, _service->_read_perm)" in bluefruit_begin
    assert "!securityModesCanInherit(_write_perm, _service->_write_perm)" in bluefruit_begin
    assert "return ERROR_INVALID_PARAM;" in bluefruit_begin
    assert "descriptors.maxValueLength = _max_len;" in bluefruit_begin
    assert "descriptors.fixedValueLength = _fixed_len;" in bluefruit_begin
    assert "setCustomGattAuthorization(" in bluefruit_begin
    assert "_rd_authorize_cb != nullptr" in bluefruit_begin
    assert "_wr_authorize_cb != nullptr" in bluefruit_begin
    assert "return ERROR_NOT_SUPPORTED;" not in bluefruit_begin
    assert "min<uint16_t>(clampValueLen(_value_len), _max_len)" in bluefruit_begin
    assert "0xFFU" not in bluefruit_begin

    set_fixed = function_body(bluefruit, "void BLECharacteristic::setFixedLen(")
    assert "if (fixed_len == 0U)" in set_fixed
    assert "_fixed_len = false;" in set_fixed
    set_buffer = function_body(bluefruit, "void BLECharacteristic::setBuffer(")
    assert "setMaxLen(bufsize);" in set_buffer
    local_write = function_body(bluefruit, "uint16_t BLECharacteristic::write(const void*")
    assert "_fixed_len ? _max_len" in local_write
    assert "min<uint16_t>(clampValueLen(len), _max_len)" in local_write
    assert "service security must raise an open characteristic" in bluefruit
    assert "a stronger characteristic permission must be preserved" in bluefruit
    assert "service inheritance must not open a disabled operation" in bluefruit
    assert "a disabled service operation must remain disabled" in bluefruit
    assert "signed and encrypted requirements must not be ordered" in bluefruit
    assert "incompatible security modes must fail closed" in bluefruit
    print(
        "PASS custom GATT max/fixed lengths, service-security inheritance, "
        "secure HVX, LESC permissions, authorization registration, and notification narrowing"
    )


def validate_phy_preserves_data_length_contract() -> None:
    implementation = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc"
    ).read_text(encoding="utf-8")
    body = function_body(
        implementation, "bool BleRadio::applyPendingConnectionPhyUpdateAtInstant("
    )
    assert "connectionMaxTxPayloadLength_ = kBleDefaultDataPduMaxPayload" not in body
    assert "connectionMaxRxPayloadLength_ = kBleDefaultDataPduMaxPayload" not in body
    assert "connectionDataLengthUpdatePending_ = true" not in body
    peripheral_probe = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Connections/Ble2MPhyProbe/Ble2MPhyProbe.ino"
    ).read_text(encoding="utf-8")
    central_probe = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Connections/Ble2MPhyCentralProbe/Ble2MPhyCentralProbe.ino"
    ).read_text(encoding="utf-8")
    handle_write = function_body(central_probe, "static void handleWriteResponse()")
    assert "g_phyRequestIssued = g_ble.requestPHY(kBlePhy2M)" in handle_write
    initial_phase = peripheral_probe.split(
        "case PhyCyclePhase::kRequestInitial2M:", 1
    )[1].split("case PhyCyclePhase::kWaitForFallback1M:", 1)[0]
    assert "maybeQueuePhyRequest" not in initial_phase
    return_phase = peripheral_probe.split(
        "case PhyCyclePhase::kRequestReturn2M:", 1
    )[1].split("case PhyCyclePhase::kComplete:", 1)[0]
    assert "maybeQueuePhyRequest" not in return_phase
    assert "request phy 2M return: queued" in central_probe
    assert "kBlePhy1M | kBlePhy2M" in peripheral_probe
    print("PASS PHY updates preserve negotiated LL data length")


def validate_parser_output_validity_contracts() -> None:
    source_root = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
    zigbee_stack = (source_root / "zigbee_stack.cpp").read_text(encoding="utf-8")
    zigbee_security = (source_root / "zigbee_security.cpp").read_text(
        encoding="utf-8"
    )
    vpr = (source_root / "nrf54l15_vpr.cpp").read_text(encoding="utf-8")

    simple_parsers = (
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseMacFrame(",
            "*outFrame = ZigbeeMacFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseNwkFrame(",
            "*outFrame = ZigbeeNetworkFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseApsDataFrame(",
            "*outFrame = ZigbeeApsDataFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseApsAcknowledgementFrame(",
            "*outFrame = ZigbeeApsAcknowledgementFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseApsTransportKeyCommand(",
            "*outKey = ZigbeeApsTransportKey{};",
            "outKey->valid = true;",
        ),
        (
            zigbee_security,
            "bool ZigbeeSecurity::parseNwkSecurityHeader(",
            "*outHeader = ZigbeeNwkSecurityHeader{};",
            "outHeader->valid = true;",
        ),
        (
            zigbee_security,
            "bool ZigbeeSecurity::parseApsSecurityHeader(",
            "*outHeader = ZigbeeApsSecurityHeader{};",
            "outHeader->valid = true;",
        ),
    )
    for source, signature, reset, valid_commit in simple_parsers:
        body = function_body(source, signature)
        assert reset in body, f"{signature} does not restore typed defaults"
        assert body.count(valid_commit) == 1
        assert body.rindex(valid_commit) > body.rfind("return false;")

    beacon_body = function_body(
        zigbee_stack, "bool ZigbeeCodec::parseBeaconFrame("
    )
    assert "*outView = ZigbeeMacBeaconView{};" in beacon_body
    assert (
        "outView->network.panCoordinator = outView->panCoordinator;"
        in beacon_body
    )
    assert (
        "outView->network.associationPermit = outView->associationPermit;"
        in beacon_body
    )

    secured_parsers = (
        (
            "bool ZigbeeSecurity::parseSecuredNwkFrame(",
            "ZigbeeNetworkFrame parsedFrame{};",
            "ZigbeeNwkSecurityHeader parsedSecurity{};",
        ),
        (
            "bool ZigbeeSecurity::parseSecuredApsCommandFrame(",
            "ZigbeeApsCommandFrame parsedFrame{};",
            "ZigbeeApsSecurityHeader parsedSecurity{};",
        ),
    )
    for signature, frame_candidate, security_candidate in secured_parsers:
        body = function_body(zigbee_security, signature)
        assert frame_candidate in body
        assert security_candidate in body
        decrypt = body.index("decryptCcmStar(")
        last_failure = body.rfind("return false;")
        assert body.rindex("parsedFrame.valid = true;") > max(decrypt, last_failure)
        assert body.rindex("*outFrame = parsedFrame;") > max(decrypt, last_failure)
        assert body.rindex("*outSecurity = parsedSecurity;") > max(
            decrypt, last_failure
        )
        assert "outFrame->valid = true;" not in body

    transport_body = function_body(
        zigbee_security,
        "bool ZigbeeSecurity::parseSecuredApsTransportKeyCommand(",
    )
    assert "ZigbeeApsSecurityHeader parsedSecurity{};" in transport_body
    assert "&parsedSecurity," in transport_body
    assert transport_body.rindex("outTransportKey->valid = true;") > (
        transport_body.rfind("return false;")
    )
    assert transport_body.rindex("*outSecurity = parsedSecurity;") > (
        transport_body.rfind("return false;")
    )

    for signature in (
        "bool ZigbeeSecurity::parseSecuredApsUpdateDeviceCommand(",
        "bool ZigbeeSecurity::parseSecuredApsSwitchKeyCommand(",
    ):
        body = function_body(zigbee_security, signature)
        assert "*outSecurity = ZigbeeApsSecurityHeader{};" in body
        assert "ZigbeeApsSecurityHeader parsedSecurity{};" in body
        assert "&parsedSecurity," in body
        assert body.rindex("*outSecurity = parsedSecurity;") > body.rfind(
            "return false;"
        )

    vpr_body = function_body(
        vpr,
        "bool VprControllerServiceHost::readBleCsWorkflowCompletedResult(",
    )
    assert "*result = VprBleCsCompletedResultPayload{};" in vpr_body
    assert vpr_body.index("parsed.valid = payload[2] != 0U;") > vpr_body.index(
        "parsed.payloadLen > VprBleCsCompletedResultPayload::kMaxPayloadBytes"
    )
    assert vpr_body.rindex("*result = parsed;") > vpr_body.rfind("return false;")
    print("PASS malformed parser outputs remain invalid until fully accepted")


def validate_zigbee_persistence_reset_contracts() -> None:
    source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/zigbee_persistence.cpp"
    ).read_text(encoding="utf-8")
    assert "std::is_trivially_copyable<ZigbeePersistentState>::value" in source
    reset_body = function_body(source, "void resetPersistentState(")
    assert "memset(static_cast<void*>(state), 0, sizeof(*state));" in reset_body
    assert "reporting = ZigbeeReportingConfiguration{};" in reset_body
    assert "binding = ZigbeeBindingEntry{};" in reset_body

    load_body = function_body(source, "bool loadChunkedState(")
    initialize_body = function_body(
        source, "void ZigbeePersistentStateStore::initialize("
    )
    assert "resetPersistentState(outState);" in load_body
    assert "resetPersistentState(state);" in initialize_body
    print("PASS Zigbee persisted state keeps deterministic padding and typed defaults")


def validate_protocol_typed_reset_contracts() -> None:
    source_root = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"

    pase_header = (source_root / "matter_pase_commissioning.h").read_text(
        encoding="utf-8"
    )
    pase_source = (source_root / "matter_pase_commissioning.cpp").read_text(
        encoding="utf-8"
    )
    assert "kMatterSpake2pPbkdf2Iterations = 2000U" in pase_header
    assert "uint32_t iterations = kMatterSpake2pPbkdf2Iterations;" in pase_header
    pase_end = function_body(pase_source, "void MatterPaseCommissioning::end()")
    assert "session_ = MatterPaseSessionState{};" in pase_end
    assert "verifier_ = MatterSpake2pVerifier{};" in pase_end
    assert "memset" not in pase_end

    credentials = (source_root / "matter_credentials.cpp").read_text(
        encoding="utf-8"
    )
    credential_reset = function_body(
        credentials, "void resetPersistentCredentialsState("
    )
    assert "bytes[i] = 0U;" in credential_reset
    assert "copyPersistentCredentialsMembers(state, defaults);" in credential_reset
    credential_save = function_body(
        credentials, "bool MatterCredentials::saveToStorage("
    )
    assert "resetPersistentCredentialsState(&canonicalState);" in credential_save
    assert "copyPersistentCredentialsMembers(&canonicalState, state);" in credential_save
    assert "canonicalState = state;" not in credential_save
    assert "prefs.putBytes(kCredentialsKey, &canonicalState" in credential_save

    openthread = (source_root / "openthread_platform_nrf54l15.cpp").read_text(
        encoding="utf-8"
    )
    ot_init = function_body(openthread, "void otSysInit(int, char**)")
    assert "state.snapshot = OpenThreadPlatformSkeletonSnapshot{};" in ot_init
    assert re.search(
        r"void\s+makeDataChunkKey\s*\(\s*uint16_t\s+key,\s*"
        r"uint16_t\s+index,",
        openthread,
    )
    chunk_key = function_body(openthread, "void makeDataChunkKey(")
    assert '"k%04X.%02u.d%02u"' in chunk_key
    assert "static_cast<unsigned>(index)" in chunk_key
    settings_get = function_body(openthread, "otError otPlatSettingsGet(")
    assert "static_cast<uint16_t>(index)" in settings_get

    channel_sounding = (source_root / "ble_channel_sounding.cpp").read_text(
        encoding="utf-8"
    )
    sweep = function_body(
        channel_sounding,
        "bool BleCsConnectedMode2SweepRunner::runInitiator(",
    )
    assert "measurements[i] = BleCsChannelMeasurement{};" in sweep
    print("PASS Matter, OpenThread, and channel-sounding typed sentinel resets")


def validate_matter_session_security_contracts() -> None:
    source_root = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
    case_source = (source_root / "matter_case_session.cpp").read_text(
        encoding="utf-8"
    )
    pase_source = (source_root / "matter_pase_commissioning.cpp").read_text(
        encoding="utf-8"
    )

    set_peer = function_body(
        case_source, "bool MatterCaseSession::setPeerCertificate("
    )
    assert "Secp256r1::decodeUncompressed" in set_peer
    assert "verifyCertificate(cert, publicKey)" in set_peer
    assert set_peer.index("verifyCertificate(cert, publicKey)") < set_peer.index(
        "peerCert_ = cert;"
    )

    for signature in (
        "bool MatterCaseSession::processSigma2(",
        "bool MatterCaseSession::processSigma3(",
    ):
        body = function_body(case_source, signature)
        assert "parseCertificate(" in body
        assert "verifyCertificate(receivedCertificate" in body
        assert "certificatesEqual(receivedCertificate, peerCert_)" in body
        assert "certLen >=" not in body

    sigma2_build = function_body(
        case_source, "bool MatterCaseSession::buildSigma2("
    )
    sigma2_process = function_body(
        case_source, "bool MatterCaseSession::processSigma2("
    )
    sigma3_build = function_body(
        case_source, "bool MatterCaseSession::buildSigma3("
    )
    sigma3_process = function_body(
        case_source, "bool MatterCaseSession::processSigma3("
    )
    assert "buildTranscriptProofHash(2U" in sigma2_build
    assert "ecdsaSign(localPrivateKey_" in sigma2_build
    assert "outMsg->transcriptSignature" in sigma2_build
    assert "buildTranscriptProofHash(2U" in sigma2_process
    assert "ecdsaVerify(peerPublicKey_" in sigma2_process
    assert "msg.transcriptSignature" in sigma2_process
    assert "buildTranscriptProofHash(3U" in sigma3_build
    assert "ecdsaSign(localPrivateKey_" in sigma3_build
    assert "outMsg->transcriptSignature" in sigma3_build
    assert "buildTranscriptProofHash(3U" in sigma3_process
    assert "ecdsaVerify(peerPublicKey_" in sigma3_process
    assert "msg.transcriptSignature" in sigma3_process
    transcript_proof = function_body(
        case_source, "bool MatterCaseSession::buildTranscriptProofHash("
    )
    assert "CASE-EXPERIMENTAL-SIGMA2-PROOF" in transcript_proof
    assert "CASE-EXPERIMENTAL-SIGMA3-PROOF" in transcript_proof
    assert "initiatorEphemeral" in transcript_proof
    assert "responderEphemeral" in transcript_proof

    encrypt = function_body(case_source, "bool MatterCaseSession::encryptMessage(")
    decrypt = function_body(case_source, "bool MatterCaseSession::decryptMessage(")
    assert "static uint32_t" not in encrypt
    assert "static uint32_t" not in decrypt
    assert "aad, aadLen" in encrypt
    assert "aad, aadLen" in decrypt
    assert decrypt.index("if (!aeadDecrypt(") < decrypt.index(
        "*counter = nextCounter;"
    )

    pase_dispatch = function_body(
        pase_source, "void MatterPaseCommissioning::handleMessage("
    )
    assert "messageExpected(header, source, sourcePort)" in pase_dispatch
    assert "messagePayloadValid(header, appPayload, appLength)" in pase_dispatch
    assert "peerMessageCounter_.accept(header.messageId)" in pase_dispatch
    assert pase_dispatch.index(
        "messagePayloadValid(header, appPayload, appLength)"
    ) < pase_dispatch.index("peerMessageCounter_.accept(header.messageId)")
    assert pase_dispatch.index(
        "peerMessageCounter_.accept(header.messageId)"
    ) < pase_dispatch.index("bindPeer(source, sourcePort, header.exchangeId)")
    spake_z = function_body(
        pase_source, "bool MatterPaseCommissioning::computeSpake2pZ("
    )
    assert "responderVerifier ? session_.X : session_.Y" in spake_z
    assert "scalarMultiply(ephemeral, verifierPoint, &Vpoint)" in spake_z
    assert "scalarMultiply(w1Scalar, peerMinusW0, &Vpoint)" in spake_z
    confirm_a = function_body(
        pase_source, "bool MatterPaseCommissioning::generateConfirmationA("
    )
    confirm_b = function_body(
        pase_source, "bool MatterPaseCommissioning::generateConfirmationB("
    )
    assert "session_.kcA" in confirm_a
    assert "session_.kcB" in confirm_b
    assert "PASE-EXPERIMENTAL-CONFIRM-A" in pase_source
    assert "PASE-EXPERIMENTAL-CONFIRM-B" in pase_source
    assert "PASE-EXPERIMENTAL-KCA" in pase_source
    assert "PASE-EXPERIMENTAL-KCB" in pase_source
    verify_a = function_body(
        pase_source, "bool MatterPaseCommissioning::verifyConfirmationA("
    )
    verify_b = function_body(
        pase_source, "bool MatterPaseCommissioning::verifyConfirmationB("
    )
    assert "constantTimeEqual" in verify_a
    assert "constantTimeEqual" in verify_b
    assert "MatterMessageExchangeFlags::kReliable" not in pase_source
    pase_complete = function_body(
        pase_source, "void MatterPaseCommissioning::handleSpake2p2("
    )
    assert "if (!sendMessage(" in pase_complete
    assert pase_complete.index("if (!sendMessage(") < pase_complete.index(
        "advanceState(MatterCommissioningState::kPaseComplete);"
    )
    header_builder = function_body(
        pase_source, "bool MatterPaseCommissioning::buildMessageHeader("
    )
    assert "requiredCapacity" in header_builder
    assert "? 22U" in header_builder
    print("PASS Matter CASE/PASE identity, ordering, replay, and send-state contracts")


def validate_channel_sounding_public_examples_contracts() -> None:
    examples = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding"
    )
    expected = ["BleChannelSoundingInitiator", "BleChannelSoundingReflector"]
    actual = sorted(path.name for path in examples.iterdir() if path.is_dir())
    assert actual == expected, (
        "public Channel Sounding examples must contain only the canonical pair: "
        f"{actual}"
    )
    for name in expected:
        sketch_path = examples / name / f"{name}.ino"
        assert sketch_path.is_file()
        sketch = sketch_path.read_text(encoding="utf-8")
        assert '#include "ble_cs_controller_runtime.h"' in sketch
        assert "BleCsControllerRuntime" in sketch
        assert "BleCsSubeventResultReassembler" in sketch
        assert "kBleCsHciEvtSubeventResult" in sketch
        assert "kBleCsHciEvtSubeventResultContinue" in sketch
        assert "measureChannelAveraged(" not in sketch
        assert "listenAndReflectOnce(" not in sketch
        assert "protocol=physical_pbr" not in sketch

    pair_gate = (ROOT / "scripts/test_cs_controller_pair.sh").read_text(
        encoding="utf-8"
    )
    assert "silent-peer: fatal/drop/reject marker" in pair_gate
    assert "reason=session_sync .*bytes=0" in pair_gate
    print(
        "PASS exactly two controller-backed public Channel Sounding examples"
    )


def validate_no_generated_uf2_artifacts() -> None:
    # Ignore deliberately retained build/measurement outputs covered by
    # .gitignore. Only files Git could include in a commit belong to this
    # repository-cleanliness contract.
    generated_uf2: set[Path] = set()
    for arguments in (
        ("ls-files", "-z", "--", "*.uf2"),
        ("ls-files", "-z", "--others", "--exclude-standard", "--", "*.uf2"),
    ):
        result = subprocess.run(
            ["git", *arguments],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
        )
        generated_uf2.update(
            ROOT / os.fsdecode(record)
            for record in result.stdout.split(b"\0")
            if record
        )
    generated_uf2_sorted = sorted(generated_uf2)
    generated_preview = [
        str(path.relative_to(ROOT)) for path in generated_uf2_sorted[:20]
    ]
    assert not generated_uf2_sorted, (
        "tracked or unignored UF2 build artifacts are not allowed in the "
        f"repository: found {len(generated_uf2_sorted)}; "
        f"first paths: {generated_preview}"
    )
    print("PASS repository contains no tracked or unignored UF2 artifacts")


def validate_spi_contracts() -> None:
    source = (PLATFORM / "cores/nrf54lm20b/SPI.cpp").read_text(encoding="utf-8")
    header = (PLATFORM / "cores/nrf54lm20b/SPI.h").read_text(encoding="utf-8")
    assert (
        "SPIClass SPI(NRF_SPIM21, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK, PIN_SPI_SS);"
        in source
    )
    assert "SPIClass SPI(NRF_SPIM23" not in source
    assert "D8/D9/D10), SPIM21, 8 MHz max." in header
    begin_transaction = function_body(
        source, "void SPIClass::beginTransaction(SPISettings settings)"
    )
    assert "if (!_initialized || _spim == nullptr)" in begin_transaction
    assert begin_transaction.index("if (!_initialized || _spim == nullptr)") < (
        begin_transaction.index("applySettings()")
    )
    assert "_inTransaction = false;" in begin_transaction
    print("PASS nrf54lm20b SPIM21 header route and beginTransaction guard")


def validate_system_off_wake_contracts() -> None:
    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "wiring_time.c").read_text(
            encoding="utf-8"
        )
        wire_source = (PLATFORM / "cores" / chip / "Wire.cpp").read_text(
            encoding="utf-8"
        )
        arm_body = function_body(
            source, "static void armSystemOffWakeCompare("
        )
        wake_selector_body = function_body(
            source, "static uint8_t systemOffWakeChannel("
        )
        systemoff_entry_body = function_body(
            source, "static void enterSystemOffInternal(bool disableRamRetention"
        )
        reset_reason_clear_body = function_body(
            source, "static bool clearResetReasonsForSystemOff(void)"
        )
        program_body = function_body(
            source, "static system_off_wake_program_status_t programSystemOffWakeUs("
        )
        wire_quiesce_body = function_body(
            wire_source, "bool TwoWire::quiesceForSystemOff(uint32_t spinLimit)"
        )
        assert "uint64_t wakeTimestampUs" in source
        assert "CCADD" not in arm_body
        assert "GRTC_CC_CCEN_ACTIVE_Enable" in arm_body
        assert "kSystemOffWakePreferredCcChannel = 6U" in source
        assert "highestSetBit(available)" not in wake_selector_body
        assert "lowestSetBit(available)" in wake_selector_body
        assert wake_selector_body.index("kSystemOffWakePreferredCcChannel") < (
            wake_selector_body.index("lowestSetBit(available)")
        )
        assert "readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs" in program_body
        assert program_body.index("readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs") < (
            program_body.index("armSystemOffWakeCompare(")
        )
        assert "kScbScrSleepDeep_Msk" in systemoff_entry_body
        assert "__asm volatile(\"wfi\")" in systemoff_entry_body
        assert "timedWake && anyGrtcCompareEvent(NRF_GRTC)" in systemoff_entry_body
        assert "abortSystemOffWithReset(" in systemoff_entry_body
        assert "clearSystemOffAbortDiagnostic();" in systemoff_entry_body
        assert "kSystemOffAbortDmaQuiesce" in systemoff_entry_body
        assert "kSystemOffAbortPreEntryCompare" in systemoff_entry_body
        assert "kSystemOffAbortResetReasonClear" in systemoff_entry_body
        assert "resetAfterSystemOffEvent();" in systemoff_entry_body
        assert "g_system_off_abort_magic" in source
        assert "__attribute__((section(\".noinit\")))" in source
        assert "uint32_t nrf54SystemOffAbortStage(void)" in source
        assert "__asm volatile(\"wfe\")" not in systemoff_entry_body
        wfi_index = systemoff_entry_body.index("__asm volatile(\"wfi\")")
        fallback_index = systemoff_entry_body.index(
            "timedWake && anyGrtcCompareEvent(NRF_GRTC)", wfi_index
        )
        assert systemoff_entry_body.index("kScbScrSleepDeep_Msk") < (
            systemoff_entry_body.index("NRF_REGULATORS->SYSTEMOFF")
        )
        assert "nrf54_core_clear_reset_reason(0xFFFFFFFFUL);" in reset_reason_clear_body
        assert "NRF_RESET->RESETREAS" in reset_reason_clear_body
        assert "kSystemOffResetReasonClearSpinLimit" in reset_reason_clear_body
        assert "kSystemOffResetReasonStableReads" in reset_reason_clear_body
        assert "consecutiveZeroReads" in reset_reason_clear_body
        assert "return false;" in reset_reason_clear_body
        assert systemoff_entry_body.index("clearResetReasonsForSystemOff()") < (
            systemoff_entry_body.index("NRF_REGULATORS->SYSTEMOFF")
        )
        assert wfi_index < fallback_index
        assert fallback_index < (
            systemoff_entry_body.index("resetAfterSystemOffEvent();", fallback_index)
        )
        assert (
            "const bool controllerWasIdle = !_targetRegistered && "
            "!_pendingRepeatedStart;" in wire_quiesce_body
        )
        assert "(stopped || controllerWasIdle)" in wire_quiesce_body
        assert wire_quiesce_body.index("const bool controllerWasIdle") < (
            wire_quiesce_body.index("reg32(base + T_TASKS_STOP) = 1U;")
        )
        assert wire_quiesce_body.index("end();") < (
            wire_quiesce_body.index("(stopped || controllerWasIdle)")
        )
        print(f"PASS {chip} timed SYSTEMOFF absolute GRTC compare/channel contract")

    system_off_probe = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/LowPower/"
        "LowPowerGrtcPwmSystemOff/LowPowerGrtcPwmSystemOff.ino"
    ).read_text(encoding="utf-8")
    assert "nrf54SystemOffAbortStage()" in system_off_probe
    assert "nrf54ClearSystemOffAbortStage()" in system_off_probe
    assert 'Serial.print("system_off_abort_stage=");' in system_off_probe
    assert 'Serial.print("debug_interface_settle_ms=");' in system_off_probe
    assert "RESET_RESETREAS_DIF_Msk" in system_off_probe
    assert 'Serial.print("reset_reason_snapshot=0x");' in system_off_probe
    assert 'Serial.print("reset_reason_off=");' in system_off_probe
    assert 'Serial.print("debug_c_debugen=");' in system_off_probe

    gate = (ROOT / "scripts/run_two_board_release_gate.py").read_text(
        encoding="utf-8"
    )
    assert "self._generations[role] += 1" in gate
    assert "if generation == self._generations[role]" in gate
    assert 'r"\\bsystem_off_abort_stage=([0-9]+)"' in gate
    assert "followed a System OFF" in gate
    assert "consecutive timed" in gate
    assert "reset_reason_value & 0x40" in gate
    assert "wake_value == 0" in gate
    assert "savemem 0x{address:08x}" in gate
    assert "if len(data) != length or any(data):" in gate

    hal_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp"
    ).read_text(encoding="utf-8")
    hal_arm_body = function_body(hal_source, "void armSystemOffWakeCompare(")
    assert "GRTC_CC_CCEN_ACTIVE_Enable" in hal_arm_body
    print("PASS HAL timed SYSTEMOFF compare explicit enable contract")


def validate_systick_monotonic_contracts() -> None:
    helper = (
        PLATFORM / "cores/nrf54common/nrf54_systick_timebase.h"
    ).read_text(encoding="utf-8")
    assert "epoch->wrapCount[next & 1U] = wraps;" in helper
    assert helper.index("epoch->wrapCount[next & 1U] = wraps;") < helper.index(
        "epoch->generation = next;"
    )
    assert "generationBefore != generationAfter" in helper

    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "wiring_time.c").read_text(
            encoding="utf-8"
        )
        sample = function_body(
            source, "static uint64_t readSysTickMicroseconds64(void)"
        )
        assert "nrf54_systick_publish_tick(&g_systick_epoch);" in source
        assert "SysTick_CTRL_COUNTFLAG_Msk" in sample
        assert "kScbIcsrPendstset_Msk" in sample
        assert "nrf54_systick_sample_is_stable(" in sample
        assert "nrf54_systick_compose_time_us(" in sample
        assert "return (unsigned long)readSysTickMicroseconds64();" in source
    print("PASS coherent COUNTFLAG/PENDSTSET-aware SysTick monotonic contracts")


def validate_two_board_gate_parser_contracts() -> None:
    namespace = runpy.run_path(str(ROOT / "scripts/run_two_board_release_gate.py"))
    gate_failure = namespace["GateFailure"]
    release_gate = namespace["ReleaseGate"]
    validate_source = namespace["validate_release_source_state"]
    validate_system_off = namespace["validate_system_off_log"]
    board_type = namespace["Board"]

    release_gate.require_exact_line(
        "Core version: 1.0.0\r\nCore version heartbeat: 1.0.0\r\n",
        "Core version: 1.0.0",
        "exact-version",
    )
    try:
        release_gate.require_exact_line(
            "Core version: 1.0.0-rc1\n",
            "Core version: 1.0.0",
            "exact-version",
        )
        raise AssertionError("RC suffix satisfied the stable version line")
    except gate_failure:
        pass

    validate_source(
        "1.0.0", "abc", False, expected_revision="abc",
        description="source-state",
    )
    for version, revision, dirty, expected in (
        ("1.0.0-rc1", "abc", False, "abc"),
        ("1.0.0", "abc", True, "abc"),
        ("1.0.0", "def", False, "abc"),
    ):
        try:
            validate_source(
                version, revision, dirty, expected_revision=expected,
                description="source-state",
            )
            raise AssertionError("invalid full-gate source state was accepted")
        except gate_failure:
            pass

    def off_block(boot: int, wake: int, reason: int, *, entered: bool = True) -> str:
        suffix = "Entering SYSTEM OFF for 5000 ms\n" if entered else ""
        return (
            "LowPowerGrtcPwmSystemOff\n"
            f"boot={boot}\n"
            f"wake_from_grtc_or_off={wake}\n"
            "system_off_abort_stage=0\n"
            f"reset_reason_snapshot=0x{reason:X}\n"
            f"{suffix}"
        )

    wrap_log = (
        "LowPowerGrtcPwmSystemOff\npartial-old-boot\n" +
        off_block(0xFFFFFFFE, 0, 0x40) +
        off_block(0xFFFFFFFF, 1, 0x800) +
        off_block(0, 1, 0x800, entered=False) +
        "LowPowerGrtcPwmSystemOff\nboot="
    )
    assert validate_system_off(wrap_log, "wrap") == 2

    invalid_logs = (
        off_block(8, 0, 0x40) +
        off_block(9, 1, 0x800) +
        off_block(10, 1, 0x100, entered=False),
        off_block(8, 0, 0x40) +
        off_block(9, 1, 0x800) +
        off_block(11, 1, 0x800, entered=False),
        off_block(8, 0, 0x40) +
        off_block(9, 1, 0x800) +
        "LowPowerGrtcPwmSystemOff\nboot=",
        off_block(8, 0, 0x40) +
        "LowPowerGrtcPwmSystemOff\nboot=9\n" +
        off_block(10, 1, 0x800, entered=False),
    )
    for invalid_log in invalid_logs:
        try:
            validate_system_off(invalid_log, "invalid")
            raise AssertionError("invalid System OFF transition was accepted")
        except gate_failure:
            pass

    gate = object.__new__(release_gate)
    gate.l15 = board_type("l15", "a", "t", "f", "p", "c")
    gate.lm20 = board_type("lm20", "b", "t", "f", "p", "c")
    phy_logs = {
        "l15": (
            "Ble2MPhyProbe start\ncycle phase: 2M return complete OLD\n"
            "Ble2MPhyProbe start\ncycle phase: 2M return complete\n"
        ),
        "lm20": (
            "Ble2MPhyCentralProbe start\n"
            "cycle phase: 2M long notify reconfirmed OLD\n"
            "Ble2MPhyCentralProbe start\n"
            "request data length 251: queued\nrequest mtu 247: queued\n"
            "notifications enabled\nrequest 2M phy: queued\n"
            "cycle phase: 1M long notify confirmed\n"
            "cycle phase: 2M long notify reconfirmed\n"
        ),
    }
    phy_session = gate.phy_logs_since_start(phy_logs, "phy")
    assert "OLD" not in phy_session["l15"]
    assert "OLD" not in phy_session["lm20"]
    gate.validate_phy_cycle(phy_session, "phy")

    stale_only_logs = {
        "l15": (
            "cycle phase: 2M return complete\n"
            "Ble2MPhyProbe start\nwaiting\n"
        ),
        "lm20": (
            "request data length 251: queued\nrequest mtu 247: queued\n"
            "notifications enabled\nrequest 2M phy: queued\n"
            "cycle phase: 1M long notify confirmed\n"
            "cycle phase: 2M long notify reconfirmed\n"
            "Ble2MPhyCentralProbe start\nscanning\n"
        ),
    }
    try:
        gate.validate_phy_cycle(
            gate.phy_logs_since_start(stale_only_logs, "stale-phy"),
            "stale-phy",
        )
        raise AssertionError("pre-start PHY markers satisfied the fresh session")
    except gate_failure:
        pass

    cs_script = ROOT / "scripts/test_cs_controller_pair.sh"
    with tempfile.TemporaryDirectory(prefix="nrf54-cs-gate-contract-") as directory:
        temp = Path(directory)
        initiator_log = temp / "initiator.log"
        reflector_log = temp / "reflector.log"
        initiator_log.write_text(
            "cs_result role=initiator result=PASS pbr_m=1 distance_m=1 "
            "used_channels=4 hci_continue=1 dropped=0 transfer_id=11 "
            "transfer_crc32=AA session_token=TOKENA\n"
            "cs_result role=initiator result=PASS pbr_m=1 distance_m=1 "
            "used_channels=0 hci_continue=1 dropped=0 transfer_id=22 "
            "transfer_crc32=BB session_token=TOKENB\n",
            encoding="utf-8",
        )
        reflector_log.write_text(
            "cs_result role=reflector result=PASS steps=8 bytes=40 "
            "transfer_acks=1 hci_continue=1 dropped=0 rejected=0 "
            "transfer_id=22 transfer_crc32=BB session_token=TOKENB\n"
            "cs_result role=reflector result=PASS steps=0 bytes=40 "
            "transfer_acks=1 hci_continue=1 dropped=0 rejected=0 "
            "transfer_id=11 transfer_crc32=AA session_token=TOKENA\n",
            encoding="utf-8",
        )

        def cs_keys(role: str, log: Path) -> set[str]:
            completed = subprocess.run(
                [
                    "bash", "-c",
                    'CS_FUNCTIONS_ONLY=1 source "$1" ""; '
                    'extract_transfer_keys "$2" "$3"',
                    "bash", str(cs_script), role, str(log),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=True,
            )
            return set(completed.stdout.splitlines())

        initiator_keys = cs_keys("initiator", initiator_log)
        reflector_keys = cs_keys("reflector", reflector_log)
        assert initiator_keys == {"11 AA TOKENA"}
        assert reflector_keys == {"22 BB TOKENB"}
        assert not initiator_keys.intersection(reflector_keys)

    print(
        "PASS two-board exact-version/source-state, fresh PHY, GRTC wake, "
        "and role-filtered CS parser contracts"
    )


def validate_xiao_low_power_board_contracts() -> None:
    source = (PLATFORM / "variants/xiao_nrf54l15/variant.cpp").read_text(
        encoding="utf-8"
    )
    board_state_body = function_body(
        source, 'extern "C" void xiaoNrf54l15EnterLowestPowerBoardState('
    )
    assert "gpioSetInputHighZ(kSamd11TxPort" not in board_state_body
    assert "gpioSetInputHighZ(kSamd11RxPort" not in board_state_body
    print("PASS XIAO low-power board state preserves SAMD11 serial bridge pins")

    lm20_time = (PLATFORM / "cores/nrf54lm20b/wiring_time.c").read_text(
        encoding="utf-8"
    )
    lm20_clock_select = function_body(
        lm20_time, "static uint32_t selectRunningGrtcLfClockSource("
    )
    assert "ARDUINO_NRF54LM20A" in lm20_clock_select
    assert "if (ensureSystemOffLfxoRunning())" in lm20_clock_select
    assert "return GRTC_CLKCFG_CLKSEL_LFXO;" in lm20_clock_select
    assert "CLOCK_LFCLK_SRC_SRC_LFRC" in lm20_clock_select
    assert "? GRTC_CLKCFG_CLKSEL_SystemLFCLK" in lm20_clock_select

    lm20_system = (PLATFORM / "cores/nrf54lm20b/system_nrf54lm20b.c").read_text(
        encoding="utf-8"
    )
    assert "lfxoIntcapFemtoF = 17000UL" in lm20_system
    assert "hfxoIntcapFemtoF = 15000UL" in lm20_system

    qspi_transport = (
        PLATFORM
        / "libraries/Adafruit_SPIFlash/src/qspi/Adafruit_FlashTransport_QSPI_NRF54.cpp"
    ).read_text(encoding="utf-8")
    qspi_end = function_body(
        qspi_transport, "void Adafruit_FlashTransport_QSPI_NRF54::end()"
    )
    assert "XiaoQspiFlash.prepareForSleep()" in qspi_end
    assert "XiaoQspiFlash.end()" not in qspi_end

    hal_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp"
    ).read_text(encoding="utf-8")
    assert (
        "constexpr uint32_t kCanonicalClearedEasyDmaPointer = 0x20000000UL;"
        in hal_source
    )
    dma_pointer_cleared = function_body(
        hal_source, "bool radioDmaPointerIsCleared(uint32_t pointer)"
    )
    assert re.sub(r"\s+", "", dma_pointer_cleared) == (
        "returnpointer==0U||"
        "pointer==kCanonicalClearedEasyDmaPointer;"
    )
    radio_dma_scrub = function_body(
        hal_source, "bool scrubRadioDmaPointersIfDisabled("
    )
    for pointer in (
        "radio->PACKETPTR",
        "radio->DFEPACKET.PTR",
        "radio->AUXDATADMA[0].PTR",
        "radio->AUXDATADMA[1].PTR",
    ):
        assert f"radioDmaPointerIsCleared({pointer})" in radio_dma_scrub
        assert f"{pointer} == 0U" not in radio_dma_scrub

    npm_source = (
        PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src/npm1300.cpp"
    ).read_text(encoding="utf-8")
    npm_header = (
        PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src/npm1300.h"
    ).read_text(encoding="utf-8")
    buck_examples = [
        (
            PLATFORM
            / "libraries/Nrf54L15-Clean-Implementation/examples/PMIC"
            / name
            / f"{name}.ino"
        ).read_text(encoding="utf-8")
        for name in (
            "nPM1300_BuckAutoLowPower",
            "nPM1300_BuckHystereticLowPower",
            "nPM1300_BuckPWMLowPower",
        )
    ]
    npm_timed_example = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/PMIC/"
        "nPM1300_TimedHibernate/nPM1300_TimedHibernate.ino"
    ).read_text(encoding="utf-8")
    assert (
        "#define NPM1300_HIBERNATE_TIMER_MAX_MS      268435440UL"
        in npm_header
    )
    assert "nRF54 GRTC System OFF while" in npm_header
    assert "returns false while VBUS is present" not in npm_header
    assert "BUCK2 is the fixed 3.3 V VSYS_3V3" in npm_header
    assert "NPM1300_ENABLE_UNSAFE_SYSTEM_RAIL_CONTROL" in npm_header
    npm_buck2_enable = function_body(
        npm_source, "bool npm1300_buck2_enable("
    )
    assert "if (!e) return false;" in npm_buck2_enable
    assert "return buck_enable(1, true);" in npm_buck2_enable
    npm_buck2_voltage = function_body(
        npm_source, "bool npm1300_buck2_set_voltage("
    )
    assert "(void)mv;" in npm_buck2_voltage
    assert "return false;" in npm_buck2_voltage
    npm_system_buck_mode = function_body(
        npm_source, "bool npm1300_system_buck_set_mode("
    )
    assert "m > NPM1300_BUCK_MODE_FORCE_HYST" in npm_system_buck_mode
    assert "return buck_set_mode(1, m);" in npm_system_buck_mode
    for example in buck_examples:
        assert "npm1300_system_buck_set_mode(" in example
        assert "npm1300_buck1_" not in example
        assert "XIAO nRF54LM20B" not in example
    assert "nRF54 GRTC System OFF wake-reset path" in npm_timed_example
    assert "returns false while VBUS is present" not in npm_timed_example
    npm_sleep = function_body(npm_source, "bool npm1300_prepare_for_sleep(")
    assert "kAdcOffsetIbatEnable, 0U" in npm_sleep
    npm_timer = function_body(
        npm_source, "bool npm1300_configure_hibernate_timer_ms("
    )
    assert "kTimerOffsetTarget = 0x08U" in npm_source
    assert "kTimerOffsetLoad = 0x03U" in npm_source
    assert "npm1300_write_burst(NPM1300_BASE_TIMER, kTimerOffsetTarget" in npm_timer
    assert "npm1300_write_reg(NPM1300_BASE_TIMER, kTimerOffsetLoad, 1U)" in npm_timer
    assert "kTimerOffsetConfig" not in npm_source
    assert "kTimerOffsetStart" not in npm_source
    npm_vbus_absent = function_body(
        npm_source, "static bool npm1300_vbus_absent_for_ship_hibernate("
    )
    assert "npm1300_vbus_status(&status)" in npm_vbus_absent
    assert "NPM1300_VBUS_STATUS_PRESENT" in npm_vbus_absent
    npm_ship = function_body(npm_source, "bool npm1300_enter_ship_mode(")
    npm_hibernate = function_body(npm_source, "bool npm1300_enter_hibernate(")
    assert "npm1300_vbus_absent_for_ship_hibernate()" in npm_ship
    assert "npm1300_vbus_absent_for_ship_hibernate()" in npm_hibernate
    npm_timed_hibernate = function_body(
        npm_source, "bool npm1300_enter_timed_hibernate_ms("
    )
    assert "delay_ms > NPM1300_HIBERNATE_TIMER_MAX_MS" in npm_timed_hibernate
    assert "npm1300_vbus_status(&vbusStatus)" in npm_timed_hibernate
    assert "NPM1300_VBUS_STATUS_PRESENT" in npm_timed_hibernate
    assert "systemOffWakeReset(delay_ms);" in npm_timed_hibernate
    assert "npm1300_vbus_absent_for_ship_hibernate()" not in npm_timed_hibernate
    assert npm_timed_hibernate.index(
        "delay_ms > NPM1300_HIBERNATE_TIMER_MAX_MS"
    ) < npm_timed_hibernate.index("npm1300_vbus_status(&vbusStatus)")
    assert npm_timed_hibernate.index("npm1300_vbus_status(&vbusStatus)") < (
        npm_timed_hibernate.index("systemOffWakeReset(delay_ms);")
    )
    assert npm_timed_hibernate.index("systemOffWakeReset(delay_ms);") < (
        npm_timed_hibernate.index("npm1300_configure_hibernate_timer_ms")
    )
    assert "delay(1);" in npm_timed_hibernate
    assert npm_timed_hibernate.index("npm1300_configure_hibernate_timer_ms") < (
        npm_timed_hibernate.index("delay(1);")
    )
    assert npm_timed_hibernate.index("delay(1);") < (
        npm_timed_hibernate.index("npm1300_enter_hibernate()")
    )
    npm_bus_end = function_body(npm_source, "void pmic_bus_end()")
    assert "GPIO_PIN_CNF_INPUT_Disconnect" in npm_bus_end
    system_off_prepare = function_body(
        lm20_time, "bool nrf54lm20b_core_prepare_system_off(void)\n{"
    )
    assert "xiaoNrf54lm20PmicPrepareForSleep" in system_off_prepare
    assert "npm1300_prepare_for_sleep" not in system_off_prepare

    lm20_variant = (
        PLATFORM / "variants/xiao_nrf54lm20b/variant.cpp"
    ).read_text(encoding="utf-8")
    variant_pmic_sleep = function_body(
        lm20_variant, 'extern "C" int xiaoNrf54lm20PmicPrepareForSleep(void)'
    )
    assert "kPmicAdcBase = 0x05U" in lm20_variant
    assert "kPmicIbatEnableOffset" in variant_pmic_sleep
    assert "parkPmicPins" in variant_pmic_sleep

    delay_probe_paths = [
        PLATFORM / "examples/Power/DelayAutoLowPowerMeasure/DelayAutoLowPowerMeasure.ino",
        PLATFORM / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15/DelayAutoLowPowerMeasure/DelayAutoLowPowerMeasure.ino",
    ]
    delay_probes = [path.read_text(encoding="utf-8") for path in delay_probe_paths]
    assert delay_probes[0] == delay_probes[1], "delay-current probe copies diverged"
    assert "npm1300_imu_mic_power_enable(false)" in delay_probes[0]
    assert "npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_AUTO)" in delay_probes[0]
    assert "npm1300_prepare_for_sleep()" in delay_probes[0]

    system_off_probe = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/LowPower/LowPowerZephyrParityBlink/LowPowerZephyrParityBlink.ino"
    ).read_text(encoding="utf-8")
    assert "npm1300_imu_mic_power_enable(false)" in system_off_probe
    assert "npm1300_system_buck_set_mode(NPM1300_BUCK_MODE_FORCE_HYST)" in system_off_probe
    assert "npm1300_prepare_for_sleep()" in system_off_probe
    print("PASS LM20A LFXO, oscillator-load, QSPI, and PMIC sleep contracts")


def validate_xiao_retention_probe_contracts() -> None:
    probe_paths = [
        PLATFORM / "examples/Power/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino",
        PLATFORM / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15-Sense/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino",
    ]
    probes = [path.read_text(encoding="utf-8") for path in probe_paths]
    assert probes[0] == probes[1], "retention probe copies diverged"
    probe = probes[0]
    assert "kProbeCompareChannel = 5U" in probe
    assert "BLE Support: Disabled (required)" in probe
    assert "#error \"SenseDelayRailRetentionProbe requires Tools -> BLE Support -> Disabled.\"" in probe
    setup_body = function_body(probe, "void setup()")
    assert setup_body.index("delay(10);") < setup_body.index(
        "runRetentionMeasurement();"
    )
    assert "g_grtc.begin(" not in probe
    assert probe.index("runRetentionMeasurement();") < probe.index("Serial.begin(115200);")
    assert "retention_status=" in probe
    assert "g_postRfCtlPin == 0U" in probe
    assert "g_vbatRaw > 0" in probe

    matrix_namespace = runpy.run_path(str(ROOT / "scripts/build_all_examples.py"))
    feature_options = matrix_namespace["feature_options"]
    for path in probe_paths:
        relative = path.relative_to(PLATFORM).as_posix()
        assert "clean_ble=off" in feature_options(relative)

    hooks_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_hooks.inc"
    ).read_text(encoding="utf-8")
    hal_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp"
    ).read_text(encoding="utf-8")
    mask_body = function_body(
        hooks_source, "extern \"C\" uint32_t nrf54l15_ble_grtc_reserved_cc_mask(void)"
    )
    irq_body = function_body(
        hal_source, "extern \"C\" void nrf54l15_ble_grtc_irq_service(void)"
    )
    assert "NRF54L15_CLEAN_BLE_DISABLED" in mask_body
    assert "return 0U;" in mask_body
    assert "NRF54L15_CLEAN_BLE_DISABLED" in irq_body
    assert irq_body.index("nrf54l15_grtc_irq_observer();") > irq_body.index("#endif")
    print("PASS BLE-disabled GRTC ownership and XIAO rail-retention probe contract")


def validate_serial_fabric_runtime_probe_contracts() -> None:
    probe = (
        PLATFORM / "examples/Serial/SerialFabricRuntimeProbe/SerialFabricRuntimeProbe.ino"
    ).read_text(encoding="utf-8")
    assert "void announceStage(const char* stage)" in probe
    assert "Serial.flush();" in probe
    assert "g_lastStatusMs" in probe
    assert "(now - g_lastStatusMs) >= 1000UL" in probe
    for stage in ("uart22", "uart30", "twim22", "twim30", "spim22", "spim30"):
        assert f'announceStage("{stage}")' in probe
    print("PASS serial-fabric runtime probe CLI-status contract")


def validate_ble_security_hardening_contracts() -> None:
    parts = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
    )
    crypto_service = (parts / "nrf54l15_hal_internal_crypto_service.inc").read_text(
        encoding="utf-8"
    )
    random_fill = function_body(
        crypto_service, "bool fillBleSecurityRandomBytes("
    )
    assert "rng.fill(out, len, spinLimit)" in random_fill
    assert "memset(out, 0, len);" in random_fill
    assert "fillPseudoRandomBytes" not in random_fill
    assert "reverseInPlace(cmacMessage, cmacMessageLen)" in crypto_service
    assert "outMac[i] = cmac[7U - i]" in crypto_service
    crypto_self_test = function_body(
        crypto_service, "bool bleHardwareCryptoSelfTest()"
    )
    assert "ATT_SIGNED_WRITE_CMD example vector" in crypto_self_test
    assert "0xF1U, 0x87U, 0x1EU, 0x93U" in crypto_self_test

    crypto_hal = (parts / "nrf54l15_hal_crypto_analog.inc").read_text(
        encoding="utf-8"
    )
    rng_begin = function_body(crypto_hal, "bool CracenRng::beginNonBlocking()")
    assert "tryAcquireCracenRng()" in rng_begin
    assert "CRACENCORE_RNGCONTROL_CONTROL_SOFTRST_Msk" in rng_begin
    assert "RNGCONTROL.WARMUPPERIOD = 512U" in rng_begin
    assert "RNGCONTROL.SAMPLINGPERIOD = 1U" in rng_begin
    assert "RNGCONTROL.INITWAITVAL = 512U" in rng_begin
    assert "NB128BITBLOCKS" in rng_begin
    rng_conditioning = function_body(
        crypto_hal, "bool CracenRng::setupConditioningKeyIfAvailable()"
    )
    assert "availableWords() < 4U" in rng_conditioning
    assert "RNGCONTROL.KEY[i] = core_->RNGCONTROL.FIFO[0]" in rng_conditioning
    rng_end = function_body(crypto_hal, "void CracenRng::end()")
    assert "if (!active_)" in rng_end
    assert "releaseCracenRng();" in rng_end

    ll_security = (parts / "nrf54l15_hal_ble_ll_security.inc").read_text(
        encoding="utf-8"
    )
    smp_clear = function_body(ll_security, "void BleRadio::clearSmpSecurityState()")
    assert "clearSmpPairingState();" in smp_clear
    assert "clearEncryptionState();" not in smp_clear
    connection_clear = function_body(
        ll_security, "void BleRadio::clearConnectionSecurityState()"
    )
    assert "clearSmpSecurityState();" in connection_clear
    assert "clearEncryptionState();" in connection_clear
    timeout = function_body(ll_security, "void BleRadio::serviceSmpTimer()")
    assert "connectionPendingTxSmpFragment_" in timeout
    assert "connectionTxHistoryValid_ = false" not in timeout
    key_distribution = function_body(
        ll_security, "bool BleRadio::completeSmpKeyDistributionIfDone()"
    )
    assert "smpLocalIdAddressAcked_" in key_distribution
    identity_ack = function_body(
        ll_security, "void BleRadio::noteLastSmpTxAcknowledged()"
    )
    assert "kSmpCodeIdentityAddressInformation" in identity_ack
    prefetch = function_body(
        ll_security, "void BleRadio::prefetchConnectionSecurityMaterial("
    )
    assert "uint8_t material[54]" in prefetch
    assert "smpPrefetchedLegacyKeyMaterial_" in prefetch
    assert "&material[28]" in prefetch
    assert prefetch.count("fillBleSecurityRandomBytes(") == 1
    bond_build = function_body(
        ll_security, "bool BleRadio::buildCurrentBondRecord("
    )
    assert "kBleBondRecordFlagAuthenticated" in bond_build
    assert "kBleBondRecordFlagSecureConnections" in bond_build
    assert "record.peerCsrk" in bond_build
    assert "record.localCsrk" in bond_build
    signed_verify = function_body(
        ll_security, "bool BleRadio::verifyAttSignedWrite("
    )
    assert "receivedCounter <= smpPeerSignCounter_" in signed_verify
    assert "persistActiveSigningCounters" not in signed_verify
    assert signed_verify.index("if (difference != 0U)") < signed_verify.index(
        "smpPeerSignCounter_ = receivedCounter;"
    )
    assert signed_verify.index("smpPeerSignCounter_ = receivedCounter;") < signed_verify.index(
        'emitBleTrace("ATT_SIGNED_WRITE_VERIFIED")'
    )
    assert "smpPeerSignCounter_ = oldCounter" not in signed_verify
    assert "bondRecord_ = oldBondRecord" not in signed_verify

    peer_counter_persist = function_body(
        ll_security, "bool BleRadio::persistDeferredSignedWriteCounter("
    )
    assert "kPeerCounterReservation" not in peer_counter_persist
    assert "signCounter == smpPeerSignCounterReservedUntil_" in peer_counter_persist
    assert "bondRecord_.peerSignCounter = signCounter;" in peer_counter_persist
    assert "persistActiveSigningCounters(true)" in peer_counter_persist
    assert 'emitBleTrace("ATT_SIGNED_WRITE_COUNTER_PERSISTED")' in peer_counter_persist
    assert peer_counter_persist.index(
        "persistActiveSigningCounters(true)"
    ) < peer_counter_persist.index(
        "smpPeerSignCounterReservedUntil_ = signCounter;"
    )

    deferred_signed_apply = function_body(
        ll_security, "void BleRadio::applyDeferredSignedGattWrite("
    )
    assert "persistDeferredSignedWriteCounter(signCounter)" in deferred_signed_apply
    assert deferred_signed_apply.index(
        "persistDeferredSignedWriteCounter(signCounter)"
    ) < deferred_signed_apply.index("findCustomCharacteristicByValueHandle(")
    assert deferred_signed_apply.index(
        "persistDeferredSignedWriteCounter(signCounter)"
    ) < deferred_signed_apply.index("writeCustomGattCharacteristic(")
    assert "&ignoredError, true" in deferred_signed_apply

    signing_persist = function_body(
        ll_security, "bool BleRadio::persistActiveSigningCounters("
    )
    assert "writeRetainedBondRecord(bondRecord_)" in signing_persist
    assert "writeFlashBondRecord(bondRecord_)" in signing_persist
    assert "writeFlashBondSigningState(bondRecord_)" in signing_persist
    assert "persistActiveBondDatabaseRecord(false)" in signing_persist
    assert "if (requireDurable)" in signing_persist
    assert "bondFlashPersistPending_ = true;" in signing_persist
    assert "bondSigningCounterPersistPending_ = true" not in signing_persist
    bond_load = function_body(
        ll_security, "bool BleRadio::loadBondRecordFromPersistence()"
    )
    assert "matchingAuthority" in bond_load
    assert "readFlashBondRecord(&signingAuthority)" in bond_load
    assert "memset(loaded.peerCsrk, 0, sizeof(loaded.peerCsrk));" in bond_load
    assert "memset(loaded.localCsrk, 0, sizeof(loaded.localCsrk));" in bond_load
    assert bond_load.index("loaded.peerCsrkValid = 0U;") < bond_load.index(
        "if (matchingAuthority)"
    )
    ll_control = function_body(
        ll_security, "bool BleRadio::buildLlControlResponse("
    )
    conn_param_req = ll_control[ll_control.index("case kBleLlCtrlConnectionParamReq:") :]
    conn_param_req = conn_param_req[: conn_param_req.index("case kBleLlCtrlFeatureReq:")]
    assert "connParamsAreValid" in conn_param_req
    assert "chooseAcceptedConnIntervalUnits" in conn_param_req
    assert "connectionCentralConnParamIndPending_ = true" in conn_param_req
    assert "rejectProcedureCollision()" in conn_param_req

    connections = (
        parts / "nrf54l15_hal_ble_scanning_connections.inc"
    ).read_text(encoding="utf-8")
    peripheral_connect = function_body(
        connections, "bool BleRadio::startConnectionFromConnectInd("
    )
    assert "preferredIntervalMax" in peripheral_connect
    assert "intervalUnits < gapPpcpIntervalMin_" in peripheral_connect
    assert "intervalUnits > preferredIntervalMax" in peripheral_connect
    assert "connectionConnParamUpdatePending_ =" in peripheral_connect

    smp = (parts / "nrf54l15_hal_ble_att_l2cap.inc").read_text(encoding="utf-8")
    assert "SMP_SECURITY_REQUEST_BOND_UPGRADE" in smp
    assert "clearSmpPairingState();" in smp
    assert "kBleBondRecordFlagSecureConnections" in smp
    assert "SMP_RESERVED_CODE_IGNORED" in smp
    pairing_request = smp[smp.index("case kSmpCodePairingRequest:") :]
    pairing_request = pairing_request[: pairing_request.index("case kSmpCodePairingPublicKey:")]
    assert "smp[1] > kSmpIoCapKeyboardDisplay" in pairing_request
    assert "(smp[3] & 0xC0U) != 0U" in pairing_request
    assert "~kSmpKeyDistDefinedMask" in pairing_request
    assert "kSmpKeyDistSignKeyMask" in pairing_request
    signed_write = smp[smp.index("case kAttOpSignedWriteCmd:") :]
    signed_write = signed_write[: signed_write.index("case kAttOpErrorRsp:")]
    assert "verifyAttSignedWrite" in signed_write
    assert "enqueueDeferredSignedGattWrite" in signed_write
    assert signed_write.index("verifyAttSignedWrite") < signed_write.index(
        "enqueueDeferredSignedGattWrite"
    )
    assert "writeCustomGattCharacteristic" not in signed_write

    timing = (parts / "nrf54l15_hal_internal_ble_timing.inc").read_text(
        encoding="utf-8"
    )
    assert "kSmpKeyDistLinkKeyMask = 0x08U" in timing
    supported_mask = timing[
        timing.index("constexpr uint8_t kSmpKeyDistSupportedMask") :
    ]
    supported_mask = supported_mask[
        : supported_mask.index("constexpr uint8_t kSmpKeyDistDefinedMask")
    ]
    assert "kSmpKeyDistLinkKeyMask" not in supported_mask
    defined_mask = timing[
        timing.index("constexpr uint8_t kSmpKeyDistDefinedMask") :
    ]
    defined_mask = defined_mask[: defined_mask.index(";") + 1]
    assert "kSmpKeyDistSupportedMask | kSmpKeyDistLinkKeyMask" in defined_mask

    pairing_response = smp[smp.index("case kSmpCodePairingResponse:") :]
    pairing_response = pairing_response[
        : pairing_response.index("case kSmpCodeSecurityRequest:")
    ]
    assert pairing_response.count("~kSmpKeyDistDefinedMask") == 2
    assert pairing_response.count("kSmpKeyDistSupportedMask") == 2
    assert pairing_response.count("kSmpKeyDistEncKeyMask") >= 1
    assert pairing_response.count("kSmpKeyDistIdKeyMask") >= 2
    assert pairing_response.count("kSmpKeyDistSignKeyMask") >= 2
    assert "memcmp(smpPairingRsp_, smp, kSmpPairingResponseLen)" in pairing_response
    # The raw response participates in retransmission matching and legacy c1.
    # Strip unsupported CTKD LinkKey bits only from negotiated state.
    assert "smpPairingRsp_[5] &= kSmpKeyDistSupportedMask;" not in pairing_response
    assert "smpPairingRsp_[6] &= kSmpKeyDistSupportedMask;" not in pairing_response

    connection_api = (parts / "nrf54l15_hal_ble_connection_api.inc").read_text(
        encoding="utf-8"
    )
    signed_tx = function_body(
        connection_api, "bool BleRadio::queueAttSignedWriteCommand("
    )
    assert "kLocalCounterReservation = 16U" in signed_tx
    assert "smpLocalSignCounter_ >= smpLocalSignCounterReservedUntil_" in signed_tx
    assert "bondRecord_.localSignCounter = reservedUntil;" in signed_tx
    assert "persistActiveSigningCounters(true)" in signed_tx
    assert signed_tx.index("persistActiveSigningCounters(true)") < signed_tx.index(
        "queueAttRequest(request"
    )
    assert signed_tx.index("persistActiveSigningCounters(true)") < signed_tx.index(
        "smpLocalSignCounterReservedUntil_ = reservedUntil;"
    )
    assert signed_tx.index("smpLocalSignCounter_ = signCounter + 1U;") < signed_tx.index(
        "queueAttRequest(request"
    )
    assert "smpLocalSignCounter_ = signCounter;" not in signed_tx
    assert "bondRecord_ = oldBondRecord" not in signed_tx

    deferred_dispatch = function_body(
        connection_api, "void BleRadio::dispatchDeferredGattWrites()"
    )
    assert "slot.signedWrite != 0U" in deferred_dispatch
    assert "applyDeferredSignedGattWrite(" in deferred_dispatch
    deferred_work = function_body(
        connection_api, "void BleRadio::serviceDeferredApplicationWork()"
    )
    assert "if (bleRunningInIsr())" in deferred_work
    assert "flushDeferredBondStorage()" in deferred_work
    assert "dispatchDeferredGattWrites();" in deferred_work

    bond_store = (parts / "nrf54l15_hal_internal_gatt_bond.inc").read_text(
        encoding="utf-8"
    )
    assert "struct BleBondRecordV2" in bond_store
    assert "convertV2BondRecord" in bond_store
    cccd_persist = function_body(
        bond_store, "bool BleRadio::persistBondedCccdState(bool forceFlash)"
    )
    assert "connected_ && !forceFlash" in cccd_persist
    assert "writeFlashCccdBondRecord(record)" in cccd_persist
    assert "return forceFlash ? flashOk" in cccd_persist
    deferred_storage_flush = function_body(
        ll_security, "bool BleRadio::flushDeferredBondStorage()"
    )
    assert "persistBondedCccdState(true)" in deferred_storage_flush
    assert "persistActiveBondDatabaseRecord(true)" in deferred_storage_flush
    first_cccd_clear = deferred_storage_flush.index(
        "cccdFlashPersistPending_ = false;"
    )
    assert deferred_storage_flush.index(
        "persistActiveBondDatabaseRecord(true)"
    ) < first_cccd_clear

    prime_bond = function_body(
        ll_security, "bool BleRadio::primeBondForCurrentPeer()"
    )
    assert "smpPeerSignCounterReservedUntil_ = bondRecord_.peerSignCounter;" in prime_bond
    assert "smpLocalSignCounterReservedUntil_ = bondRecord_.localSignCounter;" in prime_bond
    assert "connectionEncSecureConnections_ =" in prime_bond
    assert "kBleBondRecordFlagSecureConnections" in prime_bond
    print(
        "PASS BLE SMP/LinkKey masks, exact inbound signed counters, outbound "
        "reservations, CCCD foreground flush, LL CPR, and fail-closed RNG"
    )


def validate_two_board_gate_bond_clear_contracts() -> None:
    runner = (ROOT / "scripts/run_two_board_release_gate.py").read_text(
        encoding="utf-8"
    )
    assert "ble_pair bond-clear-persistent=1" in runner
    assert "ble_pair bond-clear-persistent=0" in runner
    assert "ble_pair bond-clear-failed" in runner
    assert "ble_pair bond-cleared-storage" not in runner
    assert "logs = self.pair_logs_since_boot(logs, phase)" in runner
    assert runner.count(
        'self.clear_bond_retention_cache(f"{phase}_cold_reload", self.l15, phase)'
    ) == 3
    assert runner.count(
        'self.clear_bond_retention_cache(f"{phase}_cold_reload", self.lm20, phase)'
    ) == 3

    security_examples = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Security"
    )
    for sketch in (
        security_examples / "BlePairCentral/BlePairCentral.ino",
        security_examples / "BlePairPeripheral/BlePairPeripheral.ino",
    ):
        source = sketch.read_text(encoding="utf-8")
        clear_handler = function_body(source, "void handleCmd(const char* c)")
        assert "const bool cleared = g_ble.clearBondRecord(true);" in clear_handler
        assert 'Serial.print("ble_pair bond-clear-persistent=");' in clear_handler
        assert '"ble_pair bond-clear-failed"' in clear_handler
    print("PASS two-board gate persistent bond-clear evidence contract")


def validate_board_example_menu_contracts() -> None:
    library = PLATFORM / "libraries/nRF54-Board-Examples"
    properties = {}
    for line in (library / "library.properties").read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            properties[key] = value

    assert properties["name"] == "nRF54 Board Examples"
    assert properties["version"] == "1.0.0"
    assert properties["license"] == "MIT"
    assert properties["architectures"] == "nrf54l15clean"
    assert "Nrf54L15-Clean-Implementation" in properties["depends"]
    assert "Adafruit SPIFlash" in properties["depends"]

    expected = {
        "HOLYIOT-25008": {
            "Holyiot25008Lis2dh12Spi",
            "Holyiot25008RgbButton",
            "Holyiot25008UartPadsAsGpio",
        },
        "Nordic-nRF54L15-DK": {"Nrf54L15DkLinearGpioMap"},
        "XIAO-nRF54L15": {
            "DelayAutoLowPowerMeasure",
            "InterruptPwmApiProbe",
            "XiaoBoardControlPins",
        },
        "XIAO-nRF54L15-Sense": {
            "SenseDelayRailRetentionProbe",
            "XiaoSenseImuAccelGyro",
            "XiaoSenseImuWhoAmI",
            "XiaoSenseMicLevel",
        },
        "XIAO-nRF54LM20A": {
            "FlashInfo",
            "FlashReadWrite",
            "QspiFlashDeepSleep",
            "QspiFlashInfo",
            "QspiFlashReadWrite",
            "XiaoRgbLed",
        },
        "XIAO-nRF54LM20A-Sense": {
            "XiaoLM20A_ImuAccelGyro",
            "XiaoLM20A_ImuWhoAmI",
            "XiaoLM20A_MicLevel",
        },
    }

    examples = library / "examples"
    actual = {category: set() for category in expected}
    sketch_paths = {}
    for ino in sorted(examples.rglob("*.ino")):
        relative = ino.relative_to(examples)
        assert len(relative.parts) == 3, (
            f"board example must use category/sketch/sketch.ino: {relative}"
        )
        category, sketch, filename = relative.parts
        assert category in expected, f"unexpected board example category: {category}"
        assert filename == f"{sketch}.ino", f"invalid Arduino sketch layout: {relative}"
        actual[category].add(sketch)
        sketch_paths[sketch] = ino
    assert actual == expected, f"board example menu changed: {actual}"

    visible_examples = list(PLATFORM.glob("libraries/*/examples/**/*.ino"))
    for sketch, canonical in sketch_paths.items():
        matches = [path for path in visible_examples if path.stem == sketch]
        assert matches == [canonical], (
            f"{sketch} must have exactly one visible library menu location: {matches}"
        )

    stale_roots = (
        PLATFORM / "examples/HolyIoT",
        PLATFORM / "examples/XiaoL15",
        PLATFORM / "examples/XiaoLM20A",
        PLATFORM / "libraries/Adafruit_SPIFlash/examples",
        PLATFORM / "libraries/HOLYIOT-25008-Examples/examples",
    )
    for root in stale_roots:
        assert not list(root.rglob("*.ino")), f"stale board menu examples remain in {root}"

    generic_board_examples = {
        path.stem
        for path in (
            PLATFORM / "libraries/Nrf54L15-Clean-Implementation/examples/Board"
        ).rglob("*.ino")
    }
    assert generic_board_examples == {
        "BoardBatteryAntennaBusControl",
        "PofWarningMonitor",
    }

    matrix = runpy.run_path(str(ROOT / "scripts/build_all_examples.py"))
    category_targets = {
        "HOLYIOT-25008": ("holyiot_25008_nrf54l15",),
        "Nordic-nRF54L15-DK": ("nrf54l15dk_pca10156",),
        "XIAO-nRF54L15": ("xiao_nrf54l15",),
        "XIAO-nRF54L15-Sense": ("xiao_nrf54l15",),
        "XIAO-nRF54LM20A": ("xiao_nrf54lm20b",),
        "XIAO-nRF54LM20A-Sense": ("xiao_nrf54lm20b",),
    }
    for category, sketches in expected.items():
        for sketch in sketches:
            relative = sketch_paths[sketch].relative_to(PLATFORM).as_posix()
            assert matrix["applicable_boards"](relative, True) == category_targets[category]
    print("PASS board-oriented example menu layout, uniqueness, and FQBN routing")


def validate_platform_example_uniqueness_contracts() -> None:
    examples = PLATFORM / "examples"
    canonical = (
        "Basics/CoreVersionProbe/CoreVersionProbe.ino",
        "EGU/EguTriggerDemo/EguTriggerDemo.ino",
        "KMU/KmuCracenIkgSeedProof/KmuCracenIkgSeedProof.ino",
        "KMU/KmuMetadataProbe/KmuMetadataProbe.ino",
        "PWM/PwmDatasheetStress/PwmDatasheetStress.ino",
        "Peripherals/PeripheralProbe/PeripheralProbe.ino",
        "SPI/HighSpeedSpi32MHzProbe/HighSpeedSpi32MHzProbe.ino",
        "Runtime/RuntimePeripheralPinRemap/RuntimePeripheralPinRemap.ino",
        "Serial/SerialFabricExtraInstanceProbe/SerialFabricExtraInstanceProbe.ino",
        "Serial/SerialFabricRuntimeProbe/SerialFabricRuntimeProbe.ino",
        "Serial/SerialNonBlockingWriteProbe/SerialNonBlockingWriteProbe.ino",
        "TAMPC/TampcAdvancedConfigProbe/TampcAdvancedConfigProbe.ino",
        "TAMPC/TampcStatusReporter/TampcStatusReporter.ino",
        "VBAT/VbatReadViaAnalogRead/VbatReadViaAnalogRead.ino",
        "VBAT/VddReadViaInternalSaadc/VddReadViaInternalSaadc.ino",
        "VPR/VprCrc32OffloadProbe/VprCrc32OffloadProbe.ino",
        "VPR/VprCrc32cOffloadProbe/VprCrc32cOffloadProbe.ino",
        "VPR/VprFnv1aOffloadProbe/VprFnv1aOffloadProbe.ino",
        "VPR/VprHibernateContextProbe/VprHibernateContextProbe.ino",
        "VPR/VprHibernateResumeProbe/VprHibernateResumeProbe.ino",
        "VPR/VprHibernateWakeProbe/VprHibernateWakeProbe.ino",
        "VPR/VprRestartLifecycleProbe/VprRestartLifecycleProbe.ino",
        "VPR/VprSharedTransportProbe/VprSharedTransportProbe.ino",
        "VPR/VprTickerAsyncEventProbe/VprTickerAsyncEventProbe.ino",
        "VPR/VprTickerOffloadProbe/VprTickerOffloadProbe.ino",
        "Wire/WireImuRemapScanner/WireImuRemapScanner.ino",
        "Wire/WireRepeatedStartProbe/WireRepeatedStartProbe.ino",
        "Wire/WireTargetResponder/WireTargetResponder.ino",
    )
    missing = [relative for relative in canonical if not (examples / relative).is_file()]
    assert not missing, f"canonical platform examples are missing: {missing}"

    paths_by_digest = {}
    duplicate_groups = []
    for sketch in sorted(examples.rglob("*.ino")):
        digest = hashlib.sha256(sketch.read_bytes()).hexdigest()
        previous = paths_by_digest.get(digest)
        if previous is not None:
            duplicate_groups.append(
                (previous.relative_to(examples), sketch.relative_to(examples))
            )
        else:
            paths_by_digest[digest] = sketch
    assert not duplicate_groups, (
        f"byte-identical platform examples create duplicate menu entries: {duplicate_groups}"
    )
    print("PASS platform examples have one canonical path per byte-identical sketch")


def compile_and_run_host_tests(temp: Path) -> None:
    cxx = os.environ.get("CXX")
    if not cxx:
        cxx = "/usr/bin/g++" if Path("/usr/bin/g++").is_file() else "g++"
    if shutil.which(cxx) is None:
        raise SystemExit(f"C++ compiler not found: {cxx}")
    core = PLATFORM / "cores/nrf54l15"
    common = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    sanitizer_env = os.environ.copy()
    sanitizer_env["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=1"
    sanitizer_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    wstring = temp / "wstring_alias_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{core}",
        str(TESTS / "wstring_alias_test.cpp"),
        "-o", str(wstring),
    ])
    run([str(wstring)], env=sanitizer_env)
    print("PASS WString alias/self-concat ASan+UBSan")

    timer = temp / "software_timer_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-include", str(TESTS / "software_timer_stubs.h"),
        f"-I{core}",
        str(core / "SoftwareTimer.cpp"),
        str(TESTS / "software_timer_test.cpp"),
        "-o", str(timer),
    ])
    run([str(timer)], env=sanitizer_env)
    print("PASS SoftwareTimer deletion/lifetime ASan+UBSan")

    math_test = temp / "wiring_math_test"
    run(common + [
        "-fsanitize=undefined",
        "-fno-sanitize-recover=undefined",
        f"-I{core}",
        str(TESTS / "wiring_math_test.cpp"),
        "-o", str(math_test),
    ])
    run([str(math_test)], env=sanitizer_env)
    print("PASS map extreme-range UBSan")

    systick_test = temp / "systick_monotonic_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{PLATFORM / 'cores/nrf54common'}",
        str(TESTS / "systick_monotonic_test.cpp"),
        "-o", str(systick_test),
    ])
    run([str(systick_test)], env=sanitizer_env)
    print("PASS SysTick pending-reload and torn-epoch ASan+UBSan")

    ancs_test = temp / "ble_ancs_response_parser_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{PLATFORM / 'libraries' / 'Bluefruit52Lib' / 'src'}",
        str(TESTS / "ble_ancs_response_parser_test.cpp"),
        "-o", str(ancs_test),
    ])
    run([str(ancs_test)], env=sanitizer_env)
    print("PASS ANCS fragmented Data Source parser ASan+UBSan")

    matter_crypto = temp / "matter_pbkdf2_test"
    matter_src = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src"
    )
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1",
        f"-I{matter_src}",
        str(matter_src / "matter_pbkdf2.cpp"),
        str(TESTS / "matter_pbkdf2_test.cpp"),
        "-o", str(matter_crypto),
    ])
    run([str(matter_crypto)], env=sanitizer_env)
    print("PASS Matter SHA-256/HMAC/PBKDF2 vectors ASan+UBSan")

    matter_security = temp / "matter_session_security_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-no-pie",
        "-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1",
        f"-I{TESTS / 'matter_security_stubs'}",
        f"-I{TESTS / 'matter_endpoint_stubs'}",
        f"-I{matter_src}",
        str(matter_src / "matter_pbkdf2.cpp"),
        str(matter_src / "matter_secp256r1.cpp"),
        str(matter_src / "matter_rng.cpp"),
        str(matter_src / "matter_manual_pairing.cpp"),
        str(matter_src / "matter_pase_commissioning.cpp"),
        str(matter_src / "matter_case_session.cpp"),
        str(TESTS / "matter_session_security_test.cpp"),
        "-o", str(matter_security),
    ])
    run([str(matter_security)], env=sanitizer_env)
    print(
        "PASS Matter PASE/CASE roundtrip, tamper, reflection, and capacity "
        "ASan+UBSan"
    )

    matter_ip = temp / "matter_ip_address_test"
    matter_impl = PLATFORM / "libraries/Nrf54L15-Clean-Implementation"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{matter_impl / 'src/matter_core_stage'}",
        f"-I{matter_impl / 'third_party/connectedhomeip/config/arduino'}",
        f"-I{matter_impl / 'third_party/connectedhomeip/src'}",
        str(TESTS / "matter_ip_address_test.cpp"),
        "-o", str(matter_ip),
    ])
    run([str(matter_ip)], env=sanitizer_env)
    print("PASS Matter IPv6 parsing and fabric multicast ASan+UBSan")

    matter_acl = temp / "matter_access_control_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1",
        f"-I{matter_src}",
        str(matter_src / "matter_access_control.cpp"),
        str(TESTS / "matter_access_control_test.cpp"),
        "-o", str(matter_acl),
    ])
    run([str(matter_acl)], env=sanitizer_env)
    print("PASS Matter ACL matching and null identity handling ASan+UBSan")

    matter_endpoint = temp / "matter_endpoint_access_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1",
        f"-I{TESTS / 'matter_endpoint_stubs'}",
        f"-I{matter_src}",
        f"-I{matter_src / 'matter_core_stage'}",
        f"-I{matter_impl / 'third_party/connectedhomeip/config/arduino'}",
        f"-I{matter_impl / 'third_party/connectedhomeip/src'}",
        str(matter_src / "matter_access_control.cpp"),
        str(matter_src / "matter_onoff_light_endpoint.cpp"),
        str(TESTS / "matter_endpoint_access_test.cpp"),
        "-o", str(matter_endpoint),
    ])
    run([str(matter_endpoint)], env=sanitizer_env)
    print("PASS Matter endpoint read/command ACL enforcement ASan+UBSan")

    matter_counter = temp / "matter_message_counter_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{matter_src}",
        str(TESTS / "matter_message_counter_test.cpp"),
        "-o", str(matter_counter),
    ])
    run([str(matter_counter)], env=sanitizer_env)
    print("PASS Matter message replay counter wrap/order ASan+UBSan")

    for chip, contract in CHIPS.items():
        nvic_test = temp / f"nvic_layout_{chip}"
        run(common + [
            "-Wno-int-to-pointer-cast",
            f"-DEXPECTED_LAST_IRQ={contract['last_irq']}",
            f"-I{PLATFORM / 'cores' / chip}",
            str(TESTS / "nvic_layout_test.cpp"),
            "-o", str(nvic_test),
        ])
        run([str(nvic_test)])
        print(f"PASS {chip} NVIC register layout and IRQ priority coverage")


def main() -> int:
    validate_no_generated_uf2_artifacts()
    validate_vectors()
    validate_cmsis_priority_contracts()
    validate_hardware_serial_contracts()
    validate_pca10156_serial_route_contracts()
    validate_thread_crypto_build_contracts()
    validate_ble_disconnect_reason_contracts()
    validate_ble_connection_parameter_contracts()
    validate_ble_persistence_layout_contracts()
    validate_custom_gatt_initial_value_capacity_contracts()
    validate_phy_preserves_data_length_contract()
    validate_parser_output_validity_contracts()
    validate_zigbee_persistence_reset_contracts()
    validate_protocol_typed_reset_contracts()
    validate_matter_session_security_contracts()
    validate_channel_sounding_public_examples_contracts()
    validate_spi_contracts()
    validate_system_off_wake_contracts()
    validate_systick_monotonic_contracts()
    validate_two_board_gate_parser_contracts()
    validate_xiao_low_power_board_contracts()
    validate_xiao_retention_probe_contracts()
    validate_serial_fabric_runtime_probe_contracts()
    validate_ble_security_hardening_contracts()
    validate_two_board_gate_bond_clear_contracts()
    validate_board_example_menu_contracts()
    validate_platform_example_uniqueness_contracts()
    with tempfile.TemporaryDirectory(prefix="nrf54-core-io-") as directory:
        compile_and_run_host_tests(Path(directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
