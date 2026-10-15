#!/usr/bin/env python3
"""Execute production SAADC configuration and sampling with host MMIO mocks."""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
SOURCE = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"


def between(text: str, start: str, end: str) -> str:
    first = text.index(start)
    return text[first:text.index(end, first)]


def production_declarations(core: str) -> str:
    registers = (SOURCE / "nrf54l15_regs.h").read_text(encoding="utf-8")
    header = (SOURCE / "nrf54l15_hal.h").read_text(encoding="utf-8")
    types = (PLATFORM / f"cores/{core}/{core}_types.h").read_text(
        encoding="utf-8"
    )
    internal_inputs = []
    for name in ("Avdd", "Dvdd", "Vdd"):
        match = re.search(
            rf"^\s*#define SAADC_CH_PSELP_INTERNAL_{name}\s+(\S+)",
            types,
            re.MULTILINE,
        )
        assert match, f"missing internal input {name} for {core}"
        internal_inputs.append(
            f"#define SAADC_CH_PSELP_INTERNAL_{name} {match[1]}\n"
        )
    return (
        "".join(internal_inputs)
        + "namespace nrf54l15 {\n"
        + "constexpr uint32_t SAADC_BASE = 0x40000000U;\n"
        + between(registers, "namespace saadc {", "namespace timer {")
        + "}\nnamespace xiao_nrf54l15 {\n"
        + "using namespace nrf54l15;\n"
        + between(header, "enum class AdcResolution", "enum class BoardAntennaPath")
        + "}\n"
    )


def production_implementation() -> str:
    analog = (SOURCE / "nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc").read_text(
        encoding="utf-8"
    )
    support = (SOURCE / "nrf54l15_hal_support.cpp").read_text(encoding="utf-8")
    return (
        between(support, "uint32_t saadcPselValue(", "uint32_t spimPrescaler(")
        + between(analog, "Saadc::Saadc(", "bool Saadc::sampleMilliVolts(")
    )


HARNESS = r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include "xiao_nrf54l15_pins.h"

@DECLARATIONS@

namespace xiao_nrf54l15 {
namespace {
constexpr uint32_t kBase = nrf54l15::SAADC_BASE;
constexpr uint32_t kBurst = 0x5E8U;
constexpr uint32_t kSentinel = 0xA5A55A5AU;
constexpr uint32_t kSpinLimit = 1234U;
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
constexpr bool kGlobalBurst = true;
#else
constexpr bool kGlobalBurst = false;
#endif
static std::array<uint32_t, 0x700 / sizeof(uint32_t)> registers;
static std::vector<std::pair<uint32_t, uint32_t>> writes;
static unsigned accesses;
static uint8_t expectedChannel;
alignas(4) volatile int16_t g_saadcSampleScratch;

struct Register {
  uint32_t offset;
  operator uint32_t() const { return registers.at(offset / 4U); }
  void operator=(uint32_t value) const {
    registers.at(offset / 4U) = value;
    writes.emplace_back(offset, value);
  }
};

Register reg32(uint32_t address) {
  ++accesses;
  assert(address >= kBase && (address - kBase) % 4U == 0U);
  assert(address - kBase < registers.size() * 4U);
  return {address - kBase};
}

uint32_t value(uint32_t offset) { return registers.at(offset / 4U); }

unsigned writesAt(uint32_t offset) {
  unsigned count = 0;
  for (const auto& write : writes) count += write.first == offset;
  return count;
}

void clearEvent(uint32_t base, uint32_t event) { reg32(base + event) = 0U; }

bool waitForEvent(uint32_t base, uint32_t event, uint32_t spinLimit) {
  assert(base == kBase);
  assert(spinLimit == kSpinLimit || spinLimit == 300000U);
  if (event == saadc::EVENTS_CALIBRATEDONE) {
    assert(value(saadc::TASKS_CALIBRATEOFFSET) == 1U);
    return true;
  }
  if (event == saadc::EVENTS_STARTED) {
    assert(value(saadc::TASKS_START) == 1U);
    assert(value(saadc::RESULT_MAXCNT) == sizeof(int16_t));
    assert(value(saadc::RESULT_PTR) ==
           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_saadcSampleScratch)));
    return true;
  }
  if (event == saadc::EVENTS_STOPPED) {
    assert(value(saadc::TASKS_STOP) == 1U);
    return true;
  }
  assert(event == saadc::EVENTS_END);
  // TASKS_SAMPLE is a scalar Trigger task; CH[n].PSELP enables channel n.
  assert(value(saadc::TASKS_SAMPLE) == 1U);
  uint32_t connected = 0U;
  for (uint8_t channel = 0U; channel < 8U; ++channel) {
    if ((value(saadc::CH_PSELP + channel * saadc::CH_STRIDE) >> 30U) != 0U) {
      connected |= 1U << channel;
    }
  }
  assert(connected == (1U << expectedChannel));
  const uint32_t config = value(saadc::CH_CONFIG + expectedChannel * saadc::CH_STRIDE);
  const bool burst = kGlobalBurst ? value(kBurst) != 0U : (config & (1U << 11U)) != 0U;
  // Without burst, one SAMPLE contributes only one of 2^OVERSAMPLE samples.
  if (value(saadc::OVERSAMPLE) != 0U && !burst) return false;
  g_saadcSampleScratch = 1234;
  registers[saadc::RESULT_AMOUNT / 4U] = sizeof(int16_t);
  return true;
}
}  // namespace

