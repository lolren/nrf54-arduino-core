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
static constexpr uint8_t kBleBackgroundConnRxCompareChannel = 12U;
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
static constexpr uint8_t kBleBackgroundAdvPeriLatencyDppiChannel = 3U;
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
static constexpr uint8_t kBleBackgroundAdvLatencyBridgeIndex = 3U;
static constexpr uint8_t kBleBackgroundConnRxBridgeIndex = 4U;
static constexpr uint8_t kBleBackgroundAdvStopPathGroup = 0U;
static constexpr uint32_t kBleBackgroundAdvEventWindowUs = 1000U;
static constexpr uint32_t kBleBackgroundAdvDefaultHfxoLeadUs = 1200U;
static constexpr uint32_t kBleBackgroundConnDefaultHfxoLeadUs = 3000U;
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
#if defined(ARDUINO_XIAO_NRF54L15)
#define NRF54L15_CLEAN_BLE_BACKGROUND_MANAGE_RF_PATH 1
#else
#define NRF54L15_CLEAN_BLE_BACKGROUND_MANAGE_RF_PATH 0
#endif
#endif

#if !defined(NRF54L15_CLEAN_BLE_BACKGROUND_FORCE_IRQ_TXEN)
#if defined(ARDUINO_XIAO_NRF54L15)
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

extern "C" uint8_t __attribute__((weak)) nrf54l15_constlat_users_active(void) {
  return 0U;
}

static inline bool bleBackgroundAdvertisingConstlatActive() {
  return (NRF_POWER->CONSTLATSTAT & POWER_CONSTLATSTAT_STATUS_Msk) != 0U;
}

static inline bool bleBackgroundAdvertisingShouldManageLatency() {
#if NRF54L15_CLEAN_BLE_BACKGROUND_MANAGED_CONSTLAT != 0
  return !bleBackgroundAdvertisingConstlatActive();
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
  if (!managed || !bleBackgroundAdvertisingConstlatActive()) {
    return false;
  }
  if (nrf54l15_constlat_users_active() != 0U) {
    return false;
  }
  NRF_POWER->TASKS_LOWPWR = POWER_TASKS_LOWPWR_TASKS_LOWPWR_Trigger;
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
  if (bleBackgroundAdvertisingConstlatActive()) {
    if (alreadyActive != nullptr) {
      *alreadyActive = true;
    }
    return true;
  }
  NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
  return bleBackgroundAdvertisingConstlatActive();
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
  if (!enable) {
    if (rng.active()) {
      rng.end();
    }
    return;
  }
  if (!rng.active()) {
    (void)rng.beginNonBlocking();
  }
}

