#include <Arduino.h>
#include <nrf54l15.h>
#include <variant.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_power;
static Lpcomp g_lpcomp;
static Grtc g_grtc;
static BleRadio g_ble;
static volatile uint8_t g_lpcompWakePending = 0U;
static volatile uint8_t g_lpcompWakeOverrun = 0U;
static volatile uint64_t g_lpcompWakeUs = 0ULL;
static volatile uint8_t g_lpcompWakeWasUp = 0U;

extern "C" void nrf54l15_clean_idle_service(void);

namespace {

// Measurement defaults:
// - USB debug off
// - green LED pulse indicator is useful for bring-up; disable it for current
//   measurement once the wake path is confirmed
// - board state collapsed during sleep, matching the low-power delay path
static constexpr uint8_t kDebugUsb = 0U;
static constexpr bool kCollapseBoardStateWhileSleeping = true;
static constexpr bool kEnableGreenLed = true;
static constexpr uint32_t kPulseLedOnMs = 6UL;

// BLE advertising defaults:
// - advertising only, no connections
// - one burst after each valid pulse measurement
// - battery data appended every kBatteryReportEveryMeasurements intervals
static constexpr BoardAntennaPath kBleAntennaPath = BoardAntennaPath::kCeramic;
static constexpr int8_t kBleTxPowerDbm = 0;
static constexpr uint16_t kBleServiceUuid = 0xFFF0U;
static constexpr uint8_t kBlePayloadVersion = 1U;
static constexpr uint8_t kBlePacketFlagBatteryIncluded = 0x01U;
static constexpr uint8_t kBleAdTypeFlags = 0x01U;
static constexpr uint8_t kBleAdTypeCompleteLocalName = 0x09U;
static constexpr uint8_t kBleAdTypeServiceData16 = 0x16U;
static constexpr uint8_t kBleFlagsLeGeneralDiscoverable = 0x06U;
static constexpr uint32_t kBleInterChannelDelayUs = 350UL;
static constexpr uint32_t kBleAdvertisingSpinLimit = 700000UL;
static constexpr uint8_t kBleBurstEventsPerMeasurement = 8U;
static constexpr uint32_t kBleBurstGapMs = 40UL;
static constexpr uint32_t kBatteryReportEveryMeasurements = 20UL;
static constexpr int32_t kBatteryEmptyMilliVolts = 3300;
static constexpr int32_t kBatteryFullMilliVolts = 4200;
static constexpr uint32_t kBatterySettleDelayUs = 5000UL;
static constexpr uint32_t kBatterySampleSpinLimit = 500000UL;
static constexpr char kBleAdvertiserName[] = "M54";

// Meter pulse and LPCOMP tuning.
static constexpr uint16_t kMeterImpPerKwh = 1000U;
static constexpr uint16_t kAssumedVddMv = 3300U;
static constexpr uint16_t kTargetThresholdMv = 200U;
static constexpr bool kEnableHysteresis = true;
static constexpr uint32_t kMinPulseUs = 20000UL;
static volatile uint32_t* const kScbScr = (volatile uint32_t*)0xE000ED10UL;
static constexpr uint32_t kScbScrSleepDeep_Msk = (1UL << 2);
static constexpr uint32_t kScbScrSleepOnExit_Msk = (1UL << 1);

static uint32_t g_pulseCount = 0U;
static uint32_t g_measurementCount = 0U;
static uint32_t g_bleAdvertisementsSent = 0U;
static uint64_t g_lastPulseUs = 0ULL;

enum class LpcompArmMode : uint8_t {
  kUp = 0U,
  kDown = 1U,
};

static LpcompArmMode g_lpcompArmMode = LpcompArmMode::kUp;

static void waitCounterUs(uint32_t waitUs) {
  if (waitUs == 0U) {
    return;
  }

  const uint64_t startUs = g_grtc.counter();
  while ((g_grtc.counter() - startUs) < static_cast<uint64_t>(waitUs)) {
    __NOP();
  }
}

static void waitCounterMs(uint32_t waitMs) {
  if (waitMs == 0U) {
    return;
  }

  waitCounterUs(static_cast<uint32_t>(waitMs * 1000UL));
}

static inline bool debugUsbEnabled() {
  return kDebugUsb != 0U;
}

static void debugBegin() {
  if (!debugUsbEnabled()) {
    return;
  }

  Serial.begin(115200);
  const uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 1500UL) {
    delay(10);
  }
  delay(50);
}