@IMPLEMENTATION@

void reset() {
  registers.fill(kSentinel);
  writes.clear();
  accesses = 0U;
}

void assertDisconnected() {
  for (uint8_t channel = 0U; channel < 8U; ++channel) {
    const uint32_t offset = channel * saadc::CH_STRIDE;
    assert(value(saadc::CH_PSELP + offset) == 0U);
    assert(value(saadc::CH_PSELN + offset) == 0U);
    assert(value(saadc::CH_CONFIG + offset) == 0U);
  }
}

void assertConfiguration(uint8_t path, uint8_t channel, bool burst,
                         uint16_t tacq, uint8_t tconv) {
  const uint32_t activeOffset = channel * saadc::CH_STRIDE;
  const uint32_t activeConfig =
      (7U << 8U) | (static_cast<uint32_t>(path == 2U) << 15U) |
      (static_cast<uint32_t>(tacq) << 16U) | (static_cast<uint32_t>(tconv) << 28U) |
      ((!kGlobalBurst && burst) ? (1U << 11U) : 0U);
  assert(value(saadc::CH_CONFIG + activeOffset) == activeConfig);
  for (uint8_t other = 0U; other < 8U; ++other) {
    if (other == channel) continue;
    const uint32_t offset = other * saadc::CH_STRIDE;
    assert(value(saadc::CH_PSELP + offset) == 0U);
    assert(value(saadc::CH_PSELN + offset) == 0U);
    assert(value(saadc::CH_CONFIG + offset) == 0U);
  }
  if (path == 1U) {
    assert(value(saadc::CH_PSELP + activeOffset) ==
           ((static_cast<uint32_t>(AdcInternalInput::kVdd) << 12U) | (2U << 30U)));
  } else {
    assert(value(saadc::CH_PSELP + activeOffset) == saadcPselValue(kPinA0));
  }
  assert(value(saadc::CH_PSELN + activeOffset) ==
         (path == 2U ? saadcPselValue(kPinA1) : 0U));
  assert(writesAt(kBurst) == (kGlobalBurst ? 2U : 0U));
  assert(value(kBurst) == (kGlobalBurst ? static_cast<uint32_t>(burst) : kSentinel));
  // Configuration must not touch BURST + channel stride or adjacent registers.
  for (const auto& write : writes) {
    const uint32_t offset = write.first;
    const bool channelRegister = offset >= 0x510U && offset <= 0x588U &&
        ((offset - 0x510U) % 0x10U <= 8U);
    assert(channelRegister || (kGlobalBurst && offset == kBurst));
  }
}

void configure(Saadc& adc, uint8_t path, uint8_t channel, bool burst,
               uint16_t tacq = 159U, uint8_t tconv = 4U) {
  bool configured = false;
  if (path == 0U) {
    configured = adc.configureSingleEnded(channel, kPinA0, AdcGain::k2over8,
                                          tacq, tconv, burst);
  } else if (path == 1U) {
    configured = adc.configureSingleEnded(channel, AdcInternalInput::kVdd,
                                          AdcGain::k2over8, tacq, tconv, burst);
  } else {
    configured = adc.configureDifferential(channel, kPinA0, kPinA1,
                                           AdcGain::k2over8, tacq, tconv, burst);
  }
  assert(configured);
}