static inline uint32_t bleBackgroundAdvertisingRandomDelayUs(uint32_t* state,
                                                             uint8_t addressSeed,
                                                             bool* usedHardware) {
  CracenRng& rng = bleBackgroundAdvertisingRng();
  uint32_t randomWord = 0U;
  if (rng.tryRandomWord(&randomWord)) {
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
constexpr uint8_t kBleLlErrorUnsupportedLlParamValue = 0x20U;
constexpr uint8_t kBleLlErrorUnsupportedRemoteFeature = 0x1AU;
constexpr uint8_t kBleLlErrorPinOrKeyMissing = 0x06U;
constexpr uint8_t kBleLlErrorConnectionTimeout = 0x08U;
constexpr uint8_t kBleLlErrorCommandDisallowed = 0x0CU;
constexpr uint8_t kBleLlErrorLlProcedureCollision = 0x23U;
constexpr uint8_t kBleLlErrorMicFailure = 0x3DU;
constexpr uint8_t kBleLlFeatureMaskOctet0 = 0x3FU;  // bits 0-5: encryption, conn-param-req, ext-reject, slave-features, ping, data-length-ext
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
constexpr uint32_t kBleConnTxenAfterRxUs = 100U;
// SCAN_RSP T_IFS: advertising path has only a few microseconds of software
// work between rxEndUs capture and the TXEN wait, so target 100 us here to
// land the actual over-the-air preamble near the BLE 150 us inter-frame space
// on stricter Qualcomm-based phones.
#ifndef BLE_SCAN_RSP_TXEN_AFTER_RX_US
#define BLE_SCAN_RSP_TXEN_AFTER_RX_US 100U
#endif
constexpr uint32_t kBleScanRspTxenAfterRxUs = BLE_SCAN_RSP_TXEN_AFTER_RX_US;
// Legacy advertising request/response exchanges should complete within hundreds
// of microseconds around T_IFS. Bound RX listen windows so advertising cadence
// does not become host-CPU-spin dependent.
constexpr uint32_t kBleAdvRequestListenMaxUs = 2500U;
constexpr uint32_t kBleScanRspListenMaxUs = 2500U;

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

  uint32_t mode = grtc->MODE;
  mode &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
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
constexpr uint32_t kBleConnAnchorPrewaitUs = 360U;
constexpr uint32_t kBleConnRxStartLeadUs = 2200U;
constexpr uint32_t kBleConnRxListenBaseUs = 900U;
constexpr uint32_t kBleConnRxListenMaxUs = 4500U;
constexpr uint32_t kBleConnDisableWaitUs = 750U;
constexpr uint32_t kBleConnTxDisableWaitUs = 750U;
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
constexpr uint8_t kSmpCodeSecurityRequest = 0x0BU;
constexpr uint8_t kSmpPairingRequestLen = 7U;
constexpr uint8_t kSmpPairingResponseLen = 7U;
constexpr uint8_t kSmpPairingConfirmLen = 17U;
constexpr uint8_t kSmpPairingRandomLen = 17U;
constexpr uint8_t kSmpEncryptionInformationLen = 17U;
constexpr uint8_t kSmpMasterIdentificationLen = 11U;
constexpr uint8_t kSmpIdentityInformationLen = 17U;
constexpr uint8_t kSmpIdentityAddressInformationLen = 8U;
constexpr uint8_t kSmpIoCapNoInputNoOutput = 0x03U;
constexpr uint8_t kSmpAuthReqBondingMask = 0x03U;
constexpr uint8_t kSmpKeyDistEncKeyMask = 0x01U;
constexpr uint8_t kSmpKeyDistIdKeyMask = 0x02U;
constexpr uint8_t kSmpReasonConfirmValueFailed = 0x04U;
constexpr uint8_t kSmpReasonPairingNotSupported = 0x05U;
constexpr uint8_t kSmpReasonEncryptionKeySize = 0x06U;
constexpr uint8_t kSmpReasonCommandNotSupported = 0x07U;
constexpr uint8_t kSmpReasonUnspecified = 0x08U;
constexpr uint8_t kSmpReasonInvalidParameters = 0x0AU;
constexpr uint8_t kSmpReasonOobNotAvailable = 0x02U;
constexpr uint8_t kSmpPairingStateIdle = 0U;
constexpr uint8_t kSmpPairingStateRspSent = 1U;
constexpr uint8_t kSmpPairingStateConfirmSent = 2U;
constexpr uint8_t kSmpPairingStateRandomSent = 3U;

constexpr uint32_t kBleBondRetentionMagic = 0x444E4F42U;  // "BOND"
constexpr uint16_t kBleBondRetentionVersion = 2U;
constexpr uint32_t kBleBondRramcBase = 0x5004B000UL;
constexpr uint32_t kBleBondRramcSpinLimit = 600000UL;
constexpr uint32_t kBleBondAarSpinLimit = 250000UL;

struct BleBondRecordV1 {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t ltk[16];
  uint8_t rand[8];
  uint16_t ediv;
  uint8_t keySize;
  uint8_t reserved[3];
};

struct BleBondRetentionBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  xiao_nrf54l15::BleBondRecord record;
};

__attribute__((section(".noinit"))) BleBondRetentionBlob g_bleBondRetention;
__attribute__((section(".bond_storage"), aligned(4)))
volatile BleBondRetentionBlob g_bleBondFlashStorage;

constexpr uint8_t kAttOpErrorRsp = 0x01U;
constexpr uint8_t kAttOpExchangeMtuReq = 0x02U;
constexpr uint8_t kAttOpExchangeMtuRsp = 0x03U;
constexpr uint8_t kAttOpFindInfoReq = 0x04U;
constexpr uint8_t kAttOpFindInfoRsp = 0x05U;
constexpr uint8_t kAttOpFindByTypeValueReq = 0x06U;
constexpr uint8_t kAttOpFindByTypeValueRsp = 0x07U;
constexpr uint8_t kAttOpReadByTypeReq = 0x08U;
constexpr uint8_t kAttOpReadByTypeRsp = 0x09U;
constexpr uint8_t kAttOpReadReq = 0x0AU;
constexpr uint8_t kAttOpReadRsp = 0x0BU;
constexpr uint8_t kAttOpReadBlobReq = 0x0CU;
constexpr uint8_t kAttOpReadBlobRsp = 0x0DU;
constexpr uint8_t kAttOpReadMultipleReq = 0x0EU;
constexpr uint8_t kAttOpReadMultipleRsp = 0x0FU;
constexpr uint8_t kAttOpReadMultipleVariableReq = 0x20U;
constexpr uint8_t kAttOpReadMultipleVariableRsp = 0x21U;
constexpr uint8_t kAttOpReadByGroupTypeReq = 0x10U;
constexpr uint8_t kAttOpReadByGroupTypeRsp = 0x11U;
constexpr uint8_t kAttOpWriteReq = 0x12U;
constexpr uint8_t kAttOpWriteRsp = 0x13U;
constexpr uint8_t kAttOpSignedWriteCmd = 0xD2U;
constexpr uint8_t kAttOpPrepareWriteReq = 0x16U;
constexpr uint8_t kAttOpPrepareWriteRsp = 0x17U;
constexpr uint8_t kAttOpExecuteWriteReq = 0x18U;
constexpr uint8_t kAttOpExecuteWriteRsp = 0x19U;
constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;
constexpr uint8_t kAttOpHandleValueInd = 0x1DU;
constexpr uint8_t kAttOpHandleValueCfm = 0x1EU;
constexpr uint8_t kAttOpWriteCmd = 0x52U;

static inline bool attOpcodeIsClientProgressPdu(uint8_t opcode) {
  switch (opcode) {
    case kAttOpExchangeMtuReq:
    case kAttOpFindInfoReq:
    case kAttOpFindByTypeValueReq:
    case kAttOpReadByTypeReq:
    case kAttOpReadReq:
    case kAttOpReadBlobReq:
    case kAttOpReadMultipleReq:
    case kAttOpReadMultipleVariableReq:
    case kAttOpReadByGroupTypeReq:
    case kAttOpWriteReq:
    case kAttOpPrepareWriteReq:
    case kAttOpExecuteWriteReq:
    case kAttOpWriteCmd:
    case kAttOpSignedWriteCmd:
    case kAttOpHandleValueCfm:
      return true;
    default:
      return false;
  }
}

static inline bool attOpcodeIsServerResponsePdu(uint8_t opcode) {
  switch (opcode) {
    case kAttOpErrorRsp:
    case kAttOpExchangeMtuRsp:
    case kAttOpFindInfoRsp:
    case kAttOpFindByTypeValueRsp:
    case kAttOpReadByTypeRsp:
    case kAttOpReadRsp:
    case kAttOpReadBlobRsp:
    case kAttOpReadMultipleRsp:
    case kAttOpReadMultipleVariableRsp:
    case kAttOpReadByGroupTypeRsp:
    case kAttOpWriteRsp:
    case kAttOpPrepareWriteRsp:
    case kAttOpExecuteWriteRsp:
      return true;
    default:
      return false;
  }
}

constexpr uint8_t kAttErrInvalidHandle = 0x01U;
constexpr uint8_t kAttErrReadNotPermitted = 0x02U;
constexpr uint8_t kAttErrWriteNotPermitted = 0x03U;
constexpr uint8_t kAttErrInvalidPdu = 0x04U;
constexpr uint8_t kAttErrRequestNotSupported = 0x06U;
constexpr uint8_t kAttErrInvalidOffset = 0x07U;
constexpr uint8_t kAttErrPrepareQueueFull = 0x09U;
constexpr uint8_t kAttErrInvalidAttrValueLen = 0x0DU;
constexpr uint8_t kAttErrUnsupportedGroupType = 0x10U;
constexpr uint8_t kAttErrAttributeNotFound = 0x0AU;

constexpr uint16_t kUuidPrimaryService = 0x2800U;
constexpr uint16_t kUuidCharacteristic = 0x2803U;
constexpr uint16_t kUuidClientCharacteristicConfig = 0x2902U;
constexpr uint16_t kUuidGapService = 0x1800U;
constexpr uint16_t kUuidGattService = 0x1801U;
constexpr uint16_t kUuidBatteryService = 0x180FU;
constexpr uint16_t kUuidDeviceName = 0x2A00U;
constexpr uint16_t kUuidAppearance = 0x2A01U;
constexpr uint16_t kUuidPpcp = 0x2A04U;
constexpr uint16_t kUuidServiceChanged = 0x2A05U;
constexpr uint16_t kUuidBatteryLevel = 0x2A19U;

constexpr uint16_t kHandleGapService = 0x0001U;
constexpr uint16_t kHandleGapDeviceNameDecl = 0x0002U;
constexpr uint16_t kHandleGapDeviceNameValue = 0x0003U;
constexpr uint16_t kHandleGapAppearanceDecl = 0x0004U;
constexpr uint16_t kHandleGapAppearanceValue = 0x0005U;
constexpr uint16_t kHandleGapPpcpDecl = 0x0006U;
constexpr uint16_t kHandleGapPpcpValue = 0x0007U;
constexpr uint16_t kHandleGattService = 0x0008U;
constexpr uint16_t kHandleGattServiceChangedDecl = 0x0009U;
constexpr uint16_t kHandleGattServiceChangedValue = 0x000AU;
constexpr uint16_t kHandleGattServiceChangedCccd = 0x000BU;
constexpr uint16_t kHandleBatteryService = 0x0010U;
constexpr uint16_t kHandleBatteryLevelDecl = 0x0011U;
constexpr uint16_t kHandleBatteryLevelValue = 0x0012U;
constexpr uint16_t kHandleBatteryLevelCccd = 0x0013U;

constexpr uint8_t kGattCharacteristicPropRead = 0x02U;
constexpr uint8_t kGattCharacteristicPropNotify = 0x10U;
constexpr uint8_t kGattCharacteristicPropIndicate = 0x20U;
constexpr uint8_t kBleUuid16Size = 2U;
constexpr uint8_t kBleUuid128Size = 16U;
constexpr uint8_t kAttrReadInvalidHandleLen = 0xFFU;
constexpr uint8_t kAttrReadInvalidOffsetLen = 0xFEU;
constexpr uint8_t kAttrReadNotPermittedLen = 0xFDU;
constexpr uint8_t kAttFindInfoFormat16Bit = 0x01U;
constexpr uint8_t kAttFindInfoFormat128Bit = 0x02U;
constexpr uint8_t kBleUuidLength16 = 2U;
constexpr uint8_t kBleUuidLength128 = 16U;

uint16_t readLe16(const uint8_t* p);
void writeLe16(uint8_t* p, uint16_t v);

struct BlePrimaryServiceRecord {
  uint16_t startHandle;
  uint16_t endHandle;
  uint16_t uuid16;
};

struct BleCharacteristicRecord {
  uint16_t declarationHandle;
  uint8_t properties;
  uint16_t valueHandle;
  uint16_t uuid16;
};

struct BleAttributeUuidRecord {
  uint16_t handle;
  uint16_t uuid16;
};

uint8_t customUuidLength(uint8_t uuidLength) {
  if (uuidLength == kBleUuidLength16 || uuidLength == kBleUuidLength128) {
    return uuidLength;
  }
  return 0U;
}

void setCustomUuid16(uint8_t* uuidLength, uint8_t* uuidBytes, uint16_t uuid16) {
  if (uuidLength == nullptr || uuidBytes == nullptr) {
    return;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength16;
  writeLe16(uuidBytes, uuid16);
}

bool setCustomUuid128(uint8_t* uuidLength, uint8_t* uuidBytes,
                      const uint8_t* uuid128Canonical) {
  if (uuidLength == nullptr || uuidBytes == nullptr || uuid128Canonical == nullptr) {
    return false;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength128;
  // Store 128-bit UUIDs in ATT/BLE little-endian order so wire encoding and
  // incoming ATT request matching do not need extra byte swapping later.
  for (uint8_t i = 0U; i < kBleUuidLength128; ++i) {
    uuidBytes[i] = uuid128Canonical[kBleUuidLength128 - 1U - i];
  }
  return true;
}

bool customUuidMatchesRaw(const uint8_t* uuidBytes,
                          uint8_t uuidLength,
                          const uint8_t* rawUuid,
                          uint8_t rawUuidLength) {
  uuidLength = customUuidLength(uuidLength);
  if (uuidLength == 0U || rawUuid == nullptr || rawUuidLength != uuidLength) {
    return false;
  }
  return memcmp(uuidBytes, rawUuid, uuidLength) == 0;
}

uint8_t customCharacteristicDeclarationValueLength(uint8_t uuidLength) {
  uuidLength = customUuidLength(uuidLength);
  return uuidLength == 0U ? 0U : static_cast<uint8_t>(3U + uuidLength);
}

constexpr BlePrimaryServiceRecord kBlePrimaryServices[] = {
    {kHandleGapService, kHandleGapPpcpValue, kUuidGapService},
    {kHandleGattService, kHandleGattServiceChangedCccd, kUuidGattService},
    {kHandleBatteryService, kHandleBatteryLevelCccd, kUuidBatteryService},
};

constexpr BleCharacteristicRecord kBleCharacteristics[] = {
    {kHandleGapDeviceNameDecl, kGattCharacteristicPropRead,
     kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kGattCharacteristicPropRead,
     kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kGattCharacteristicPropRead,
     kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattServiceChangedDecl, kGattCharacteristicPropIndicate,
     kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleBatteryLevelDecl, static_cast<uint8_t>(kGattCharacteristicPropRead |
                                                   kGattCharacteristicPropNotify),
     kHandleBatteryLevelValue, kUuidBatteryLevel},
};

constexpr BleAttributeUuidRecord kBleAttributeUuids[] = {
    {kHandleGapService, kUuidPrimaryService},
    {kHandleGapDeviceNameDecl, kUuidCharacteristic},
    {kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kUuidCharacteristic},
    {kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kUuidCharacteristic},
    {kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattService, kUuidPrimaryService},
    {kHandleGattServiceChangedDecl, kUuidCharacteristic},
    {kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleGattServiceChangedCccd, kUuidClientCharacteristicConfig},
    {kHandleBatteryService, kUuidPrimaryService},
    {kHandleBatteryLevelDecl, kUuidCharacteristic},
    {kHandleBatteryLevelValue, kUuidBatteryLevel},
    {kHandleBatteryLevelCccd, kUuidClientCharacteristicConfig},
};

uint8_t bleChannelToFrequency(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 2U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 26U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 80U;
  }
}

uint8_t bleChannelToIndex(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 37U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 38U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 39U;
  }
}

uint8_t bleDataChannelToFrequency(uint8_t dataChannel) {
  if (dataChannel > 36U) {
    return 4U;
  }
  if (dataChannel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * dataChannel));
  }
  return static_cast<uint8_t>(6U + (2U * dataChannel));
}

