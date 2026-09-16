namespace {

using namespace nrf54l15;
using namespace xiao_nrf54l15;
using namespace xiao_nrf54l15::hal_internal;

uint64_t readLittleEndian64(const uint8_t* data, uint8_t length) {
  if (data == nullptr) {
    return 0U;
  }
  uint64_t value = 0U;
  const uint8_t capped = (length > 8U) ? 8U : length;
  for (uint8_t i = 0U; i < capped; ++i) {
    value |= (static_cast<uint64_t>(data[i]) << (8U * i));
  }
  return value;
}

bool isAllBytes(const uint8_t* data, uint8_t length, uint8_t value) {
  if (data == nullptr) {
    return false;
  }
  for (uint8_t i = 0U; i < length; ++i) {
    if (data[i] != value) {
      return false;
    }
  }
  return true;
}
xiao_nrf54l15::I2sTx* g_activeI2sTx = nullptr;
xiao_nrf54l15::I2sRx* g_activeI2sRx = nullptr;
xiao_nrf54l15::I2sDuplex* g_activeI2sDuplex = nullptr;
xiao_nrf54l15::BleRadio* g_activeBleRadio = nullptr;
// Set only when a BleRadio owner is destroyed without observing RADIO Disabled.
// The hardware and its shared power leases remain quarantined until reset.
volatile uint8_t g_bleRadioHardwareQuarantined = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_connect_debug_stage = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_connect_debug_header = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_connect_debug_length = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_connect_debug_flags = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_connect_debug_address[6] = {0};
extern "C" {
volatile uint8_t __attribute__((used)) g_ble_periph_last_att_req_opcode = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_last_att_rsp_opcode = 0U;
volatile uint32_t __attribute__((used)) g_ble_periph_att_req_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_periph_att_rsp_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_follow_rx_preserved_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_posttx_observe_count = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_posttx_observe_flags = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_posttx_observe_state = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_rxtimeout_observe_count = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_rxtimeout_observe_flags = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_rxtimeout_observe_state = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_rxtimeout_hdr0 = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_rxtimeout_hdr1 = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_rxtimeout_hdr2 = 0U;
volatile uint8_t __attribute__((used)) g_ble_central_rxtimeout_hdr3 = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_rxstart_gate_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_rxstart_gate_timeout_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_rxstart_gate_extend_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_missing_peer_fast_timeout_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_missing_peer_last_missed_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_central_missing_peer_threshold = 0U;
volatile uint32_t __attribute__((used)) g_ble_periph_conn_trace_magic = 0x43545243UL;
volatile uint32_t __attribute__((used)) g_ble_periph_conn_trace_count = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_conn_trace_stage[16] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_conn_trace_channel[16] = {0};
volatile uint16_t __attribute__((used)) g_ble_periph_conn_trace_event[16] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_conn_trace_value[16] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_last_rx_llid = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_last_rx_len = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_last_rx_packet_is_new = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_last_rx_peer_acked = 0U;
volatile uint16_t __attribute__((used)) g_ble_periph_last_rx_l2cap_cid = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_last_rx_att_opcode = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_last_rx_ll_opcode = 0U;
volatile uint32_t __attribute__((used)) g_ble_periph_rx_trace_count = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_rx_trace_llid[32] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_rx_trace_len[32] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_rx_trace_flags[32] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_rx_trace_opcode[32] = {0};
volatile uint16_t __attribute__((used)) g_ble_periph_rx_trace_cid[32] = {0};
volatile uint32_t __attribute__((used)) g_ble_periph_tx_trace_count = 0U;
volatile uint8_t __attribute__((used)) g_ble_periph_tx_trace_llid[32] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_tx_trace_len[32] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_tx_trace_opcode[32] = {0};
volatile uint16_t __attribute__((used)) g_ble_periph_tx_trace_cid[32] = {0};
volatile uint8_t __attribute__((used)) g_ble_periph_connect_win_size = 0U;
volatile uint16_t __attribute__((used)) g_ble_periph_connect_win_offset = 0U;
volatile uint32_t __attribute__((used)) g_ble_unconnected_radio_diag_magic =
    0x42524144UL;
volatile uint32_t __attribute__((used)) g_ble_unconnected_radio_begin_count =
    0U;
volatile uint32_t __attribute__((used)) g_ble_unconnected_radio_end_count = 0U;
volatile uint32_t __attribute__((used)) g_ble_unconnected_radio_active = 0U;
volatile uint32_t __attribute__((used)) g_ble_unconnected_radio_last_begin_us =
    0U;
volatile uint32_t __attribute__((used)) g_ble_unconnected_radio_last_end_us =
    0U;
volatile uint32_t __attribute__((used)) g_ble_scan_active_adv_rx_count = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_channel = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_header = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_length = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_flags = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_return_code = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_return_value = 0U;
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_address[6] = {0};
volatile uint8_t __attribute__((used)) g_ble_scan_active_last_adv_data[24] = {0};
}
// Reserve one GRTC compare channel for BLE connection anchors so the custom LL
// can arm itself independently of sketch polling.
static constexpr uint8_t kBleBackgroundCompareChannel = 2U;
static constexpr uint8_t kBleBackgroundAdvPrewarmCompareChannel = 3U;
static constexpr uint8_t kBleBackgroundAdvTxCompareChannel = 4U;
static constexpr uint8_t kBleBackgroundConnPrewarmCompareChannel = 5U;
static constexpr uint8_t kBleBackgroundAdvCleanupCompareChannel = 6U;
static constexpr uint8_t kBleBackgroundAdvStage1ServiceCompareChannel = 7U;
static constexpr uint8_t kBleBackgroundAdvStage1TxCompareChannel = 8U;
static constexpr uint8_t kBleBackgroundAdvStage2ServiceCompareChannel = 9U;
static constexpr uint8_t kBleBackgroundAdvStage2TxCompareChannel = 10U;
static constexpr uint8_t kBleBackgroundAdvFinalCleanupCompareChannel = 11U;
// The device exposes GRTC CC[0..11] only. Background connection RX and
// disconnected advertising cleanup never run at the same time, so they can
// safely share the same compare channel.
static constexpr uint8_t kBleBackgroundConnRxCompareChannel =
    kBleBackgroundAdvFinalCleanupCompareChannel;
#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_LP_TX_DPPI_CHANNEL)
#define NRF54L15_CLEAN_BLE_BACKGROUND_LP_TX_DPPI_CHANNEL 1
#endif
#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_PERI_TX_DPPI_CHANNEL)
#define NRF54L15_CLEAN_BLE_BACKGROUND_PERI_TX_DPPI_CHANNEL 5
#endif
#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_RADIO_TX_DPPI_CHANNEL)
#define NRF54L15_CLEAN_BLE_BACKGROUND_RADIO_TX_DPPI_CHANNEL 0
#endif
static constexpr uint8_t kBleBackgroundAdvLpPrewarmDppiChannel = 0U;
static constexpr uint8_t kBleBackgroundConnLpPrewarmDppiChannel =
    kBleBackgroundAdvLpPrewarmDppiChannel;
static constexpr uint8_t kBleBackgroundConnLpRxDppiChannel = 4U;
static constexpr uint8_t kBleBackgroundAdvLpTxDppiChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_BLE_BACKGROUND_LP_TX_DPPI_CHANNEL);
static constexpr uint8_t kBleBackgroundAdvLpStopDppiChannel = 2U;
static constexpr uint8_t kBleBackgroundAdvLpStopEnableDppiChannel = 3U;
static constexpr uint8_t kBleBackgroundAdvPeriTxDppiChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_BLE_BACKGROUND_PERI_TX_DPPI_CHANNEL);
static constexpr uint8_t kBleBackgroundAdvPeriStopDppiChannel = 1U;
static constexpr uint8_t kBleBackgroundAdvPeriStopEnableDppiChannel = 2U;
static constexpr uint8_t kBleBackgroundConnPeriRxDppiChannel = 4U;
static constexpr uint8_t kBleBackgroundAdvRadioPllenDppiChannel = 1U;
static constexpr uint8_t kBleBackgroundAdvRadioTxDppiChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_BLE_BACKGROUND_RADIO_TX_DPPI_CHANNEL);
static constexpr uint8_t kBleBackgroundAdvRadioStopDppiChannel = 1U;
static constexpr uint8_t kBleBackgroundAdvRadioStopEnableDppiChannel = 2U;
static constexpr uint8_t kBleBackgroundConnRadioRxDppiChannel = 3U;
static constexpr uint8_t kBleBackgroundAdvLpToPeriBridgeIndex = 0U;
static constexpr uint8_t kBleBackgroundAdvRadioToLpBridgeIndex = 1U;
static constexpr uint8_t kBleBackgroundAdvStopEnableBridgeIndex = 2U;
static constexpr uint8_t kBleBackgroundConnRxBridgeIndex = 4U;
static constexpr uint8_t kBleBackgroundAdvStopPathGroup = 0U;
static constexpr uint32_t kBleBackgroundAdvEventWindowUs = 1000U;
static constexpr uint32_t kBleBackgroundAdvDefaultHfxoLeadUs = 1200U;
static constexpr uint32_t kBleBackgroundConnDefaultHfxoLeadUs = 650U;
static constexpr uint32_t kBleBackgroundConnHardwareWakeLeadUs = 40U;
static constexpr uint32_t kBleBackgroundConnRxFallbackLeadUs = 16U;
static constexpr uint32_t kBleBackgroundAdvFirstEventGuardUs = 1000U;
static constexpr uint32_t kBleBackgroundAdvMinIntervalUs = 5000U;
static constexpr uint32_t kBleBackgroundAdvTxRampUpUs = 40U;
static constexpr uint32_t kBleBackgroundAdvDisableGuardUs = 320U;
static constexpr uint32_t kBleBackgroundAdvStageServiceLeadUs = 250U;
static constexpr uint32_t kBleBackgroundAdvStageSettleBudgetUs = 320U;
static constexpr uint32_t kBleBackgroundAdvEventSettleBudgetUs = 3000U;
static constexpr uint32_t kBleBackgroundAdvTxKickRetryUs = 50U;
static constexpr uint8_t kBleBackgroundAdvTxKickRetryLimit = 64U;
static constexpr uint8_t kBleBackgroundInvalidCompareChannel = 0xFFU;
static constexpr uint32_t kBleBackgroundAdvMinInterChannelDelayUs =
    kBleBackgroundAdvStageServiceLeadUs;
