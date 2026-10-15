#!/usr/bin/env python3
"""Source and model contracts for the generation-qualified RADIO arbiter."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMPLEMENTATION = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation/src"
)
HAL_CPP = IMPLEMENTATION / "nrf54l15_hal.cpp"
HAL_HEADER = IMPLEMENTATION / "nrf54l15_hal.h"
RAW_RADIO = (
    IMPLEMENTATION
    / "nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc"
)
BLE_CORE = (
    IMPLEMENTATION / "nrf54l15_hal_parts/nrf54l15_hal_ble_core_setup.inc"
)
RAW_CS_CPP = IMPLEMENTATION / "ble_channel_sounding.cpp"
RAW_CS_HEADER = IMPLEMENTATION / "ble_channel_sounding.h"
MPSL_CPP = IMPLEMENTATION / "ble_cs_controller_runtime.cpp"
MPSL_HEADER = IMPLEMENTATION / "ble_cs_controller_runtime.h"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    opening = text.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"missing opening brace: {signature}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def assert_order(text: str, *tokens: str) -> None:
    cursor = 0
    offsets: list[int] = []
    for token in tokens:
        offset = text.find(token, cursor)
        assert offset >= 0, f"missing ordered token: {token}"
        offsets.append(offset)
        cursor = offset + len(token)
    assert offsets == sorted(offsets)


class Owner(IntEnum):
    NONE = 0
    BLE = 1
    ZIGBEE = 2
    PROPRIETARY = 3
    RAW_CS = 4
    MPSL_CS = 5


@dataclass
class ArbiterModel:
    """Executable model of the source's PRIMASK-serialized lease state."""

    owner: Owner = Owner.NONE
    quarantined: bool = False
    generation: int = 0
    token: int = 0

    def acquire(self, owner: Owner | int) -> int:
        if (
            owner not in tuple(Owner)[1:]
            or self.owner != Owner.NONE
            or self.quarantined
        ):
            return 0
        if self.generation == 0xFFFFFFFF:
            self.quarantined = True
            return 0
        self.generation += 1
        self.token = self.generation
        self.owner = owner
        return self.token

    def release(self, owner: Owner, token: int) -> bool:
        matches = (
            owner != Owner.NONE
            and token != 0
            and not self.quarantined
            and owner == self.owner
            and token == self.token
        )
        if matches:
            self.token = 0
            self.owner = Owner.NONE
        return matches

    def quarantine(self, owner: Owner, token: int) -> bool:
        matches = (
            owner != Owner.NONE
            and token != 0
            and owner == self.owner
            and token == self.token
        )
        if matches:
            self.quarantined = True
        return matches

    def is_owned_by(self, owner: Owner, token: int) -> bool:
        return token != 0 and owner == self.owner and token == self.token


def test_exclusive_lease_model() -> None:
    model = ArbiterModel()
    assert model.acquire(Owner.NONE) == 0
    assert model.acquire(6) == 0
    assert model.acquire(0xFF) == 0

    first = model.acquire(Owner.BLE)
    assert first != 0
    assert model.is_owned_by(Owner.BLE, first)
    for contender in Owner:
        if contender != Owner.NONE:
            assert model.acquire(contender) == 0

    snapshot = (model.owner, model.token, model.quarantined)
    assert not model.release(Owner.ZIGBEE, first)
    assert not model.release(Owner.BLE, first + 1)
    assert not model.quarantine(Owner.PROPRIETARY, first)
    assert not model.quarantine(Owner.BLE, first + 1)
    assert (model.owner, model.token, model.quarantined) == snapshot

    assert model.release(Owner.BLE, first)
    assert model.owner == Owner.NONE and model.token == 0
    second = model.acquire(Owner.ZIGBEE)
    assert second != 0 and second != first
    assert not model.release(Owner.ZIGBEE, first)
    assert model.quarantine(Owner.ZIGBEE, second)
    assert model.is_owned_by(Owner.ZIGBEE, second)
    assert not model.release(Owner.ZIGBEE, second)
    for contender in Owner:
        assert model.acquire(contender) == 0

    exhausted = ArbiterModel(generation=0xFFFFFFFF)
    assert exhausted.acquire(Owner.RAW_CS) == 0
    assert exhausted.quarantined and exhausted.owner == Owner.NONE
    assert exhausted.acquire(Owner.BLE) == 0
    print(
        "PASS exclusive lease generation, contention, invalid-owner, "
        "stale-token, exhaustion, and quarantine model"
    )


