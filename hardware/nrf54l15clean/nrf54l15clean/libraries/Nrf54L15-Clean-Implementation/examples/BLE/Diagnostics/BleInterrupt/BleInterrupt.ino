#include <Arduino.h>
#include <nrf54l15.h>
#include <variant.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleInterrupt
 *
 * Advertises battery level every 2 seconds and sleeps with WFI between bursts.
 * Wake source: GRTC compare channel 0 interrupt (GRTC_0_IRQn).
 *
 * Power strategy:
 *   1. Sample battery voltage.
 *   2. Advertise a burst of BLE events with battery % in service data.
 *   3. Call ble.end() to release GRTC ACTIVE — critical for low sleep current.
 *   4. Collapse board state (RF switch, IMU, battery sense all off).
 *   5. Arm a GRTC compare interrupt 2 seconds out and enter WFI.
 *   6. Repeat.
 *
 * BLE payload (legacy advertising, non-connectable):
 *   Flags | Complete Local Name "XIAO54" | Service Data UUID 0x180F (Battery
 *   Service) carrying one byte: battery percentage 0..100.
 */

static PowerManager g_power;
static Grtc g_grtc;
static BleRadio g_ble;

static volatile bool g_timerFired = false;

extern "C" void nrf54l15_clean_idle_service(void);
extern "C" uint32_t nrf54l15_core_enter_idle_cpu_scaling(void);
extern "C" void nrf54l15_core_exit_idle_cpu_scaling(uint32_t restoreRaw);

namespace {

static constexpr uint8_t kDebugUsb                  = 0U;
static constexpr bool    kCollapseBoardState         = true;
static constexpr bool    kEnableLed                  = true;

static constexpr BoardAntennaPath kBleAntennaPath    = BoardAntennaPath::kCeramic;
static constexpr int8_t  kBleTxPowerDbm              = 0;
static constexpr uint8_t kBleBurstEvents             = 3U;
static constexpr uint32_t kBleInterChannelDelayUs    = 350UL;
static constexpr uint32_t kBleAdvertisingSpinLimit   = 700000UL;
static constexpr uint32_t kBleBurstGapMs             = 15UL;
static constexpr uint32_t kAdvertiseIntervalUs       = 2000000UL; // 2 s

// GRTC compare channel 0 → GRTC_0_IRQn
static constexpr uint8_t  kGrtcChannel               = 0U;

static constexpr int32_t  kBatteryEmptyMv            = 3300;
static constexpr int32_t  kBatteryFullMv             = 4200;
static constexpr uint32_t kBatterySettleUs           = 5000UL;
static constexpr uint32_t kBatterySampleSpinLimit    = 500000UL;

static constexpr uint8_t  kBleAdTypeFlags            = 0x01U;
static constexpr uint8_t  kBleAdTypeCompleteLocalName= 0x09U;
static constexpr uint8_t  kBleAdTypeServiceData16    = 0x16U;
static constexpr uint8_t  kBleFlagsLeGeneralDisc     = 0x06U;
static constexpr uint16_t kBleBatteryServiceUuid     = 0x180FU; // Standard BLE Battery Service
static constexpr char     kBleName[]                 = "XIAO54";

static volatile uint32_t* const kScbScr              = (volatile uint32_t*)0xE000ED10UL;
static constexpr uint32_t kScbScrSleepDeep_Msk       = (1UL << 2);
static constexpr uint32_t kScbScrSleepOnExit_Msk     = (1UL << 1);

static uint64_t g_nextWakeUs = 0ULL;
static uint32_t g_advCount   = 0U;

// ============================================================================
// Helpers
// ============================================================================

static inline bool debugEnabled() { return kDebugUsb != 0U; }

static uint8_t batteryPercent(int32_t mv) {
  if (mv <= kBatteryEmptyMv) return 0U;
  if (mv >= kBatteryFullMv)  return 100U;
  const int32_t span = kBatteryFullMv - kBatteryEmptyMv;
  return static_cast<uint8_t>(((mv - kBatteryEmptyMv) * 100 + span / 2) / span);
}

static size_t buildAdvPayload(uint8_t* out, size_t outSize, uint8_t battPct) {
  const size_t nameLen  = sizeof(kBleName) - 1U;
  const size_t total    = 3U + (2U + nameLen) + (2U + 2U + 1U);
  if (out == nullptr || outSize < total) return 0U;

  size_t i = 0U;
  // Flags
  out[i++] = 2U;
  out[i++] = kBleAdTypeFlags;
  out[i++] = kBleFlagsLeGeneralDisc;
  // Complete Local Name
  out[i++] = static_cast<uint8_t>(1U + nameLen);
  out[i++] = kBleAdTypeCompleteLocalName;
  memcpy(&out[i], kBleName, nameLen);
  i += nameLen;
  // Service Data: Battery Service UUID 0x180F + 1 byte battery %
  out[i++] = 4U; // len = 1(type) + 2(UUID) + 1(value)
  out[i++] = kBleAdTypeServiceData16;
  out[i++] = static_cast<uint8_t>(kBleBatteryServiceUuid & 0xFFU);
  out[i++] = static_cast<uint8_t>((kBleBatteryServiceUuid >> 8U) & 0xFFU);
  out[i++] = battPct;
  return i;
}

static void armNextWake() {
  g_timerFired = false;
  g_nextWakeUs += static_cast<uint64_t>(kAdvertiseIntervalUs);
  (void)g_grtc.setCompareAbsoluteUs(kGrtcChannel, g_nextWakeUs, true);
  g_grtc.enableCompareInterrupt(kGrtcChannel, true);
  NVIC_ClearPendingIRQ(GRTC_0_IRQn);
  NVIC_SetPriority(GRTC_0_IRQn, 3U);
  NVIC_EnableIRQ(GRTC_0_IRQn);
}

static void sleepUntilTimer() {
  if (kCollapseBoardState && !debugEnabled()) {
    xiaoNrf54l15EnterLowestPowerBoardState();
  }

  while (!g_timerFired) {
    nrf54l15_clean_idle_service();
    const uint32_t restoreRaw = nrf54l15_core_enter_idle_cpu_scaling();
    *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    __asm volatile("wfi");
    nrf54l15_core_exit_idle_cpu_scaling(restoreRaw);
  }
}

static void ledPulse() {
  if (!kEnableLed) return;
  (void)Gpio::write(kPinUserLed, false);
  for (volatile uint32_t d = 0; d < 30000UL; ++d) {}
  (void)Gpio::write(kPinUserLed, true);
}

} // namespace