bool ieee802154ChannelValid(uint8_t channel) {
  return (channel >= 11U) && (channel <= 26U);
}

uint8_t ieee802154ChannelToFrequency(uint8_t channel) {
  return static_cast<uint8_t>(5U + ((channel - 11U) * 5U));
}

uint32_t radioTxPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t readLe24(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

void writeLe16(uint8_t* p, uint16_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
}

uint8_t bleConnectionPayloadProtocolOpcode(uint8_t llid, const uint8_t* payload,
                                           uint8_t length) {
  if (payload == nullptr || length == 0U) {
    return 0U;
  }
  if (llid == kBlePduLlControl) {
    return payload[0];
  }
  if ((llid == kBlePduDataStartOrComplete) && (length >= 5U)) {
    const uint16_t l2capLen = readLe16(&payload[0]);
    const uint16_t cid = readLe16(&payload[2]);
    const uint8_t availableAttLen =
        (length > kBleL2capHeaderLen)
            ? static_cast<uint8_t>(length - kBleL2capHeaderLen)
            : 0U;
    if ((cid == kBleL2capCidAtt) && (l2capLen > 0U) && (availableAttLen > 0U)) {
      return payload[kBleL2capHeaderLen];
    }
  }
  return payload[0];
}

void writeLe24(uint8_t* p, uint32_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
}

void writeLe32(uint8_t* p, uint32_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
  p[3] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
}

void fillPseudoRandomBytes(uint8_t* out, uint8_t len, uint32_t seed) {
  if (out == nullptr || len == 0U) {
    return;
  }

  uint32_t state = seed ^ 0xA55AA55AUL;
  if (state == 0U) {
    state = 0x1F123BB5UL;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    // Xorshift32 for lightweight deterministic byte generation.
    state ^= (state << 13U);
    state ^= (state >> 17U);
    state ^= (state << 5U);
    out[i] = static_cast<uint8_t>(state & 0xFFU);
  }
}

uint32_t crc32(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0U) {
    return 0U;
  }

  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0U; i < length; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = (crc & 1U) ? 0xFFFFFFFFUL : 0UL;
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

bool bleBondRecordLooksSane(const xiao_nrf54l15::BleBondRecord& record) {
  if (record.peerAddressRandom > 1U || record.peerIdentityAddressRandom > 1U ||
      record.localAddressRandom > 1U || record.peerIrkValid > 1U) {
    return false;
  }
  if (record.keySize < 7U || record.keySize > 16U) {
    return false;
  }
  bool keyNonZero = false;
  for (uint8_t i = 0U; i < 16U; ++i) {
    if (record.ltk[i] != 0U) {
      keyNonZero = true;
      break;
    }
  }
  return keyNonZero;
}

void clearBondRecordV2Tail(xiao_nrf54l15::BleBondRecord* record) {
  if (record == nullptr) {
    return;
  }
  memset(record->peerIdentityAddress, 0, sizeof(record->peerIdentityAddress));
  record->peerIdentityAddressRandom = 0U;
  memset(record->peerIrk, 0, sizeof(record->peerIrk));
  record->peerIrkValid = 0U;
  memset(record->reserved, 0, sizeof(record->reserved));
}

void convertLegacyBondRecord(const BleBondRecordV1& legacy,
                             xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, legacy.peerAddress, sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = legacy.peerAddressRandom;
  memcpy(outRecord->localAddress, legacy.localAddress, sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = legacy.localAddressRandom;
  memcpy(outRecord->ltk, legacy.ltk, sizeof(outRecord->ltk));
  memcpy(outRecord->rand, legacy.rand, sizeof(outRecord->rand));
  outRecord->ediv = legacy.ediv;
  outRecord->keySize = legacy.keySize;
  clearBondRecordV2Tail(outRecord);
}

NRF_RRAMC_Type* bondRramc() {
  return reinterpret_cast<NRF_RRAMC_Type*>(kBleBondRramcBase);
}

bool waitForBondRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) ==
        RRAMC_READY_READY_Ready) {
      return true;
    }
  }
  return false;
}

bool waitForBondRramcReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >>
         RRAMC_READYNEXT_READYNEXT_Pos) == RRAMC_READYNEXT_READYNEXT_Ready) {
      return true;
    }
  }
  return false;
}

void copyFromVolatileMemory(const volatile uint8_t* src, uint8_t* dst, size_t len) {
  if (src == nullptr || dst == nullptr) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    dst[i] = src[i];
  }
}

bool writeBondBlobToRram(const BleBondRetentionBlob& blob) {
  NRF_RRAMC_Type* const rramc = bondRramc();
  if (rramc == nullptr) {
    return false;
  }

  const uint32_t targetAddress =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_bleBondFlashStorage));
  const uint8_t* const src = reinterpret_cast<const uint8_t*>(&blob);
  const uint32_t prevConfig = rramc->CONFIG;
  const uint32_t writeConfig = prevConfig | RRAMC_CONFIG_WEN_Msk;
  bool writeOk = true;

  rramc->CONFIG = writeConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }

  if (writeOk) {
    rramc->EVENTS_ACCESSERROR = 0U;
    for (size_t i = 0U; i < sizeof(blob); ++i) {
      if (!waitForBondRramcReadyNext(rramc, kBleBondRramcSpinLimit)) {
        writeOk = false;
        break;
      }
      *reinterpret_cast<volatile uint8_t*>(targetAddress + static_cast<uint32_t>(i)) = src[i];
    }
    if (rramc->EVENTS_ACCESSERROR != 0U) {
      writeOk = false;
    }
  }

  if (writeOk) {
    rramc->EVENTS_READY = 0U;
    rramc->TASKS_COMMITWRITEBUF = 1U;
    writeOk = waitForBondRramcReady(rramc, kBleBondRramcSpinLimit);
  }

  rramc->CONFIG = prevConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }
  return writeOk;
}

void clearFlashBondBlobImage(BleBondRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
}

void buildFlashBondBlob(const xiao_nrf54l15::BleBondRecord& record,
                        BleBondRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
  blob->magic = kBleBondRetentionMagic;
  blob->version = kBleBondRetentionVersion;
  blob->recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&blob->record, &record, sizeof(record));
  blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(&blob->record), sizeof(blob->record));
}

bool readFlashBondBlob(BleBondRetentionBlob* outBlob) {
  if (outBlob == nullptr) {
    return false;
  }
  copyFromVolatileMemory(reinterpret_cast<const volatile uint8_t*>(&g_bleBondFlashStorage),
                         reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
  return true;
}

bool writeFlashBondBlob(const BleBondRetentionBlob& blob) {
  if (!writeBondBlobToRram(blob)) {
    return false;
  }
  BleBondRetentionBlob verify{};
  if (!readFlashBondBlob(&verify)) {
    return false;
  }
  return (memcmp(&verify, &blob, sizeof(blob)) == 0);
}

bool clearFlashBondRecord() {
  BleBondRetentionBlob blob{};
  clearFlashBondBlobImage(&blob);
  return writeFlashBondBlob(blob);
}

bool writeFlashBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  BleBondRetentionBlob blob{};
  buildFlashBondBlob(record, &blob);
  return writeFlashBondBlob(blob);
}

bool readFlashBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }

  BleBondRetentionBlob blob{};
  if (!readFlashBondBlob(&blob)) {
    return false;
  }
  if (blob.magic != kBleBondRetentionMagic) {
    return false;
  }
  if (blob.version == kBleBondRetentionVersion &&
      blob.recordLength == sizeof(blob.record)) {
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&blob.record), sizeof(blob.record));
    if (computedCrc != blob.crc32) {
      return false;
    }
    if (!bleBondRecordLooksSane(blob.record)) {
      return false;
    }
    memcpy(outRecord, &blob.record, sizeof(*outRecord));
    return true;
  }
  if (blob.version == 1U && blob.recordLength == sizeof(BleBondRecordV1)) {
    BleBondRecordV1 legacy{};
    memcpy(&legacy, &blob.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != blob.crc32) {
      return false;
    }
    convertLegacyBondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  return false;
}

