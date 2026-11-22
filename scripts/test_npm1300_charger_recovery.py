#!/usr/bin/env python3
"""Exercise production charger recovery commands and the opt-in example policy."""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

from test_ble_security_policy_contracts import function_body


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
HAL = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
EXAMPLE = PLATFORM / "examples/Power/nPM1300_Fuel_Gauge/nPM1300_Fuel_Gauge.ino"


def extract(source: str, signatures: tuple[str, ...]) -> str:
    return "\n".join(function_body(source, (sig,), sig) for sig in signatures)


def driver_harness(source: str) -> str:
    functions = extract(source, (
        "static bool read_charger_enabled(",
        "static bool set_charger_enabled_task(",
        "static bool begin_charger_config(",
        "bool npm1300_charger_enable(",
        "bool npm1300_charger_clear_error(",
        "bool npm1300_charger_clear_safety_timers(",
    ))
    constants = "\n".join(
        re.search(
            rf"^(?:static )?constexpr u?int\d+_t\s+{name}\s*=[^;]+;",
            source, re.MULTILINE,
        ).group(0)
        for name in sorted(set(re.findall(r"\bk[A-Z]\w+", functions)))
    )
    return r"""
#include "npm1300.h"
#include <cassert>
#include <cstring>
#include <vector>

struct Operation { bool write; uint8_t base, offset, value; };
static std::vector<Operation> operations;
static uint8_t registers[4][256];
static unsigned failAt;
static bool acceptFailedWrite;
static bool expectInvalidated;
static bool g_adcValid;
bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value) {
  if (expectInvalidated) assert(!g_adcValid);
  operations.push_back({true, base, offset, value});
  const bool ok = operations.size() != failAt;
  if (ok || acceptFailedWrite) registers[base][offset] = value;
  return ok;
}
bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t* value) {
  operations.push_back({false, base, offset, 0});
  if (!value || operations.size() == failAt) return false;
  *value = registers[base][offset];
  return true;
}
""" + constants + "\n" + functions + r"""
static void reset(bool invalidate = true) {
  operations.clear();
  memset(registers, 0, sizeof(registers));
  failAt = 0;
  acceptFailedWrite = false;
  expectInvalidated = invalidate;
  g_adcValid = true;
}
static void expect(size_t index, bool write, uint8_t offset) {
  assert(index < operations.size());
  const auto& op = operations[index];
  assert(op.write == write && op.base == 3 && op.offset == offset);
  if (write) assert(op.value == 1);
}
static void noImplicitClear() {
  for (const auto& op : operations) {
    assert(!op.write || (op.offset != 1 && op.offset != 2));
  }
}
int main() {
  reset();
  assert(npm1300_charger_enable(true));
  assert(operations.size() == 3 && !g_adcValid);
  expect(0, true, 0); expect(1, true, 7); expect(2, true, 4);
  noImplicitClear();
  reset();
  assert(npm1300_charger_enable(false));
  assert(operations.size() == 1 && !g_adcValid);
  expect(0, true, 5);
  noImplicitClear();
  for (bool applied : {false, true}) {
    for (unsigned failure = 1; failure <= 3; ++failure) {
      reset(); failAt = failure; acceptFailedWrite = applied;
      assert(!npm1300_charger_enable(true));
      assert(operations.size() == failure && !g_adcValid);
      noImplicitClear();
    }
    reset(); failAt = 1; acceptFailedWrite = applied;
    assert(!npm1300_charger_enable(false));
    assert(operations.size() == 1 && !g_adcValid);
    expect(0, true, 5);
    reset();
    assert(npm1300_charger_clear_error());
    assert(operations.size() == 2 && !g_adcValid);
    expect(0, true, 0); expect(1, true, 1);
    for (unsigned failure = 1; failure <= 2; ++failure) {
      reset(); failAt = failure; acceptFailedWrite = applied;
      assert(!npm1300_charger_clear_error());
      assert(operations.size() == failure && !g_adcValid);
      // A release failure must not erase the remaining fault diagnostics.
      if (failure == 1) expect(0, true, 0);
    }
    reset();
    assert(npm1300_charger_clear_safety_timers());
    assert(operations.size() == 1 && !g_adcValid);
    expect(0, true, 2);
    reset(); failAt = 1; acceptFailedWrite = applied;
    assert(!npm1300_charger_clear_safety_timers());
    assert(operations.size() == 1 && !g_adcValid);
    expect(0, true, 2);
  }
  reset(false);
  bool wasEnabled = true;
  failAt = 1;
  assert(!begin_charger_config(&wasEnabled));
  assert(wasEnabled && operations.size() == 1);
  expect(0, false, 4);
  reset(false);
  registers[3][4] = 1;
  assert(begin_charger_config(&wasEnabled) && wasEnabled);
  assert(operations.size() == 2);
  expect(0, false, 4); expect(1, true, 5);
  reset(false);
  assert(begin_charger_config(&wasEnabled) && !wasEnabled);
  assert(operations.size() == 1);
  reset(false); registers[3][4] = 1; failAt = 2;
  assert(!begin_charger_config(&wasEnabled));
  assert(operations.size() == 2);
}
"""