// ============================================================================
// GRTC compare IRQ handler — fires when the 2-second alarm expires
// ============================================================================

extern "C" void GRTC_0_IRQHandler(void) {
  if (NRF_GRTC->EVENTS_COMPARE[kGrtcChannel] == 0U) {
    return;
  }
  NRF_GRTC->EVENTS_COMPARE[kGrtcChannel] = 0U;
  g_grtc.enableCompareInterrupt(kGrtcChannel, false);
  g_timerFired = true;
}

// ============================================================================
// Arduino entry points
// ============================================================================

void setup() {
  if (debugEnabled()) {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 1500UL) { delay(10); }
    delay(50);
    Serial.println("BleInterrupt: BLE battery beacon, 2s sleep interval");
  }

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  BoardControl::collapseRfPathIdle();

  if (kEnableLed) {
    (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
    (void)Gpio::write(kPinUserLed, true);
  }

  if (!g_grtc.begin(GrtcClockSource::kSystemLfclk)) {
    if (debugEnabled()) { Serial.println("ERROR: GRTC begin failed"); }
    while (true) { delay(1000); }
  }

  // Align the first wake to now + interval so the loop is immediately uniform.
  g_nextWakeUs = g_grtc.counter();
  armNextWake();
}

void loop() {
  sleepUntilTimer();

  // Sample battery
  int32_t battMv  = 0;
  uint8_t battPct = 0U;
  if (BoardControl::sampleBatteryMilliVolts(&battMv, kBatterySettleUs,
                                            kBatterySampleSpinLimit)) {
    battPct = batteryPercent(battMv);
  }

  // Build and send BLE advertising burst
  uint8_t advData[31];
  const size_t advLen = buildAdvPayload(advData, sizeof(advData), battPct);

  bool ok = (advLen > 0U) && BoardControl::enableRfPath(kBleAntennaPath);
  bool bleBegun = false;
  if (ok) {
    bleBegun = true;
    ok = g_ble.begin(kBleTxPowerDbm);
  }
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) { ok = g_ble.setAdvertisingData(advData, advLen); }
  if (ok) { ok = g_ble.buildAdvertisingPacket(); }
  if (ok) {
    for (uint8_t i = 0U; i < kBleBurstEvents; ++i) {
      ok = g_ble.advertiseEvent(kBleInterChannelDelayUs, kBleAdvertisingSpinLimit);
      if (!ok) break;
      if ((i + 1U) < kBleBurstEvents) {
        const uint64_t gapStart = g_grtc.counter();
        while ((g_grtc.counter() - gapStart) < static_cast<uint64_t>(kBleBurstGapMs * 1000UL)) {}
      }
    }
  }

  // Always end BLE — releases GRTC ACTIVE so CPU can enter deep sleep.
  if (bleBegun) { g_ble.end(); }
  BoardControl::collapseRfPathIdle();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  ++g_advCount;
  ledPulse();

  if (debugEnabled()) {
    char line[80];
    snprintf(line, sizeof(line),
             "adv=%lu batt_mv=%ld batt_pct=%u status=%s\r\n",
             static_cast<unsigned long>(g_advCount),
             static_cast<long>(battMv),
             static_cast<unsigned>(battPct),
             ok ? "OK" : "FAIL");
    Serial.print(line);
    Serial.flush();
  }

  // Arm the next 2-second wake before going back to sleep.
  armNextWake();
}