void clearRetainedBondBlob() {
  memset(&g_bleBondRetention, 0, sizeof(g_bleBondRetention));
}

bool writeRetainedBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  g_bleBondRetention.magic = kBleBondRetentionMagic;
  g_bleBondRetention.version = kBleBondRetentionVersion;
  g_bleBondRetention.recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&g_bleBondRetention.record, &record, sizeof(record));
  g_bleBondRetention.crc32 =
      crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
            sizeof(g_bleBondRetention.record));
  return true;
}

bool readRetainedBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }
  if (g_bleBondRetention.magic != kBleBondRetentionMagic) {
    return false;
  }
  if (g_bleBondRetention.version == kBleBondRetentionVersion &&
      g_bleBondRetention.recordLength == sizeof(g_bleBondRetention.record)) {
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
              sizeof(g_bleBondRetention.record));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    if (!bleBondRecordLooksSane(g_bleBondRetention.record)) {
      return false;
    }
    memcpy(outRecord, &g_bleBondRetention.record, sizeof(*outRecord));
    return true;
  }
  if (g_bleBondRetention.version == 1U &&
      g_bleBondRetention.recordLength == sizeof(BleBondRecordV1)) {
    BleBondRecordV1 legacy{};
    memcpy(&legacy, &g_bleBondRetention.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    convertLegacyBondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  return false;
}

constexpr uint8_t kAesSbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16};

constexpr uint8_t kAesRcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
                                   0x20, 0x40, 0x80, 0x1B, 0x36};

using AesState = uint8_t[4][4];

uint8_t aesXtime(uint8_t x) {
  return static_cast<uint8_t>((x << 1U) ^ (((x >> 7U) & 0x01U) * 0x1BU));
}

void aesAddRoundKey(uint8_t round, AesState* state, const uint8_t* roundKey) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[i][j] ^= roundKey[(round * 16U) + (i * 4U) + j];
    }
  }
}

void aesSubBytes(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[j][i] = kAesSbox[(*state)[j][i]];
    }
  }
}

void aesShiftRows(AesState* state) {
  uint8_t temp = (*state)[0][1];
  (*state)[0][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[3][1];
  (*state)[3][1] = temp;

  temp = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;
  temp = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  temp = (*state)[0][3];
  (*state)[0][3] = (*state)[3][3];
  (*state)[3][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[1][3];
  (*state)[1][3] = temp;
}

void aesMixColumns(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    uint8_t t = (*state)[i][0];
    uint8_t tmp = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1] ^
                                       (*state)[i][2] ^ (*state)[i][3]);
    uint8_t tm = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1]);
    tm = aesXtime(tm);
    (*state)[i][0] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][1] ^ (*state)[i][2]);
    tm = aesXtime(tm);
    (*state)[i][1] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][2] ^ (*state)[i][3]);
    tm = aesXtime(tm);
    (*state)[i][2] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][3] ^ t);
    tm = aesXtime(tm);
    (*state)[i][3] ^= static_cast<uint8_t>(tm ^ tmp);
  }
}

void aesKeyExpansion128(const uint8_t key[16], uint8_t roundKey[176]) {
  for (uint8_t i = 0U; i < 16U; ++i) {
    roundKey[i] = key[i];
  }

  uint8_t temp[4] = {0U, 0U, 0U, 0U};
  uint16_t bytesGenerated = 16U;
  uint8_t rconIndex = 1U;
  while (bytesGenerated < 176U) {
    for (uint8_t i = 0U; i < 4U; ++i) {
      temp[i] = roundKey[static_cast<uint16_t>(bytesGenerated - 4U + i)];
    }

    if ((bytesGenerated % 16U) == 0U) {
      const uint8_t rot = temp[0];
      temp[0] = temp[1];
      temp[1] = temp[2];
      temp[2] = temp[3];
      temp[3] = rot;
      temp[0] = kAesSbox[temp[0]];
      temp[1] = kAesSbox[temp[1]];
      temp[2] = kAesSbox[temp[2]];
      temp[3] = kAesSbox[temp[3]];
      temp[0] ^= kAesRcon[rconIndex];
      ++rconIndex;
    }

    for (uint8_t i = 0U; i < 4U; ++i) {
      roundKey[bytesGenerated] =
          static_cast<uint8_t>(roundKey[bytesGenerated - 16U] ^ temp[i]);
      ++bytesGenerated;
    }
  }
}

// AES-128 encryption path adapted from tiny-AES-c (Unlicense), reduced to ECB block encrypt.
void aesEncryptBlockBe(const uint8_t key[16], const uint8_t plaintext[16], uint8_t out[16]) {
  uint8_t roundKey[176] = {0};
  uint8_t stateBuffer[16] = {0};
  aesKeyExpansion128(key, roundKey);
  memcpy(stateBuffer, plaintext, 16U);

  auto* state = reinterpret_cast<AesState*>(&stateBuffer[0]);
  aesAddRoundKey(0U, state, roundKey);
  for (uint8_t round = 1U; round < 10U; ++round) {
    aesSubBytes(state);
    aesShiftRows(state);
    aesMixColumns(state);
    aesAddRoundKey(round, state, roundKey);
  }
  aesSubBytes(state);
  aesShiftRows(state);
  aesAddRoundKey(10U, state, roundKey);
  memcpy(out, stateBuffer, 16U);
}

void reverseBytes(const uint8_t* in, uint8_t* out, uint8_t len) {
  if (in == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    out[i] = in[static_cast<uint8_t>(len - 1U - i)];
  }
}

void xor16(const uint8_t* a, const uint8_t* b, uint8_t* out) {
  if (a == nullptr || b == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < 16U; ++i) {
    out[i] = static_cast<uint8_t>(a[i] ^ b[i]);
  }
}

bool aesEncryptLeSoftware(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                         uint8_t outLe[16]) {
  if (keyLe == nullptr || plaintextLe == nullptr || outLe == nullptr) {
    return false;
  }

  uint8_t keyBe[16] = {0};
  uint8_t plainBe[16] = {0};
  uint8_t encBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  reverseBytes(plaintextLe, plainBe, 16U);
  aesEncryptBlockBe(keyBe, plainBe, encBe);
  reverseBytes(encBe, outLe, 16U);
  return true;
}

bool aesEncryptCcmBlockSoftware(const uint8_t keyLe[16], const uint8_t plaintext[16],
                                uint8_t out[16]) {
  if (keyLe == nullptr || plaintext == nullptr || out == nullptr) {
    return false;
  }

  // Link-layer CCM operates on byte strings as-is (AES big-endian block
  // convention). Session keys are held in Bluetooth little-endian order, so
  // only the key bytes are swapped before the block encrypt.
  uint8_t keyBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  aesEncryptBlockBe(keyBe, plaintext, out);
  return true;
}

bool aesEncryptLe(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                  uint8_t outLe[16]);
bool bleCcmEncryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* plaintext, uint8_t plaintextLen,
                          uint8_t* outCipherWithMic, uint8_t* outCipherWithMicLen);
bool bleCcmDecryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                          uint8_t* outPlaintext, uint8_t* outPlaintextLen);
bool bleCcmEncryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen);
bool bleCcmDecryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen);

#if !defined(NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO)
// Default the live BLE path to the silicon ECB/CCM blocks. The runtime still
// self-tests the hardware backend once and falls back to the software path if
// the peripheral bring-up or packet formatting does not match the software
// reference implementation on this board/package.
#define NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO 1
#endif

constexpr uint32_t kBleHardwareCryptoEcbSpinLimit = 200000UL;
constexpr uint32_t kBleHardwareCryptoCcmSpinLimit = 200000UL;
constexpr uint32_t kBleSecurityRandomSpinLimit = 400000UL;

struct BleHardwareCryptoState {
  bool checked;
  bool usable;
};

BleHardwareCryptoState& bleHardwareCryptoState() {
  static BleHardwareCryptoState state = {false, false};
  return state;
}

Ecb& bleHardwareEcb() {
  static Ecb ecb;
  return ecb;
}

Ccm& bleHardwareCcm() {
  static Ccm ccm;
  return ccm;
}

bool aesEncryptLeHardware(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                          uint8_t outLe[16]) {
  return bleHardwareEcb().encryptBlock(keyLe, plaintextLe, outLe,
                                       kBleHardwareCryptoEcbSpinLimit);
}

bool bleCcmEncryptPayloadHardware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen) {
  return bleHardwareCcm().encryptBlePacket(
      key, iv, counter, direction, header, plaintext, plaintextLen,
      outCipherWithMic, outCipherWithMicLen, CcmBleDataRate::k1Mbit, 0xE3U,
      kBleHardwareCryptoCcmSpinLimit);
}