static constexpr uint8_t kBleBackgroundAdvStopReasonNone = 0U;
static constexpr uint8_t kBleBackgroundAdvStopReasonExplicit = 1U;
static constexpr uint8_t kBleBackgroundAdvStopReasonSettleTimeout = 2U;
static constexpr uint8_t kBleBackgroundAdvStopReasonPrepareFailed = 3U;
static constexpr uint8_t kBleBackgroundAdvStopReasonArmFailed = 4U;
static constexpr uint8_t kBleBackgroundAdvStopReasonPrewarmFailed = 5U;
static constexpr uint8_t kBleBackgroundAdvStopReasonInvalidState = 6U;
static constexpr uint8_t kBleBackgroundAdvStopReasonTxCompareFailed = 7U;
#if NRF54L15_GRTC_IRQ_GROUP == 2U
static constexpr IRQn_Type kBleBackgroundGrtcIrq = GRTC_2_IRQn;
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
static constexpr IRQn_Type kBleBackgroundGrtcIrq = GRTC_1_IRQn;
#else
static constexpr IRQn_Type kBleBackgroundGrtcIrq = GRTC_0_IRQn;
#endif
static constexpr IRQn_Type kBleBackgroundClockIrq = CLOCK_POWER_IRQn;
xiao_nrf54l15::BleRadio* volatile g_bleBackgroundRadio = nullptr;

static inline void bleDetachRadioRestartAutomation(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }
  radio->SHORTS = 0U;
  radio->SUBSCRIBE_TXEN = 0U;
  radio->SUBSCRIBE_RXEN = 0U;
  radio->SUBSCRIBE_START = 0U;
  radio->SUBSCRIBE_STOP = 0U;
  radio->SUBSCRIBE_DISABLE = 0U;
  radio->SUBSCRIBE_RSSISTART = 0U;
  radio->SUBSCRIBE_BCSTART = 0U;
  radio->SUBSCRIBE_BCSTOP = 0U;
  radio->SUBSCRIBE_CCASTART = 0U;
  radio->SUBSCRIBE_CCASTOP = 0U;
  radio->SUBSCRIBE_EDSTART = 0U;
  radio->SUBSCRIBE_EDSTOP = 0U;
  radio->SUBSCRIBE_AUXDATADMASTART = 0U;
  radio->SUBSCRIBE_AUXDATADMASTOP = 0U;
  radio->SUBSCRIBE_PLLEN = 0U;
  radio->SUBSCRIBE_CSTONESSTART = 0U;
  radio->SUBSCRIBE_SOFTRESET = 0U;
}
static volatile uint8_t g_bleIdleWakePending = 0U;
static volatile uint8_t g_bleIdleWakeArmed = 0U;
static volatile uint8_t g_bleIdleWakePermitForeground = 0U;

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_SERVICE)
#define NRF54L15_CLEAN_BLE_BACKGROUND_SERVICE 1
#endif

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_GRTC_IRQ_RUN)
#define NRF54L15_CLEAN_BLE_BACKGROUND_GRTC_IRQ_RUN 1
#endif

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_PENDSV_HANDOFF)
#define NRF54L15_CLEAN_BLE_BACKGROUND_PENDSV_HANDOFF 1
#endif

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_MANAGED_CONSTLAT)
#define NRF54L15_CLEAN_BLE_BACKGROUND_MANAGED_CONSTLAT 0
#endif

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_MANAGE_RF_PATH)
#if defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN)
#define NRF54L15_CLEAN_BLE_BACKGROUND_MANAGE_RF_PATH 1
#else
#define NRF54L15_CLEAN_BLE_BACKGROUND_MANAGE_RF_PATH 0
#endif
#endif

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_FORCE_IRQ_TXEN)
#if defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN)
#define NRF54L15_CLEAN_BLE_BACKGROUND_FORCE_IRQ_TXEN 1
#else
#define NRF54L15_CLEAN_BLE_BACKGROUND_FORCE_IRQ_TXEN 0
#endif
#endif

static volatile uint8_t g_bleBackgroundPendSvPending = 0U;
static volatile uint8_t g_bleBackgroundAdvDirectTxKickBlacklisted = 0U;

static inline uint32_t halEnterCritical();
static inline void halExitCritical(uint32_t primask);

static inline NRF_DPPIC_Type* bleDppic10() {
  return reinterpret_cast<NRF_DPPIC_Type*>(
      static_cast<uintptr_t>(nrf54l15::DPPIC10_BASE));
}

static inline NRF_DPPIC_Type* bleDppic20() {
  return reinterpret_cast<NRF_DPPIC_Type*>(
      static_cast<uintptr_t>(nrf54l15::DPPIC20_BASE));
}

static inline NRF_DPPIC_Type* bleDppic30() {
  return reinterpret_cast<NRF_DPPIC_Type*>(
      static_cast<uintptr_t>(nrf54l15::DPPIC30_BASE));
}

static inline NRF_PPIB_Type* blePpib11() {
  return reinterpret_cast<NRF_PPIB_Type*>(
      static_cast<uintptr_t>(nrf54l15::PPIB11_BASE));
}

static inline NRF_PPIB_Type* blePpib21() {
  return reinterpret_cast<NRF_PPIB_Type*>(
      static_cast<uintptr_t>(nrf54l15::PPIB21_BASE));
}

static inline NRF_PPIB_Type* blePpib22() {
  return reinterpret_cast<NRF_PPIB_Type*>(
      static_cast<uintptr_t>(nrf54l15::PPIB22_BASE));
}

static inline NRF_PPIB_Type* blePpib30() {
  return reinterpret_cast<NRF_PPIB_Type*>(
      static_cast<uintptr_t>(nrf54l15::PPIB30_BASE));
}

static inline bool bleBackgroundAdvertisingInitialUseIrqTxKick() {
  return (NRF54L15_CLEAN_BLE_BACKGROUND_FORCE_IRQ_TXEN != 0) ||
         (g_bleBackgroundAdvDirectTxKickBlacklisted != 0U);
}

uint32_t bleDataPduAirTimeUs(uint8_t payloadLength);
uint32_t bleDataPduAirTimeUsOnPhy(uint8_t payloadLength, uint8_t phy);
uint32_t bleConnectionTxEndBudgetUsOnPhy(uint8_t payloadLength, uint8_t phy);

static inline BleAdvertisingChannel bleBackgroundAdvertisingStageChannel(uint8_t stage) {
  switch (stage % 3U) {
    case 1U:
      return BleAdvertisingChannel::k38;
    case 2U:
      return BleAdvertisingChannel::k39;
    default:
      return BleAdvertisingChannel::k37;
  }
}

static inline BleAdvertisingChannel bleBackgroundAdvertisingNextChannel(
    BleAdvertisingChannel channel) {
  switch (channel) {
    case BleAdvertisingChannel::k37:
      return BleAdvertisingChannel::k38;
    case BleAdvertisingChannel::k38:
      return BleAdvertisingChannel::k39;
    default:
      return BleAdvertisingChannel::k37;
  }
}

static inline uint8_t bleBackgroundAdvertisingServiceCompareChannelForTxCompare(
    bool threeChannel, uint8_t txCompareChannel) {
  if (!threeChannel) {
    return (txCompareChannel == kBleBackgroundAdvTxCompareChannel)
               ? kBleBackgroundAdvCleanupCompareChannel
               : kBleBackgroundInvalidCompareChannel;
  }

  switch (txCompareChannel) {
    case kBleBackgroundAdvTxCompareChannel:
      return kBleBackgroundAdvStage1ServiceCompareChannel;
    case kBleBackgroundAdvStage1TxCompareChannel:
      return kBleBackgroundAdvStage2ServiceCompareChannel;
    case kBleBackgroundAdvStage2TxCompareChannel:
      return kBleBackgroundAdvFinalCleanupCompareChannel;
    default:
      return kBleBackgroundInvalidCompareChannel;
  }
}

static inline bool bleBackgroundAdvertisingSetStopPathEnabled(bool enable) {
  if (bleDppic30() == nullptr) {
    return false;
  }

  Dppic lpDomain(nrf54l15::DPPIC30_BASE);
  return lpDomain.enableChannel(kBleBackgroundAdvLpStopDppiChannel, enable);
}

static inline bool bleBackgroundAdvertisingSetTxPathEnabled(bool enable) {
  if (bleDppic30() == nullptr) {
    return false;
  }

  Dppic lpDomain(nrf54l15::DPPIC30_BASE);
  return lpDomain.enableChannel(kBleBackgroundAdvLpTxDppiChannel, enable);
}

static inline bool bleHfxoReadyForRadio() {
  return bleHfxoRunning() && NRF_CLOCK->EVENTS_XOTUNED != 0U &&
         NRF_CLOCK->EVENTS_XOTUNEERROR == 0U &&
         NRF_CLOCK->EVENTS_XOTUNEFAILED == 0U;
}

static volatile uint16_t g_fallbackConstlatUsers = 0U;

extern "C" uint8_t __attribute__((weak)) nrf54l15_constlat_acquire(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (g_fallbackConstlatUsers == UINT16_MAX) {
    __set_PRIMASK(primask);
    return 0U;
  }
  if (g_fallbackConstlatUsers == 0U) {
    NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
    __DSB();
    __ISB();
  }
  ++g_fallbackConstlatUsers;
  __set_PRIMASK(primask);
  return 1U;
}

