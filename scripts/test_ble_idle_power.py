#!/usr/bin/env python3
"""Run issue #111 clock-selection and BLE GRTC initialization regressions.

Production functions run against Nordic's actual register layouts in host
memory. Only ARM barriers, IRQ masking and oscillator-start completion are
stubbed; no host test can measure current or prove oscillator startup timing.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
TIMING = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
    / "nrf54l15_hal_internal_ble_timing.inc"
)


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                result = source[start : index + 1]
                result = result.replace(
                    '__asm volatile("dsb 0xF" ::: "memory");', "hostBarrier();"
                )
                assert "__asm" not in result, "unexpected assembly in host harness"
                return result
    raise AssertionError(f"unterminated function: {signature}")


PREAMBLE = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

// Permit the simulated device to supply values for read-only registers.
#define __IM volatile
#include "DEVICE_TYPES_HEADER"
#include "nrf54_grtc_sleep.h"

static NRF_CLOCK_Type clockRegisters{};
static NRF_CLOCK_Type* const NRF_CLOCK = &clockRegisters;
static NRF_GRTC_Type grtcRegisters{};
static NRF_GRTC_Type* NRF_GRTC = nullptr;
#define NRF54L15_GRTC_SYSCOUNTER(grtc) ((grtc)->SYSCOUNTER[0])

static void hostBarrier() {}
static bool irqMasked = false;
static unsigned int irqDepth = 0;
static unsigned int lfclkEnsureCalls = 0;
#if defined(TEST_LM20)
static uint32_t resetReason = 0;
static uint32_t nrf54_core_reset_reason() { return resetReason; }
#endif
static uint32_t halEnterCritical() {
    assert(irqDepth++ == 0);
    const uint32_t previous = irqMasked ? 1U : 0U;
    irqMasked = true;
    return previous;
}
static void halExitCritical(uint32_t previous) {
    assert(irqDepth-- == 1 && irqMasked);
    irqMasked = previous != 0;
}
static void __attribute__((unused)) ensureBleGrtcLfclkRunning() {
    assert(irqMasked && irqDepth == 1);
    ++lfclkEnsureCalls;
}

static bool lfxoStarts = true;
static bool lfrcStarts = true;
static std::vector<uint32_t> waitedSources;

static void setRunningSource(uint32_t source) {
    NRF_CLOCK->LFCLK.STAT =
        (CLOCK_LFCLK_STAT_STATE_Running << CLOCK_LFCLK_STAT_STATE_Pos) |
        (source << CLOCK_LFCLK_STAT_SRC_Pos);
}

// Simulate completion or timeout at the existing oscillator-wait boundary.
static bool waitForLfclkStarted(uint32_t source, uint32_t spinLimit) {
    assert(spinLimit > 0 && spinLimit <= 240000000UL);
    assert(NRF_CLOCK->EVENTS_LFCLKSTARTED == 0);
    assert(NRF_CLOCK->TASKS_LFCLKSTART ==
           CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger);
    assert(((NRF_CLOCK->LFCLK.SRC & CLOCK_LFCLK_SRC_SRC_Msk) >>
            CLOCK_LFCLK_SRC_SRC_Pos) == source);
    waitedSources.push_back(source);
    const bool ready = source == CLOCK_LFCLK_STAT_SRC_LFXO
        ? lfxoStarts : lfrcStarts;
    if (ready) {
        setRunningSource(source);
        NRF_CLOCK->EVENTS_LFCLKSTARTED = 1;
    }
    return ready;
}

static void resetClock() {
    std::memset(static_cast<void*>(&clockRegisters), 0, sizeof(clockRegisters));
    waitedSources.clear();
    lfxoStarts = true;
    lfrcStarts = true;
}
"""