bool bleCcmDecryptPayloadHardware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen) {
  bool macValid = false;
  return bleHardwareCcm().decryptBlePacket(
             key, iv, counter, direction, header, cipherWithMic,
             cipherWithMicLen, outPlaintext, outPlaintextLen, &macValid,
             CcmBleDataRate::k1Mbit, 0xE3U, kBleHardwareCryptoCcmSpinLimit) &&
         macValid;
}

bool bleHardwareCryptoSelfTest() {
  const uint8_t key[16] = {0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU,
                           0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU, 0xFEU};
  const uint8_t plaintextLe[16] = {0xFEU, 0xDCU, 0xBAU, 0x98U, 0x76U, 0x54U, 0x32U, 0x10U,
                                   0xEFU, 0xCDU, 0xABU, 0x89U, 0x67U, 0x45U, 0x23U, 0x01U};
  uint8_t swEcb[16] = {0};
  uint8_t hwEcb[16] = {0};
  if (!aesEncryptLeSoftware(key, plaintextLe, swEcb) ||
      !aesEncryptLeHardware(key, plaintextLe, hwEcb) ||
      (memcmp(swEcb, hwEcb, sizeof(swEcb)) != 0)) {
    return false;
  }

  const uint8_t iv[8] = {0x34U, 0x12U, 0x78U, 0x56U, 0xBCU, 0x9AU, 0xF0U, 0xDEU};
  const uint8_t header = 0x0BU;
  const uint64_t counter = 0x12345ULL;
  const uint8_t direction = 1U;
  const uint8_t payload[] = {
      0x03U, 0x07U, 0x00U, 0x06U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U,
      0x77U, 0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xF0U};
  uint8_t swCipher[kBleDataPduMaxPayload + kBleMicLen] = {0};
  uint8_t hwCipher[kBleDataPduMaxPayload + kBleMicLen] = {0};
  uint8_t swCipherLen = 0U;
  uint8_t hwCipherLen = 0U;
  if (!bleCcmEncryptPayloadSoftware(key, iv, counter, direction, header, payload,
                                    sizeof(payload), swCipher, &swCipherLen) ||
      !bleCcmEncryptPayloadHardware(key, iv, counter, direction, header, payload,
                                    sizeof(payload), hwCipher, &hwCipherLen) ||
      (swCipherLen != hwCipherLen) ||
      (memcmp(swCipher, hwCipher, swCipherLen) != 0)) {
    return false;
  }

  uint8_t swPlain[kBleDataPduMaxPayload] = {0};
  uint8_t hwPlain[kBleDataPduMaxPayload] = {0};
  uint8_t swPlainLen = 0U;
  uint8_t hwPlainLen = 0U;
  if (!bleCcmDecryptPayloadSoftware(key, iv, counter, direction, header, swCipher,
                                    swCipherLen, swPlain, &swPlainLen) ||
      !bleCcmDecryptPayloadHardware(key, iv, counter, direction, header, swCipher,
                                    swCipherLen, hwPlain, &hwPlainLen) ||
      (swPlainLen != sizeof(payload)) || (hwPlainLen != sizeof(payload)) ||
      (memcmp(swPlain, payload, sizeof(payload)) != 0) ||
      (memcmp(hwPlain, payload, sizeof(payload)) != 0)) {
    return false;
  }

  return true;
}

bool bleHardwareCryptoAvailable() {
#if NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO == 1
  BleHardwareCryptoState& state = bleHardwareCryptoState();
  if (!state.checked) {
    state.checked = true;
    state.usable = bleHardwareCryptoSelfTest();
  }
  return state.usable;
#else
  return false;
#endif
}

void bleHardwareCryptoDisable() {
  BleHardwareCryptoState& state = bleHardwareCryptoState();
  state.checked = true;
  state.usable = false;
}

bool fillBleSecurityRandomBytes(uint8_t* out, uint8_t len, uint32_t seed,
                                uint32_t spinLimit) {
  if (out == nullptr || len == 0U) {
    return false;
  }

  CracenRng rng;
  if (rng.fill(out, len, spinLimit)) {
    return true;
  }

  fillPseudoRandomBytes(out, len, seed);
  return false;
}

bool smpC1(const uint8_t tk[16], const uint8_t r[16], const uint8_t preq[7],
           const uint8_t pres[7], uint8_t iat, const uint8_t ia[6], uint8_t rat,
           const uint8_t ra[6], uint8_t out[16]) {
  if (tk == nullptr || r == nullptr || preq == nullptr || pres == nullptr ||
      ia == nullptr || ra == nullptr || out == nullptr) {
    return false;
  }

  uint8_t p1[16] = {0};
  uint8_t p2[16] = {0};
  uint8_t tmp[16] = {0};
  p1[0] = iat;
  p1[1] = rat;
  memcpy(&p1[2], preq, 7U);
  memcpy(&p1[9], pres, 7U);

  xor16(r, p1, tmp);
  if (!aesEncryptLe(tk, tmp, tmp)) {
    return false;
  }

  memcpy(&p2[0], ra, 6U);
  memcpy(&p2[6], ia, 6U);
  memset(&p2[12], 0, 4U);
  xor16(tmp, p2, tmp);
  return aesEncryptLe(tk, tmp, out);
}

bool smpS1(const uint8_t tk[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]) {
  if (tk == nullptr || r1 == nullptr || r2 == nullptr || out == nullptr) {
    return false;
  }
  uint8_t plaintext[16] = {0};
  // s1(k, r1, r2) per BLE spec Vol 3 Part H §2.2.4:
  //   AES input (big-endian) = r1[63:0] || r2[63:0]
  //                          = Srand_BE || Mrand_BE (with r1=Srand, r2=Mrand)
  // aesEncryptLe byte-reverses the plaintext before AES, so we must supply
  // the LE buffer in reverse order: [r2 LSBs, r1 LSBs] to get [r1_BE, r2_BE]
  // after the reversal.
  memcpy(&plaintext[0], r2, 8U);
  memcpy(&plaintext[8], r1, 8U);
  return aesEncryptLe(tk, plaintext, out);
}

void buildBleCcmNonce(const uint8_t iv[8], uint64_t counter, uint8_t direction,
                      uint8_t nonce[13]) {
  if (iv == nullptr || nonce == nullptr) {
    return;
  }

  const uint64_t ctr = counter & 0x7FFFFFFFFFULL;
  nonce[0] = static_cast<uint8_t>(ctr & 0xFFU);
  nonce[1] = static_cast<uint8_t>((ctr >> 8U) & 0xFFU);
  nonce[2] = static_cast<uint8_t>((ctr >> 16U) & 0xFFU);
  nonce[3] = static_cast<uint8_t>((ctr >> 24U) & 0xFFU);
  nonce[4] = static_cast<uint8_t>(((ctr >> 32U) & 0x7FU) | ((direction & 0x01U) << 7U));
  // BLE sample data and controller interop both expect the nonce IV bytes in
  // the same little-endian order they are carried over LL control PDUs:
  // IVm first, then IVs.
  memcpy(&nonce[5], iv, 8U);
}

void buildCcmCtrBlock(const uint8_t nonce[13], uint16_t ctr, uint8_t out[16]) {
  if (nonce == nullptr || out == nullptr) {
    return;
  }

  memset(out, 0, 16U);
  out[0] = 0x01U;  // L = 2 bytes for BLE CCM.
  memcpy(&out[1], nonce, 13U);
  out[14] = static_cast<uint8_t>((ctr >> 8U) & 0xFFU);
  out[15] = static_cast<uint8_t>(ctr & 0xFFU);
}

bool bleComputeCcmMicSoftware(const uint8_t key[16], const uint8_t nonce[13],
                              uint8_t header, const uint8_t* payload,
                              uint8_t payloadLen, uint8_t outMic[4]) {
  if (key == nullptr || nonce == nullptr || outMic == nullptr) {
    return false;
  }
  if (payloadLen > 0U && payload == nullptr) {
    return false;
  }

  uint8_t x[16] = {0};
  uint8_t block[16] = {0};
  uint8_t tmp[16] = {0};

  // B0 flags: Adata present + M=4 + L=2.
  block[0] = 0x49U;
  memcpy(&block[1], nonce, 13U);
  block[14] = 0x00U;
  block[15] = payloadLen;
  xor16(x, block, tmp);
  if (!aesEncryptCcmBlockSoftware(key, tmp, x)) {
    return false;
  }

  // AAD is one-byte header with SN/NESN/MD masked to zero (0xE3).
  memset(block, 0, sizeof(block));
  block[0] = 0x00U;
  block[1] = 0x01U;
  block[2] = static_cast<uint8_t>(header & 0xE3U);
  xor16(x, block, tmp);
  if (!aesEncryptCcmBlockSoftware(key, tmp, x)) {
    return false;
  }

  uint8_t offset = 0U;
  while (offset < payloadLen) {
    memset(block, 0, sizeof(block));
    const uint8_t remaining = static_cast<uint8_t>(payloadLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    memcpy(block, &payload[offset], chunk);
    xor16(x, block, tmp);
    if (!aesEncryptCcmBlockSoftware(key, tmp, x)) {
      return false;
    }
    offset = static_cast<uint8_t>(offset + chunk);
  }

  uint8_t s0[16] = {0};
  buildCcmCtrBlock(nonce, 0U, block);
  if (!aesEncryptCcmBlockSoftware(key, block, s0)) {
    return false;
  }

  for (uint8_t i = 0U; i < 4U; ++i) {
    outMic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);
  }
  return true;
}