def test_arbiter_source_atomicity() -> None:
    header = source(HAL_HEADER)
    implementation = source(HAL_CPP)
    for token in (
        "kNone = 0U",
        "kBle = 1U",
        "kZigbee802154 = 2U",
        "kProprietary = 3U",
        "kRawChannelSounding = 4U",
        "kMpslChannelSounding = 5U",
        "nrf54l15_acquire_exclusive_radio",
        "nrf54l15_release_exclusive_radio",
        "nrf54l15_quarantine_exclusive_radio",
        "nrf54l15_exclusive_radio_is_owned_by",
        "nrf54l15_quiesce_owned_radio",
        "nrf54l15_exclusive_radio_fail_stop",
    ):
        assert token in header

    acquire = function_body(
        implementation, "uint32_t nrf54l15_acquire_exclusive_radio("
    )
    release = function_body(
        implementation, "bool nrf54l15_release_exclusive_radio("
    )
    quarantine = function_body(
        implementation, "bool nrf54l15_quarantine_exclusive_radio("
    )
    owned = function_body(
        implementation, "bool nrf54l15_exclusive_radio_is_owned_by("
    )
    quiesce = function_body(
        implementation, "bool nrf54l15_quiesce_owned_radio("
    )
    fail_stop = function_body(
        implementation, "void nrf54l15_exclusive_radio_fail_stop()"
    )

    for body in (acquire, release, quarantine, owned):
        assert_order(body, "__get_PRIMASK()", "__disable_irq()", "__set_PRIMASK(primask)")
    valid_owner = function_body(implementation, "bool validExclusiveRadioOwner(")
    assert "value >= static_cast<uint8_t>(Nrf54ExclusiveRadioOwner::kBle)" in valid_owner
    assert "Nrf54ExclusiveRadioOwner::kMpslChannelSounding" in valid_owner
    assert "if (!validExclusiveRadioOwner(owner))" in acquire
    assert "g_exclusiveRadioQuarantined == 0U" in acquire
    assert_order(
        acquire,
        "g_exclusiveRadioGeneration == UINT32_MAX",
        "g_exclusiveRadioQuarantined = 1U",
        "++g_exclusiveRadioGeneration",
        "g_exclusiveRadioToken = token",
        "g_exclusiveRadioOwner = static_cast<uint8_t>(owner)",
    )
    assert "g_exclusiveRadioQuarantined == 0U" in release
    assert "g_exclusiveRadioOwner == static_cast<uint8_t>(owner)" in release
    assert "g_exclusiveRadioToken == token" in release
    assert_order(release, "if (matches)", "g_exclusiveRadioToken = 0U", "kNone")
    assert "g_exclusiveRadioOwner == static_cast<uint8_t>(owner)" in quarantine
    assert "g_exclusiveRadioToken == token" in quarantine
    assert "g_exclusiveRadioQuarantined = 1U" in quarantine
    assert implementation.count("g_exclusiveRadioQuarantined = 0U") == 1
    assert "token != 0U" in owned
    assert_order(
        quiesce,
        "nrf54l15_exclusive_radio_is_owned_by(owner, token)",
        "quiesceSharedRadioForSystemOff(spinLimit)",
    )
    assert_order(fail_stop, "__disable_irq()", "NVIC_SystemReset()", "__WFE()")
    print("PASS arbiter source is atomic, generation-qualified, and fail-stop quarantined")


def test_all_radio_clients_acquire_before_hardware_setup() -> None:
    raw = source(RAW_RADIO)
    ble = source(BLE_CORE)
    raw_cs = source(RAW_CS_CPP)
    mpsl = source(MPSL_CPP)

    zigbee_begin = function_body(raw, "bool ZigbeeRadio::begin(")
    proprietary_begin = function_body(raw, "bool RawRadioLink::begin(")
    ble_begin = function_body(ble, "bool BleRadio::begin(")
    raw_cs_begin = function_body(raw_cs, "bool BleChannelSoundingRadio::begin(")
    mpsl_begin = function_body(mpsl, "bool BleCsControllerRuntime::begin()")

    assert_order(
        zigbee_begin,
        "nrf54l15_acquire_exclusive_radio",
        "rawRadioHfxoRunning()",
        "ClockControl::startHfxo",
        "BoardControl::enableRfPath",
        "configureIeee802154()",
    )
    assert_order(
        proprietary_begin,
        "nrf54l15_acquire_exclusive_radio",
        "rawRadioHfxoRunning()",
        "ClockControl::startHfxo",
        "power_.setLatencyMode(PowerLatencyMode::kConstantLatency)",
        "configureProprietary1M()",
    )
    assert_order(
        ble_begin,
        "nrf54l15_acquire_exclusive_radio",
        "initBleGrtc()",
        "beginUnconnectedRadioActivity",
        "configureBle1M()",
    )
    assert_order(
        raw_cs_begin,
        "nrf54l15_acquire_exclusive_radio",
        "channelSoundingHfxoRunning()",
        "ClockControl::startHfxo",
        "power_.setLatencyMode(PowerLatencyMode::kConstantLatency)",
        "configureBle2MCommon()",
    )
    assert_order(
        mpsl_begin,
        "nrf54l15_acquire_exclusive_radio",
        "nrf54l15_quiesce_owned_radio",
        "gControllerActive = true",
        "configureMpslInterrupts()",
        "mpsl_init(",
    )
    for body in (
        zigbee_begin,
        proprietary_begin,
        ble_begin,
        raw_cs_begin,
        mpsl_begin,
    ):
        assert "if (radioOwnershipToken_ == 0U)" in body
        token_failure = body.index("if (radioOwnershipToken_ == 0U)")
        assert "return false;" in body[token_failure : token_failure + 180]

    print("PASS all five RADIO clients acquire their lease before hardware setup")