extern "C" void __attribute__((weak)) nrf54l15_constlat_release(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (g_fallbackConstlatUsers > 0U) {
    --g_fallbackConstlatUsers;
    if (g_fallbackConstlatUsers == 0U) {
      NRF_POWER->TASKS_LOWPWR = POWER_TASKS_LOWPWR_TASKS_LOWPWR_Trigger;
      __DSB();
      __ISB();
    }
  }
  __set_PRIMASK(primask);
}

extern "C" uint8_t __attribute__((weak)) nrf54l15_constlat_users_active(void) {
  return (g_fallbackConstlatUsers != 0U) ? 1U : 0U;
}

static inline bool bleBackgroundAdvertisingConstlatActive() {
  return (NRF_POWER->CONSTLATSTAT & POWER_CONSTLATSTAT_STATUS_Msk) != 0U;
}

static inline bool bleBackgroundAdvertisingShouldManageLatency() {
#if NRF54L15_CLEAN_BLE_BACKGROUND_MANAGED_CONSTLAT != 0
  return true;
#else
  return false;
#endif
}

static inline bool bleBackgroundAdvertisingShouldManageRfPath() {
#if NRF54L15_CLEAN_BLE_BACKGROUND_MANAGE_RF_PATH != 0
  return true;
#else
  return false;
#endif
}

static inline bool bleBackgroundAdvertisingReleaseManagedLatency(bool managed) {
  if (!managed) {
    return false;
  }
  nrf54l15_constlat_release();
  return true;
}

static inline bool bleBackgroundAdvertisingAssertManagedLatency(bool managed,
                                                               bool* alreadyActive) {
  if (alreadyActive != nullptr) {
    *alreadyActive = false;
  }
  if (!managed) {
    return false;
  }
  if (alreadyActive != nullptr) {
    *alreadyActive = bleBackgroundAdvertisingConstlatActive();
  }
  // Ownership follows the counted acquire result. CONSTLATSTAT can lag the
  // task by peripheral clock cycles and is diagnostic only; treating that lag
  // as an acquire failure would leak the newly incremented shared lease.
  return nrf54l15_constlat_acquire() != 0U;
}

static inline uint32_t bleBackgroundAdvertisingPacketWindowUs(uint8_t payloadLength) {
  return kBleBackgroundAdvTxRampUpUs + bleDataPduAirTimeUs(payloadLength) +
         kBleBackgroundAdvDisableGuardUs;
}

static inline uint32_t bleBackgroundAdvertisingServiceLeadUs(uint32_t interChannelDelayUs) {
  return (interChannelDelayUs < kBleBackgroundAdvStageServiceLeadUs)
             ? interChannelDelayUs
             : kBleBackgroundAdvStageServiceLeadUs;
}

static inline CracenRng& bleBackgroundAdvertisingRng() {
  static CracenRng rng;
  return rng;
}

static inline void bleBackgroundAdvertisingConfigureRandomSource(bool enable) {
  CracenRng& rng = bleBackgroundAdvertisingRng();
  if (!enable && rng.active()) {
    rng.end();
  }
}

static inline uint32_t bleBackgroundAdvertisingRandomDelayUs(uint32_t* state,
                                                             uint8_t addressSeed,
                                                             bool* usedHardware) {
  CracenRng& rng = bleBackgroundAdvertisingRng();
  uint32_t randomWord = 0U;
  static constexpr uint32_t kBleBackgroundAdvRandomWordSpinLimit = 4096U;
  if (rng.randomWord(&randomWord, kBleBackgroundAdvRandomWordSpinLimit)) {
    if (usedHardware != nullptr) {
      *usedHardware = true;
    }
    if (state != nullptr) {
      *state = randomWord;
    }
    return randomWord % 10001U;
  }
  if (usedHardware != nullptr) {
    *usedHardware = false;
  }

  uint32_t fallbackState = (state != nullptr) ? *state : 0U;
  if (fallbackState == 0U) {
    fallbackState = micros() ^ (static_cast<uint32_t>(addressSeed) << 24U);
  }
  fallbackState = fallbackState * 1103515245U + 12345U;
  if (state != nullptr) {
    *state = fallbackState;
  }
  return fallbackState % 10001U;
}

static inline void blePendSvSetPending() {
  constexpr uintptr_t kIcsrAddress = 0xE000ED04UL;
  constexpr uint32_t kPendSvSetMask = (1UL << 28U);
  *reinterpret_cast<volatile uint32_t*>(kIcsrAddress) = kPendSvSetMask;
  __DSB();
  __ISB();
}

static inline void blePendSvClearPending() {
  constexpr uintptr_t kIcsrAddress = 0xE000ED04UL;
  constexpr uint32_t kPendSvClearMask = (1UL << 27U);
  *reinterpret_cast<volatile uint32_t*>(kIcsrAddress) = kPendSvClearMask;
  __DSB();
  __ISB();
}

enum class ComparatorOwner : uint8_t {
  kNone = 0U,
  kComp = 1U,
  kLpcomp = 2U,
};

ComparatorOwner g_comparatorOwner = ComparatorOwner::kNone;

bool claimComparator(ComparatorOwner owner) {
  const uint32_t irqState = halEnterCritical();
  if (g_comparatorOwner != ComparatorOwner::kNone &&
      g_comparatorOwner != owner) {
    halExitCritical(irqState);
    return false;
  }
  g_comparatorOwner = owner;
  halExitCritical(irqState);
  return true;
}

void releaseComparator(ComparatorOwner owner) {
  const uint32_t irqState = halEnterCritical();
  if (g_comparatorOwner == owner) {
    g_comparatorOwner = ComparatorOwner::kNone;
  }
  halExitCritical(irqState);
}

bool pinSupportsAnalogInput(const Pin& pin) {
  return isConnected(pin) && (xiao_nrf54l15::saadcInputForPin(pin) >= 0);
}