CASES = r"""
static void testClockSelection() {
    // A running crystal is reused without an additional start or wait.
    resetClock();
    setRunningSource(CLOCK_LFCLK_STAT_SRC_LFXO);
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 7;
    NRF_CLOCK->TASKS_LFCLKSTART = 9;
    assert(selectRunningGrtcLfClockSource() == GRTC_CLKCFG_CLKSEL_LFXO);
    assert(waitedSources.empty());
    assert(NRF_CLOCK->EVENTS_LFCLKSTARTED == 7);
    assert(NRF_CLOCK->TASKS_LFCLKSTART == 9);

    // First use waits for crystal readiness and selects direct LFXO.
    resetClock();
    assert(selectRunningGrtcLfClockSource() == GRTC_CLKCFG_CLKSEL_LFXO);
    assert(lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFXO));
#if defined(TEST_LM20)
    assert((waitedSources == std::vector<uint32_t>{CLOCK_LFCLK_STAT_SRC_LFXO}));
#else
    assert((waitedSources == std::vector<uint32_t>{
        CLOCK_LFCLK_STAT_SRC_LFRC, CLOCK_LFCLK_STAT_SRC_LFXO}));
#endif

    // Failed crystal startup preserves the running LFRC fallback.
    resetClock();
    setRunningSource(CLOCK_LFCLK_STAT_SRC_LFRC);
    lfxoStarts = false;
    assert(selectRunningGrtcLfClockSource() == GRTC_CLKCFG_CLKSEL_SystemLFCLK);
    assert(lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFRC));
    assert((waitedSources == std::vector<uint32_t>{CLOCK_LFCLK_STAT_SRC_LFXO}));

    // With no initial source, crystal failure still reaches a running LFRC.
    resetClock();
    lfxoStarts = false;
    assert(selectRunningGrtcLfClockSource() == GRTC_CLKCFG_CLKSEL_SystemLFCLK);
    assert(lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFRC));
#if defined(TEST_LM20)
    assert((waitedSources == std::vector<uint32_t>{
        CLOCK_LFCLK_STAT_SRC_LFXO, CLOCK_LFCLK_STAT_SRC_LFRC}));
#else
    assert((waitedSources == std::vector<uint32_t>{
        CLOCK_LFCLK_STAT_SRC_LFRC, CLOCK_LFCLK_STAT_SRC_LFXO}));
#endif

    // Complete clock failure must return through the bounded fallback path.
    resetClock();
    lfxoStarts = false;
    lfrcStarts = false;
    (void)selectRunningGrtcLfClockSource();
    assert(waitedSources.size() == 2);
}

#if defined(TEST_LM20)
static void testClockPreparation() {
    NRF_GRTC = &grtcRegisters;
    resetClock();
    std::memset(static_cast<void*>(&grtcRegisters), 0xA5, sizeof(grtcRegisters));
    grtcRegisters.MODE |= GRTC_MODE_SYSCOUNTEREN_Msk;
    NRF_GRTC_Type expected;
    std::memcpy(static_cast<void*>(&expected), &grtcRegisters, sizeof(expected));
    nrf54lm20b_core_prepare_grtc_clock();
    assert(std::memcmp(&grtcRegisters, &expected, sizeof(expected)) == 0);
    assert(waitedSources.empty());

    // A retained System OFF timer is protected even without SYSCOUNTEREN.
    grtcRegisters.MODE &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
    for (uint32_t reason : {RESET_RESETREAS_OFF_Msk, RESET_RESETREAS_GRTC_Msk}) {
        resetReason = reason;
        std::memcpy(static_cast<void*>(&expected), &grtcRegisters, sizeof(expected));
        nrf54lm20b_core_prepare_grtc_clock();
        assert(std::memcmp(&grtcRegisters, &expected, sizeof(expected)) == 0);
        assert(waitedSources.empty());
    }

    resetReason = 0;
    std::memcpy(static_cast<void*>(&expected), &grtcRegisters, sizeof(expected));
    expected.CLKCFG = (expected.CLKCFG & ~GRTC_CLKCFG_CLKSEL_Msk) |
        (GRTC_CLKCFG_CLKSEL_LFXO << GRTC_CLKCFG_CLKSEL_Pos);
    nrf54lm20b_core_prepare_grtc_clock();
    assert(std::memcmp(&grtcRegisters, &expected, sizeof(expected)) == 0);
    assert((waitedSources == std::vector<uint32_t>{CLOCK_LFCLK_STAT_SRC_LFXO}));
    NRF_GRTC = nullptr;
}
#endif

static void testBleInitialization(bool delayFirst) {
    // An unavailable device must not latch initialization or leak IRQ masking.
    initBleGrtc();
    assert(!irqMasked && irqDepth == 0 && lfclkEnsureCalls == 0);

    NRF_GRTC = &grtcRegisters;
    resetClock();
    std::memset(static_cast<void*>(&grtcRegisters), 0xA5, sizeof(grtcRegisters));
    grtcRegisters.MODE &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
#if defined(TEST_LM20)
    grtcRegisters.CLKCFG = GRTC_CLKCFG_CLKSEL_SystemLFCLK << GRTC_CLKCFG_CLKSEL_Pos;
    if (delayFirst) {
        // Simulate the timebase owner crossing its clock-prepare/START boundary.
        nrf54lm20b_core_prepare_grtc_clock();
        grtcRegisters.MODE |= GRTC_MODE_SYSCOUNTEREN_Msk;
        grtcRegisters.TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
    }
#else
    (void)delayFirst;
    grtcRegisters.CLKCFG = GRTC_CLKCFG_CLKSEL_LFXO << GRTC_CLKCFG_CLKSEL_Pos;
#endif
    const uint32_t initialMode = grtcRegisters.MODE;
    NRF_GRTC_Type expected;
    std::memcpy(static_cast<void*>(&expected), &grtcRegisters, sizeof(expected));
#if defined(TEST_LM20)
    expected.CLKCFG = (expected.CLKCFG & ~GRTC_CLKCFG_CLKSEL_Msk) |
        (GRTC_CLKCFG_CLKSEL_LFXO << GRTC_CLKCFG_CLKSEL_Pos);
#endif
    expected.TIMEOUT =
        (kNrf54GrtcSystemOnTimeoutLfclk << GRTC_TIMEOUT_VALUE_Pos) &
        GRTC_TIMEOUT_VALUE_Msk;
    expected.WAKETIME =
        (kNrf54GrtcSystemOnWakeLfclk << GRTC_WAKETIME_VALUE_Pos) &
        GRTC_WAKETIME_VALUE_Msk;
    expected.MODE = (initialMode &
        ~(GRTC_MODE_SYSCOUNTEREN_Msk | GRTC_MODE_AUTOEN_Msk)) |
        (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos) |
        (GRTC_MODE_AUTOEN_CpuActive << GRTC_MODE_AUTOEN_Pos);
    expected.TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
    expected.SYSCOUNTER[0].ACTIVE = GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active <<
        GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos;

    irqMasked = true;
    initBleGrtc();
    assert(irqMasked && irqDepth == 0);
#if defined(TEST_LM20)
    assert(lfclkEnsureCalls == 0);
    assert((waitedSources == std::vector<uint32_t>{CLOCK_LFCLK_STAT_SRC_LFXO}));
#else
    assert(lfclkEnsureCalls == 1);
#endif
    const uint32_t timeout =
        (grtcRegisters.TIMEOUT & GRTC_TIMEOUT_VALUE_Msk) >> GRTC_TIMEOUT_VALUE_Pos;
    const uint32_t wake =
        (grtcRegisters.WAKETIME & GRTC_WAKETIME_VALUE_Msk) >> GRTC_WAKETIME_VALUE_Pos;
    assert(timeout > wake + kNrf54GrtcSleepGuardLfclk);
    // Check the entire register image: counters, pending compares, clock source,
    // stop/clear tasks and unrelated mode bits must survive BLE initialization.
    assert(std::memcmp(&grtcRegisters, &expected, sizeof(expected)) == 0);

#if defined(TEST_LM20)
    // A later timebase initializer must not reselect a clock after BLE START.
    const auto previousWaits = waitedSources;
    nrf54lm20b_core_prepare_grtc_clock();
    assert(waitedSources == previousWaits);
    assert(std::memcmp(&grtcRegisters, &expected, sizeof(expected)) == 0);
#endif

    // Mark every register after initialization to detect even repeated writes.
    std::memset(static_cast<void*>(&grtcRegisters), 0x3C, sizeof(grtcRegisters));
    std::memcpy(static_cast<void*>(&expected), &grtcRegisters, sizeof(expected));
    irqMasked = false;
    const auto previousEnsureCalls = lfclkEnsureCalls;
    initBleGrtc();
    assert(!irqMasked && irqDepth == 0 && lfclkEnsureCalls == previousEnsureCalls);
    assert(std::memcmp(&grtcRegisters, &expected, sizeof(expected)) == 0);
}

int main(int argc, char**) {
    testClockSelection();
#if defined(TEST_LM20)
    testClockPreparation();
#endif
    testBleInitialization(argc > 1);
}
"""