static inline void debugFlush() {
  if (debugUsbEnabled()) {
    Serial.flush();
  }
}

static void printU64(uint64_t value) {
  char buffer[32];
  size_t index = sizeof(buffer) - 1U;
  buffer[index] = '\0';

  do {
    const uint8_t digit = static_cast<uint8_t>(value % 10ULL);
    value /= 10ULL;
    buffer[--index] = static_cast<char>('0' + digit);
  } while (value != 0ULL && index > 0U);

  Serial.print(&buffer[index]);
}

static uint32_t wattsFromIntervalUs(uint64_t dtUs) {
  if (dtUs == 0ULL) {
    return 0U;
  }

  const uint64_t numerator = 3600000000000ULL;
  const uint64_t denominator = static_cast<uint64_t>(kMeterImpPerKwh) * dtUs;
  return static_cast<uint32_t>(numerator / denominator);
}

static void writeLe16(uint8_t* out, uint16_t value) {
  if (out == nullptr) {
    return;
  }

  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

static void writeLe32(uint8_t* out, uint32_t value) {
  if (out == nullptr) {
    return;
  }

  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

static uint32_t intervalMsFromUs(uint64_t dtUs) {
  if (dtUs == 0ULL) {
    return 0U;
  }

  const uint64_t roundedMs = (dtUs + 500ULL) / 1000ULL;
  if (roundedMs > 0xFFFFFFFFULL) {
    return 0xFFFFFFFFUL;
  }

  return static_cast<uint32_t>(roundedMs);
}

static uint8_t batteryPercentFromMilliVolts(int32_t batteryMv) {
  if (batteryMv <= kBatteryEmptyMilliVolts) {
    return 0U;
  }
  if (batteryMv >= kBatteryFullMilliVolts) {
    return 100U;
  }

  const int32_t span = kBatteryFullMilliVolts - kBatteryEmptyMilliVolts;
  const int32_t scaled =
      (batteryMv - kBatteryEmptyMilliVolts) * 100 + (span / 2);
  return static_cast<uint8_t>(scaled / span);
}

static bool shouldReportBattery(uint32_t measurementCount) {
  return (kBatteryReportEveryMeasurements > 0UL) &&
         (measurementCount > 0UL) &&
         ((measurementCount % kBatteryReportEveryMeasurements) == 0UL);
}

static bool sampleBatteryReading(int32_t* outBatteryMv,
                                 uint8_t* outBatteryPercent) {
  if (outBatteryMv == nullptr || outBatteryPercent == nullptr) {
    return false;
  }

  int32_t batteryMv = 0;
  if (!BoardControl::sampleBatteryMilliVolts(&batteryMv, kBatterySettleDelayUs,
                                             kBatterySampleSpinLimit)) {
    return false;
  }

  *outBatteryMv = batteryMv;
  *outBatteryPercent = batteryPercentFromMilliVolts(batteryMv);
  return true;
}

static size_t buildMeasurementAdvertisingData(uint8_t* out, size_t outSize,
                                              uint32_t measurementCount,
                                              uint32_t pulseCount,
                                              uint32_t intervalMs,
                                              uint32_t watts,
                                              const int32_t* batteryMv,
                                              const uint8_t* batteryPercent) {
  if (out == nullptr) {
    return 0U;
  }

  uint8_t payload[21];
  size_t payloadLen = 0U;
  const size_t localNameLen = sizeof(kBleAdvertiserName) - 1U;
  uint8_t packetFlags = 0U;
  if (batteryMv != nullptr && batteryPercent != nullptr) {
    packetFlags |= kBlePacketFlagBatteryIncluded;
  }

  payload[payloadLen++] = kBlePayloadVersion;
  payload[payloadLen++] = packetFlags;
  writeLe32(&payload[payloadLen], measurementCount);
  payloadLen += 4U;
  writeLe32(&payload[payloadLen], pulseCount);
  payloadLen += 4U;
  writeLe32(&payload[payloadLen], intervalMs);
  payloadLen += 4U;
  writeLe32(&payload[payloadLen], watts);
  payloadLen += 4U;

  if ((packetFlags & kBlePacketFlagBatteryIncluded) != 0U) {
    uint16_t clampedBatteryMv = 0U;
    if (*batteryMv > 0) {
      clampedBatteryMv = (*batteryMv > 0xFFFF) ? 0xFFFFU
                                               : static_cast<uint16_t>(*batteryMv);
    }
    writeLe16(&payload[payloadLen], clampedBatteryMv);
    payloadLen += 2U;
    payload[payloadLen++] = *batteryPercent;
  }

  const size_t serviceDataLen = 2U + payloadLen;
  const size_t nameFieldLen = 2U + localNameLen;
  const size_t totalLen = 3U + 2U + serviceDataLen + nameFieldLen;
  if (outSize < totalLen) {
    return 0U;
  }

  size_t outLen = 0U;
  out[outLen++] = 2U;
  out[outLen++] = kBleAdTypeFlags;
  out[outLen++] = kBleFlagsLeGeneralDiscoverable;
  out[outLen++] = static_cast<uint8_t>(1U + serviceDataLen);
  out[outLen++] = kBleAdTypeServiceData16;
  writeLe16(&out[outLen], kBleServiceUuid);
  outLen += 2U;
  memcpy(&out[outLen], payload, payloadLen);
  outLen += payloadLen;
  out[outLen++] = static_cast<uint8_t>(1U + localNameLen);
  out[outLen++] = kBleAdTypeCompleteLocalName;
  memcpy(&out[outLen], kBleAdvertiserName, localNameLen);
  outLen += localNameLen;
  return outLen;
}

static bool sendMeasurementAdvertisement(uint32_t intervalMs, uint32_t watts) {
  uint8_t advData[31];
  int32_t batteryMv = 0;
  uint8_t batteryPercent = 0U;
  const int32_t* batteryMvPtr = nullptr;
  const uint8_t* batteryPercentPtr = nullptr;

  if (shouldReportBattery(g_measurementCount) &&
      sampleBatteryReading(&batteryMv, &batteryPercent)) {
    batteryMvPtr = &batteryMv;
    batteryPercentPtr = &batteryPercent;
  }

  const size_t advLen =
      buildMeasurementAdvertisingData(advData, sizeof(advData),
                                      g_measurementCount, g_pulseCount,
                                      intervalMs, watts, batteryMvPtr,
                                      batteryPercentPtr);
  if (advLen == 0U) {
    return false;
  }

  bool ok = BoardControl::enableRfPath(kBleAntennaPath);
  bool bleBeginAttempted = false;
  if (ok) {
    bleBeginAttempted = true;
    ok = g_ble.begin(kBleTxPowerDbm);
  }
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) {
    ok = g_ble.setAdvertisingData(advData, advLen);
  }
  if (ok) {
    ok = g_ble.buildAdvertisingPacket();
  }
  if (ok) {
    for (uint8_t i = 0U; i < kBleBurstEventsPerMeasurement; ++i) {
      ok = g_ble.advertiseEvent(kBleInterChannelDelayUs, kBleAdvertisingSpinLimit);
      if (!ok) {
        break;
      }
      if ((i + 1U) < kBleBurstEventsPerMeasurement) {
        waitCounterMs(kBleBurstGapMs);
      }
    }
  }

  if (bleBeginAttempted) {
    g_ble.end();
  }
  const bool collapseOk = BoardControl::collapseRfPathIdle();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  ok = ok && collapseOk;
  if (ok) {
    ++g_bleAdvertisementsSent;
  }

  if (debugUsbEnabled()) {
    Serial.print("adv=");
    Serial.print(ok ? "ok" : "fail");
    Serial.print(" measure=");
    Serial.print(g_measurementCount);
    Serial.print(" pulse=");
    Serial.print(g_pulseCount);
    Serial.print(" interval_ms=");
    Serial.print(intervalMs);
    Serial.print(" watts=");
    Serial.print(watts);
    if (batteryMvPtr != nullptr && batteryPercentPtr != nullptr) {
      Serial.print(" batt_mv=");
      Serial.print(*batteryMvPtr);
      Serial.print(" batt_pct=");
      Serial.print(*batteryPercentPtr);
    }
    Serial.println();
    debugFlush();
  }

  return ok;
}

static void initPulseLed() {
  if (!kEnableGreenLed) {
    return;
  }

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);
}