int runTests() {
  Saadc adc;
  int16_t raw = -1;
  reset();
  assert(!adc.sampleRaw(&raw, kSpinLimit));
  assert(!adc.configureSingleEnded(8U, kPinA0));
  assert(!adc.configureSingleEnded(8U, AdcInternalInput::kVdd));
  assert(!adc.configureSingleEnded(0U, kPinDisconnected));
  assert(!adc.configureDifferential(8U, kPinA0, kPinA1));
  assert(!adc.configureDifferential(0U, kPinA0, kPinA0));
  assert(accesses == 0U);

  reset();
  assert(adc.begin(AdcResolution::k12bit, kSpinLimit));
  assert(value(saadc::OVERSAMPLE) == 0U);
  assertDisconnected();
  assert(writesAt(kBurst) == (kGlobalBurst ? 1U : 0U));

  for (uint8_t oversample = 0U; oversample <= 8U; ++oversample) {
    for (uint8_t path = 0U; path < 3U; ++path) {
      for (uint8_t channel : {0U, 1U, 7U}) {
        for (bool burst : {false, true}) {
          reset();
          assert(adc.begin(AdcResolution::k14bit,
                           static_cast<AdcOversample>(oversample), kSpinLimit));
          assert(value(saadc::RESOLUTION) == 3U);
          assert(value(saadc::OVERSAMPLE) == oversample);
          assert(value(saadc::SAMPLERATE) == 0U);
          assert(value(saadc::NOISESHAPE) == 0U);
          assertDisconnected();
          assert(writesAt(kBurst) == (kGlobalBurst ? 1U : 0U));
          writes.clear();
          configure(adc, path, channel, burst);
          assertConfiguration(path, channel, burst, 159U, 4U);
          assert(value(saadc::RESOLUTION) == 3U);
          assert(value(saadc::OVERSAMPLE) == oversample);
          assert(value(saadc::SAMPLERATE) == 0U);
          assert(value(saadc::NOISESHAPE) == 0U);

          expectedChannel = channel;
          raw = -1;
          const bool expectedResult = oversample == 0U || burst;
          assert(adc.sampleRaw(&raw, kSpinLimit) == expectedResult);
          assert(raw == (expectedResult ? 1234 : -1));
          assert(value(saadc::RESULT_MAXCNT) == 0U);
          assert(value(saadc::RESULT_PTR) == 0U);
          writes.clear();
          adc.end();
          assertDisconnected();
          assert(value(saadc::ENABLE) == 0U);
          assert(writesAt(kBurst) == (kGlobalBurst ? 1U : 0U));
          assert(value(kBurst) == (kGlobalBurst ? 0U : kSentinel));
        }
      }
    }
  }

  for (uint8_t path = 0U; path < 3U; ++path) {
    reset();
    configure(adc, path, 7U, true, 0U, 0U);
    assertConfiguration(path, 7U, true, 1U, 1U);
    writes.clear();
    configure(adc, path, 1U, false, 1000U, 255U);
    assertConfiguration(path, 1U, false, 319U, 7U);
  }
  return 0;
}
}  // namespace xiao_nrf54l15

int main() { return xiao_nrf54l15::runTests(); }
"""


def main() -> None:
    implementation = production_implementation()
    targets = (
        ("NRF54L15_XXAA", "nrf54l15"),
        ("NRF54LM20A_XXAA", "nrf54lm20b"),
        ("NRF54LM20B_XXAA", "nrf54lm20b"),
    )
    with tempfile.TemporaryDirectory(prefix="nrf54-saadc-") as temporary:
        for chip, core in targets:
            cpp = Path(temporary) / f"{chip}.cpp"
            binary = Path(temporary) / chip
            cpp.write_text(
                HARNESS.replace("@DECLARATIONS@", production_declarations(core))
                .replace("@IMPLEMENTATION@", implementation),
                encoding="utf-8",
            )
            subprocess.run(
                shlex.split(os.environ.get("CXX", "c++"))
                + [
                    "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    f"-D{chip}", "-I", str(SOURCE), str(cpp), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
            print(f"PASS {chip}: 3 configure paths, channels 0/1/7, all oversampling modes")
            print(f"PASS {chip}: burst/bypass, single SAMPLE, reset, bounds and adjacent MMIO")


if __name__ == "__main__":
    main()