def retry_harness(source: str) -> str:
    function = extract(source, ("static bool restartAfterTrickleTimeout(",))
    default_flag = re.search(
        r"^#ifndef\s+NPM1300_FUEL_GAUGE_TRICKLE_RETRY\s*\n"
        r"#define\s+NPM1300_FUEL_GAUGE_TRICKLE_RETRY\s+0\s*\n#endif",
        source, re.MULTILINE,
    ).group(0)
    declarations = {
        match.group(1): match.group(0)
        for match in re.finditer(
            r"^constexpr\s+(?:u?int\d+_t|bool)\s+([A-Z][A-Z0-9_]+)\s*=[^;]+;",
            source, re.MULTILINE,
        )
    }
    required = set(re.findall(r"\b[A-Z][A-Z0-9_]+\b", function))
    while True:
        expanded = required | {
            token for name in required if name in declarations
            for token in re.findall(r"\b[A-Z][A-Z0-9_]+\b", declarations[name])
        }
        if expanded == required:
            break
        required = expanded
    constants = "\n".join(declaration for name, declaration in declarations.items()
                          if name in required)
    return default_flag + r"""
#include "npm1300.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

static uint32_t nowMs;
uint32_t millis() { return nowMs; }
struct SerialSink {
  template<typename T> void print(const T&) {}
  template<typename T> void println(const T&) {}
};
SerialSink OutputSerial;
static std::vector<unsigned> calls;
static unsigned failAt;
static bool step(unsigned id) {
  calls.push_back(id);
  return calls.size() != failAt;
}
bool npm1300_charger_clear_error() { return step(1); }
bool npm1300_charger_clear_safety_timers() { return step(2); }
bool npm1300_charger_enable(bool enabled) { assert(enabled); return step(3); }
""" + constants + "\n" + function + r"""
static void reset() {
  (void)restartAfterTrickleTimeout(0, 0, 0, -1);
  calls.clear();
  failAt = 0;
  nowMs = 100;
}
static bool retry(uint8_t chg = 1, uint8_t err = 0x48, uint8_t vbus = 0x21,
                  int32_t mv = 3300) {
  return restartAfterTrickleTimeout(chg, err, vbus, mv);
}
int main() {
#if !NPM1300_FUEL_GAUGE_TRICKLE_RETRY
  for (unsigned err = 0; err <= 255; ++err) {
    for (uint8_t chg : {0, 1, 0x21, 0x40, 0x80}) {
      reset();
      assert(!retry(chg, uint8_t(err)) && calls.empty());
      nowMs += 60000;
      assert(!retry(chg, uint8_t(err)) && calls.empty());
    }
  }
#else
  for (unsigned err = 0; err <= 255; ++err) {
    reset();
    const bool accepted = err == 0x40 || err == 0x48;
    assert(retry(1, uint8_t(err)) == accepted);
    assert(calls.size() == (accepted ? 3 : 0));
  }
  for (uint8_t chg : {0, 1, 0x20, 0x21}) {
    reset(); assert(retry(chg));
    assert((calls == std::vector<unsigned>{1, 2, 3}));
  }
  for (uint8_t blocked : {2, 4, 8, 0x10, 0x40, 0x80}) {
    for (uint8_t benign : {0, 1, 0x20, 0x21}) {
      reset(); assert(!retry(blocked | benign) && calls.empty());
    }
  }
  for (uint8_t bad : {4, 8, 0x10}) {
    reset(); assert(!retry(1, 0x48, 1 | bad) && calls.empty());
  }
  for (uint8_t good : {1, 3, 0x21, 0x23}) {
    reset(); assert(retry(1, 0x48, good));
  }
  for (int32_t mv : {std::numeric_limits<int32_t>::min(), -1, 0, 2500, 2899,
                     2900, 4099, 4100, 4200, std::numeric_limits<int32_t>::max()}) {
    reset();
    const bool accepted = mv >= 2900 && mv < 4100;
    assert(retry(1, 0x48, 0x21, mv) == accepted);
    assert(calls.size() == (accepted ? 3 : 0));
  }
  reset();
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    assert(retry());
    nowMs += 60000;
  }
  assert(calls.size() == 15);
  assert(!retry() && calls.size() == 15);
  assert(!retry(1, 0x48, 0) && calls.size() == 15);
  assert(retry() && calls.size() == 18);
  reset();
  assert(retry());
  nowMs = 60099;
  assert(!retry() && calls.size() == 3);
  nowMs = 60100;
  assert(retry() && calls.size() == 6);
  reset();
  nowMs = std::numeric_limits<uint32_t>::max() - 20000;
  const uint32_t previous = nowMs;
  assert(retry());
  nowMs = previous + uint32_t(59999);
  assert(!retry() && calls.size() == 3);
  nowMs = previous + uint32_t(60000);
  assert(retry() && calls.size() == 6);
  for (unsigned failure = 1; failure <= 3; ++failure) {
    reset(); failAt = failure;
    assert(!retry() && calls.size() == failure);
    assert(!retry() && calls.size() == failure);
    nowMs += 60000; failAt = 0;
    assert(retry() && calls.size() == failure + 3);
  }
  reset();
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    failAt = calls.size() + 1;
    assert(!retry());
    nowMs += 60000;
  }
  failAt = 0;
  assert(calls.size() == 5 && !retry() && calls.size() == 5);
#endif
}
"""