bool bleCcmEncryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen) {
  if (key == nullptr || iv == nullptr || outCipherWithMic == nullptr ||
      outCipherWithMicLen == nullptr) {
    return false;
  }
  if (plaintextLen > 0U && plaintext == nullptr) {
    return false;
  }
  if (plaintextLen > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t nonce[13] = {0};
  buildBleCcmNonce(iv, counter, direction, nonce);

  uint8_t offset = 0U;
  uint16_t ctr = 1U;
  while (offset < plaintextLen) {
    uint8_t stream[16] = {0};
    uint8_t ctrBlock[16] = {0};
    buildCcmCtrBlock(nonce, ctr, ctrBlock);
    if (!aesEncryptCcmBlockSoftware(key, ctrBlock, stream)) {
      return false;
    }

    const uint8_t remaining = static_cast<uint8_t>(plaintextLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    for (uint8_t i = 0U; i < chunk; ++i) {
      outCipherWithMic[offset + i] =
          static_cast<uint8_t>(plaintext[offset + i] ^ stream[i]);
    }
    offset = static_cast<uint8_t>(offset + chunk);
    ++ctr;
  }

  uint8_t mic[4] = {0};
  if (!bleComputeCcmMicSoftware(key, nonce, header, plaintext, plaintextLen, mic)) {
    return false;
  }
  memcpy(&outCipherWithMic[plaintextLen], mic, sizeof(mic));
  *outCipherWithMicLen = static_cast<uint8_t>(plaintextLen + kBleMicLen);
  return true;
}

bool bleCcmDecryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen) {
  if (key == nullptr || iv == nullptr || cipherWithMic == nullptr ||
      outPlaintext == nullptr || outPlaintextLen == nullptr) {
    return false;
  }
  if (cipherWithMicLen < kBleMicLen) {
    return false;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(cipherWithMicLen - kBleMicLen);
  if (payloadLen > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t nonce[13] = {0};
  buildBleCcmNonce(iv, counter, direction, nonce);

  uint8_t offset = 0U;
  uint16_t ctr = 1U;
  while (offset < payloadLen) {
    uint8_t stream[16] = {0};
    uint8_t ctrBlock[16] = {0};
    buildCcmCtrBlock(nonce, ctr, ctrBlock);
    if (!aesEncryptCcmBlockSoftware(key, ctrBlock, stream)) {
      return false;
    }

    const uint8_t remaining = static_cast<uint8_t>(payloadLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    for (uint8_t i = 0U; i < chunk; ++i) {
      outPlaintext[offset + i] =
          static_cast<uint8_t>(cipherWithMic[offset + i] ^ stream[i]);
    }
    offset = static_cast<uint8_t>(offset + chunk);
    ++ctr;
  }

  uint8_t expectedMic[4] = {0};
  if (!bleComputeCcmMicSoftware(key, nonce, header, outPlaintext, payloadLen,
                                expectedMic)) {
    return false;
  }

  uint8_t diff = 0U;
  for (uint8_t i = 0U; i < 4U; ++i) {
    diff |= static_cast<uint8_t>(expectedMic[i] ^ cipherWithMic[payloadLen + i]);
  }
  if (diff != 0U) {
    return false;
  }

  *outPlaintextLen = payloadLen;
  return true;
}

bool aesEncryptLe(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                  uint8_t outLe[16]) {
  if (bleHardwareCryptoAvailable() &&
      aesEncryptLeHardware(keyLe, plaintextLe, outLe)) {
    return true;
  }
  if (bleHardwareCryptoAvailable()) {
    bleHardwareCryptoDisable();
  }
  return aesEncryptLeSoftware(keyLe, plaintextLe, outLe);
}

bool bleCcmEncryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* plaintext, uint8_t plaintextLen,
                          uint8_t* outCipherWithMic, uint8_t* outCipherWithMicLen) {
  if (bleHardwareCryptoAvailable() &&
      bleCcmEncryptPayloadHardware(key, iv, counter, direction, header, plaintext,
                                   plaintextLen, outCipherWithMic,
                                   outCipherWithMicLen)) {
    return true;
  }
  if (bleHardwareCryptoAvailable()) {
    bleHardwareCryptoDisable();
  }
  return bleCcmEncryptPayloadSoftware(key, iv, counter, direction, header,
                                      plaintext, plaintextLen, outCipherWithMic,
                                      outCipherWithMicLen);
}

bool bleCcmDecryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                          uint8_t* outPlaintext, uint8_t* outPlaintextLen) {
  if (bleHardwareCryptoAvailable() &&
      bleCcmDecryptPayloadHardware(key, iv, counter, direction, header,
                                   cipherWithMic, cipherWithMicLen, outPlaintext,
                                   outPlaintextLen)) {
    return true;
  }
  if (bleHardwareCryptoAvailable()) {
    bleHardwareCryptoDisable();
  }
  return bleCcmDecryptPayloadSoftware(key, iv, counter, direction, header,
                                      cipherWithMic, cipherWithMicLen,
                                      outPlaintext, outPlaintextLen);
}

uint8_t bitCount37(const uint8_t map[5]) {
  if (map == nullptr) {
    return 0U;
  }

  uint8_t count = 0;
  for (uint8_t ch = 0; ch < 37U; ++ch) {
    if ((map[ch >> 3U] & (1U << (ch & 7U))) != 0U) {
      ++count;
    }
  }
  return count;
}

uint8_t remapChannelByIndex(const uint8_t map[5], uint8_t usedIndex) {
  uint8_t channel = 0;
  for (uint8_t byte = 0; byte < 5U; ++byte) {
    uint8_t bits = map[byte];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if (channel >= 37U) {
        break;
      }
      if ((bits & 0x01U) != 0U) {
        if (usedIndex == 0U) {
          return channel;
        }
        --usedIndex;
      }
      bits >>= 1U;
      ++channel;
    }
  }
  return 0U;
}

void blePeriphConnTraceReset() {
  g_ble_periph_conn_trace_count = 0U;
  for (uint8_t i = 0U; i < 16U; ++i) {
    g_ble_periph_conn_trace_stage[i] = 0U;
    g_ble_periph_conn_trace_channel[i] = 0U;
    g_ble_periph_conn_trace_event[i] = 0U;
    g_ble_periph_conn_trace_value[i] = 0U;
  }
}

void blePeriphConnTracePush(uint16_t eventCounter, uint8_t dataChannel,
                            uint8_t stage, uint8_t value) {
  const uint32_t count = g_ble_periph_conn_trace_count;
  const uint8_t slot = static_cast<uint8_t>(count & 0x0FU);
  g_ble_periph_conn_trace_stage[slot] = stage;
  g_ble_periph_conn_trace_channel[slot] = dataChannel;
  g_ble_periph_conn_trace_event[slot] = eventCounter;
  g_ble_periph_conn_trace_value[slot] = value;
  g_ble_periph_conn_trace_count = count + 1U;
}

void blePeriphRxTraceReset() {
  g_ble_periph_rx_trace_count = 0U;
  for (uint8_t i = 0U; i < 32U; ++i) {
    g_ble_periph_rx_trace_llid[i] = 0U;
    g_ble_periph_rx_trace_len[i] = 0U;
    g_ble_periph_rx_trace_flags[i] = 0U;
    g_ble_periph_rx_trace_opcode[i] = 0U;
    g_ble_periph_rx_trace_cid[i] = 0U;
  }
}

void blePeriphRxTracePush(uint8_t llid, uint8_t length, bool packetIsNew,
                          bool peerAckedLastTx, uint16_t l2capCid,
                          uint8_t opcode) {
  const uint32_t count = g_ble_periph_rx_trace_count;
  const uint8_t slot = static_cast<uint8_t>(count & 0x1FU);
  uint8_t flags = 0U;
  if (packetIsNew) {
    flags |= 0x01U;
  }
  if (peerAckedLastTx) {
    flags |= 0x02U;
  }
  if (l2capCid != 0U) {
    flags |= 0x04U;
  }
  g_ble_periph_rx_trace_llid[slot] = llid;
  g_ble_periph_rx_trace_len[slot] = length;
  g_ble_periph_rx_trace_flags[slot] = flags;
  g_ble_periph_rx_trace_opcode[slot] = opcode;
  g_ble_periph_rx_trace_cid[slot] = l2capCid;
  g_ble_periph_rx_trace_count = count + 1U;
}

void blePeriphTxTraceReset() {
  g_ble_periph_tx_trace_count = 0U;
  for (uint8_t i = 0U; i < 32U; ++i) {
    g_ble_periph_tx_trace_llid[i] = 0U;
    g_ble_periph_tx_trace_len[i] = 0U;
    g_ble_periph_tx_trace_opcode[i] = 0U;
    g_ble_periph_tx_trace_cid[i] = 0U;
  }
}

void blePeriphTxTracePush(uint8_t llid, uint8_t length, uint16_t l2capCid,
                          uint8_t opcode) {
  const uint32_t count = g_ble_periph_tx_trace_count;
  const uint8_t slot = static_cast<uint8_t>(count & 0x1FU);
  g_ble_periph_tx_trace_llid[slot] = llid;
  g_ble_periph_tx_trace_len[slot] = length;
  g_ble_periph_tx_trace_opcode[slot] = opcode;
  g_ble_periph_tx_trace_cid[slot] = l2capCid;
  g_ble_periph_tx_trace_count = count + 1U;
}

void blePeriphTxTracePushPacket(uint8_t llid, uint8_t length,
                                const uint8_t* payload) {
  if (length == 0U || payload == nullptr) {
    return;
  }
  uint16_t l2capCid = 0U;
  uint8_t opcode = 0U;
  if (llid == kBlePduLlControl) {
    opcode = payload[0];
  } else if (llid == kBlePduDataStartOrComplete) {
    if (length >= kBleL2capHeaderLen) {
      l2capCid = readLe16(&payload[2]);
    }
    if (length >= (kBleL2capHeaderLen + 1U)) {
      opcode = payload[4];
    }
  }
  blePeriphTxTracePush(llid, length, l2capCid, opcode);
}

bool timeReachedUs(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

bool isValidBleConnectionAccessAddress(uint32_t accessAddress) {
  if (accessAddress == kBleAccessAddress ||
      accessAddress == 0U ||
      accessAddress == 0xFFFFFFFFUL) {
    return false;
  }

  const uint32_t diffBits =
      static_cast<uint32_t>(__builtin_popcount(accessAddress ^ kBleAccessAddress));
  if (diffBits <= 1U) {
    return false;
  }

  uint8_t transitions = 0U;
  uint8_t runLength = 1U;
  uint8_t maxRunLength = 1U;
  bool lastBit = (accessAddress & 0x1U) != 0U;
  for (uint8_t bit = 1U; bit < 32U; ++bit) {
    const bool currentBit = ((accessAddress >> bit) & 0x1U) != 0U;
    if (currentBit != lastBit) {
      ++transitions;
      runLength = 1U;
    } else {
      ++runLength;
      if (runLength > maxRunLength) {
        maxRunLength = runLength;
      }
    }
    lastBit = currentBit;
  }

  if (maxRunLength > 6U || transitions > 24U) {
    return false;
  }

  uint8_t topTransitions = 0U;
  bool lastTopBit = ((accessAddress >> 31U) & 0x1U) != 0U;
  for (int8_t bit = 30; bit >= 26; --bit) {
    const bool currentBit = ((accessAddress >> bit) & 0x1U) != 0U;
    if (currentBit != lastTopBit) {
      ++topTransitions;
    }
    lastTopBit = currentBit;
  }
  return topTransitions >= 2U;
}

uint32_t nextPseudoRandomWord(uint32_t state) {
  uint32_t x = state;
  if (x == 0U) {
    x = 0xA5A55A5AUL;
  }
  x ^= (x << 13U);
  x ^= (x >> 17U);
  x ^= (x << 5U);
  return x;
}

uint32_t generateBleConnectionAccessAddress(uint32_t seed) {
  uint32_t state = seed ^ 0xC3A55A3CUL;
  for (uint8_t attempt = 0U; attempt < 64U; ++attempt) {
    state = nextPseudoRandomWord(state);
    if (isValidBleConnectionAccessAddress(state)) {
      return state;
    }
  }

  static constexpr uint32_t kFallbackCandidates[] = {
      0xAF9A8C69UL,
      0xD7A46C5BUL,
      0xC59A3B6DUL,
  };
  for (uint32_t candidate : kFallbackCandidates) {
    if (isValidBleConnectionAccessAddress(candidate)) {
      return candidate;
    }
  }

  return 0xAF9A8C69UL;
}

uint32_t bleMasterScaPpmUpperBound(uint8_t scaCode) {
  switch (scaCode & 0x07U) {
    case 0U:
      return 500UL;
    case 1U:
      return 250UL;
    case 2U:
      return 150UL;
    case 3U:
      return 100UL;
    case 4U:
      return 75UL;
    case 5U:
      return 50UL;
    case 6U:
      return 30UL;
    case 7U:
    default:
      return 20UL;
  }
}

uint32_t bleConnectionWindowWideningUs(uint16_t intervalUnits,
                                       uint8_t masterScaCode,
                                       uint16_t unsyncedEventCount = 1U) {
  if (intervalUnits == 0U || unsyncedEventCount == 0U) {
    return 0U;
  }

  const uint32_t intervalUs = static_cast<uint32_t>(intervalUnits) * 1250UL;
  const uint32_t totalPpm =
      bleMasterScaPpmUpperBound(masterScaCode) + kBleConnSlaveScaPpm;
  const uint64_t wideningProduct =
      static_cast<uint64_t>(intervalUs) * static_cast<uint64_t>(totalPpm) *
      static_cast<uint64_t>(unsyncedEventCount);
  return static_cast<uint32_t>((wideningProduct + 999999ULL) / 1000000ULL);
}

uint32_t bleConnectionRxListenUs(uint16_t intervalUnits, uint8_t masterScaCode,
                                 uint16_t unsyncedEventCount = 1U) {
  const uint32_t wideningUs =
      bleConnectionWindowWideningUs(intervalUnits, masterScaCode,
                                    unsyncedEventCount);
  uint32_t postAnchorListenUs = kBleConnRxListenBaseUs + wideningUs;
  if (postAnchorListenUs > kBleConnRxListenMaxUs) {
    postAnchorListenUs = kBleConnRxListenMaxUs;
  }
  // RX is armed kBleConnRxStartLeadUs before the nominal anchor. The radio
  // budget must include this lead, otherwise the listen window can expire
  // before the expected packet arrival.
  return kBleConnRxStartLeadUs + postAnchorListenUs;
}

uint32_t bleDataPduAirTimeUs(uint8_t payloadLength) {
  return static_cast<uint32_t>(10U + payloadLength) * 8U;
}

uint32_t bleConnectionTxEndBudgetUs(uint8_t payloadLength) {
  return bleDataPduAirTimeUs(payloadLength) + kBleConnTxDisableWaitUs + 400U;
}

uint8_t bleReverse8(uint8_t v) {
  v = static_cast<uint8_t>(((v & 0xF0U) >> 4U) | ((v & 0x0FU) << 4U));
  v = static_cast<uint8_t>(((v & 0xCCU) >> 2U) | ((v & 0x33U) << 2U));
  v = static_cast<uint8_t>(((v & 0xAAU) >> 1U) | ((v & 0x55U) << 1U));
  return v;
}

uint16_t bleCsa2Permute(uint16_t v) {
  const uint8_t msb = bleReverse8(static_cast<uint8_t>((v >> 8U) & 0xFFU));
  const uint8_t lsb = bleReverse8(static_cast<uint8_t>(v & 0xFFU));
  return static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8U) | lsb);
}