def test_strict_teardown_and_timeout_retention() -> None:
    hal = source(HAL_CPP)
    raw = source(RAW_RADIO)
    ble = source(BLE_CORE)
    raw_cs = source(RAW_CS_CPP)
    mpsl = source(MPSL_CPP)

    release_raw = function_body(raw, "bool releaseExclusiveRadioIfDisabled(")
    release_raw_cs = function_body(
        raw_cs, "bool releaseRawCsRadioOwnershipIfDisabled("
    )
    release_mpsl = function_body(
        mpsl, "bool releaseMpslRadioOwnershipIfQuiesced("
    )

    scrub_owned = function_body(hal, "bool nrf54l15_scrub_owned_radio_dma(")
    scrub_disabled = function_body(hal, "bool scrubRadioDmaPointersIfDisabled(")
    assert_order(
        scrub_owned,
        "nrf54l15_exclusive_radio_is_owned_by(owner, token)",
        "scrubRadioDmaPointersIfDisabled(radio)",
    )
    strict_scrub = scrub_disabled[scrub_disabled.index("const uint32_t state") :]
    assert_order(
        strict_scrub,
        "state != RADIO_STATE_STATE_Disabled",
        "return false;",
        "radio->TASKS_AUXDATADMASTOP",
        "__DSB()",
        "radio->AUXDATADMA[index].ENABLE = 0U",
        "radio->AUXDATADMA[index].PTR = 0U",
        "radio->AUXDATADMA[index].MAXCNT = 0U",
        "radio->DFEPACKET.PTR = 0U",
        "radio->DFEPACKET.MAXCNT = 0U",
        "radio->PACKETPTR = 0U",
        "__DSB()",
        "return radioDmaPointerIsCleared(radio->PACKETPTR)",
    )
    for readback in (
        "radioDmaPointerIsCleared(radio->DFEPACKET.PTR)",
        "radio->DFEPACKET.MAXCNT == 0U",
        "radio->AUXDATADMA[0].ENABLE == 0U",
        "radioDmaPointerIsCleared(radio->AUXDATADMA[0].PTR)",
        "radio->AUXDATADMA[0].MAXCNT == 0U",
        "radio->AUXDATADMA[1].ENABLE == 0U",
        "radioDmaPointerIsCleared(radio->AUXDATADMA[1].PTR)",
        "radio->AUXDATADMA[1].MAXCNT == 0U",
    ):
        assert readback in strict_scrub
    assert_order(
        release_raw,
        "nrf54l15_scrub_owned_radio_dma(owner, *token, radio)",
        "nrf54l15_release_exclusive_radio(owner, *token)",
        "*token = 0U",
    )
    assert_order(
        release_raw_cs,
        "!channelSoundingRadioDisabled(radio)",
        "nrf54l15_scrub_owned_radio_dma",
        "nrf54l15_release_exclusive_radio",
        "*token = 0U",
    )
    assert_order(
        release_mpsl,
        "nrf54l15_quiesce_owned_radio",
        "nrf54l15_release_exclusive_radio",
        "*token = 0U",
    )

    zigbee_end = function_body(raw, "void ZigbeeRadio::end()")
    proprietary_end = function_body(raw, "void RawRadioLink::end()")
    ble_end = function_body(ble, "void BleRadio::end()")
    raw_cs_end = function_body(raw_cs, "void BleChannelSoundingRadio::end()")
    mpsl_end = function_body(mpsl, "bool BleCsControllerRuntime::end()")

    for body, owner in (
        (zigbee_end, "kZigbee802154"),
        (proprietary_end, "kProprietary"),
        (ble_end, "kBle"),
        (raw_cs_end, "kRawChannelSounding"),
    ):
        assert body.index("if (radioOwnershipToken_ == 0U)") < body.index(
            "nrf54l15_exclusive_radio_is_owned_by"
        )
        assert owner in body

    zigbee_active = zigbee_end[zigbee_end.index("radio_->TASKS_DISABLE") :]
    assert_order(
        zigbee_active,
        "waitRadioStateDisabledBudgeted",
        "if (disabled)",
        "clearRadioCoreEvents",
        "releaseExclusiveRadioIfDisabled",
    )
    assert "releaseExclusiveRadioIfDisabled" not in zigbee_active[
        zigbee_active.index("const bool disabled") : zigbee_active.index("if (disabled)")
    ]

    proprietary_active = proprietary_end[
        proprietary_end.index("radio_->TASKS_DISABLE") :
    ]
    assert_order(
        proprietary_active,
        "waitRadioStateDisabledBudgeted",
        "if (disabled)",
        "clearRadioCoreEvents",
        "releaseExclusiveRadioIfDisabled",
    )

    ble_active = ble_end[ble_end.index("bleBackgroundDisableClockIrq()") :]
    assert_order(
        ble_active,
        "configureBackgroundAdvertisingHardware(false)",
        "bleDetachRadioRestartAutomation(radio_)",
        "waitRadioStateDisabledBudgeted",
        "clearRadioCoreEvents",
        "endUnconnectedRadioActivity()",
        "releaseExclusiveRadioIfDisabled",
    )
    failed_wait = ble_active[ble_active.index("if (!waitRadioStateDisabledBudgeted") :]
    assert_order(failed_wait, "return;", "clearRadioCoreEvents")

    raw_cs_active = raw_cs_end[raw_cs_end.index("radio_->TASKS_DISABLE") :]
    assert_order(
        raw_cs_active,
        "waitForRadioDisabled",
        "if (disabled)",
        "stopAndDisableAuxDataDma",
        "detachRawRadioAutomation",
        "clearEvents()",
        "releaseRawCsRadioOwnershipIfDisabled",
    )
    raw_cs_receive = function_body(
        raw_cs, "bool BleChannelSoundingRadio::receiveFrame("
    )
    phy_timeout = raw_cs_receive[
        raw_cs_receive.index("if (!waitForRadioPhyEnd") :
        raw_cs_receive.index("if (config_.enableRawDfeCapture")
    ]
    assert_order(
        phy_timeout,
        "radio_->TASKS_DISABLE",
        "waitForRadioDisabled",
        "if (captureRtt &&",
        "stopAndDisableAuxDataDma",
        "clearEvents()",
    )

    assert_order(
        mpsl_end,
        "sdc_disable()",
        "if (result != 0)",
        "return false;",
        "gControllerActive = false",
        "disableMpslInterrupts()",
        "mpsl_uninit()",
        "releaseMpslRadioOwnershipIfQuiesced",
    )

    for text, signature, owner in (
        (raw, "ZigbeeRadio::~ZigbeeRadio()", "kZigbee802154"),
        (raw, "RawRadioLink::~RawRadioLink()", "kProprietary"),
        (ble, "BleRadio::~BleRadio()", "kBle"),
        (raw_cs, "BleChannelSoundingRadio::~BleChannelSoundingRadio()", "kRawChannelSounding"),
        (mpsl, "BleCsControllerRuntime::~BleCsControllerRuntime()", "kMpslChannelSounding"),
    ):
        destructor = function_body(text, signature)
        assert "radioOwnershipToken_ != 0U" in destructor
        assert owner in destructor
        assert_order(
            destructor,
            "nrf54l15_quarantine_exclusive_radio",
            "nrf54l15_exclusive_radio_fail_stop",
        )

    print("PASS teardown releases last, retains timed-out leases, and fail-stops destructors")