bool configureAnalogPeripheralPin(const Pin& pin) {
  if (!pinSupportsAnalogInput(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  uint32_t cnf = 0U;
  cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
  cnf |= GPIO_PIN_CNF_PULL_Disabled;
  cnf |= GPIO_PIN_CNF_SENSE_Disabled;
  cnf |= ((GPIO_PIN_CNF_CTRLSEL_GPIO << GPIO_PIN_CNF_CTRLSEL_Pos) &
          GPIO_PIN_CNF_CTRLSEL_Msk);
  reg32(base + gpio::PIN_CNF +
        (static_cast<uint32_t>(pin.pin) * sizeof(uint32_t))) = cnf;
  return true;
}

uint32_t makeCompPinSelect(const Pin& pin) {
  return ((static_cast<uint32_t>(pin.pin) << COMP_PSEL_PIN_Pos) &
          COMP_PSEL_PIN_Msk) |
         ((static_cast<uint32_t>(pin.port) << COMP_PSEL_PORT_Pos) &
          COMP_PSEL_PORT_Msk);
}

uint32_t makeLpcompPinSelect(const Pin& pin) {
  return ((static_cast<uint32_t>(pin.pin) << LPCOMP_PSEL_PIN_Pos) &
          LPCOMP_PSEL_PIN_Msk) |
         ((static_cast<uint32_t>(pin.port) << LPCOMP_PSEL_PORT_Pos) &
          LPCOMP_PSEL_PORT_Msk);
}

uint32_t makeQdecConnectedPinSelect(const Pin& pin) {
  if (!isConnected(pin)) {
    return PSEL_DISCONNECTED;
  }

  return ((static_cast<uint32_t>(pin.pin) << QDEC_PSEL_A_PIN_Pos) &
          QDEC_PSEL_A_PIN_Msk) |
         ((static_cast<uint32_t>(pin.port) << QDEC_PSEL_A_PORT_Pos) &
          QDEC_PSEL_A_PORT_Msk) |
         ((QDEC_PSEL_A_CONNECT_Connected << QDEC_PSEL_A_CONNECT_Pos) &
          QDEC_PSEL_A_CONNECT_Msk);
}

uint16_t clampPermille(uint16_t permille) {
  if (permille > 1000U) {
    return 1000U;
  }
  return permille;
}

uint32_t compThresholdCodeFromPermille(uint16_t permille) {
  permille = clampPermille(permille);
  uint32_t scaled = (static_cast<uint32_t>(permille) * 64U + 500U) / 1000U;
  if (scaled < 1U) {
    scaled = 1U;
  }
  if (scaled > 64U) {
    scaled = 64U;
  }
  return scaled - 1U;
}

uint32_t compThresholdRegValue(uint16_t lowPermille, uint16_t highPermille) {
  const uint32_t lowCode = compThresholdCodeFromPermille(lowPermille);
  const uint32_t highCode = compThresholdCodeFromPermille(highPermille);
  return ((lowCode << COMP_TH_THDOWN_Pos) & COMP_TH_THDOWN_Msk) |
         ((highCode << COMP_TH_THUP_Pos) & COMP_TH_THUP_Msk);
}

bool waitForCompReady(volatile uint32_t* readyEvent, uint32_t spinLimit) {
  return waitForNonZero(readyEvent, spinLimit);
}

LpcompReference nearestLpcompReference(uint16_t thresholdPermille) {
  thresholdPermille = clampPermille(thresholdPermille);
  struct Candidate {
    uint16_t permille;
    LpcompReference ref;
  };
  static constexpr Candidate kCandidates[] = {
      {63U, LpcompReference::k1over16Vdd},
      {125U, LpcompReference::k1over8Vdd},
      {188U, LpcompReference::k3over16Vdd},
      {250U, LpcompReference::k2over8Vdd},
      {313U, LpcompReference::k5over16Vdd},
      {375U, LpcompReference::k3over8Vdd},
      {438U, LpcompReference::k7over16Vdd},
      {500U, LpcompReference::k4over8Vdd},
      {563U, LpcompReference::k9over16Vdd},
      {625U, LpcompReference::k5over8Vdd},
      {688U, LpcompReference::k11over16Vdd},
      {750U, LpcompReference::k6over8Vdd},
      {813U, LpcompReference::k13over16Vdd},
      {875U, LpcompReference::k7over8Vdd},
      {938U, LpcompReference::k15over16Vdd},
  };

  uint32_t bestDiff = 0xFFFFFFFFUL;
  LpcompReference best = LpcompReference::k4over8Vdd;
  for (const Candidate& candidate : kCandidates) {
    const uint32_t diff = absDiffU32(candidate.permille, thresholdPermille);
    if (diff < bestDiff) {
      bestDiff = diff;
      best = candidate.ref;
      if (diff == 0U) {
        break;
      }
    }
  }
  return best;
}

GpioPull gpioPullFromQdecInputPull(QdecInputPull pull) {
  switch (pull) {
    case QdecInputPull::kPullDown:
      return GpioPull::kPullDown;
    case QdecInputPull::kPullUp:
      return GpioPull::kPullUp;
    case QdecInputPull::kDisabled:
    default:
      return GpioPull::kDisabled;
  }
}

constexpr uint32_t kBleAccessAddress = 0x8E89BED6UL;
constexpr uint32_t kBleAdvertisingCrcInit = 0x555555UL;
constexpr uint32_t kBleCrcPolynomial = 0x00065BUL;
constexpr uint8_t kBlePduScanReq = 0x03U;
constexpr uint8_t kBlePduAdvExtInd = 0x07U;
constexpr uint8_t kBleExtendedAdvModeNonConnNonScan = 0x00U;
constexpr uint8_t kBleExtendedAdvModeConnNonScan = 0x01U;
constexpr uint8_t kBleExtendedAdvModeNonConnScan = 0x02U;
constexpr uint8_t kBleExtendedPrimaryHeaderFlags = 0x19U;  // AdvA + ADI + AuxPtr
constexpr uint8_t kBleExtendedAuxHeaderFlags = 0x08U;      // ADI
constexpr uint8_t kBleExtendedAuxPhy1M = 0x00U;
constexpr uint8_t kBleExtendedAuxOffsetUnit30Us = 30U;
constexpr uint16_t kBleExtendedAuxOffsetUnit300Us = 300U;
constexpr uint16_t kBleExtendedAuxOffsetMaxUnits = 0x1FFFU;
constexpr uint32_t kBleExtendedAuxRxLeadUs = 220U;
constexpr uint8_t kBleExtendedClockAccuracy500Ppm = 0x00U;
constexpr uint8_t kBlePduScanRsp = 0x04U;
constexpr uint8_t kBlePduConnectInd = 0x05U;
constexpr uint8_t kBlePduLlControl = 0x03U;
constexpr uint8_t kBlePduDataContinuation = 0x01U;
constexpr uint8_t kBlePduDataStartOrComplete = 0x02U;
constexpr uint8_t kBleLlCtrlTerminateInd = 0x02U;
constexpr uint8_t kBleLlCtrlEncReq = 0x03U;
constexpr uint8_t kBleLlCtrlEncRsp = 0x04U;
constexpr uint8_t kBleLlCtrlStartEncReq = 0x05U;
constexpr uint8_t kBleLlCtrlStartEncRsp = 0x06U;
constexpr uint8_t kBleLlCtrlConnectionUpdateInd = 0x00U;
constexpr uint8_t kBleLlCtrlChannelMapInd = 0x01U;

struct BleExtendedAuxPtrView {
  bool valid;
  uint8_t channel;
  uint8_t phy;
  uint32_t offsetUs;
};

struct BleExtendedHeaderView {
  bool valid;
  bool hasAdvA;
  bool hasAdi;
  bool hasAuxPtr;
  uint8_t advMode;
  const uint8_t* advA;
  uint16_t did;
  uint8_t sid;
  BleExtendedAuxPtrView auxPtr;
  const uint8_t* advData;
  uint8_t advDataLength;
};

bool parseBleExtendedAuxPtr(const uint8_t auxPtr[3], BleExtendedAuxPtrView* outView) {
  if (auxPtr == nullptr || outView == nullptr) {
    return false;
  }

  memset(outView, 0, sizeof(*outView));
  const uint8_t channel = static_cast<uint8_t>(auxPtr[0] & 0x3FU);
  const bool offsetUnit300Us = ((auxPtr[0] >> 7U) & 0x1U) != 0U;
  const uint16_t offsetUnits = static_cast<uint16_t>(
      static_cast<uint16_t>(auxPtr[1]) |
      (static_cast<uint16_t>(auxPtr[2] & 0x1FU) << 8U));
  const uint8_t phy = static_cast<uint8_t>((auxPtr[2] >> 5U) & 0x07U);
  if (channel > 36U) {
    return false;
  }

  outView->valid = true;
  outView->channel = channel;
  outView->phy = phy;
  outView->offsetUs = static_cast<uint32_t>(offsetUnits) *
                      (offsetUnit300Us ? kBleExtendedAuxOffsetUnit300Us
                                       : kBleExtendedAuxOffsetUnit30Us);
  return true;
}

bool parseBleExtendedHeader(const uint8_t* payload, uint8_t payloadLength,
                            BleExtendedHeaderView* outView) {
  if (payload == nullptr || outView == nullptr || payloadLength < 1U) {
    return false;
  }

  memset(outView, 0, sizeof(*outView));

  outView->advMode = static_cast<uint8_t>((payload[0] >> 6U) & 0x03U);
  const uint8_t extHdrLen = static_cast<uint8_t>(payload[0] & 0x3FU);
  if ((payloadLength < 2U) || (payloadLength < static_cast<uint8_t>(1U + extHdrLen)) ||
      (extHdrLen < 1U)) {
    return false;
  }

  const uint8_t flags = payload[1];
  const uint8_t* cursor = &payload[2];
  uint8_t remainingHdr = static_cast<uint8_t>(extHdrLen - 1U);

  if ((flags & 0x01U) != 0U) {
    if (remainingHdr < 6U) {
      return false;
    }
    outView->hasAdvA = true;
    outView->advA = cursor;
    cursor += 6U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 6U);
  }
  if ((flags & 0x02U) != 0U) {
    if (remainingHdr < 6U) {
      return false;
    }
    cursor += 6U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 6U);
  }
  if ((flags & 0x04U) != 0U) {
    if (remainingHdr < 1U) {
      return false;
    }
    cursor += 1U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 1U);
  }
  if ((flags & 0x08U) != 0U) {
    if (remainingHdr < 2U) {
      return false;
    }
    outView->hasAdi = true;
    outView->did = static_cast<uint16_t>(cursor[0]) |
                   (static_cast<uint16_t>(cursor[1] & 0x0FU) << 8U);
    outView->sid = static_cast<uint8_t>((cursor[1] >> 4U) & 0x0FU);
    cursor += 2U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 2U);
  }
  if ((flags & 0x10U) != 0U) {
    if (remainingHdr < 3U) {
      return false;
    }
    if (!parseBleExtendedAuxPtr(cursor, &outView->auxPtr)) {
      return false;
    }
    outView->hasAuxPtr = true;
    cursor += 3U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 3U);
  }
  if ((flags & 0x20U) != 0U) {
    if (remainingHdr < 18U) {
      return false;
    }
    cursor += 18U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 18U);
  }
  if ((flags & 0x40U) != 0U) {
    if (remainingHdr < 1U) {
      return false;
    }
    cursor += 1U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 1U);
  }
  if (remainingHdr != 0U) {
    return false;
  }

  const uint8_t consumed = static_cast<uint8_t>(1U + extHdrLen);
  outView->advData = &payload[consumed];
  outView->advDataLength = static_cast<uint8_t>(payloadLength - consumed);
  outView->valid = true;
  return true;
}

void initBleExtendedScanResult(BleExtendedScanResult* result,
                               BleAdvertisingChannel channel) {
  if (result == nullptr) {
    return;
  }
  memset(result, 0, sizeof(*result));
  result->primaryChannel = channel;
  result->primaryRssiDbm = -127;
  result->scanRspRssiDbm = -127;
}

void appendBleExtendedScanBytes(uint8_t* destination,
                                uint16_t* usedLength,
                                const uint8_t* source,
                                uint16_t sourceLength) {
  if (destination == nullptr || usedLength == nullptr || source == nullptr ||
      sourceLength == 0U || *usedLength >= kBleExtendedAdvDataMaxLength) {
    return;
  }

  const uint16_t remaining =
      static_cast<uint16_t>(kBleExtendedAdvDataMaxLength - *usedLength);
  const uint16_t copyLength =
      (sourceLength < remaining) ? sourceLength : remaining;
  memcpy(&destination[*usedLength], source, copyLength);
  *usedLength = static_cast<uint16_t>(*usedLength + copyLength);
}

void updateBleExtendedScanIdentity(BleExtendedScanResult* result,
                                   const BleExtendedHeaderView& view,
                                   bool* outHasAdvertiserAddress) {
  if (result == nullptr) {
    return;
  }

  if (view.hasAdvA && outHasAdvertiserAddress != nullptr &&
      !(*outHasAdvertiserAddress)) {
    memcpy(result->advertiserAddress, view.advA, sizeof(result->advertiserAddress));
    *outHasAdvertiserAddress = true;
  }

  if (view.hasAdi) {
    result->did = view.did;
    result->sid = view.sid;
  }
}

