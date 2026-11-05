#!/usr/bin/env python3
"""Execute the production PMIC timer, sleep and telemetry APIs with a fake bus."""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

from test_ble_security_policy_contracts import function_body


ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src"


def main() -> None:
    source = (HAL / "npm1300.cpp").read_text()
    functions = "\n".join(
        function_body(source, (signature,), signature)
        for signature in (
            "static inline uint16_t adc10(",
            "static int32_t rounded_div_i64(",
            "static int32_t ibat_to_ma(",
            "static bool timer_load(",
            "bool npm1300_vbus_status(",
            "static bool npm1300_vbus_absent_for_ship_hibernate(",
            "bool npm1300_read_ibat_signed_ma(",
            "bool npm1300_enter_ship_mode(",
            "bool npm1300_enter_hibernate(",
            "bool npm1300_configure_hibernate_timer_ms(",
            "bool npm1300_enter_timed_hibernate_ms(",
            "bool npm1300_enter_hibernate_after_ms(",
            "bool npm1300_set_ship_debounce(",
            "bool npm1300_apply_ship_config(",
            "bool npm1300_watchdog_start(",
            "bool npm1300_watchdog_kick(",
            "bool npm1300_watchdog_stop(",
            "bool npm1300_reset_cause(",
            "bool npm1300_clear_reset_cause(",
        )
    )
    constants = "\n".join(
        re.search(
            rf"^(?:static )?constexpr (?:u?int\d+_t)\s+{name}\s*=[^;]+;",
            source,
            re.MULTILINE,
        ).group(0)
        for name in sorted(set(re.findall(r"\bk[A-Z]\w+", functions)))
    )
    adc_struct = source[source.index("struct AdcResults {") : source.index("static bool g_probeValid")]
    harness = r"""
#include "npm1300.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

struct Operation {
  bool write;
  uint8_t base;
  uint8_t offset;
  std::vector<uint8_t> data;
};
static std::vector<Operation> operations;
static uint8_t registers[15][256];
static unsigned failAt;
static bool failAllStatusReads;
static unsigned statusReadsBeforeReady;
static unsigned statusReads;
static unsigned delays;
static bool timerRunning;
static bool adcReadOk;
static unsigned adcReads;
static int32_t g_chargeCurrentUa = 100000;
static int32_t g_dischargeLimitUa = 1000000;
struct SleepEntered { uint32_t milliseconds; };
[[noreturn]] void systemOffWakeReset(uint32_t ms) { throw SleepEntered{ms}; }
void delay(uint32_t ms) { assert(ms == 1); ++delays; }
bool npm1300_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t length) {
  operations.push_back({true, base, offset, std::vector<uint8_t>(data, data + length)});
  if (operations.size() == failAt) return false;
  memcpy(&registers[base][offset], data, length);
  if (base == NPM1300_BASE_TIMER && offset == 0x01) timerRunning = false;
  if (base == NPM1300_BASE_TIMER && offset == 0x00) timerRunning = true;
  return true;
}
bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value) {
  return npm1300_write_burst(base, offset, &value, 1);
}
bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t* value) {
  operations.push_back({false, base, offset, {}});
  if (!value || operations.size() == failAt) return false;
  if (base == NPM1300_BASE_TIMER && offset == 0x06) {
    ++statusReads;
    if (failAllStatusReads) return false;
    *value = statusReads > statusReadsBeforeReady ? 0x02 : 0;
  } else {
    *value = registers[base][offset];
  }
  return true;
}
static bool read_charge_current_limit_ua(int32_t* value) {
  *value = 100000;
  return true;
}
static bool read_discharge_limit_ua(int32_t* value) {
  *value = 1000000;
  return true;
}
""" + adc_struct + r"""
static AdcResults snapshot;
static bool read_adc_results(AdcResults* out) {
  ++adcReads;
  if (!adcReadOk) return false;
  *out = snapshot;
  return true;
}
""" + constants + "\n" + functions + r"""

static void reset(uint8_t timerMode = 0) {
  operations.clear();
  memset(registers, 0, sizeof(registers));
  registers[7][0x05] = timerMode;
  failAt = 0;
  failAllStatusReads = false;
  statusReadsBeforeReady = 0;
  statusReads = 0;
  delays = 0;
  timerRunning = true;
  adcReadOk = true;
  adcReads = 0;
  snapshot = {};
}
static void expect(size_t index, bool write, uint8_t base, uint8_t offset,
                   const std::vector<uint8_t>& data = {}) {
  assert(index < operations.size());
  const auto& op = operations[index];
  assert(op.write == write && op.base == base && op.offset == offset && op.data == data);
}
static bool wrote(uint8_t base, uint8_t offset) {
  for (const auto& op : operations) if (op.write && op.base == base && op.offset == offset) return true;
  return false;
}
static void expectTarget(uint32_t ms) {
  const uint32_t ticks = (ms + 15) / 16;
  expect(0, true, 7, 0x01, {1});
  expect(2, true, 7, 0x08,
         {uint8_t(ticks >> 16), uint8_t(ticks >> 8), uint8_t(ticks)});
  expect(3, true, 7, 0x03, {1});
}

int main() {
  for (uint32_t ms : {0U, uint32_t(NPM1300_HIBERNATE_TIMER_MAX_MS + 1),
                     std::numeric_limits<uint32_t>::max()}) {
    reset();
    assert(!npm1300_watchdog_start(ms));
    assert(!npm1300_configure_hibernate_timer_ms(ms));
    assert(!npm1300_enter_timed_hibernate_ms(ms));
    assert(operations.empty());
  }
  for (uint32_t ms : {1U, 15U, 16U, 17U, 4096U, 1048576U,
                     uint32_t(NPM1300_HIBERNATE_TIMER_MAX_MS)}) {
    reset(4);
    assert(npm1300_watchdog_start(ms));
    assert(operations.size() == 6 && timerRunning);
    expectTarget(ms);
    expect(1, true, 7, 0x05, {2});
    expect(4, false, 7, 0x06);
    expect(5, true, 7, 0x00, {1});
    reset(2);
    assert(npm1300_configure_hibernate_timer_ms(ms));
    assert(operations.size() == 4 && !timerRunning);
    expectTarget(ms);
    expect(1, true, 7, 0x05, {4});
  }
  // Setup writes fail closed; no start task can follow an unsuccessful setup.
  for (unsigned failure : {1U, 2U, 3U, 4U, 6U}) {
    reset();
    failAt = failure;
    assert(!npm1300_watchdog_start(10000));
    assert(operations.size() == failure);
    if (failure < 6) assert(!wrote(7, 0x00));
  }
  for (unsigned failure = 1; failure <= 4; ++failure) {
    reset();
    failAt = failure;
    assert(!npm1300_configure_hibernate_timer_ms(10000));
    assert(operations.size() == failure && !wrote(7, 0x00));
  }
  reset();
  statusReadsBeforeReady = 3;
  assert(npm1300_watchdog_start(10000));
  assert(statusReads == 4 && delays == 3 && timerRunning);
  for (bool readFailure : {false, true}) {
    reset();
    statusReadsBeforeReady = 100;
    failAllStatusReads = readFailure;
    assert(!npm1300_watchdog_start(10000));
    assert(statusReads == 20 && delays == 20 && !timerRunning && !wrote(7, 0x00));
  }
  reset();
  failAt = 5;  // A transient status-read failure may be retried before starting.
  assert(npm1300_watchdog_start(10000));
  assert(delays == 1 && operations.size() == 7);
  reset();
  assert(npm1300_watchdog_kick());
  assert(npm1300_watchdog_stop());
  expect(0, true, 7, 0x04, {1});
  expect(1, true, 7, 0x01, {1});
  for (auto function : {npm1300_watchdog_kick, npm1300_watchdog_stop}) {
    reset(); failAt = 1;
    assert(!function());
  }

  reset(2);
  assert(npm1300_enter_ship_mode());
  expect(0, false, 2, 0x07);
  expect(1, true, 7, 0x01, {1});
  expect(2, true, 11, 0x02, {1});
  for (unsigned failure = 1; failure <= 3; ++failure) {
    reset(2); failAt = failure;
    assert(!npm1300_enter_ship_mode());
    assert(operations.size() == failure);
    if (failure < 3) assert(!wrote(11, 0x02));
  }
  for (uint8_t mode = 0; mode < 16; ++mode) {
    reset(mode);
    assert(npm1300_enter_hibernate());
    expect(0, false, 2, 0x07);
    expect(1, false, 7, 0x05);
    if ((mode & 7) == 4) {
      assert(operations.size() == 3 && timerRunning);
      expect(2, true, 11, 0x00, {1});
      assert(!wrote(7, 0x01));
    } else {
      assert(operations.size() == 4 && !timerRunning);
      expect(2, true, 7, 0x01, {1});
      expect(3, true, 11, 0x00, {1});
    }
  }
  for (unsigned failure = 1; failure <= 4; ++failure) {
    reset(2); failAt = failure;
    assert(!npm1300_enter_hibernate());
    assert(operations.size() == failure);
    if (failure < 4) assert(!wrote(11, 0x00));
  }
  for (auto function : {npm1300_enter_ship_mode, npm1300_enter_hibernate}) {
    reset(); registers[2][0x07] = NPM1300_VBUS_STATUS_PRESENT;
    assert(!function());
    assert(operations.size() == 1);
  }

  reset(2);
  assert(npm1300_enter_timed_hibernate_ms(20000));
  assert(registers[7][0x05] == 4 && !timerRunning && delays == 1);
  assert(wrote(11, 0x00) && !wrote(7, 0x00));
  // Clear precedes wake-mode setup; entering Hibernate must not clear it again.
  unsigned stopWrites = 0;
  for (const auto& op : operations) if (op.write && op.base == 7 && op.offset == 1) ++stopWrites;
  assert(stopWrites == 1);
  reset(2);
  assert(npm1300_enter_hibernate_after_ms(20000));
  reset(2); registers[2][0x07] = NPM1300_VBUS_STATUS_PRESENT;
  try {
    (void)npm1300_enter_timed_hibernate_ms(20000);
    assert(false);
  } catch (const SleepEntered& wake) {
    assert(wake.milliseconds == 20000 && operations.size() == 2);
    expect(1, true, 7, 0x01, {1});
  }
  for (unsigned failure : {1U, 2U}) {
    reset(2); registers[2][0x07] = NPM1300_VBUS_STATUS_PRESENT; failAt = failure;
    assert(!npm1300_enter_timed_hibernate_ms(20000));
    assert(operations.size() == failure && !wrote(7, 0x05) && !wrote(11, 0));
  }

  for (uint8_t code = 0; code < 8; ++code) {
    reset();
    assert(npm1300_set_ship_debounce(code));
    assert(npm1300_apply_ship_config());
    expect(0, true, 11, 0x04, {code});
    expect(1, true, 11, 0x01, {1});
  }
  for (uint8_t code : {8, 255}) {
    reset(); assert(!npm1300_set_ship_debounce(code)); assert(operations.empty());
  }
  reset(); failAt = 1; assert(!npm1300_set_ship_debounce(7));
  reset(); failAt = 1; assert(!npm1300_apply_ship_config());
  for (uint8_t cause : {0, 1, 2, 4, 8, 15}) {
    reset(); registers[14][0x03] = cause;
    uint8_t observed = 0xA5;
    assert(npm1300_reset_cause(&observed) && observed == cause);
    assert(npm1300_clear_reset_cause());
    expect(0, false, 14, 0x03);
    expect(1, true, 14, 0x00, {1});
  }
  reset(); failAt = 1;
  uint8_t cause = 0xA5;
  assert(!npm1300_reset_cause(&cause) && cause == 0xA5);
  reset(); failAt = 1; assert(!npm1300_clear_reset_cause());

  reset();
  int32_t ma = 777;
  assert(!npm1300_read_ibat_signed_ma(nullptr) && adcReads == 0);
  adcReadOk = false;
  assert(!npm1300_read_ibat_signed_ma(&ma) && ma == 777);
  adcReadOk = true;
  snapshot.ibatStat = 0x10;
  assert(!npm1300_read_ibat_signed_ma(&ma) && ma == 777);
  snapshot.msbIbat = 255;
  snapshot.lsbB = 0x30;
  snapshot.ibatStat = 1 << 2;
  assert(npm1300_read_ibat_signed_ma(&ma) && ma == -1120);
  snapshot.ibatStat = 3 << 2;
  assert(npm1300_read_ibat_signed_ma(&ma) && ma == 125);
  snapshot.msbIbat = 0; snapshot.lsbB = 0;
  assert(npm1300_read_ibat_signed_ma(&ma) && ma == 0);
}
"""
    with tempfile.TemporaryDirectory(prefix="nrf54-pmic-controls-") as tmp:
        cpp = Path(tmp) / "power_controls.cpp"
        binary = Path(tmp) / "power_controls"
        cpp.write_text(harness)
        subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(HAL), str(cpp), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("PASS production PMIC timer/watchdog, sleep guards, debounce, reset cause and signed current")


if __name__ == "__main__":
    main()
