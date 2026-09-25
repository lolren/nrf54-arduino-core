#!/usr/bin/env python3
"""Execute the production PMIC ADC transaction with a fault-injecting bus."""

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
            "static bool ensure_present()",
            "static bool read_adc_results(AdcResults* out)",
            "bool npm1300_is_present(void)",
        )
    )
    constants = "\n".join(
        re.search(rf"^constexpr uint\d+_t {name} = [^;]+;", source, re.MULTILINE).group(0)
        for name in sorted(set(re.findall(r"\bk[A-Z]\w+", functions)))
    )
    state = source[source.index("struct AdcResults {") : source.index("static int32_t g_chargeCurrentUa")]
    harness = r"""
#include "npm1300.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <limits>

struct Operation {
  bool write;
  uint8_t base;
  uint8_t offset;
  size_t length;
  uint8_t value;
};
static std::vector<Operation> operations;
static uint8_t registers[12][256];
static unsigned failAt = 0;
static bool applyFailedWrites = false;
static uint32_t clockMs = 1000;
static unsigned conversionWaits = 0;
uint32_t millis() { return clockMs; }
void delayMicroseconds(uint32_t us) {
  assert(us == 1500);
  ++conversionWaits;
}
bool npm1300_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t length) {
  operations.push_back({true, base, offset, length, data[0]});
  const bool ok = operations.size() != failAt;
  if (base == NPM1300_BASE_ADC && offset == 0) {
    assert(registers[5][0x24] == 1);
    assert(registers[5][0x09] == 0);
    assert(length == 4);
    for (size_t i = 0; i < length; ++i) assert(data[i] == 1);
  }
  if (ok || applyFailedWrites) memcpy(&registers[base][offset], data, length);
  return ok;
}
bool npm1300_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t length) {
  operations.push_back({false, base, offset, length, 0});
  if (operations.size() == failAt) return false;
  memcpy(data, &registers[base][offset], length);
  return true;
}
bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t* value) {
  return npm1300_read_burst(base, offset, value, 1);
}
bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value) {
  return npm1300_write_burst(base, offset, &value, 1);
}
""" + constants + "\n" + state + "\n" + functions + r"""

void reset(uint8_t ibat, uint8_t config) {
  operations.clear();
  memset(registers, 0, sizeof(registers));
  registers[5][0x24] = ibat;
  registers[5][0x09] = config;
  for (unsigned i = 0; i < 11; ++i) registers[5][0x10 + i] = i + 1;
  registers[3][0x34] = 0x14;
  registers[3][0x36] = 0x12;
  registers[2][0x07] = 0x11;
  g_probeValid = false;
  g_present = false;
  g_adcValid = false;
  g_adcCachedMs = 0;
  g_adcCache = {};
  g_chargerStatus = 0xA1;
  g_chargerError = 0xA2;
  g_vbusStatus = 0xA3;
  conversionWaits = 0;
  failAt = 0;
  applyFailedWrites = false;
  clockMs = 1000;
}

void assertRestoreAttempts(uint8_t ibat, uint8_t config) {
  assert(operations.size() >= 2);
  const auto& restoreIbat = operations[operations.size() - 2];
  const auto& restoreConfig = operations.back();
  assert(restoreIbat.write && restoreIbat.base == 5 && restoreIbat.offset == 0x24);
  assert(restoreIbat.value == ibat);
  assert(restoreConfig.write && restoreConfig.base == 5 && restoreConfig.offset == 0x09);
  assert(restoreConfig.value == config);
}

int main() {
  reset(0, 3);
  assert(npm1300_is_present());
  assert(operations.size() == 1 && !operations.front().write);
  assert(registers[5][0x24] == 0 && registers[5][0x09] == 3);
  assert(npm1300_is_present());
  assert(operations.size() == 1);
  reset(0, 0);
  failAt = 1;
  assert(!npm1300_is_present());
  failAt = 0;
  assert(npm1300_is_present());  // A later presence query retries an unsuccessful probe.
  assert(operations.size() == 2);
  unsigned successOperations = 0;
  for (uint8_t ibat = 0; ibat < 2; ++ibat) {
    for (uint8_t config = 0; config < 4; ++config) {
      reset(ibat, config);
      AdcResults out{};
      assert(read_adc_results(&out));
      assert(g_adcValid);
      assert(g_adcCachedMs == clockMs);
      assert(registers[5][0x24] == ibat && registers[5][0x09] == config);
      assert(registers[5][0x0C] == 1);  // Keep charger thermal-monitor activation.
      assertRestoreAttempts(ibat, config);
      assert(conversionWaits == 1);
      assert(out.ibatStat == 1 && out.msbVbat == 2 && out.msbNtc == 3);
      assert(out.msbDie == 4 && out.msbVsys == 5 && out.lsbA == 6);
      assert(out.msbIbat == 9 && out.msbVbus == 10 && out.lsbB == 11);
      assert(g_chargerStatus == 0x14 && g_chargerError == 0x12 && g_vbusStatus == 0x11);
      successOperations = operations.size();
      clockMs += 500;
      assert(read_adc_results(&out));
      assert(operations.size() == successOperations);  // Cache performs no bus operations.
      ++clockMs;
      assert(read_adc_results(&out));
      assert(operations.size() > successOperations);
      assert(registers[5][0x24] == ibat && registers[5][0x09] == config);
    }
  }
  // Every bus operation can fail either before delivery or after the PMIC applied it.
  // In both cases return failure, publish nothing, and attempt both cleanup writes.
  for (unsigned accepted = 0; accepted < 2; ++accepted) {
    for (uint8_t ibat = 0; ibat < 2; ++ibat) {
      for (uint8_t config = 0; config < 4; ++config) {
        for (unsigned failure = 1; failure <= successOperations; ++failure) {
          reset(ibat, config);
          failAt = failure;
          applyFailedWrites = accepted;
          g_adcValid = true;
          g_adcCachedMs = 1;  // Expired cache must not survive a failed refresh.
          g_adcCache.msbVbat = 0xAA;
          AdcResults out{};
          memset(&out, 0xE7, sizeof(out));
          const AdcResults sentinel = out;
          assert(!read_adc_results(&out));
          assert(!g_adcValid);
          assert(g_adcCache.msbVbat == 0xAA);
          assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
          assert(g_chargerStatus == 0xA1 && g_chargerError == 0xA2 && g_vbusStatus == 0xA3);
          if (failure <= 3) {
            for (const auto& op : operations) assert(!op.write);
          } else {
            assertRestoreAttempts(ibat, config);
          }
          // An unsuccessful cleanup cannot promise restoration. Earlier failures must restore.
          if (accepted || failure < successOperations - 1) {
            assert(registers[5][0x24] == ibat && registers[5][0x09] == config);
          }
          if (failure >= 7) assert(conversionWaits == 1);
        }
      }
    }
  }
  reset(0, 0);
  assert(!read_adc_results(nullptr));
  assert(operations.empty());
  // Timestamp wraparound must preserve the same cache-age behavior.
  AdcResults out{};
  clockMs = std::numeric_limits<uint32_t>::max() - 100;
  assert(read_adc_results(&out));
  const auto beforeWrap = operations.size();
  clockMs = 100;
  assert(read_adc_results(&out));
  assert(operations.size() == beforeWrap);
  clockMs = 500;
  assert(read_adc_results(&out));
  assert(operations.size() > beforeWrap);
}
"""
    with tempfile.TemporaryDirectory(prefix="nrf54-pmic-adc-") as tmp:
        cpp = Path(tmp) / "adc_power.cpp"
        binary = Path(tmp) / "adc_power"
        cpp.write_text(harness)
        subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(HAL), str(cpp), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("PASS production PMIC ADC snapshot/restore, cached reads, timer rollover and all bus-failure paths")


if __name__ == "__main__":
    main()