constexpr uint8_t kBleLlCtrlUnknownRsp = 0x07U;
constexpr uint8_t kBleLlCtrlFeatureReq = 0x08U;
constexpr uint8_t kBleLlCtrlFeatureRsp = 0x09U;
constexpr uint8_t kBleLlCtrlPauseEncReq = 0x0AU;
constexpr uint8_t kBleLlCtrlPauseEncRsp = 0x0BU;
constexpr uint8_t kBleLlCtrlVersionInd = 0x0CU;
constexpr uint8_t kBleLlCtrlRejectInd = 0x0DU;
constexpr uint8_t kBleLlCtrlSlaveFeatureReq = 0x0EU;
constexpr uint8_t kBleLlCtrlConnectionParamReq = 0x0FU;
constexpr uint8_t kBleLlCtrlConnectionParamRsp = 0x10U;
constexpr uint8_t kBleLlCtrlRejectExtInd = 0x11U;
constexpr uint8_t kBleLlCtrlPingReq = 0x12U;
constexpr uint8_t kBleLlCtrlPingRsp = 0x13U;
constexpr uint8_t kBleLlCtrlLengthReq = 0x14U;
constexpr uint8_t kBleLlCtrlLengthRsp = 0x15U;
constexpr uint8_t kBleLlCtrlPhyReq = 0x16U;
constexpr uint8_t kBleLlCtrlPhyRsp = 0x17U;
constexpr uint8_t kBleLlCtrlPhyUpdateInd = 0x18U;
constexpr uint8_t kBleLlCtrlMinUsedChannelsInd = 0x19U;
constexpr uint8_t kBleLlCtrlCteReq = 0x1AU;
constexpr uint8_t kBleLlCtrlCteRsp = 0x1BU;
constexpr uint8_t kBleLlCtrlPeriodicSyncInd = 0x1CU;
constexpr uint8_t kBleLlCtrlClockAccuracyReq = 0x1DU;
constexpr uint8_t kBleLlCtrlClockAccuracyRsp = 0x1EU;
constexpr uint8_t kBleLlCtrlCisReq = 0x1FU;
constexpr uint8_t kBleLlCtrlCisRsp = 0x20U;
constexpr uint8_t kBleLlCtrlCisInd = 0x21U;
constexpr uint8_t kBleLlCtrlCisTerminateInd = 0x22U;
constexpr uint8_t kBleLlCtrlCsReq = 0x2CU;
constexpr uint8_t kBleLlCtrlCsRsp = 0x2DU;
constexpr uint8_t kBleLlCtrlCsCfg = 0x2EU;
constexpr uint8_t kBleLlCtrlCsProcReq = 0x2FU;
constexpr uint8_t kBleLlCtrlCsProcRsp = 0x30U;
constexpr uint8_t kBleLlCtrlCsSecReq = 0x31U;
constexpr uint8_t kBleLlCtrlCsSecRsp = 0x32U;
constexpr uint8_t kBleLlCtrlCsStart = 0x33U;
constexpr uint8_t kBleLlCtrlCsTerminate = 0x34U;
constexpr uint8_t kBleLlCtrlCsAbort = 0x35U;
constexpr uint8_t kBleLlErrorUnsupportedLlParamValue = 0x20U;
constexpr uint8_t kBleLlErrorUnsupportedRemoteFeature = 0x1AU;
constexpr uint8_t kBleLlErrorPinOrKeyMissing = 0x06U;
constexpr uint8_t kBleLlErrorConnectionTimeout = 0x08U;
constexpr uint8_t kBleLlErrorCommandDisallowed = 0x0CU;
constexpr uint8_t kBleLlErrorLlProcedureCollision = 0x23U;
constexpr uint8_t kBleLlErrorMicFailure = 0x3DU;
constexpr uint8_t kBleLlFeatureMaskOctet0 = 0x3FU;  // bits 0-5: encryption, conn-param-req, ext-reject, slave-features, ping, data-length-ext
constexpr uint8_t kBleLlFeatureMaskOctet1 = 0x09U;  // bits 8 + 11: LE 2M PHY + LE coded PHY
constexpr uint64_t kBleEncPacketCounterMask = 0x7FFFFFFFFFULL;

constexpr uint16_t kBleL2capCidAtt = 0x0004U;
constexpr uint16_t kBleL2capCidLeSignaling = 0x0005U;
constexpr uint16_t kBleL2capCidSmp = 0x0006U;
constexpr uint8_t kBleL2capHeaderLen = 4U;
constexpr uint8_t kBleDefaultDataPduMaxPayload = 27U;
constexpr uint8_t kBleDataPduMaxPayload = 251U;
constexpr uint8_t kBleMicLen = 4U;
constexpr uint16_t kBleDefaultAttMtu = 23U;
constexpr uint16_t kBleServerPreferredMtu =
    static_cast<uint16_t>(kBleDataPduMaxPayload - kBleL2capHeaderLen);
constexpr uint16_t kBleDataLengthMaxTimeUs =
    static_cast<uint16_t>((static_cast<uint16_t>(kBleDataPduMaxPayload) + 14U) * 8U);
constexpr uint16_t kBleCodedDataLengthMaxTimeUs = 2704U;
constexpr uint8_t kL2capSigCodeCommandRejectRsp = 0x01U;
constexpr uint8_t kL2capSigCodeConnParamUpdateReq = 0x12U;
constexpr uint8_t kL2capSigCodeConnParamUpdateRsp = 0x13U;
constexpr uint8_t kL2capSigCodeLeCreditConnReq = 0x14U;
constexpr uint8_t kL2capSigCodeLeCreditConnRsp = 0x15U;
constexpr uint8_t kL2capSigCodeLeFlowControlCredit = 0x16U;
constexpr uint16_t kL2capCmdRejectReasonCmdNotUnderstood = 0x0000U;
constexpr uint16_t kL2capCmdRejectReasonSignalingMtuExceeded = 0x0001U;
constexpr uint16_t kL2capCmdRejectReasonInvalidCid = 0x0002U;
constexpr uint16_t kL2capConnParamResultRejected = 0x0001U;
constexpr uint16_t kL2capConnParamResultAccepted = 0x0000U;
constexpr uint16_t kL2capLeCreditConnResultPsmNotSupported = 0x0002U;
constexpr uint16_t kBleL2capLeSignalingMtu = 23U;

constexpr uint32_t kBleConnSlaveScaPpm = 50UL;
constexpr uint8_t kBleConnMicrosPollDivider = 32U;
constexpr uint32_t kBleConnEventSpinFloor = 450000UL;
// With fast ramp-up enabled, trigger TXEN ~100 us after RX end so packet start
// lands near the BLE 150 us inter-frame spacing on strict centrals too.
#if !defined(NRF54L15_CLEAN_BLE_CONN_TXEN_AFTER_RX_US)
#define NRF54L15_CLEAN_BLE_CONN_TXEN_AFTER_RX_US 100U
#endif
constexpr uint32_t kBleConnTxenAfterRxUs =
    static_cast<uint32_t>(NRF54L15_CLEAN_BLE_CONN_TXEN_AFTER_RX_US);
constexpr uint32_t kBleConnEncRspTxenAfterRxUs = 110U;
#if !defined(NRF54L15_CLEAN_BLE_ENCRSP_RXEND_COMP_US)
// Snapdragon-based Android centrals reject the first LL_ENC_RSP if it is sent
// later than the normal connection-control response point. Keep the historical
// 110 us ENC_RSP target available for override, but default it back to 100 us.
#define NRF54L15_CLEAN_BLE_ENCRSP_RXEND_COMP_US 10
#endif
constexpr uint32_t kBleConnEncRspRxEndTimestampCompUs =
    static_cast<uint32_t>(NRF54L15_CLEAN_BLE_ENCRSP_RXEND_COMP_US);
// SCAN_RSP T_IFS: advertising path has only a few microseconds of software
// work between rxEndUs capture and the TXEN wait, so target 100 us here to
// land the actual over-the-air preamble near the BLE 150 us inter-frame space
// on stricter Qualcomm-based phones.
#ifndef BLE_SCAN_RSP_TXEN_AFTER_RX_US
#define BLE_SCAN_RSP_TXEN_AFTER_RX_US 100U
#endif
constexpr uint32_t kBleScanRspTxenAfterRxUs = BLE_SCAN_RSP_TXEN_AFTER_RX_US;
// Advertising TX->RX turnaround can be power-optimized by arming RXEN close to
// the BLE T_IFS point instead of letting DISABLED->RXEN start the receiver as
// soon as TX has disabled.
#ifndef BLE_ADV_REQUEST_RXEN_AFTER_TX_US
#define BLE_ADV_REQUEST_RXEN_AFTER_TX_US 100U
#endif
constexpr uint32_t kBleAdvRequestRxenAfterTxUs =
    BLE_ADV_REQUEST_RXEN_AFTER_TX_US;
// Legacy advertising request/response exchanges should complete around T_IFS.
// With RXEN delayed by kBleAdvRequestRxenAfterTxUs, a valid 1M SCAN_REQ /
// CONNECT_IND should raise ADDRESS well before this default. Keep it tunable so
// field reports can widen the window without reverting the lower-power path.
#ifndef BLE_ADV_REQUEST_START_WAIT_MAX_US
#define BLE_ADV_REQUEST_START_WAIT_MAX_US 130U
#endif
constexpr uint32_t kBleAdvRequestStartWaitMaxUs =
    BLE_ADV_REQUEST_START_WAIT_MAX_US;
constexpr uint32_t kBleAdvRequestListenMaxUs = 2500U;
constexpr uint32_t kBleScanRspListenMaxUs = 2500U;