uint16_t bleCsa2Mam(uint16_t a, uint16_t b) {
  return static_cast<uint16_t>((static_cast<uint32_t>(17U) * a + b) & 0xFFFFU);
}

uint16_t bleCsa2PrnE(uint16_t eventCounter, uint16_t channelId) {
  uint16_t prn = static_cast<uint16_t>(eventCounter ^ channelId);
  for (uint8_t i = 0U; i < 3U; ++i) {
    prn = bleCsa2Permute(prn);
    prn = bleCsa2Mam(prn, channelId);
  }
  return static_cast<uint16_t>(prn ^ channelId);
}

bool waitRadioEndBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit,
                          uint32_t* endTimestampUs = nullptr) {
  if (radio == nullptr || (spinLimit == 0U && budgetUs == 0U)) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (true) {
    if (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U) {
      if (endTimestampUs != nullptr) {
        *endTimestampUs = bleTimingUs();
      }
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      const uint32_t elapsedUs = static_cast<uint32_t>(bleTimingUs() - startUs);
      if ((budgetUs > 0U) && (elapsedUs >= budgetUs)) {
        break;
      }
      if ((budgetUs == 0U) && (spinLimit == 0U)) {
        break;
      }
    }

    if (spinLimit > 0U) {
      --spinLimit;
      if (spinLimit == 0U) {
        break;
      }
    }
  }

  const bool endSeen = (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U);
  if (endSeen && endTimestampUs != nullptr) {
    *endTimestampUs = bleTimingUs();
  }
  return endSeen;
}

bool waitRadioDisabledBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit) {
  if (radio == nullptr || (spinLimit == 0U && budgetUs == 0U)) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (true) {
    if (radio->EVENTS_DISABLED != 0U) {
      return true;
    }

    const uint32_t state =
        (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      const uint32_t elapsedUs = static_cast<uint32_t>(bleTimingUs() - startUs);
      if ((budgetUs > 0U) && (elapsedUs >= budgetUs)) {
        break;
      }
      if (spinLimit == 0U) {
        break;
      }
    }

    if (spinLimit > 0U) {
      --spinLimit;
    }
  }

  if (radio->EVENTS_DISABLED != 0U) {
    return true;
  }
  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  return (state == RADIO_STATE_STATE_Disabled);
}

