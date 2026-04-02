/*
 * BleBackgroundAdvertiser3ChannelDiagnostics
 *
 * Brings up the controller-style background advertiser, then prints scheduler
 * counters once per second so the board can be validated without relying on a
 * host-side BLE scan.
 *
 * By default this exercises the 37/38/39 path. Define
 * NRF54L15_BG_DIAG_SINGLE_CHANNEL=1 at build time to run the same diagnostics
 * against the single-channel scheduler.
 *
 * The sketch still never calls an advertising event function from loop().
 * After setup() the HAL owns cadence and loop() only sleeps between prints.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr int8_t kTxPowerDbm = -10;
constexpr uint32_t kAdvertisingIntervalMs = 100;
constexpr uint32_t kInterChannelDelayUs = 350;
constexpr BleAdvertisingChannel kSingleChannel = BleAdvertisingChannel::k37;
#if defined(NRF54L15_BG_DIAG_SINGLE_CHANNEL) && \
    (NRF54L15_BG_DIAG_SINGLE_CHANNEL != 0)
constexpr bool kUseThreeChannel = false;
constexpr const char* kAdvertisingName = "X54-BG-1CH";
#else
constexpr bool kUseThreeChannel = true;
constexpr const char* kAdvertisingName = "X54-BG-DIAG";
#endif
#if defined(NRF54L15_BG_EXAMPLE_HFXO_LEAD_US)
constexpr uint32_t kHfxoLeadUs =
    static_cast<uint32_t>(NRF54L15_BG_EXAMPLE_HFXO_LEAD_US);
#else
constexpr uint32_t kHfxoLeadUs = 1200;
#endif
#if defined(NRF54L15_BG_DIAG_RANDOM_DELAY) && (NRF54L15_BG_DIAG_RANDOM_DELAY != 0)
constexpr bool kAddRandomDelay = true;
#else
constexpr bool kAddRandomDelay = false;
#endif
#if defined(NRF54L15_BG_DIAG_STOP_AFTER_MS)
constexpr unsigned long kStopAfterMs =
    static_cast<unsigned long>(NRF54L15_BG_DIAG_STOP_AFTER_MS);
#else
constexpr unsigned long kStopAfterMs = 0UL;
#endif
#if defined(NRF54L15_BG_DIAG_RENAME_AFTER_MS)
constexpr unsigned long kRenameAfterMs =
    static_cast<unsigned long>(NRF54L15_BG_DIAG_RENAME_AFTER_MS);
#else
constexpr unsigned long kRenameAfterMs = 0UL;
#endif
#if defined(NRF54L15_BG_DIAG_UNSUPPORTED_PDU_AFTER_MS)
constexpr unsigned long kUnsupportedPduAfterMs =
    static_cast<unsigned long>(NRF54L15_BG_DIAG_UNSUPPORTED_PDU_AFTER_MS);
#else
constexpr unsigned long kUnsupportedPduAfterMs = 0UL;
#endif
#if defined(NRF54L15_BG_DIAG_SERVICE_UUID_AFTER_MS)
constexpr unsigned long kServiceUuidAfterMs =
    static_cast<unsigned long>(NRF54L15_BG_DIAG_SERVICE_UUID_AFTER_MS);
#else
constexpr unsigned long kServiceUuidAfterMs = 0UL;
#endif
constexpr unsigned long kSerialBaud = 115200UL;
constexpr unsigned long kPrintPeriodMs = 973UL;
constexpr uint32_t kDiagShadowMagic = 0x42474431UL;
constexpr uint8_t kDiagServiceUuid128[16] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};

enum DiagShadowState : uint32_t {
  kDiagStateBoot = 1U,
  kDiagStateSerialReady = 2U,
  kDiagStateBoardConfigured = 3U,
  kDiagStateBleReady = 4U,
  kDiagStateAdvertisingRunning = 5U,
  kDiagStateAdvertisingStopped = 6U,
  kDiagStateFailure = 0x80000000UL,
};

BleRadio gBle;
PowerManager gPower;
volatile uint32_t g_ble_bg_diag_shadow[50] = {0};
volatile uint8_t gRfPowerSeenLow = 0U;
volatile uint8_t gRfPowerSeenHigh = 0U;
volatile uint8_t gRfPathSeenHiz = 0U;
volatile uint8_t gRfPathSeenActive = 0U;
volatile uint8_t gXoSeenLow = 0U;
volatile uint8_t gXoSeenHigh = 0U;
unsigned long gDiagStartMs = 0UL;

void setDiagState(uint32_t state, uint32_t detail = 0U) {
  g_ble_bg_diag_shadow[0] = kDiagShadowMagic;
  g_ble_bg_diag_shadow[1] = state;
  g_ble_bg_diag_shadow[2] = detail;
}

[[noreturn]] void failStop(uint32_t detail) {
  setDiagState(kDiagStateFailure, detail);
  while (true) {
    __asm volatile("wfi");
  }
}

void configureBoardForBleLowPower() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  BoardControl::enableRfPath(kAntennaPath);
}

void sampleRfPathState() {
  const bool rfPowerEnabled = BoardControl::rfSwitchPowerEnabled();
  const BoardAntennaPath rfPath = BoardControl::antennaPath();
  if (rfPowerEnabled) {
    gRfPowerSeenHigh = 1U;
  } else {
    gRfPowerSeenLow = 1U;
  }
  if (rfPath == BoardAntennaPath::kControlHighImpedance) {
    gRfPathSeenHiz = 1U;
  } else {
    gRfPathSeenActive = 1U;
  }
}

void sampleXoState() {
  const bool xoRunning =
      ((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >> CLOCK_XO_STAT_STATE_Pos) ==
      CLOCK_XO_STAT_STATE_Running;
  if (xoRunning) {
    gXoSeenHigh = 1U;
  } else {
    gXoSeenLow = 1U;
  }
}

void snapshotCountersToShadow(const BleBackgroundAdvertisingDebugCounters& counters,
                              uint32_t rfPowerEnabled,
                              uint32_t rfPath,
                              uint32_t rfSeenLow,
                              uint32_t rfSeenHigh,
                              uint32_t rfSeenHiz,
                              uint32_t rfSeenActive,
                              uint32_t xoRunning,
                              uint32_t xoRunRequested,
                              uint32_t xoSeenLow,
                              uint32_t xoSeenHigh) {
  g_ble_bg_diag_shadow[3] = counters.idleFallbackCount;
  g_ble_bg_diag_shadow[4] = counters.randomHardwareCount;
  g_ble_bg_diag_shadow[5] = counters.randomFallbackCount;
  g_ble_bg_diag_shadow[6] = counters.eventArmCount;
  g_ble_bg_diag_shadow[7] = counters.eventCompleteCount;
  g_ble_bg_diag_shadow[8] = counters.lastRandomDelayUs;
  g_ble_bg_diag_shadow[9] = counters.serviceRunCount;
  g_ble_bg_diag_shadow[10] = counters.stageAdvanceCount;
  g_ble_bg_diag_shadow[11] = counters.irqCompareCount;
  g_ble_bg_diag_shadow[12] = counters.lastChannel;
  g_ble_bg_diag_shadow[13] = counters.currentStage;
  g_ble_bg_diag_shadow[14] = counters.enabled;
  g_ble_bg_diag_shadow[15] = counters.threeChannel;
  g_ble_bg_diag_shadow[16] = counters.constlatServiceObservedCount;
  g_ble_bg_diag_shadow[17] = counters.lowPowerReleaseCount;
  g_ble_bg_diag_shadow[18] = counters.latencyManaged;
  g_ble_bg_diag_shadow[19] = counters.constlatActive;
  g_ble_bg_diag_shadow[20] = counters.constlatPrewarmHardwareCount;
  g_ble_bg_diag_shadow[21] = counters.constlatPrewarmFallbackCount;
  g_ble_bg_diag_shadow[22] = counters.rfPathPrewarmRestoreCount;
  g_ble_bg_diag_shadow[23] = counters.rfPathIdleCollapseCount;
  g_ble_bg_diag_shadow[24] = counters.rfPathManaged;
  g_ble_bg_diag_shadow[25] = counters.rfPathActive;
  g_ble_bg_diag_shadow[26] = rfPowerEnabled;
  g_ble_bg_diag_shadow[27] = rfPath;
  g_ble_bg_diag_shadow[28] = rfSeenLow;
  g_ble_bg_diag_shadow[29] = rfSeenHigh;
  g_ble_bg_diag_shadow[30] = rfSeenHiz;
  g_ble_bg_diag_shadow[31] = rfSeenActive;
  g_ble_bg_diag_shadow[32] = gBle.isBackgroundAdvertisingEnabled() ? 1U : 0U;
  g_ble_bg_diag_shadow[33] = gBle.getBackgroundAdvertisingLastStopReason();
  g_ble_bg_diag_shadow[34] = counters.txReadyCount;
  g_ble_bg_diag_shadow[35] = counters.txStartKickCount;
  g_ble_bg_diag_shadow[36] = counters.txKickRetryCount;
  g_ble_bg_diag_shadow[37] = counters.txKickFallbackCount;
  g_ble_bg_diag_shadow[38] = counters.txSettleTimeoutCount;
  g_ble_bg_diag_shadow[39] = counters.txPhyendCount;
  g_ble_bg_diag_shadow[40] = counters.txDisabledCount;
  g_ble_bg_diag_shadow[41] = counters.lastRadioState;
  g_ble_bg_diag_shadow[42] = counters.clockIrqCount;
  g_ble_bg_diag_shadow[43] = counters.clockXotunedCount;
  g_ble_bg_diag_shadow[44] = counters.clockXotuneErrorCount;
  g_ble_bg_diag_shadow[45] = counters.clockXotuneFailedCount;
  g_ble_bg_diag_shadow[46] = xoRunning;
  g_ble_bg_diag_shadow[47] = xoRunRequested;
  g_ble_bg_diag_shadow[48] = xoSeenLow;
  g_ble_bg_diag_shadow[49] = xoSeenHigh;
}

void printCounters() {
  BleBackgroundAdvertisingDebugCounters counters{};
  gBle.getBackgroundAdvertisingDebugCounters(&counters);
  const uint32_t xoRunning =
      ((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >> CLOCK_XO_STAT_STATE_Pos) ==
              CLOCK_XO_STAT_STATE_Running
          ? 1U
          : 0U;
  const uint32_t xoRunRequested =
      ((NRF_CLOCK->XO.RUN & CLOCK_XO_RUN_STATUS_Msk) >> CLOCK_XO_RUN_STATUS_Pos) ==
              CLOCK_XO_RUN_STATUS_Triggered
          ? 1U
          : 0U;
  const uint32_t rfPowerEnabled = BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
  const uint32_t rfPath = static_cast<uint32_t>(BoardControl::antennaPath());
  const uint32_t rfSeenLow = gRfPowerSeenLow;
  const uint32_t rfSeenHigh = gRfPowerSeenHigh;
  const uint32_t rfSeenHiz = gRfPathSeenHiz;
  const uint32_t rfSeenActive = gRfPathSeenActive;
  const uint32_t xoSeenLow = gXoSeenLow;
  const uint32_t xoSeenHigh = gXoSeenHigh;
  gRfPowerSeenLow = 0U;
  gRfPowerSeenHigh = 0U;
  gRfPathSeenHiz = 0U;
  gRfPathSeenActive = 0U;
  gXoSeenLow = 0U;
  gXoSeenHigh = 0U;

  snapshotCountersToShadow(counters, rfPowerEnabled, rfPath, rfSeenLow, rfSeenHigh,
                           rfSeenHiz, rfSeenActive, xoRunning, xoRunRequested,
                           xoSeenLow, xoSeenHigh);

  Serial.print(F("ms="));
  Serial.print(millis());
  Serial.print(F(" enabled="));
  Serial.print(counters.enabled);
  Serial.print(F(" three_ch="));
  Serial.print(counters.threeChannel);
  Serial.print(F(" arm="));
  Serial.print(counters.eventArmCount);
  Serial.print(F(" complete="));
  Serial.print(counters.eventCompleteCount);
  Serial.print(F(" stage="));
  Serial.print(counters.stageAdvanceCount);
  Serial.print(F(" irq="));
  Serial.print(counters.irqCompareCount);
  Serial.print(F(" service="));
  Serial.print(counters.serviceRunCount);
  Serial.print(F(" idle="));
  Serial.print(counters.idleFallbackCount);
  Serial.print(F(" rng_hw="));
  Serial.print(counters.randomHardwareCount);
  Serial.print(F(" rng_sw="));
  Serial.print(counters.randomFallbackCount);
  Serial.print(F(" constlat_service="));
  Serial.print(counters.constlatServiceObservedCount);
  Serial.print(F(" lowpwr_release="));
  Serial.print(counters.lowPowerReleaseCount);
  Serial.print(F(" managed="));
  Serial.print(counters.latencyManaged);
  Serial.print(F(" constlat_now="));
  Serial.print(counters.constlatActive);
  Serial.print(F(" rf_managed="));
  Serial.print(counters.rfPathManaged);
  Serial.print(F(" rf_dbg_active="));
  Serial.print(counters.rfPathActive);
  Serial.print(F(" rf_restore="));
  Serial.print(counters.rfPathPrewarmRestoreCount);
  Serial.print(F(" rf_collapse="));
  Serial.print(counters.rfPathIdleCollapseCount);
  Serial.print(F(" rf_live_power="));
  Serial.print(rfPowerEnabled);
  Serial.print(F(" rf_live_path="));
  Serial.print(rfPath);
  Serial.print(F(" rf_seen_low="));
  Serial.print(rfSeenLow);
  Serial.print(F(" rf_seen_high="));
  Serial.print(rfSeenHigh);
  Serial.print(F(" rf_seen_hiz="));
  Serial.print(rfSeenHiz);
  Serial.print(F(" rf_seen_active="));
  Serial.print(rfSeenActive);
  Serial.print(F(" constlat_hw_prewarm="));
  Serial.print(counters.constlatPrewarmHardwareCount);
  Serial.print(F(" constlat_sw_prewarm="));
  Serial.print(counters.constlatPrewarmFallbackCount);
  Serial.print(F(" tx_ready="));
  Serial.print(counters.txReadyCount);
  Serial.print(F(" tx_start_kick="));
  Serial.print(counters.txStartKickCount);
  Serial.print(F(" tx_kick_retry="));
  Serial.print(counters.txKickRetryCount);
  Serial.print(F(" tx_kick_fallback="));
  Serial.print(counters.txKickFallbackCount);
  Serial.print(F(" clk_irq="));
  Serial.print(counters.clockIrqCount);
  Serial.print(F(" clk_tuned="));
  Serial.print(counters.clockXotunedCount);
  Serial.print(F(" clk_err="));
  Serial.print(counters.clockXotuneErrorCount);
  Serial.print(F(" clk_fail="));
  Serial.print(counters.clockXotuneFailedCount);
  Serial.print(F(" xo_running="));
  Serial.print(xoRunning);
  Serial.print(F(" xo_run_req="));
  Serial.print(xoRunRequested);
  Serial.print(F(" xo_seen_low="));
  Serial.print(xoSeenLow);
  Serial.print(F(" xo_seen_high="));
  Serial.print(xoSeenHigh);
  Serial.print(F(" tx_settle_to="));
  Serial.print(counters.txSettleTimeoutCount);
  Serial.print(F(" tx_phyend="));
  Serial.print(counters.txPhyendCount);
  Serial.print(F(" tx_disabled="));
  Serial.print(counters.txDisabledCount);
  Serial.print(F(" radio_state="));
  Serial.print(counters.lastRadioState);
  Serial.print(F(" last_ready="));
  Serial.print(counters.lastTxReadySeen);
  Serial.print(F(" last_phyend="));
  Serial.print(counters.lastTxPhyendSeen);
  Serial.print(F(" last_disabled="));
  Serial.print(counters.lastTxDisabledSeen);
  Serial.print(F(" live_enabled="));
  Serial.print(gBle.isBackgroundAdvertisingEnabled() ? 1 : 0);
  Serial.print(F(" stop_reason="));
  Serial.print(gBle.getBackgroundAdvertisingLastStopReason());
  Serial.print(F(" ch="));
  Serial.print(counters.lastChannel);
  Serial.print(F(" cur_stage="));
  Serial.print(counters.currentStage);
  Serial.print(F(" last_start_us="));
  Serial.print(counters.lastEventStartUs);
  Serial.print(F(" last_done_us="));
  Serial.print(counters.lastCompletedEventStartUs);
  Serial.print(F(" last_service_us="));
  Serial.print(counters.lastServiceUs);
  Serial.print(F(" last_rand_us="));
  Serial.println(counters.lastRandomDelayUs);
}

}  // namespace

void setup() {
  setDiagState(kDiagStateBoot, kAddRandomDelay ? 1U : 0U);

  Serial.begin(kSerialBaud);
  const unsigned long serialWaitStartMs = millis();
  while (!Serial && (millis() - serialWaitStartMs) < 1500UL) {
    __NOP();
  }
  setDiagState(kDiagStateSerialReady, kAddRandomDelay ? 1U : 0U);

  configureBoardForBleLowPower();
  setDiagState(kDiagStateBoardConfigured, kAddRandomDelay ? 1U : 0U);

  bool ok = gBle.begin(kTxPowerDbm);
  uint32_t failStep = 1U;
  if (ok) {
    gPower.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    setDiagState(kDiagStateBleReady, kAddRandomDelay ? 1U : 0U);
    failStep = 2U;
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) {
    failStep = 3U;
    ok = gBle.setAdvertisingName(kAdvertisingName, true);
  }
  if (ok && kServiceUuidAfterMs > 0UL) {
    failStep = 4U;
    ok = gBle.setScanResponseData(nullptr, 0U);
  }
  if (ok) {
    failStep = 5U;
    if (kUseThreeChannel) {
      ok = gBle.beginBackgroundAdvertising3Channel(
          kAdvertisingIntervalMs, kInterChannelDelayUs, kHfxoLeadUs,
          kAddRandomDelay);
    } else {
      ok = gBle.beginBackgroundAdvertising(
          kAdvertisingIntervalMs, kSingleChannel, kHfxoLeadUs,
          kAddRandomDelay);
    }
  }
  if (!ok) {
    Serial.println(F("background advertiser start failed"));
    failStop(failStep);
  }

  setDiagState(kDiagStateAdvertisingRunning, kAddRandomDelay ? 1U : 0U);
  gDiagStartMs = millis();
  Serial.println(F("background advertiser running"));
}

void loop() {
  static unsigned long nextPrintMs = 0UL;
  static bool renamed = false;
  static bool serviceUuidUpdated = false;
  static bool unsupportedPduAttempted = false;
  static bool stopped = false;
  const unsigned long nowMs = millis();
  const unsigned long elapsedMs = nowMs - gDiagStartMs;
  if (!renamed && !stopped && kRenameAfterMs > 0UL &&
      elapsedMs >= kRenameAfterMs) {
    const bool renamedOk = gBle.setAdvertisingName("X54-BG-DYN", true);
    Serial.print(F("background advertiser renamed="));
    Serial.println(renamedOk ? 1 : 0);
    renamed = true;
  }
  if (!serviceUuidUpdated && !stopped && kServiceUuidAfterMs > 0UL &&
      elapsedMs >= kServiceUuidAfterMs) {
    const bool serviceUuidOk =
        gBle.setAdvertisingServiceUuid128(kDiagServiceUuid128);
    Serial.print(F("background advertiser service_uuid="));
    Serial.println(serviceUuidOk ? 1 : 0);
    serviceUuidUpdated = true;
  }
  if (!unsupportedPduAttempted && !stopped && kUnsupportedPduAfterMs > 0UL &&
      elapsedMs >= kUnsupportedPduAfterMs) {
    const bool pduOk = gBle.setAdvertisingPduType(BleAdvPduType::kAdvScanInd);
    Serial.print(F("background advertiser unsupported_pdu="));
    Serial.println(pduOk ? 1 : 0);
    unsupportedPduAttempted = true;
  }
  if (!stopped && kStopAfterMs > 0UL && elapsedMs >= kStopAfterMs) {
    gBle.stopBackgroundAdvertising();
    setDiagState(kDiagStateAdvertisingStopped, kAddRandomDelay ? 1U : 0U);
    Serial.println(F("background advertiser stopped"));
    stopped = true;
  }
  if ((long)(nowMs - nextPrintMs) >= 0L) {
    printCounters();
    nextPrintMs = nowMs + kPrintPeriodMs;
  }

  sampleRfPathState();
  sampleXoState();
  {
    BleBackgroundAdvertisingDebugCounters counters{};
    gBle.getBackgroundAdvertisingDebugCounters(&counters);
    snapshotCountersToShadow(counters,
                             BoardControl::rfSwitchPowerEnabled() ? 1U : 0U,
                             static_cast<uint32_t>(BoardControl::antennaPath()),
                             gRfPowerSeenLow, gRfPowerSeenHigh, gRfPathSeenHiz,
                             gRfPathSeenActive,
                             ((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >>
                              CLOCK_XO_STAT_STATE_Pos) ==
                                     CLOCK_XO_STAT_STATE_Running
                                 ? 1U
                                 : 0U,
                             ((NRF_CLOCK->XO.RUN & CLOCK_XO_RUN_STATUS_Msk) >>
                              CLOCK_XO_RUN_STATUS_Pos) ==
                                     CLOCK_XO_RUN_STATUS_Triggered
                                 ? 1U
                                 : 0U,
                             gXoSeenLow, gXoSeenHigh);
  }
  __asm volatile("wfi");
}