#if !defined(NRF54LM20A_XXAA) && !defined(NRF54LM20B_XXAA)
static void ensureBleGrtcLfclkRunning(void) {
  // The GRTC clock source defaults to SystemLFCLK (CLKCFG reset = 0x00010001).
  // On a cold power-on boot the LFCLK is not running, which causes the GRTC
  // syscounter to stall and breaks all BLE timing.  Start LFRC first (fast
  // warm-up) and then switch to LFXO for accuracy.
  auto* clock =
      reinterpret_cast<NRF_CLOCK_Type*>(static_cast<uintptr_t>(nrf54l15::CLOCK_BASE));
  if (clock == nullptr) {
    return;
  }

  const uint32_t stat = clock->LFCLK.STAT;
  const uint32_t state =
      (stat & CLOCK_LFCLK_STAT_STATE_Msk) >> CLOCK_LFCLK_STAT_STATE_Pos;
  if (state == CLOCK_LFCLK_STAT_STATE_Running) {
    // LFCLK already running; nothing to do.  On a warm reset the CLOCK
    // peripheral may retain its pre-reset LFCLK configuration.
    return;
  }

  // Start LFRC first so the GRTC has a clock source immediately.
  clock->EVENTS_LFCLKSTARTED = 0U;
  clock->LFCLK.SRC =
      (CLOCK_LFCLK_SRC_SRC_LFRC << CLOCK_LFCLK_SRC_SRC_Pos) &
      CLOCK_LFCLK_SRC_SRC_Msk;
  __asm volatile("dsb 0xF" ::: "memory");
  clock->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;

  // LFRC starts in microseconds.  A short spin is sufficient.
  uint32_t spin = 200000UL;
  while (spin-- > 0U) {
    if ((clock->EVENTS_LFCLKSTARTED != 0U) &&
        (((clock->LFCLK.STAT & CLOCK_LFCLK_STAT_STATE_Msk) >>
          CLOCK_LFCLK_STAT_STATE_Pos) == CLOCK_LFCLK_STAT_STATE_Running)) {
      break;
    }
  }

  // Now start the accurate LFXO crystal.  Crystal startup can take
  // 250-500 ms; use a generous spin budget (about 800 ms at 64 MHz
  // with roughly 5 cycles per iteration).
  clock->EVENTS_LFCLKSTARTED = 0U;
  clock->LFCLK.SRC =
      (CLOCK_LFCLK_SRC_SRC_LFXO << CLOCK_LFCLK_SRC_SRC_Pos) &
      CLOCK_LFCLK_SRC_SRC_Msk;
  __asm volatile("dsb 0xF" ::: "memory");
  clock->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;

  spin = 10000000UL;
  while (spin-- > 0U) {
    const uint32_t s = clock->LFCLK.STAT;
    if ((clock->EVENTS_LFCLKSTARTED != 0U) &&
        (((s & CLOCK_LFCLK_STAT_STATE_Msk) >>
          CLOCK_LFCLK_STAT_STATE_Pos) == CLOCK_LFCLK_STAT_STATE_Running) &&
        (((s & CLOCK_LFCLK_STAT_SRC_Msk) >>
          CLOCK_LFCLK_STAT_SRC_Pos) == CLOCK_LFCLK_STAT_SRC_LFXO)) {
      break;
    }
  }
}

#endif