def validate_example_integration(source: str) -> None:
    assert re.search(r"#ifndef\s+NPM1300_FUEL_GAUGE_TRICKLE_RETRY\s*\n"
                     r"#define\s+NPM1300_FUEL_GAUGE_TRICKLE_RETRY\s+0\b", source)
    setup = extract(source, ("void setup()",))
    variant_guard = setup.index("nrf_fuel_gauge_variant != NRF_FUEL_GAUGE_VARIANT_SECONDARY")
    assert variant_guard < setup.index("npm1300_charger_set_term_voltage(")
    assert variant_guard < setup.index("npm1300_charger_set_current(")
    assert variant_guard < setup.index("npm1300_charger_enable(")
    loop = extract(source, ("void loop()",))
    failure = loop.index("if (!readBattery(")
    assert loop.index("npm1300_vbus_status(") < failure
    assert loop.index("npm1300_charger_error(") < failure
    assert "NPM1300_VBUS_STATUS_PRESENT" in loop[:failure]
    assert "restartAfterTrickleTimeout" in loop[:failure]
    assert re.search(r"if\s*\(haveVbus\s*&&\s*!\(vbus\s*&\s*"
                     r"NPM1300_VBUS_STATUS_PRESENT\)\)", loop[:failure])
    assert "chgErr" in loop[failure:loop.index("return;", failure)]
    assert not re.search(r"restartAfterTrickleTimeout\([^;]*v\s*\*\s*1000", loop)
    assert re.search(r"restartAfterTrickleTimeout\([^;]*npm1300_read_vbat_mv\(", loop) or (
        "npm1300_read_vbat_mv()" in loop
        and re.search(r"restartAfterTrickleTimeout\([^;]*rawVbat", loop)
    )
    recovery = loop.rindex("restartAfterTrickleTimeout")
    assert loop.index("nrf_fuel_gauge_process(") < recovery
    assert loop.rindex("OutputSerial.println();") < recovery


def main() -> None:
    source = (HAL / "npm1300.cpp").read_text()
    example = EXAMPLE.read_text()
    validate_example_integration(example)
    with tempfile.TemporaryDirectory(prefix="nrf54-pmic-charger-") as directory:
        root = Path(directory)
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        builds = [("driver", driver_harness(source), [])]
        builds.extend((name, retry_harness(example), flags) for name, flags in (
            ("retry-default", []),
            ("retry-opt-in", ["-DNPM1300_FUEL_GAUGE_TRICKLE_RETRY=1"]),
        ))
        for name, harness, flags in builds:
            cpp = root / f"{name}.cpp"
            binary = root / name
            cpp.write_text(harness)
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                                       "-I", str(HAL), *flags, str(cpp), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)
    print("PASS production charger tasks, failure propagation and explicit timer-reset separation")
    print("PASS default-disabled and opt-in trickle retry guards, budgets, failures and timer rollover")
    print("PASS fuel-gauge fault telemetry and raw-voltage recovery integration")


if __name__ == "__main__":
    main()