static void blinkPulseLed() {
  if (!kEnableGreenLed) {
    return;
  }

  (void)Gpio::write(kPinUserLed, false);
  waitCounterMs(kPulseLedOnMs);
  (void)Gpio::write(kPinUserLed, true);
}

static void printBootBanner() {
  if (!debugUsbEnabled()) {
    return;
  }

  Serial.println();
  Serial.println("LpcompMeterPulseInterruptWake");
  Serial.println("A0 watches the meter sensor through LPCOMP IRQ wake.");
  Serial.print("meter_const_imp_per_kwh=");
  Serial.println(kMeterImpPerKwh);
  Serial.print("target_threshold_mv=");
  Serial.println(kTargetThresholdMv);
  Serial.print("assumed_vdd_mv=");
  Serial.println(kAssumedVddMv);
  Serial.print("collapse_board_state=");
  Serial.println(kCollapseBoardStateWhileSleeping ? 1 : 0);
  Serial.println("wake_mode=lpcomp_irq_handler_wfi");
  Serial.println("ble_mode=advertising_only");
  Serial.print("ble_tx_power_dbm=");
  Serial.println(kBleTxPowerDbm);
  Serial.print("ble_name=");
  Serial.println(kBleAdvertiserName);
  Serial.print("ble_burst_events=");
  Serial.println(kBleBurstEventsPerMeasurement);
  Serial.print("ble_burst_gap_ms=");
  Serial.println(kBleBurstGapMs);
  Serial.println("ble_idle_state=end_after_each_measurement");
  Serial.print("battery_every_measurements=");
  Serial.println(kBatteryReportEveryMeasurements);
  #if defined(NRF54L15_CLEAN_POWER_LOW)
  Serial.println("build_power_profile=low_power_wfi_idle");
#else
  Serial.println("build_power_profile=balanced");
  Serial.println("For lowest sleep current, compile with clean_power=low.");
#endif
  Serial.println("Waiting for meter flashes...");
  debugFlush();
}