void initBleGrtc() {
  static bool initialized = false;
  const uint32_t irqState = halEnterCritical();
  if (initialized) {
    halExitCritical(irqState);
    return;
  }

  NRF_GRTC_Type* const grtc = NRF_GRTC;
  if (grtc == nullptr) {
    halExitCritical(irqState);
    return;
  }

#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  // Direct LFXO is prepared by the core for either BLE-first or delay-first
  // startup. Retained GRTC clocks do not require a new system-LFCLK request.
  nrf54lm20b_core_prepare_grtc_clock();
#else
  // Ensure the LFCLK is running before the GRTC starts.  The GRTC's
  // default clock source is SystemLFCLK (CLKCFG reset = 0x00010001),
  // which follows whatever LFCLK source the CLOCK peripheral provides.
  // On a cold power-on boot the LFCLK is not started, so we must start
  // it here (LFRC first for fast warm-up, then LFXO for accuracy).
  ensureBleGrtcLfclkRunning();
#endif

  // Keep the shared delay()/BLE settings within the datasheet's strict
  // TIMEOUT > WAKETIME + one-LFCLK-cycle guard requirement.
  grtc->TIMEOUT =
      ((static_cast<uint32_t>(kNrf54GrtcSystemOnTimeoutLfclk)
        << GRTC_TIMEOUT_VALUE_Pos) & GRTC_TIMEOUT_VALUE_Msk);
  grtc->WAKETIME =
      ((static_cast<uint32_t>(kNrf54GrtcSystemOnWakeLfclk)
        << GRTC_WAKETIME_VALUE_Pos) & GRTC_WAKETIME_VALUE_Msk);

  // Clear AUTOEN before OR-ing in the desired bits so we don't carry
  // a stale POR default into the write.
  uint32_t mode = grtc->MODE;
  mode &= ~(GRTC_MODE_SYSCOUNTEREN_Msk | GRTC_MODE_AUTOEN_Msk);
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  mode |= (GRTC_MODE_AUTOEN_CpuActive << GRTC_MODE_AUTOEN_Pos);
  grtc->MODE = mode;
  __asm volatile("dsb 0xF" ::: "memory");

  grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
      (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active <<
       GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
  __asm volatile("dsb 0xF" ::: "memory");
  initialized = true;
  halExitCritical(irqState);
}

static inline uint32_t bleTimingUs() {
  // Connection-event hot paths call this inside tight pre-anchor / T_IFS
  // loops. Keep that local timestamp path on the direct low counter read and
  // reserve readGrtcCounter() for places that actually need a coherent 64-bit
  // absolute time.
  return NRF54L15_GRTC_SYSCOUNTER(NRF_GRTC).SYSCOUNTERL;
}

static inline bool bleBackgroundCompareEventPending() {
  return NRF_GRTC->EVENTS_COMPARE[kBleBackgroundCompareChannel] != 0U;
}

static inline bool bleBackgroundCompareEnabled() {
  return ((NRF_GRTC->CC[kBleBackgroundCompareChannel].CCEN &
           GRTC_CC_CCEN_ACTIVE_Msk) >>
          GRTC_CC_CCEN_ACTIVE_Pos) == GRTC_CC_CCEN_ACTIVE_Enable;
}

static inline bool bleCompareEventPending(uint8_t channel) {
  return NRF_GRTC->EVENTS_COMPARE[channel] != 0U;
}

static inline bool bleCompareEnabled(uint8_t channel) {
  return ((NRF_GRTC->CC[channel].CCEN & GRTC_CC_CCEN_ACTIVE_Msk) >>
          GRTC_CC_CCEN_ACTIVE_Pos) == GRTC_CC_CCEN_ACTIVE_Enable;
}

static inline uint32_t halEnterCritical() {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  __DSB();
  __ISB();
  return primask;
}

static inline void halExitCritical(uint32_t primask) {
  __DSB();
  __ISB();
  if ((primask & 0x1U) == 0U) {
    __enable_irq();
  }
}

static inline void bleDisableCompare(uint8_t channel, bool clearInterruptEnable) {
  if (clearInterruptEnable) {
    NRF54L15_GRTC_INTENCLR_REG(NRF_GRTC) = (1UL << static_cast<uint32_t>(channel));
  }
  NRF_GRTC->CC[channel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
  NRF_GRTC->EVENTS_COMPARE[channel] = 0U;
}

static inline void bleBackgroundDisableCompare() {
  bleDisableCompare(kBleBackgroundCompareChannel, true);
}

static inline void bleBackgroundEnableIrq() {
  NVIC->ICPR[static_cast<uint32_t>(kBleBackgroundGrtcIrq) >> 5U] =
      (1UL << (static_cast<uint32_t>(kBleBackgroundGrtcIrq) & 0x1FUL));
  NVIC_SetPriority(kBleBackgroundGrtcIrq, 3U);
  NVIC_EnableIRQ(kBleBackgroundGrtcIrq);
  NVIC_SetPriority(PendSV_IRQn, 7U);
}

static inline uint32_t bleBackgroundClockTuneInterruptMask() {
  return CLOCK_INTENSET_XOTUNED_Msk | CLOCK_INTENSET_XOTUNEERROR_Msk |
         CLOCK_INTENSET_XOTUNEFAILED_Msk;
}

static inline void bleBackgroundEnableClockIrq() {
  NVIC->ICPR[static_cast<uint32_t>(kBleBackgroundClockIrq) >> 5U] =
      (1UL << (static_cast<uint32_t>(kBleBackgroundClockIrq) & 0x1FUL));
  NVIC_SetPriority(kBleBackgroundClockIrq, 3U);
  NVIC_EnableIRQ(kBleBackgroundClockIrq);
}

static inline void bleBackgroundDisableClockIrq() {
  NRF_CLOCK->INTENCLR = bleBackgroundClockTuneInterruptMask();
  NVIC_DisableIRQ(kBleBackgroundClockIrq);
  NVIC->ICPR[static_cast<uint32_t>(kBleBackgroundClockIrq) >> 5U] =
      (1UL << (static_cast<uint32_t>(kBleBackgroundClockIrq) & 0x1FUL));
}

static inline void bleBackgroundEnableClockTuneInterrupts() {
  const uint32_t mask = bleBackgroundClockTuneInterruptMask();
  NRF_CLOCK->INTENCLR = mask;
  NRF_CLOCK->INTENSET = mask;
  bleBackgroundEnableClockIrq();
}

static inline void bleBackgroundEnableClockQualityInterrupts() {
  NRF_CLOCK->INTENCLR = bleBackgroundClockTuneInterruptMask();
  NRF_CLOCK->INTENSET = CLOCK_INTENSET_XOTUNEERROR_Msk;
  bleBackgroundEnableClockIrq();
}

static inline uint64_t bleAbsoluteTimeFromLowCounter(uint32_t targetUs) {
  const uint64_t nowAbs = readGrtcCounter(NRF_GRTC);
  const uint32_t nowUs = static_cast<uint32_t>(nowAbs & 0xFFFFFFFFULL);
  const bool targetAlreadyReached =
      static_cast<int32_t>(nowUs - targetUs) >= 0;
  const uint32_t deltaUs =
      targetAlreadyReached ? 0U : static_cast<uint32_t>(targetUs - nowUs);
  return nowAbs + static_cast<uint64_t>(deltaUs);
}

static inline void bleProgramCompare(uint8_t channel, uint32_t targetUs,
                                     bool enableInterrupt) {
  const uint64_t targetAbs = bleAbsoluteTimeFromLowCounter(targetUs);
  const uint32_t lo = static_cast<uint32_t>(targetAbs & 0xFFFFFFFFULL);
  const uint32_t hi = static_cast<uint32_t>((targetAbs >> 32U) & 0xFFFFFUL);

  NRF_GRTC->EVENTS_COMPARE[channel] = 0U;
  NRF_GRTC->CC[channel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
  if (enableInterrupt) {
    NRF54L15_GRTC_INTENSET_REG(NRF_GRTC) = (1UL << static_cast<uint32_t>(channel));
  } else {
    NRF54L15_GRTC_INTENCLR_REG(NRF_GRTC) = (1UL << static_cast<uint32_t>(channel));
  }
  NRF_GRTC->CC[channel].CCL = lo;
  NRF_GRTC->CC[channel].CCH =
      (hi << GRTC_CC_CCH_CCH_Pos) & GRTC_CC_CCH_CCH_Msk;
  NRF_GRTC->CC[channel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
}

static inline bool bleRunningInIsr() {
  return (__get_IPSR() != 0U);
}

static inline bool bleAnyBackgroundCompareActive() {
  const uint8_t kChannels[] = {
      kBleBackgroundAdvPrewarmCompareChannel,
      kBleBackgroundAdvTxCompareChannel,
      kBleBackgroundConnPrewarmCompareChannel,
      kBleBackgroundAdvCleanupCompareChannel,
      kBleBackgroundAdvStage1ServiceCompareChannel,
      kBleBackgroundAdvStage1TxCompareChannel,
      kBleBackgroundAdvStage2ServiceCompareChannel,
      kBleBackgroundAdvStage2TxCompareChannel,
      kBleBackgroundAdvFinalCleanupCompareChannel,
      kBleBackgroundConnRxCompareChannel,
  };

  for (uint8_t i = 0U; i < sizeof(kChannels); ++i) {
    const uint8_t channel = kChannels[i];
    if (bleCompareEnabled(channel) || bleCompareEventPending(channel)) {
      return true;
    }
  }
  return false;
}

static inline bool bleBackgroundOwnerBlocksIdleWake() {
  // If foreground (advertising) has requested idle wake permission, bypass
  // the background check to allow wake arming and consumption.
  if (g_bleIdleWakePermitForeground != 0U) {
    return false;
  }
  return g_bleBackgroundRadio != nullptr &&
         (g_bleBackgroundPendSvPending != 0U || bleAnyBackgroundCompareActive());
}

extern "C" void nrf54l15_ble_idle_wake_arm(uint32_t targetUs) {
  if (NRF_GRTC == nullptr) {
    return;
  }

  const uint32_t irqState = halEnterCritical();
  if (bleBackgroundOwnerBlocksIdleWake()) {
    halExitCritical(irqState);
    return;
  }

  g_bleIdleWakePending = 0U;
  g_bleIdleWakeArmed = 1U;
  bleBackgroundEnableIrq();
  bleProgramCompare(kBleBackgroundCompareChannel, targetUs, true);
  halExitCritical(irqState);
}

extern "C" void nrf54l15_ble_idle_wake_arm_foreground(uint32_t targetUs) {
  // Bypass bleBackgroundOwnerBlocksIdleWake() for foreground activities
  // (e.g., advertising after disconnect) where the background radio's
  // stale compares would block the wake. This is safe because the
  // background radio is disconnected and the wake target is for a
  // foreground activity that will be serviced by the main loop.
  if (NRF_GRTC == nullptr) {
    return;
  }

  const uint32_t irqState = halEnterCritical();
  g_bleIdleWakePending = 0U;
  g_bleIdleWakeArmed = 1U;
  bleBackgroundEnableIrq();
  bleProgramCompare(kBleBackgroundCompareChannel, targetUs, true);
  halExitCritical(irqState);
}

extern "C" void nrf54l15_ble_idle_wake_cancel(void) {
  if (NRF_GRTC == nullptr) {
    return;
  }

  const uint32_t irqState = halEnterCritical();
  if (!bleBackgroundOwnerBlocksIdleWake()) {
    g_bleIdleWakePending = 0U;
    g_bleIdleWakeArmed = 0U;
    bleBackgroundDisableCompare();
  }
  halExitCritical(irqState);
}

// Release background radio ownership so the GRTC idle wake can be consumed.
// Called after disconnect to clear stale background state that would block
// bleBackgroundOwnerBlocksIdleWake() and prevent WFI wake consumption.
extern "C" void nrf54l15_ble_background_radio_release(void) {
  g_bleBackgroundRadio = nullptr;
  g_bleBackgroundPendSvPending = 0U;
  bleBackgroundDisableCompare();
}

// Query whether the idle wake was successfully armed. Returns 1 if armed.
extern "C" uint8_t nrf54l15_ble_idle_wake_is_armed(void) {
  return g_bleIdleWakeArmed;
}

// Set the foreground wake permission flag. When set, bleBackgroundOwnerBlocksIdleWake()
// returns false, allowing foreground (advertising) to arm and consume the idle wake.
extern "C" void nrf54l15_ble_idle_wake_set_permit_foreground(uint8_t permit) {
  g_bleIdleWakePermitForeground = permit;
}

extern "C" uint8_t nrf54l15_ble_idle_wake_consume(void) {
  const uint32_t irqState = halEnterCritical();
  const uint8_t pending = g_bleIdleWakePending;
  g_bleIdleWakePending = 0U;
  halExitCritical(irqState);
  return pending;
}

static inline uint32_t bleEnterCritical() { return halEnterCritical(); }

static inline void bleExitCritical(uint32_t primask) { halExitCritical(primask); }

static inline void bleTriggerTxAtTargetUsCritical(NRF_RADIO_Type* radio,
                                                  uint32_t targetUs) {
  if (radio == nullptr) {
    return;
  }

  const uint32_t irqState = bleEnterCritical();
  uint32_t nowUs = bleTimingUs();
  while (static_cast<int32_t>(nowUs - targetUs) < 0) {
    nowUs = bleTimingUs();
  }
  radio->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  __DSB();
  bleExitCritical(irqState);
}

static inline void bleTriggerRxAtTargetUsCritical(NRF_RADIO_Type* radio,
                                                  uint32_t targetUs) {
  if (radio == nullptr) {
    return;
  }

  const uint32_t irqState = bleEnterCritical();
  uint32_t nowUs = bleTimingUs();
  while (static_cast<int32_t>(nowUs - targetUs) < 0) {
    nowUs = bleTimingUs();
  }
  radio->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  __DSB();
  bleExitCritical(irqState);
}

// Keep the RADIO shortcut combinations explicit in one place. These match the
// datasheet-backed TIFS flow the current LL already relies on:
// PHYEND->DISABLE + DISABLED->RXEN/TXEN + READY->START.
static inline uint32_t bleRadioShortsRxListen() {
  return ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
          RADIO_SHORTS_RXREADY_START_Msk) |
         ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
           << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
          RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
         ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
          RADIO_SHORTS_PHYEND_DISABLE_Msk);
}

static inline uint32_t bleRadioShortsTxOnly() {
  return ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
          RADIO_SHORTS_TXREADY_START_Msk) |
         ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
          RADIO_SHORTS_PHYEND_DISABLE_Msk);
}

static inline uint32_t bleRadioShortsBackgroundAdvDirectTx() {
  // The nRF54L15 RADIO exposes both READY->START and TXREADY->START. The
  // direct DPPI/PPIB background-advertising kickoff is more reliable on the
  // connected XIAO when the generic READY path is enabled alongside the TX
  // specific shortcut.
  return bleRadioShortsTxOnly() |
         ((RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) &
          RADIO_SHORTS_READY_START_Msk);
}

static inline uint32_t bleRadioShortsTxThenRxAtTifs() {
  return bleRadioShortsTxOnly() |
         ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
          RADIO_SHORTS_DISABLED_RXEN_Msk) |
         ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
          RADIO_SHORTS_RXREADY_START_Msk) |
         ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
           << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
          RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
}

static inline uint32_t bleRadioShortsRxThenTxAtTifs() {
  return bleRadioShortsBackgroundAdvDirectTx() |
         ((RADIO_SHORTS_DISABLED_TXEN_Enabled << RADIO_SHORTS_DISABLED_TXEN_Pos) &
          RADIO_SHORTS_DISABLED_TXEN_Msk);
}

static inline void noteConnectionMissedEvent(BleEncryptionDebugCounters* counters,
                                             uint32_t missedCount) {
  if (counters == nullptr) {
    return;
  }
  counters->connMissedEventCountLast = missedCount;
  if (missedCount > counters->connMissedEventCountMax) {
    counters->connMissedEventCountMax = missedCount;
  }
}

static inline void noteTxenLag(BleEncryptionDebugCounters* counters,
                               uint32_t lagUs) {
  if (counters == nullptr) {
    return;
  }
  counters->txenLagLastUs = lagUs;
  if (lagUs > counters->txenLagMaxUs) {
    counters->txenLagMaxUs = lagUs;
  }
  if (lagUs > 0U) {
    ++counters->txenLateCount;
  }
}