bool waitRadioRxDoneBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit,
                             uint32_t* outDoneTimestampUs = nullptr) {
  if (radio == nullptr || (spinLimit == 0U && budgetUs == 0U)) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (true) {
    if ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U)) {
      if (outDoneTimestampUs != nullptr) {
        *outDoneTimestampUs = bleTimingUs();
      }
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      const uint32_t elapsedUs = static_cast<uint32_t>(bleTimingUs() - startUs);
      if ((budgetUs > 0U) && (elapsedUs >= budgetUs)) {
        break;
      }
      if ((budgetUs == 0U) && (spinLimit == 0U)) {
        break;
      }
    }

    if (spinLimit > 0U) {
      --spinLimit;
      if (spinLimit == 0U) {
        break;
      }
    }
  }

  const bool doneSeen = ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U));
  if (doneSeen && outDoneTimestampUs != nullptr) {
    *outDoneTimestampUs = bleTimingUs();
  }
  return doneSeen;
}

void forceRadioDisabled(NRF_RADIO_Type* radio, uint32_t spinLimit) {
  if (radio == nullptr) {
    return;
  }

  radio->SHORTS = 0U;
  radio->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  (void)waitRadioDisabledBudgeted(radio, 3000U, spinLimit);
  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_PLLREADY = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_RXADDRESS = 0U;
  radio->EVENTS_DEVMATCH = 0U;
  radio->EVENTS_DEVMISS = 0U;
  radio->EVENTS_MHRMATCH = 0U;
  radio->EVENTS_SYNC = 0U;
}

uint8_t ieee802154MacPayloadLengthFromPhr(uint8_t phrLength) {
  // The IEEE 802.15.4 PHR length counts the on-air FCS as part of the PSDU.
  // The stack uses MAC payload lengths without the trailing two-byte FCS.
  return (phrLength >= 2U) ? static_cast<uint8_t>(phrLength - 2U) : 0U;
}

uint8_t ieee802154PhrLengthFromMacPayloadLength(uint8_t macLength) {
  return (macLength <= 125U) ? static_cast<uint8_t>(macLength + 2U) : 0U;
}

void setIeee802154CrcLengthMode(NRF_RADIO_Type* radio, bool includeFcsInLength) {
  if (radio == nullptr) {
    return;
  }
  uint32_t pcnf0 = radio->PCNF0 & ~RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= ((includeFcsInLength ? RADIO_PCNF0_CRCINC_Include
                                : RADIO_PCNF0_CRCINC_Exclude)
            << RADIO_PCNF0_CRCINC_Pos) & RADIO_PCNF0_CRCINC_Msk;
  radio->PCNF0 = pcnf0;
}

void snapshotZigbeeReceiveEvents(NRF_RADIO_Type* radio,
                                 ZigbeeTransmitDebug* debug) {
  if (radio == nullptr || debug == nullptr) {
    return;
  }

  debug->syncSeen = (radio->EVENTS_SYNC != 0U);
  debug->rxReadySeen = (radio->EVENTS_RXREADY != 0U);
  debug->frameStartSeen = (radio->EVENTS_FRAMESTART != 0U);
  debug->addressSeen = (radio->EVENTS_ADDRESS != 0U);
  debug->rxAddressSeen = (radio->EVENTS_RXADDRESS != 0U);
  debug->endEventSeen = (radio->EVENTS_END != 0U);
  debug->devMatchSeen = (radio->EVENTS_DEVMATCH != 0U);
  debug->devMissSeen = (radio->EVENTS_DEVMISS != 0U);
  debug->mhrMatchSeen = (radio->EVENTS_MHRMATCH != 0U);
}

bool llEventInstantReached(uint16_t currentEventCounter, uint16_t instant) {
  return static_cast<uint16_t>(currentEventCounter - instant) < 0x8000U;
}

uint32_t bleDataWhiteValue(uint8_t channelIndex) {
  const uint32_t iv = static_cast<uint32_t>(0x40U | (channelIndex & 0x3FU));
  const uint32_t poly = 0x89UL;  // x^7 + x^4 + 1
  return ((poly << RADIO_DATAWHITE_POLY_Pos) & RADIO_DATAWHITE_POLY_Msk) |
         ((iv << RADIO_DATAWHITE_IV_Pos) & RADIO_DATAWHITE_IV_Msk);
}

uint32_t bleAccessAddressBase(uint32_t accessAddress) {
  return (accessAddress << 8U);
}

uint32_t bleAccessAddressPrefix(uint32_t accessAddress) {
  return ((accessAddress >> 24U) & 0xFFU);
}

int8_t radioRssiDbm(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return 0;
  }
  const uint8_t raw =
      static_cast<uint8_t>((radio->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                           RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
  return -static_cast<int8_t>(raw);
}

void clearRadioCoreEvents(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_PLLREADY = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_RXADDRESS = 0U;
  radio->EVENTS_DEVMATCH = 0U;
  radio->EVENTS_DEVMISS = 0U;
  radio->EVENTS_MHRMATCH = 0U;
  radio->EVENTS_SYNC = 0U;
}

bool bleAddressEqual(const uint8_t* a, const uint8_t* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  return memcmp(a, b, 6U) == 0;
}

bool bleAddressLooksResolvablePrivate(const uint8_t* address) {
  if (address == nullptr) {
    return false;
  }
  return (address[5] & 0xC0U) == 0x40U;
}

uint8_t minU8(uint8_t a, uint8_t b) { return (a < b) ? a : b; }

uint16_t minU16(uint16_t a, uint16_t b) { return (a < b) ? a : b; }

bool inHandleRange(uint16_t handle, uint16_t start, uint16_t end) {
  return (handle >= start) && (handle <= end);
}

void bleUuid16ToBytes(uint16_t uuid16, uint8_t out[16]) {
  if (out == nullptr) {
    return;
  }
  memset(out, 0, 16U);
  writeLe16(out, uuid16);
}

bool bleUuidIs16(const uint8_t* uuid, uint8_t uuidSize) {
  return (uuid != nullptr) && (uuidSize == kBleUuid16Size);
}

uint16_t bleUuidRead16(const uint8_t* uuid, uint8_t uuidSize) {
  return bleUuidIs16(uuid, uuidSize) ? readLe16(uuid) : 0U;
}

bool bleUuidMatches16(const uint8_t* uuid, uint8_t uuidSize, uint16_t uuid16) {
  return bleUuidRead16(uuid, uuidSize) == uuid16;
}

bool bleUuidEquals(const uint8_t* lhs, uint8_t lhsSize, const uint8_t* rhs,
                   uint8_t rhsSize) {
  if (lhs == nullptr || rhs == nullptr || lhsSize != rhsSize) {
    return false;
  }
  return memcmp(lhs, rhs, lhsSize) == 0;
}

bool bleUuidEqualsAttWire(const uint8_t* storedUuid, uint8_t storedUuidSize,
                          const uint8_t* attWireUuid, uint8_t attWireUuidSize) {
  if (storedUuid == nullptr || attWireUuid == nullptr ||
      storedUuidSize != attWireUuidSize) {
    return false;
  }
  if (storedUuidSize == kBleUuid128Size) {
    for (uint8_t i = 0U; i < kBleUuid128Size; ++i) {
      if (storedUuid[i] != attWireUuid[kBleUuid128Size - 1U - i]) {
        return false;
      }
    }
    return true;
  }
  return memcmp(storedUuid, attWireUuid, storedUuidSize) == 0;
}

void writeUuidToAttWire(uint8_t* out, const uint8_t* uuid, uint8_t uuidSize) {
  if (out == nullptr || uuid == nullptr) {
    return;
  }
  if (uuidSize == kBleUuid128Size) {
    for (uint8_t i = 0U; i < kBleUuid128Size; ++i) {
      out[i] = uuid[kBleUuid128Size - 1U - i];
    }
    return;
  }
  memcpy(out, uuid, uuidSize);
}

uint8_t bleCharacteristicDeclarationValueLen(uint8_t uuidSize) {
  return (uuidSize == kBleUuid128Size) ? 19U : 5U;
}

void writeCharacteristicDeclarationValue(uint8_t* out,
                                         uint8_t properties,
                                         uint16_t valueHandle,
                                         const uint8_t* uuid,
                                         uint8_t uuidSize) {
  if (out == nullptr || uuid == nullptr) {
    return;
  }

  out[0] = properties;
  writeLe16(&out[1], valueHandle);
  if (uuidSize == kBleUuid128Size) {
    writeUuidToAttWire(&out[3], uuid, kBleUuid128Size);
  } else {
    writeLe16(&out[3], bleUuidRead16(uuid, uuidSize));
  }
}

extern "C" void nrf54l15_bluefruit_compat_idle_service(void)
    __attribute__((weak));
extern "C" uint32_t nrf54l15_bluefruit_compat_idle_sleep_cap_us(void)
    __attribute__((weak));

bool g_bleIdleServiceActive = false;

}  // namespace