static void configureLpcompInterrupt(LpcompArmMode mode) {
  g_lpcompArmMode = mode;
  g_lpcomp.clearEvents();
  NRF_LPCOMP->INTENCLR = 0xFFFFFFFFUL;
  NRF_LPCOMP->EVENTS_UP = 0U;
  NRF_LPCOMP->EVENTS_DOWN = 0U;
  NRF_LPCOMP->EVENTS_CROSS = 0U;
  NVIC_ClearPendingIRQ(LPCOMP_IRQn);
  NVIC_SetPriority(LPCOMP_IRQn, 3U);
  if (mode == LpcompArmMode::kUp) {
    g_lpcomp.configureAnalogDetect(LpcompDetect::kUp);
    NRF_LPCOMP->INTENSET = LPCOMP_INTENSET_UP_Msk;
  } else {
    g_lpcomp.configureAnalogDetect(LpcompDetect::kDown);
    NRF_LPCOMP->INTENSET = LPCOMP_INTENSET_DOWN_Msk;
  }
  NVIC_EnableIRQ(LPCOMP_IRQn);
}

static bool fetchPendingWake(uint64_t* outPulseUs, bool* outWasUp) {
  if (outPulseUs == nullptr || outWasUp == nullptr) {
    return false;
  }

  noInterrupts();
  if (g_lpcompWakePending == 0U) {
    interrupts();
    return false;
  }

  *outPulseUs = g_lpcompWakeUs;
  *outWasUp = (g_lpcompWakeWasUp != 0U);
  g_lpcompWakePending = 0U;
  interrupts();
  return true;
}

static void clearStaleLpcompWakeState() {
  if (g_lpcompWakePending != 0U) {
    return;
  }

  g_lpcomp.clearEvents();
  NVIC_ClearPendingIRQ(LPCOMP_IRQn);
  __DSB();
  __ISB();
}

static void sleepUntilLpcompWake() {
  if (kCollapseBoardStateWhileSleeping && !debugUsbEnabled()) {
    xiaoNrf54l15EnterLowestPowerBoardState();
  }

  while (true) {
    nrf54l15_clean_idle_service();
    if (g_lpcompWakePending != 0U) {
      break;
    }

    const uint32_t restoreRaw = nrf54l15_core_enter_idle_cpu_scaling();
    *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    __asm volatile("wfi");
    nrf54l15_core_exit_idle_cpu_scaling(restoreRaw);
    clearStaleLpcompWakeState();
  }
}