def harness(chip: str, clock_source: str, ble_source: str) -> str:
    preamble = PREAMBLE.replace("DEVICE_TYPES_HEADER", f"{chip}_types.h")
    functions = [
        function(clock_source, "static bool lfclkRunningFrom("),
        function(clock_source, "static void startLfclkSource("),
        function(clock_source, "static bool ensureSystemOffLfxoRunning("),
        function(clock_source, "static uint32_t selectRunningGrtcLfClockSource("),
    ]
    if chip == "nrf54lm20b":
        functions.append(function(clock_source, "void nrf54lm20b_core_prepare_grtc_clock("))
        for owner in (
            function(clock_source, "static void initLowPowerTimebase("),
            function(ble_source, "void initBleGrtc()"),
        ):
            assert owner.index("nrf54lm20b_core_prepare_grtc_clock();") < owner.index(
                "->TASKS_START ="
            ), "clock must be selected before either GRTC owner issues START"
    functions.append(function(ble_source, "void initBleGrtc()"))
    return "\n".join([preamble, *functions, CASES])


def main() -> int:
    ble_source = TIMING.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="nrf54-ble-idle-power-") as directory:
        temporary = Path(directory)
        for chip in ("nrf54l15", "nrf54lm20b"):
            core = PLATFORM / "cores" / chip
            clock_source = (core / "wiring_time.c").read_text(encoding="utf-8")
            source = temporary / f"{chip}.cpp"
            binary = temporary / chip
            source.write_text(harness(chip, clock_source, ble_source), encoding="utf-8")
            command = [
                os.environ.get("CXX", "g++"), "-std=c++11", "-O2",
                "-Wall", "-Wextra", "-Werror", "-I", str(core),
                "-I", str(PLATFORM / "cores/nrf54common"),
            ]
            if chip == "nrf54lm20b":
                command += [
                    "-DARDUINO_XIAO_NRF54LM20A_CLEAN", "-DNRF54LM20A_XXAA",
                    "-DTEST_LM20",
                ]
            subprocess.run(command + [str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
            if chip == "nrf54lm20b":
                subprocess.run([str(binary), "delay-first"], check=True, timeout=10)
            print(f"PASS {chip} LF clock selection and BLE GRTC idle-power registers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