#if defined(NRF54L15_CLEAN_BLE_TIMING_AGGRESSIVE) && (NRF54L15_CLEAN_BLE_TIMING_AGGRESSIVE == 1)
constexpr uint32_t kBleConnAnchorPrewaitUs = 120U;
constexpr uint32_t kBleConnRxStartLeadUs = 2200U;
constexpr uint32_t kBleConnRxStartLeadSyncedUs = 360U;
constexpr uint32_t kBleConnRxListenBaseUs = 420U;
constexpr uint32_t kBleConnRxListenMaxUs = 2200U;
constexpr uint32_t kBleConnDisableWaitUs = 450U;
constexpr uint32_t kBleConnTxDisableWaitUs = 450U;
constexpr uint32_t kBleConnFirstEventFallbackUs = 1000U;
constexpr uint16_t kBlePreferredConnIntervalMinUnits = 80U;
constexpr uint16_t kBlePreferredConnIntervalMaxUnits = 160U;
constexpr uint16_t kBlePreferredConnLatency = 4U;
constexpr uint16_t kBlePreferredConnTimeoutUnits = 600U;
#elif defined(NRF54L15_CLEAN_BLE_TIMING_BALANCED) && (NRF54L15_CLEAN_BLE_TIMING_BALANCED == 1)
constexpr uint32_t kBleConnAnchorPrewaitUs = 220U;
constexpr uint32_t kBleConnRxStartLeadUs = 2200U;
constexpr uint32_t kBleConnRxStartLeadSyncedUs = 560U;
constexpr uint32_t kBleConnRxListenBaseUs = 560U;
constexpr uint32_t kBleConnRxListenMaxUs = 3000U;
constexpr uint32_t kBleConnDisableWaitUs = 550U;
constexpr uint32_t kBleConnTxDisableWaitUs = 550U;
constexpr uint32_t kBleConnFirstEventFallbackUs = 1200U;
constexpr uint16_t kBlePreferredConnIntervalMinUnits = 40U;
constexpr uint16_t kBlePreferredConnIntervalMaxUnits = 80U;
constexpr uint16_t kBlePreferredConnLatency = 0U;
constexpr uint16_t kBlePreferredConnTimeoutUnits = 500U;
#else
constexpr uint32_t kBleConnAnchorPrewaitUs = 120U;
constexpr uint32_t kBleConnRxStartLeadUs = 2200U;
constexpr uint32_t kBleConnRxStartLeadSyncedUs = 360U;
constexpr uint32_t kBleConnRxListenBaseUs = 420U;
constexpr uint32_t kBleConnRxListenMaxUs = 4500U;
constexpr uint32_t kBleConnDisableWaitUs = 450U;
constexpr uint32_t kBleConnTxDisableWaitUs = 450U;
constexpr uint32_t kBleConnFirstEventFallbackUs = 1500U;
constexpr uint16_t kBlePreferredConnIntervalMinUnits = 24U;
constexpr uint16_t kBlePreferredConnIntervalMaxUnits = 40U;
constexpr uint16_t kBlePreferredConnLatency = 0U;
constexpr uint16_t kBlePreferredConnTimeoutUnits = 500U;
#endif

// When link layer control PDUs are exchanged, the central may send multiple packets in the
// same connection event (e.g., ENC_REQ -> ENC_RSP -> START_ENC_REQ -> START_ENC_RSP). We
// keep a short post-TX receive window to catch the follow-up without holding RX for the
// full event budget.
// Post-ENC_RSP follow-up listen is only to catch a same-event START_ENC_REQ.
// Keep this bounded, but long enough to tolerate controller/event jitter.
constexpr uint32_t kBleConnFollowupRxListenMaxUs = 4000U;
constexpr uint32_t kBleConnRxPacketMarginUs = 400U;
constexpr uint32_t kBleConnFollowupRxStartDetectUs = 700U;
constexpr uint32_t kBleConnFastCccdArmBudgetUs = 40U;
constexpr uint32_t kBleConnResyncExtraListenUs = 2400U;
constexpr uint32_t kBleConnResyncPostAnchorMaxUs = 10000U;
constexpr uint8_t kBleConnChannelMapPendingResyncEventsMax = 10U;
constexpr uint8_t kBleConnChannelMapApplyResyncEvents = 5U;
constexpr uint8_t kBleConnGenericMissResyncEvents = 4U;

constexpr uint8_t kSmpCodePairingRequest = 0x01U;
constexpr uint8_t kSmpCodePairingResponse = 0x02U;
constexpr uint8_t kSmpCodePairingConfirm = 0x03U;
constexpr uint8_t kSmpCodePairingRandom = 0x04U;
constexpr uint8_t kSmpCodePairingFailed = 0x05U;
constexpr uint8_t kSmpCodeEncryptionInformation = 0x06U;
constexpr uint8_t kSmpCodeMasterIdentification = 0x07U;
constexpr uint8_t kSmpCodeIdentityInformation = 0x08U;
constexpr uint8_t kSmpCodeIdentityAddressInformation = 0x09U;
constexpr uint8_t kSmpCodeSigningInformation = 0x0AU;
constexpr uint8_t kSmpCodeSecurityRequest = 0x0BU;
constexpr uint8_t kSmpCodePairingPublicKey = 0x0CU;
constexpr uint8_t kSmpCodeDhKeyCheck = 0x0DU;
constexpr uint8_t kSmpCodeKeypressNotification = 0x0EU;
constexpr uint8_t kSmpPairingRequestLen = 7U;
constexpr uint8_t kSmpPairingResponseLen = 7U;
constexpr uint8_t kSmpPairingConfirmLen = 17U;
constexpr uint8_t kSmpPairingRandomLen = 17U;
constexpr uint8_t kSmpPairingPublicKeyLen = 65U;
constexpr uint8_t kSmpDhKeyCheckLen = 17U;
constexpr uint8_t kSmpEncryptionInformationLen = 17U;
constexpr uint8_t kSmpMasterIdentificationLen = 11U;
constexpr uint8_t kSmpIdentityInformationLen = 17U;
constexpr uint8_t kSmpIdentityAddressInformationLen = 8U;
constexpr uint8_t kSmpSigningInformationLen = 17U;
constexpr uint8_t kSmpIoCapDisplayOnly = 0x00U;
constexpr uint8_t kSmpIoCapDisplayYesNo = 0x01U;
constexpr uint8_t kSmpIoCapKeyboardOnly = 0x02U;
constexpr uint8_t kSmpIoCapNoInputNoOutput = 0x03U;
constexpr uint8_t kSmpIoCapKeyboardDisplay = 0x04U;
constexpr uint8_t kSmpAuthReqBondingMask = 0x03U;
constexpr uint8_t kSmpAuthReqMitmMask = 0x04U;
constexpr uint8_t kSmpAuthReqSecureConnectionsMask = 0x08U;
constexpr uint8_t kSmpKeyDistEncKeyMask = 0x01U;
constexpr uint8_t kSmpKeyDistIdKeyMask = 0x02U;
constexpr uint8_t kSmpKeyDistSignKeyMask = 0x04U;
constexpr uint8_t kSmpKeyDistLinkKeyMask = 0x08U;
constexpr uint8_t kSmpKeyDistSupportedMask =
    kSmpKeyDistEncKeyMask | kSmpKeyDistIdKeyMask | kSmpKeyDistSignKeyMask;
constexpr uint8_t kSmpKeyDistDefinedMask =
    kSmpKeyDistSupportedMask | kSmpKeyDistLinkKeyMask;
constexpr uint8_t kSmpReasonPasskeyEntryFailed = 0x01U;
constexpr uint8_t kSmpReasonAuthRequirements = 0x03U;
constexpr uint8_t kSmpReasonConfirmValueFailed = 0x04U;
constexpr uint8_t kSmpReasonPairingNotSupported = 0x05U;
constexpr uint8_t kSmpReasonEncryptionKeySize = 0x06U;
constexpr uint8_t kSmpReasonCommandNotSupported = 0x07U;
constexpr uint8_t kSmpReasonUnspecified = 0x08U;
constexpr uint8_t kSmpReasonRepeatedAttempts = 0x09U;
constexpr uint8_t kSmpReasonDhKeyCheckFailed = 0x0BU;
constexpr uint8_t kSmpReasonNumericComparisonFailed = 0x0CU;
constexpr uint8_t kSmpReasonInvalidParameters = 0x0AU;
constexpr uint8_t kSmpReasonOobNotAvailable = 0x02U;
constexpr uint32_t kSmpTimeoutMs = 30000UL;
constexpr uint32_t kSmpBackoffMinimumMs = 1000UL;
constexpr uint32_t kSmpBackoffMaximumMs = 64000UL;
constexpr uint32_t kSmpBackoffDecayMs = 60000UL;
constexpr uint8_t kSmpBackoffMaximumLevel = 6U;
constexpr uint8_t kSmpScIoActionNone = 0x00U;
constexpr uint8_t kSmpScIoActionOob = 0x01U;
constexpr uint8_t kSmpScIoActionInput = 0x02U;
constexpr uint8_t kSmpScIoActionDisplay = 0x03U;
constexpr uint8_t kSmpScIoActionNumericComparison = 0x04U;
constexpr uint8_t kSmpScPairAlgJustWorks = 0x00U;
constexpr uint8_t kSmpScPairAlgOob = 0x01U;
constexpr uint8_t kSmpScPairAlgPasskey = 0x02U;
constexpr uint8_t kSmpScPairAlgNumericComparison = 0x03U;
constexpr uint8_t kBleBondRecordFlagAuthenticated = 0x01U;
constexpr uint8_t kBleBondRecordFlagSecureConnections = 0x02U;
constexpr uint8_t kSmpPairingStateIdle = 0U;
constexpr uint8_t kSmpPairingStateReqSent = 4U;
constexpr uint8_t kSmpPairingStateRspSent = 1U;
constexpr uint8_t kSmpPairingStateConfirmSent = 2U;
constexpr uint8_t kSmpPairingStateRandomSent = 3U;

constexpr uint32_t kBleBondRetentionMagic = 0x444E4F42U;  // "BOND"
constexpr uint16_t kBleBondRetentionVersion = 3U;
constexpr uint32_t kBleCccdRetentionMagic = 0x44434343U;  // "CCCD"
constexpr uint16_t kBleCccdRetentionVersion = 2U;
constexpr uint32_t kBleBondRramcSpinLimit = 600000UL;
constexpr uint32_t kBleBondAarSpinLimit = 250000UL;