static void handlePulseRise(uint64_t pulseUs) {
  if (g_lastPulseUs != 0ULL) {
    const uint64_t dtUs = pulseUs - g_lastPulseUs;
    if (dtUs < static_cast<uint64_t>(kMinPulseUs)) {
      if (debugUsbEnabled()) {
        Serial.print("noise pulse ignored dt_us=");
        printU64(dtUs);
        Serial.println();
        debugFlush();
      }
      return;
    }

    ++g_pulseCount;
    ++g_measurementCount;
    const uint32_t watts = wattsFromIntervalUs(dtUs);
    const uint32_t intervalMs = intervalMsFromUs(dtUs);
    g_lastPulseUs = pulseUs;

    if (debugUsbEnabled()) {
      Serial.print("measurement=");
      Serial.print(g_measurementCount);
      Serial.print(" pulse=");
      Serial.print(g_pulseCount);
      Serial.print(" t_us=");
      printU64(pulseUs);
      Serial.print(" dt_us=");
      printU64(dtUs);
      Serial.print(" dt_ms=");
      Serial.print(intervalMs);
      Serial.print(" watts=");
      Serial.println(watts);
      debugFlush();
    }

    blinkPulseLed();
    (void)sendMeasurementAdvertisement(intervalMs, watts);
  } else {
    g_pulseCount = 1U;
    g_lastPulseUs = pulseUs;
    if (debugUsbEnabled()) {
      Serial.print("pulse=1 t_us=");
      printU64(pulseUs);
      Serial.println(" first_pulse waiting_for_interval");
      debugFlush();
    }
    blinkPulseLed();
  }

  configureLpcompInterrupt(LpcompArmMode::kDown);
}

static void handlePulseRearmedBelowThreshold() {
  configureLpcompInterrupt(LpcompArmMode::kUp);
}

}  // namespace

extern "C" void LPCOMP_IRQHandler(void) {
  bool fired = false;
  bool wasUp = false;
  if (g_lpcompArmMode == LpcompArmMode::kUp) {
    if (NRF_LPCOMP->EVENTS_UP == 0U) {
      return;
    }
    NRF_LPCOMP->EVENTS_UP = 0U;
    fired = true;
    wasUp = true;
  } else {
    if (NRF_LPCOMP->EVENTS_DOWN == 0U) {
      return;
    }
    NRF_LPCOMP->EVENTS_DOWN = 0U;
    fired = true;
    wasUp = false;
  }

  if (!fired) {
    return;
  }

  if (g_lpcompWakePending != 0U) {
    g_lpcompWakeOverrun = 1U;
    return;
  }

  g_lpcompWakeUs = g_grtc.counter();
  g_lpcompWakeWasUp = wasUp ? 1U : 0U;
  g_lpcompWakePending = 1U;
}

void setup() {
  debugBegin();

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  BoardControl::collapseRfPathIdle();
  initPulseLed();

  if (!g_grtc.begin(GrtcClockSource::kSystemLfclk)) {
    if (debugUsbEnabled()) {
      Serial.println("ERROR: GRTC begin failed");
      debugFlush();
    }
    while (true) {
      delay(1000);
    }
  }

  if (!g_lpcomp.beginThresholdMv(kPinA0, kAssumedVddMv, kTargetThresholdMv,
                                 kEnableHysteresis, LpcompDetect::kUp)) {
    if (debugUsbEnabled()) {
      Serial.println("ERROR: LPCOMP begin failed");
      debugFlush();
    }
    while (true) {
      delay(1000);
    }
  }

  g_lpcomp.sample();
  configureLpcompInterrupt(g_lpcomp.resultAbove() ? LpcompArmMode::kDown
                                                  : LpcompArmMode::kUp);
  printBootBanner();
}

void loop() {
  sleepUntilLpcompWake();

  uint64_t pulseUs = 0ULL;
  bool wasUp = false;
  if (fetchPendingWake(&pulseUs, &wasUp)) {
    if (wasUp) {
      handlePulseRise(pulseUs);
    } else {
      handlePulseRearmedBelowThreshold();
    }
  }

  if (g_lpcompWakeOverrun != 0U && debugUsbEnabled()) {
    noInterrupts();
    g_lpcompWakeOverrun = 0U;
    interrupts();
    Serial.println("warning=lpcomp_wake_overrun");
    debugFlush();
  }
}