def test_unbegun_instance_and_mutator_isolation() -> None:
    raw = source(RAW_RADIO)
    ble = source(BLE_CORE)
    raw_cs = source(RAW_CS_CPP)
    mpsl = source(MPSL_CPP)
    header = source(HAL_HEADER)
    raw_cs_header = source(RAW_CS_HEADER)
    mpsl_header = source(MPSL_HEADER)

    for text in (header, raw_cs_header, mpsl_header):
        assert "radioOwnershipToken_" in text

    for text, signature in (
        (raw, "bool ZigbeeRadio::setChannel("),
        (raw, "bool ZigbeeRadio::setTxPowerDbm("),
        (raw, "bool ZigbeeRadio::setCcaEnergyDetectThresholdDbm("),
        (raw, "bool RawRadioLink::setFrequencyOffsetMhz("),
        (raw, "bool RawRadioLink::setPipe("),
        (raw, "bool RawRadioLink::setTxPowerDbm("),
        (ble, "bool BleRadio::setTxPowerDbm("),
        (ble, "bool BleRadio::selectExternalAntenna("),
        (raw_cs, "bool BleChannelSoundingRadio::setLogicalChannel("),
    ):
        body = function_body(text, signature)
        assert "nrf54l15_exclusive_radio_is_owned_by" in body, (
            f"RADIO mutator lacks exact lease guard: {signature}"
        )
        assert "radioOwnershipToken_" in body

    # A second, never-begun instance has token zero. Its end path must return
    # before TASKS_DISABLE, HFXO, RF, MPSL, or interrupt teardown can touch the
    # first instance's live hardware lease.
    for text, signature, forbidden in (
        (raw, "void ZigbeeRadio::end()", "radio_->TASKS_DISABLE"),
        (raw, "void RawRadioLink::end()", "radio_->TASKS_DISABLE"),
        (ble, "void BleRadio::end()", "bleBackgroundDisableClockIrq()"),
        (raw_cs, "void BleChannelSoundingRadio::end()", "radio_->TASKS_DISABLE"),
    ):
        body = function_body(text, signature)
        zero = body.index("if (radioOwnershipToken_ == 0U)")
        early_return = body.index("return;", zero)
        hardware = body.index(forbidden)
        assert zero < early_return < hardware

    inactive_mpsl_end = function_body(mpsl, "bool BleCsControllerRuntime::end()")
    inactive = inactive_mpsl_end[: inactive_mpsl_end.index("const int32_t result")]
    assert "releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_)" in inactive
    release_helper = function_body(
        mpsl, "bool releaseMpslRadioOwnershipIfQuiesced("
    )
    assert_order(release_helper, "*token == 0U", "return true;")
    print("PASS unbegun instances and RADIO mutators cannot disturb another lease")


def test_irq_dispatch_is_owner_gated() -> None:
    implementation = source(HAL_CPP)
    mpsl = source(MPSL_CPP)
    radio_irq = function_body(implementation, 'extern "C" void RADIO_0_IRQHandler(void)')
    assert "nrf54l15_exclusive_radio_owner()" in radio_irq
    assert "kMpslChannelSounding" in radio_irq
    assert "kZigbee802154" in radio_irq
    assert "kBle" in radio_irq
    assert "nrf54_cs_controller_radio_irq_service" in radio_irq
    assert "g_activeZigbeeRadioIrq->serviceBufferedReceiveIrq()" in radio_irq
    assert "bleScanSleepWaitHandleRadioIrq" in radio_irq

    for signature, handler in (
        ('extern "C" void TIMER10_IRQHandler(void)', "MPSL_IRQ_TIMER0_Handler"),
        ('extern "C" void GRTC_3_IRQHandler(void)', "MPSL_IRQ_RTC0_Handler"),
        ('extern "C" void SWI00_IRQHandler(void)', "gLowPriorityPending = true"),
    ):
        body = function_body(mpsl, signature)
        assert "gControllerActive" in body
        assert "nrf54l15_exclusive_radio_owner()" in body
        assert "kMpslChannelSounding" in body
        assert handler in body

    clock_irq = function_body(
        mpsl, 'extern "C" void CLOCK_POWER_IRQHandler(void)'
    )
    assert "kMpslChannelSounding" in clock_irq
    assert "kBle" in clock_irq
    assert "nrf54l15_ble_clock_irq_service" in clock_irq
    cs_radio_irq = function_body(
        mpsl, 'extern "C" bool nrf54_cs_controller_radio_irq_service(void)'
    )
    assert "kMpslChannelSounding" in cs_radio_irq
    assert "MPSL_IRQ_RADIO_Handler()" in cs_radio_irq
    print("PASS RADIO, MPSL timer, RTC, SWI, and CLOCK IRQ dispatch is owner-gated")


def main() -> None:
    test_exclusive_lease_model()
    test_arbiter_source_atomicity()
    test_all_radio_clients_acquire_before_hardware_setup()
    test_strict_teardown_and_timeout_retention()
    test_unbegun_instance_and_mutator_isolation()
    test_irq_dispatch_is_owner_gated()
    print("PASS all exclusive RADIO arbiter contracts")


if __name__ == "__main__":
    main()
