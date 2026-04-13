#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "nrf54l15_vpr_transport_shared.h"

#ifndef VPR_CS_DEDICATED_IMAGE
#define VPR_CS_DEDICATED_IMAGE 0
#endif

#define VPRCSR_NORDIC_VPRNORDICCTRL 0x7C0U
#define VPRCSR_NORDIC_VPRNORDICSLEEPCTRL 0x7C1U
#define VPRCSR_NORDIC_TASKS 0x7E0U
#define VPRCSR_NORDIC_EVENTS 0x7E2U
#define VPRCSR_NORDIC_VPRNORDICCTRL_ENABLERTPERIPH_Msk (1UL << 0U)
#define VPRCSR_NORDIC_VPRNORDICCTRL_NORDICKEY_Pos 16U
#define VPRCSR_NORDIC_VPRNORDICCTRL_NORDICKEY_Enabled 0x507DUL
#define VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_HIBERNATE 0xFU
#define VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_STACKONSLEEP_Msk (1UL << 17U)

#define BLE_CS_MAIN_MODE1 0x01U
#define BLE_CS_MAIN_MODE2 0x02U

#define BLE_CS_HCI_OP_READ_REMOTE_SUPPORTED_CAPABILITIES 0x208AU
#define BLE_CS_HCI_OP_SECURITY_ENABLE 0x208CU
#define BLE_CS_HCI_OP_SET_DEFAULT_SETTINGS 0x208DU
#define BLE_CS_HCI_OP_CREATE_CONFIG 0x2090U
#define BLE_CS_HCI_OP_REMOVE_CONFIG 0x2091U
#define BLE_CS_HCI_OP_SET_PROCEDURE_PARAMETERS 0x2093U
#define BLE_CS_HCI_OP_PROCEDURE_ENABLE 0x2094U
#if !VPR_CS_DEDICATED_IMAGE
#define VPR_HCI_OP_VENDOR_PING 0xFCF0U
#define VPR_HCI_OP_VENDOR_GET_TRANSPORT_INFO 0xFCF1U
#define VPR_HCI_OP_VENDOR_FNV1A32 0xFCF2U
#define VPR_HCI_OP_VENDOR_GET_CAPABILITIES 0xFCF3U
#define VPR_HCI_OP_VENDOR_CRC32 0xFCF4U
#define VPR_HCI_OP_VENDOR_CRC32C 0xFCF5U
#define VPR_HCI_OP_VENDOR_TICKER_CONFIGURE 0xFCF6U
#define VPR_HCI_OP_VENDOR_TICKER_READ_STATE 0xFCF7U
#define VPR_HCI_OP_VENDOR_ENTER_HIBERNATE 0xFCF8U
#define VPR_HCI_OP_VENDOR_TICKER_EVENT_CONFIGURE 0xFCF9U

#define VPR_VENDOR_SERVICE_VERSION_MAJOR 1U
#define VPR_VENDOR_SERVICE_VERSION_MINOR 7U
#define VPR_VENDOR_OP_PING (1UL << 0U)
#define VPR_VENDOR_OP_INFO (1UL << 1U)
#define VPR_VENDOR_OP_FNV1A32 (1UL << 2U)
#define VPR_VENDOR_OP_CAPABILITIES (1UL << 3U)
#define VPR_VENDOR_OP_CRC32 (1UL << 4U)
#define VPR_VENDOR_OP_CRC32C (1UL << 5U)
#define VPR_VENDOR_OP_TICKER_CONFIGURE (1UL << 6U)
#define VPR_VENDOR_OP_TICKER_READ_STATE (1UL << 7U)
#define VPR_VENDOR_OP_ENTER_HIBERNATE (1UL << 8U)
#define VPR_VENDOR_OP_TICKER_EVENT_CONFIGURE (1UL << 9U)
#define VPR_VENDOR_TRANSPORT_FLAG_RESTORED_FROM_HIBERNATE 0x80U
#define VPR_VENDOR_EVENT_TICKER 0xA0U
#endif
#define VPR_VENDOR_EVENT_CS_PEER_RESULT_TRIGGER 0xB1U
#define VPR_VENDOR_EVENT_CS_PEER_RESULT_SOURCE 0xB2U
#if !VPR_CS_DEDICATED_IMAGE
#define VPR_TICKER_EVENT_QUEUE_DEPTH 8U
#endif

#define BLE_CS_HCI_EVT_READ_REMOTE_SUPPORTED_CAPS_COMPLETE_V2 0x38U
#define BLE_CS_HCI_EVT_SECURITY_ENABLE_COMPLETE 0x2EU
#define BLE_CS_HCI_EVT_CONFIG_COMPLETE 0x2FU
#define BLE_CS_HCI_EVT_PROCEDURE_ENABLE_COMPLETE 0x30U
#define BLE_CS_HCI_EVT_SUBEVENT_RESULT 0x31U
#define BLE_CS_HCI_EVT_SUBEVENT_RESULT_CONTINUE 0x32U

#define BLE_HCI_PACKET_TYPE_EVENT 0x04U
#define BLE_HCI_EVT_COMMAND_COMPLETE 0x0EU
#define BLE_HCI_EVT_COMMAND_STATUS 0x0FU
#define BLE_HCI_EVT_LE_META 0x3EU
#define BLE_HCI_EVT_VENDOR 0xFFU

extern uint8_t __stack_top[];

static volatile Nrf54l15VprTransportHostShared* const g_host_transport =
    (volatile Nrf54l15VprTransportHostShared*)(uintptr_t)NRF54L15_VPR_TRANSPORT_HOST_BASE;
static volatile Nrf54l15VprTransportVprShared* const g_vpr_transport =
    (volatile Nrf54l15VprTransportVprShared*)(uintptr_t)NRF54L15_VPR_TRANSPORT_VPR_BASE;

typedef struct {
  uint32_t count;
  uint32_t step;
  uint32_t heartbeat;
  uint32_t sequence;
  uint8_t flags;
} vpr_ticker_event_entry_t;

#if !VPR_CS_DEDICATED_IMAGE
static uint32_t g_ticker_enabled = 0U;
static uint32_t g_ticker_period_ticks = 0U;
static uint32_t g_ticker_step = 1U;
static uint32_t g_ticker_accum = 0U;
static uint32_t g_ticker_count = 0U;
static uint32_t g_ticker_event_enabled = 0U;
static uint32_t g_ticker_event_emit_every = 0U;
static uint32_t g_ticker_event_last_emitted_count = 0U;
static uint32_t g_ticker_event_sequence = 0U;
static uint32_t g_ticker_event_drop_count = 0U;
static vpr_ticker_event_entry_t g_ticker_event_queue[VPR_TICKER_EVENT_QUEUE_DEPTH];
static uint32_t g_ticker_event_queue_head = 0U;
static uint32_t g_ticker_event_queue_tail = 0U;
static uint32_t g_ticker_event_queue_count = 0U;
#endif
static uint8_t g_pending_cs_result_stage = 0U;
#if VPR_CS_DEDICATED_IMAGE
static uint32_t g_cs_next_procedure_heartbeat = 0U;
static uint32_t g_cs_next_peer_stage_heartbeat = 0U;
static uint32_t g_cs_next_chunk_stage_heartbeat = 0U;
static uint8_t g_cs_last_peer_gap_ticks = 0U;
static uint8_t g_cs_last_interval_selector = 0U;
static uint8_t g_cs_local_chunk_start_step = 0U;
static uint8_t g_cs_peer_chunk_start_step = 0U;
typedef struct __attribute__((packed)) {
  uint8_t configId;
  uint16_t procedureCounter;
  uint16_t sessionConnHandle;
  uint32_t demoChannelsPacked;
  uint8_t mainModeType;
  uint8_t subModeType;
  uint8_t minMainModeSteps;
  uint8_t maxMainModeSteps;
  uint8_t mainModeRepetition;
  uint8_t mode0Steps;
  uint8_t role;
  uint8_t rttType;
  uint8_t csSyncPhy;
  uint8_t channelMap[10];
  uint8_t channelMapRepetition;
  uint8_t channelSelectionType;
  uint8_t ch3cShape;
  uint8_t ch3cJump;
  uint8_t enhancements1;
  int8_t maxTxPowerDbm;
  uint16_t maxProcedureLen;
  uint16_t minProcedureInterval;
  uint16_t maxProcedureInterval;
  uint16_t maxProcedureCount;
  uint32_t minSubeventLen;
  uint32_t maxSubeventLen;
  uint8_t toneAntennaConfigSelection;
  uint8_t phy;
  int8_t txPowerDelta;
  uint8_t configCreated;
  uint8_t securityEnabled;
  uint8_t procedureParamsApplied;
  uint8_t procedureEnabled;
  uint8_t sessionOpen;
} vpr_cs_dedicated_state_t;

static vpr_cs_dedicated_state_t g_cs_state = {
    .configId = 1U,
    .procedureCounter = 0U,
    .sessionConnHandle = 0U,
    .demoChannelsPacked = 0x241A0E02U,
    .mainModeType = BLE_CS_MAIN_MODE2,
    .subModeType = 0xFFU,
    .minMainModeSteps = 3U,
    .maxMainModeSteps = 5U,
    .mainModeRepetition = 1U,
    .mode0Steps = 1U,
    .role = 0U,
    .rttType = 1U,
    .csSyncPhy = 2U,
    .channelMap = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x1FU,
                   0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
    .channelMapRepetition = 1U,
    .channelSelectionType = 1U,
    .ch3cShape = 1U,
    .ch3cJump = 3U,
    .enhancements1 = 0x01U,
    .maxTxPowerDbm = -8,
    .maxProcedureLen = 12U,
    .minProcedureInterval = 200U,
    .maxProcedureInterval = 300U,
    .maxProcedureCount = 8U,
    .minSubeventLen = 0x000456UL,
    .maxSubeventLen = 0x000678UL,
    .toneAntennaConfigSelection = 2U,
    .phy = 2U,
    .txPowerDelta = -6,
    .configCreated = 0U,
    .securityEnabled = 0U,
    .procedureParamsApplied = 0U,
    .procedureEnabled = 0U,
    .sessionOpen = 0U,
};

#define g_cs_config_id g_cs_state.configId
#define g_cs_procedure_counter g_cs_state.procedureCounter
#define g_cs_session_conn_handle g_cs_state.sessionConnHandle
#define g_cs_demo_channels_packed g_cs_state.demoChannelsPacked
#define g_cs_main_mode_type g_cs_state.mainModeType
#define g_cs_sub_mode_type g_cs_state.subModeType
#define g_cs_min_main_mode_steps g_cs_state.minMainModeSteps
#define g_cs_max_main_mode_steps g_cs_state.maxMainModeSteps
#define g_cs_main_mode_repetition g_cs_state.mainModeRepetition
#define g_cs_mode0_steps g_cs_state.mode0Steps
#define g_cs_role g_cs_state.role
#define g_cs_rtt_type g_cs_state.rttType
#define g_cs_cs_sync_phy g_cs_state.csSyncPhy
#define g_cs_channel_map g_cs_state.channelMap
#define g_cs_channel_map_repetition g_cs_state.channelMapRepetition
#define g_cs_channel_selection_type g_cs_state.channelSelectionType
#define g_cs_ch3c_shape g_cs_state.ch3cShape
#define g_cs_ch3c_jump g_cs_state.ch3cJump
#define g_cs_enhancements1 g_cs_state.enhancements1
#define g_cs_max_tx_power_dbm g_cs_state.maxTxPowerDbm
#define g_cs_max_procedure_len g_cs_state.maxProcedureLen
#define g_cs_min_procedure_interval g_cs_state.minProcedureInterval
#define g_cs_max_procedure_interval g_cs_state.maxProcedureInterval
#define g_cs_max_procedure_count g_cs_state.maxProcedureCount
#define g_cs_min_subevent_len g_cs_state.minSubeventLen
#define g_cs_max_subevent_len g_cs_state.maxSubeventLen
#define g_cs_tone_antenna_config_selection g_cs_state.toneAntennaConfigSelection
#define g_cs_phy g_cs_state.phy
#define g_cs_tx_power_delta g_cs_state.txPowerDelta
#define g_cs_config_created g_cs_state.configCreated
#define g_cs_security_enabled g_cs_state.securityEnabled
#define g_cs_procedure_params_applied g_cs_state.procedureParamsApplied
#define g_cs_procedure_enabled g_cs_state.procedureEnabled
#define g_cs_session_open g_cs_state.sessionOpen
#endif
#if !VPR_CS_DEDICATED_IMAGE
static uint32_t g_pending_hibernate = 0U;
static uint32_t g_restored_from_hibernate = 0U;
#endif

static bool host_request_pending(void);

#if VPR_CS_DEDICATED_IMAGE
enum {
  BLE_CS_HCI_STATUS_SUCCESS = 0x00U,
  BLE_CS_HCI_STATUS_COMMAND_DISALLOWED = 0x0CU,
  BLE_CS_HCI_STATUS_INVALID_PARAMS = 0x12U,
};

static void clear_active_cs_state(void);
#endif

static inline void fence_rw(void) {
  __asm__ volatile("fence rw, rw" ::: "memory");
}

static inline void enable_machine_interrupts(void) {
  const uint32_t mie_mask = 0x8U;
  __asm__ volatile("csrs mstatus, %0" : : "r"(mie_mask) : "memory");
}

static inline void write_vpr_sleepctrl(uint32_t value) {
  __asm__ volatile("csrw %0, %1" : : "i"(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL), "r"(value));
}

static inline void write_vpr_nordic_ctrl(uint32_t value) {
  __asm__ volatile("csrw %0, %1" : : "i"(VPRCSR_NORDIC_VPRNORDICCTRL), "r"(value));
}

static void bytes_zero(void *dst, size_t len) {
  uint8_t *out = (uint8_t *)dst;
  if (out == NULL) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    out[i] = 0U;
  }
}

static void bytes_copy(void *dst, const void *src, size_t len) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  if (out == NULL || in == NULL) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    out[i] = in[i];
  }
}

#if VPR_CS_DEDICATED_IMAGE
static void reset_dedicated_cs_state(void) {
  g_cs_config_id = 1U;
  g_cs_procedure_counter = 0U;
  g_cs_session_conn_handle = 0U;
  g_cs_demo_channels_packed = 0x241A0E02U;
  g_cs_main_mode_type = BLE_CS_MAIN_MODE2;
  g_cs_sub_mode_type = 0xFFU;
  g_cs_min_main_mode_steps = 3U;
  g_cs_max_main_mode_steps = 5U;
  g_cs_main_mode_repetition = 1U;
  g_cs_mode0_steps = 1U;
  g_cs_role = 0U;
  g_cs_rtt_type = 1U;
  g_cs_cs_sync_phy = 2U;
  g_cs_channel_map[0] = 0xFFU;
  g_cs_channel_map[1] = 0xFFU;
  g_cs_channel_map[2] = 0xFFU;
  g_cs_channel_map[3] = 0xFFU;
  g_cs_channel_map[4] = 0x1FU;
  g_cs_channel_map[5] = 0x00U;
  g_cs_channel_map[6] = 0x00U;
  g_cs_channel_map[7] = 0x00U;
  g_cs_channel_map[8] = 0x00U;
  g_cs_channel_map[9] = 0x00U;
  g_cs_channel_map_repetition = 1U;
  g_cs_channel_selection_type = 1U;
  g_cs_ch3c_shape = 1U;
  g_cs_ch3c_jump = 3U;
  g_cs_enhancements1 = 0x01U;
  g_cs_max_tx_power_dbm = -8;
  g_cs_max_procedure_len = 12U;
  g_cs_min_procedure_interval = 200U;
  g_cs_max_procedure_interval = 300U;
  g_cs_max_procedure_count = 8U;
  g_cs_min_subevent_len = 0x000456UL;
  g_cs_max_subevent_len = 0x000678UL;
  g_cs_tone_antenna_config_selection = 2U;
  g_cs_phy = 2U;
  g_cs_tx_power_delta = -6;
  g_cs_config_created = 0U;
  g_cs_security_enabled = 0U;
  g_cs_procedure_params_applied = 0U;
  g_cs_procedure_enabled = 0U;
  g_cs_session_open = 0U;
  g_cs_next_procedure_heartbeat = 0U;
  g_cs_next_peer_stage_heartbeat = 0U;
  g_cs_next_chunk_stage_heartbeat = 0U;
  g_cs_last_peer_gap_ticks = 0U;
  g_cs_last_interval_selector = 0U;
  g_cs_local_chunk_start_step = 0U;
  g_cs_peer_chunk_start_step = 0U;
}
#endif

static void zero_vpr_data(void) {
  for (uint32_t i = 0U; i < NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA; ++i) {
    g_vpr_transport->vprData[i] = 0U;
  }
}

#if !VPR_CS_DEDICATED_IMAGE
static void clear_ticker_event_queue(void) {
  bytes_zero(g_ticker_event_queue, sizeof(g_ticker_event_queue));
  g_ticker_event_queue_head = 0U;
  g_ticker_event_queue_tail = 0U;
  g_ticker_event_queue_count = 0U;
}

static bool enqueue_ticker_event(uint32_t count, uint32_t step, uint32_t heartbeat) {
  if (g_ticker_event_queue_count >= VPR_TICKER_EVENT_QUEUE_DEPTH) {
    g_ticker_event_drop_count = g_ticker_event_drop_count + 1U;
    return false;
  }
  vpr_ticker_event_entry_t *entry = &g_ticker_event_queue[g_ticker_event_queue_tail];
  entry->flags = 0U;
  entry->count = count;
  entry->step = step;
  entry->heartbeat = heartbeat;
  g_ticker_event_sequence = g_ticker_event_sequence + 1U;
  entry->sequence = g_ticker_event_sequence;
  g_ticker_event_queue_tail = (g_ticker_event_queue_tail + 1U) % VPR_TICKER_EVENT_QUEUE_DEPTH;
  g_ticker_event_queue_count = g_ticker_event_queue_count + 1U;
  g_ticker_event_last_emitted_count = count;
  return true;
}

static void clear_retained_hibernate_state(void) {
  g_host_transport->hibernateCookie = 0U;
  g_host_transport->hibernateFlags = 0U;
  g_host_transport->retainedTickerPeriodTicks = 0U;
  g_host_transport->retainedTickerStep = 0U;
  g_host_transport->retainedTickerAccum = 0U;
  g_host_transport->retainedTickerCount = 0U;
}

static void persist_hibernate_state(void) {
  uint32_t flags = NRF54L15_VPR_TRANSPORT_HIBERNATE_FLAG_TICKER_VALID;
  if (g_ticker_enabled != 0U) {
    flags |= NRF54L15_VPR_TRANSPORT_HIBERNATE_FLAG_TICKER_ENABLED;
  }
  g_host_transport->hibernateCookie = NRF54L15_VPR_TRANSPORT_HIBERNATE_COOKIE;
  g_host_transport->hibernateFlags = flags;
  g_host_transport->retainedTickerPeriodTicks = g_ticker_period_ticks;
  g_host_transport->retainedTickerStep = g_ticker_step;
  g_host_transport->retainedTickerAccum = g_ticker_accum;
  g_host_transport->retainedTickerCount = g_ticker_count;
}

static bool restore_hibernate_state(void) {
  if (g_host_transport->hibernateCookie != NRF54L15_VPR_TRANSPORT_HIBERNATE_COOKIE) {
    return false;
  }
  const uint32_t flags = g_host_transport->hibernateFlags;
  if ((flags & NRF54L15_VPR_TRANSPORT_HIBERNATE_FLAG_TICKER_VALID) == 0U) {
    clear_retained_hibernate_state();
    return false;
  }
  g_ticker_enabled =
      (flags & NRF54L15_VPR_TRANSPORT_HIBERNATE_FLAG_TICKER_ENABLED) != 0U ? 1U : 0U;
  g_ticker_period_ticks = g_host_transport->retainedTickerPeriodTicks;
  g_ticker_step = g_host_transport->retainedTickerStep;
  if (g_ticker_step == 0U) {
    g_ticker_step = 1U;
  }
  g_ticker_accum = g_host_transport->retainedTickerAccum;
  g_ticker_count = g_host_transport->retainedTickerCount;
  clear_retained_hibernate_state();
  return true;
}
#endif

static void write_le16(uint8_t *dst, uint16_t value) {
  if (dst == NULL) {
    return;
  }
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static uint16_t read_le16(const volatile uint8_t *src) {
  if (src == NULL) {
    return 0U;
  }
  return (uint16_t)src[0] | ((uint16_t)src[1] << 8U);
}

static void write_le24(uint8_t *dst, uint32_t value) {
  if (dst == NULL) {
    return;
  }
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
}

static uint32_t read_le24(const volatile uint8_t *src) {
  if (src == NULL) {
    return 0U;
  }
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8U) | ((uint32_t)src[2] << 16U);
}

static void write_le32(uint8_t *dst, uint32_t value) {
  if (dst == NULL) {
    return;
  }
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
  dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint16_t read_conn_handle(void) {
  if (g_host_transport->hostLen < 6U) {
    return 0x0040U;
  }
  return (uint16_t)g_host_transport->hostData[4] |
         ((uint16_t)g_host_transport->hostData[5] << 8U);
}

static uint16_t current_conn_handle(void) {
#if VPR_CS_DEDICATED_IMAGE
  return (g_cs_session_open != 0U) ? g_cs_session_conn_handle : read_conn_handle();
#else
  return read_conn_handle();
#endif
}

#if VPR_CS_DEDICATED_IMAGE
static uint32_t current_link_state_packed(void) {
  return ((uint32_t)(g_cs_session_conn_handle & 0x0FFFU)) |
         (((uint32_t)g_cs_last_interval_selector & 0x0FU) << 12U) |
         ((g_cs_session_open != 0U ? 1UL : 0UL) << 16U) |
         ((g_cs_config_created != 0U ? 1UL : 0UL) << 17U) |
         ((g_cs_security_enabled != 0U ? 1UL : 0UL) << 18U) |
         ((g_cs_procedure_params_applied != 0U ? 1UL : 0UL) << 19U) |
         ((g_cs_procedure_enabled != 0U ? 1UL : 0UL) << 20U) |
         ((uint32_t)g_cs_config_id << 21U) |
         (((uint32_t)g_cs_last_peer_gap_ticks & 0x07U) << 29U);
}

static bool command_conn_handle_matches_active_link(void) {
  return (g_cs_session_open != 0U) && (read_conn_handle() == g_cs_session_conn_handle);
}
#endif

static size_t append_h4_command_status(uint8_t *dst, size_t max_len, uint16_t opcode,
                                       uint8_t status) {
  if (dst == NULL || max_len < 7U) {
    return 0U;
  }
  dst[0] = BLE_HCI_PACKET_TYPE_EVENT;
  dst[1] = BLE_HCI_EVT_COMMAND_STATUS;
  dst[2] = 4U;
  dst[3] = status;
  dst[4] = 1U;
  write_le16(&dst[5], opcode);
  return 7U;
}

static size_t append_h4_command_complete(uint8_t *dst, size_t max_len, uint16_t opcode,
                                         uint8_t status) {
  if (dst == NULL || max_len < 7U) {
    return 0U;
  }
  dst[0] = BLE_HCI_PACKET_TYPE_EVENT;
  dst[1] = BLE_HCI_EVT_COMMAND_COMPLETE;
  dst[2] = 4U;
  dst[3] = 1U;
  write_le16(&dst[4], opcode);
  dst[6] = status;
  return 7U;
}

static size_t append_h4_command_complete_payload(uint8_t *dst, size_t max_len,
                                                 uint16_t opcode, const uint8_t *payload,
                                                 size_t payload_len) {
  if (dst == NULL || (payload_len != 0U && payload == NULL) || max_len < (6U + payload_len)) {
    return 0U;
  }
  dst[0] = BLE_HCI_PACKET_TYPE_EVENT;
  dst[1] = BLE_HCI_EVT_COMMAND_COMPLETE;
  dst[2] = (uint8_t)(3U + payload_len);
  dst[3] = 1U;
  write_le16(&dst[4], opcode);
  bytes_copy(&dst[6], payload, payload_len);
  return 6U + payload_len;
}

static size_t append_h4_le_meta(uint8_t *dst, size_t max_len, uint8_t subevent_code,
                                const uint8_t *payload, size_t payload_len) {
  if (dst == NULL || payload == NULL || max_len < (4U + payload_len)) {
    return 0U;
  }
  dst[0] = BLE_HCI_PACKET_TYPE_EVENT;
  dst[1] = BLE_HCI_EVT_LE_META;
  dst[2] = (uint8_t)(1U + payload_len);
  dst[3] = subevent_code;
  bytes_copy(&dst[4], payload, payload_len);
  return 4U + payload_len;
}

static size_t append_h4_vendor_event(uint8_t *dst, size_t max_len, uint8_t subevent_code,
                                     const uint8_t *payload, size_t payload_len) {
  if (dst == NULL || payload == NULL || max_len < (4U + payload_len)) {
    return 0U;
  }
  dst[0] = BLE_HCI_PACKET_TYPE_EVENT;
  dst[1] = BLE_HCI_EVT_VENDOR;
  dst[2] = (uint8_t)(1U + payload_len);
  dst[3] = subevent_code;
  bytes_copy(&dst[4], payload, payload_len);
  return 4U + payload_len;
}

static const uint8_t k_local_demo_pct_sample[3] = {0x00U, 0x04U, 0x00U};
#if VPR_CS_DEDICATED_IMAGE
static uint16_t current_step_count_group(void);
static uint16_t current_channel_selection_group(void);
enum {
  VPR_CS_TONE_QUALITY_HIGH = 0x00U,
  VPR_CS_TONE_QUALITY_MEDIUM = 0x01U,
};
static const uint8_t k_peer_demo_pct_samples[39][3] = {
    {0xF0U, 0xB3U, 0xF4U}, {0xE2U, 0xC3U, 0xF0U}, {0xD1U, 0xE3U, 0xECU},
    {0xBCU, 0x13U, 0xE9U}, {0xA3U, 0x63U, 0xE5U}, {0x86U, 0xC3U, 0xE1U},
    {0x66U, 0x43U, 0xDEU}, {0x43U, 0xF3U, 0xDAU}, {0x1CU, 0xB3U, 0xD7U},
    {0xF2U, 0xB2U, 0xD4U}, {0xC4U, 0xD2U, 0xD1U}, {0x62U, 0xA2U, 0xCCU},
    {0x2DU, 0x52U, 0xCAU}, {0xF6U, 0x41U, 0xC8U}, {0xBDU, 0x61U, 0xC6U},
    {0x82U, 0xC1U, 0xC4U}, {0x46U, 0x51U, 0xC3U}, {0x08U, 0x31U, 0xC2U},
    {0xCAU, 0x40U, 0xC1U}, {0x8AU, 0x90U, 0xC0U}, {0x4AU, 0x30U, 0xC0U},
    {0x0AU, 0x00U, 0xC0U}, {0xC9U, 0x1FU, 0xC0U}, {0x89U, 0x7FU, 0xC0U},
    {0x4AU, 0x0FU, 0xC1U}, {0x0BU, 0xEFU, 0xC1U}, {0xCDU, 0xFEU, 0xC2U},
    {0x90U, 0x4EU, 0xC4U}, {0x55U, 0xDEU, 0xC5U}, {0x1BU, 0xAEU, 0xC7U},
    {0xE3U, 0xADU, 0xC9U}, {0xAEU, 0xEDU, 0xCBU}, {0x7AU, 0x5DU, 0xCEU},
    {0x4AU, 0xFDU, 0xD0U}, {0x1CU, 0xCDU, 0xD3U}, {0xF1U, 0xCCU, 0xD6U},
    {0xC9U, 0xFCU, 0xD9U}, {0xA4U, 0x4CU, 0xDDU}, {0x83U, 0xBCU, 0xE0U},
};

static int16_t clamp_pct12_component(int32_t value) {
  if (value < -2048) {
    return -2048;
  }
  if (value > 2047) {
    return 2047;
  }
  return (int16_t)value;
}

static int16_t decode_pct12_component(uint16_t bits) {
  return (int16_t)((bits ^ (1U << 11U)) - (1U << 11U));
}

static void decode_pct_sample_bytes(const uint8_t pct[3], int16_t *out_i, int16_t *out_q) {
  uint32_t packed = 0U;
  if (pct == NULL || out_i == NULL || out_q == NULL) {
    return;
  }
  packed = (uint32_t)pct[0] | ((uint32_t)pct[1] << 8U) | ((uint32_t)pct[2] << 16U);
  *out_i = decode_pct12_component((uint16_t)(packed & 0x0FFFU));
  *out_q = decode_pct12_component((uint16_t)((packed >> 12U) & 0x0FFFU));
}

static void encode_pct_sample_bytes(int16_t i, int16_t q, uint8_t out_pct[3]) {
  uint16_t i12 = 0U;
  uint16_t q12 = 0U;
  uint32_t packed = 0U;
  if (out_pct == NULL) {
    return;
  }
  i12 = (uint16_t)clamp_pct12_component(i) & 0x0FFFU;
  q12 = (uint16_t)clamp_pct12_component(q) & 0x0FFFU;
  packed = (uint32_t)i12 | ((uint32_t)q12 << 12U);
  out_pct[0] = (uint8_t)(packed & 0xFFU);
  out_pct[1] = (uint8_t)((packed >> 8U) & 0xFFU);
  out_pct[2] = (uint8_t)((packed >> 16U) & 0xFFU);
}

static void scale_pct_sample_bytes(const uint8_t input_pct[3], uint16_t scale_q10,
                                   uint8_t out_pct[3]) {
  int16_t i = 0;
  int16_t q = 0;
  int32_t scaled_i = 0;
  int32_t scaled_q = 0;
  if (input_pct == NULL || out_pct == NULL) {
    return;
  }
  decode_pct_sample_bytes(input_pct, &i, &q);
  scaled_i = ((int32_t)i * (int32_t)scale_q10 + 512) / 1024;
  scaled_q = ((int32_t)q * (int32_t)scale_q10 + 512) / 1024;
  encode_pct_sample_bytes(clamp_pct12_component(scaled_i),
                          clamp_pct12_component(scaled_q), out_pct);
}

static void rotate_pct_sample_bytes_quarter_turns(const uint8_t input_pct[3], uint8_t quarter_turns,
                                                  uint8_t out_pct[3]) {
  int16_t i = 0;
  int16_t q = 0;
  int16_t rotated_i = 0;
  int16_t rotated_q = 0;
  if (input_pct == NULL || out_pct == NULL) {
    return;
  }
  decode_pct_sample_bytes(input_pct, &i, &q);
  switch (quarter_turns & 0x03U) {
    case 0U:
      rotated_i = i;
      rotated_q = q;
      break;
    case 1U:
      rotated_i = clamp_pct12_component(-(int32_t)q);
      rotated_q = i;
      break;
    case 2U:
      rotated_i = clamp_pct12_component(-(int32_t)i);
      rotated_q = clamp_pct12_component(-(int32_t)q);
      break;
    default:
      rotated_i = q;
      rotated_q = clamp_pct12_component(-(int32_t)i);
      break;
  }
  encode_pct_sample_bytes(rotated_i, rotated_q, out_pct);
}

static uint16_t current_local_demo_scale_q10(uint8_t step_index) {
  uint16_t scale = ((step_index & 0x01U) != 0U) ? 736U : 896U;
  scale = (uint16_t)(scale + ((current_step_count_group() & 0x03U) * 32U));
  if (scale > 1024U) {
    scale = 1024U;
  }
  return scale;
}

static uint8_t current_local_demo_phase_quadrant(uint8_t step_index) {
  return (uint8_t)((current_step_count_group() + current_channel_selection_group() +
                    step_index) &
                   0x03U);
}

static uint16_t current_peer_demo_scale_q10(uint8_t channel, uint8_t step_index) {
  uint16_t attenuation = 0U;
  uint16_t channel_offset = 0U;
  int32_t scale = (int32_t)current_local_demo_scale_q10(step_index);

  if (g_cs_tx_power_delta < 0) {
    attenuation = (uint16_t)((uint8_t)(-g_cs_tx_power_delta) * 12U);
  }
  channel_offset =
      (uint16_t)(((channel + (uint8_t)current_channel_selection_group()) % 3U) * 24U);
  scale -= (int32_t)attenuation;
  scale += (int32_t)channel_offset - 24;
  if (scale < 512) {
    scale = 512;
  }
  if (scale > 1024) {
    scale = 1024;
  }
  return (uint16_t)scale;
}
#endif

static uint8_t current_demo_antenna_permutation(uint8_t step_index) {
  uint8_t count = g_cs_tone_antenna_config_selection;
  if (count == 0U) {
    count = 1U;
  }
  if (count > 4U) {
    count = 4U;
  }
  return (uint8_t)((current_step_count_group() + step_index) % count);
}

static void append_mode2_sample_step(uint8_t *dst, uint8_t channel, uint8_t permutation_index,
                                     uint8_t quality, const uint8_t pct[3]) {
  if (dst == NULL || pct == NULL) {
    return;
  }
  dst[0] = BLE_CS_MAIN_MODE2;
  dst[1] = channel;
  dst[2] = 5U;
  dst[3] = permutation_index;
  dst[4] = pct[0];
  dst[5] = pct[1];
  dst[6] = pct[2];
  dst[7] = (uint8_t)(quality & 0x0FU);
}

static size_t append_mode1_unavailable_step(uint8_t *dst, uint8_t channel,
                                            uint8_t packet_antenna) {
  if (dst == NULL) {
    return 0U;
  }
  dst[0] = BLE_CS_MAIN_MODE1;
  dst[1] = channel;
  dst[2] = 6U;
  dst[3] = 0xF2U;
  dst[4] = 0xFFU;
  dst[5] = 0x7FU;
  write_le16(&dst[6], 0x8000U);
  dst[8] = packet_antenna;
  return 9U;
}

static void append_mode2_demo_step(uint8_t *dst, uint8_t channel, uint8_t step_index) {
#if VPR_CS_DEDICATED_IMAGE
  uint8_t scaled_pct[3];
  uint8_t shaped_pct[3];
  scale_pct_sample_bytes(k_local_demo_pct_sample, current_local_demo_scale_q10(step_index),
                         scaled_pct);
  rotate_pct_sample_bytes_quarter_turns(scaled_pct, current_local_demo_phase_quadrant(step_index),
                                        shaped_pct);
  append_mode2_sample_step(dst, channel, current_demo_antenna_permutation(step_index),
                           ((step_index & 0x01U) != 0U) ? VPR_CS_TONE_QUALITY_MEDIUM
                                                        : VPR_CS_TONE_QUALITY_HIGH,
                           shaped_pct);
#else
  append_mode2_sample_step(dst, channel, current_demo_antenna_permutation(step_index),
                           ((step_index & 0x01U) != 0U) ? VPR_CS_TONE_QUALITY_MEDIUM
                                                        : VPR_CS_TONE_QUALITY_HIGH,
                           k_local_demo_pct_sample);
#endif
}

#if VPR_CS_DEDICATED_IMAGE
static void append_mode2_peer_demo_step(uint8_t *dst, uint8_t channel, uint8_t step_index) {
  uint8_t scaled_pct[3];
  uint8_t shaped_pct[3];
  const uint8_t *pct =
      (channel < 39U) ? k_peer_demo_pct_samples[channel] : k_local_demo_pct_sample;
  const uint8_t phase_quadrant = current_local_demo_phase_quadrant(step_index);
  scale_pct_sample_bytes(pct, current_peer_demo_scale_q10(channel, step_index), scaled_pct);
  rotate_pct_sample_bytes_quarter_turns(scaled_pct, (uint8_t)((4U - phase_quadrant) & 0x03U),
                                        shaped_pct);
  append_mode2_sample_step(dst, channel, current_demo_antenna_permutation(step_index),
                           ((step_index & 0x01U) != 0U) ? VPR_CS_TONE_QUALITY_MEDIUM
                                                        : VPR_CS_TONE_QUALITY_HIGH,
                           shaped_pct);
}

static bool channel_map_bit_enabled(const uint8_t *channel_map, uint8_t bit) {
  if (channel_map == NULL || bit >= 80U) {
    return false;
  }
  return (channel_map[bit >> 3U] & (uint8_t)(1U << (bit & 0x07U))) != 0U;
}

static bool channel_map_has_enabled_channels(const uint8_t *channel_map) {
  if (channel_map == NULL) {
    return false;
  }
  for (uint8_t i = 0U; i < 10U; ++i) {
    if (channel_map[i] != 0U) {
      return true;
    }
  }
  return false;
}

static uint16_t current_channel_selection_group(void) {
  uint16_t procedure_index = (g_cs_procedure_counter > 0U) ? (uint16_t)(g_cs_procedure_counter - 1U)
                                                           : 0U;
  if (g_cs_channel_map_repetition > 1U) {
    procedure_index = (uint16_t)(procedure_index / g_cs_channel_map_repetition);
  }
  return procedure_index;
}

static uint16_t current_step_count_group(void) {
  uint16_t procedure_index = (g_cs_procedure_counter > 0U) ? (uint16_t)(g_cs_procedure_counter - 1U)
                                                           : 0U;
  if (g_cs_main_mode_repetition > 1U) {
    procedure_index = (uint16_t)(procedure_index / g_cs_main_mode_repetition);
  }
  return procedure_index;
}

static uint8_t current_demo_num_antenna_paths(void);

static uint8_t current_demo_step_count(void) {
  uint8_t min_steps = (g_cs_min_main_mode_steps != 0U) ? g_cs_min_main_mode_steps : 1U;
  uint8_t max_steps = (g_cs_max_main_mode_steps >= min_steps) ? g_cs_max_main_mode_steps
                                                              : min_steps;
  uint8_t count = min_steps;
  if (max_steps > min_steps) {
    const uint8_t span = (uint8_t)(max_steps - min_steps + 1U);
    count = (uint8_t)(min_steps + (current_step_count_group() % span));
  }
  if (count > 6U) {
    count = 6U;
  }
  return count;
}

static bool current_demo_has_mode1_timing_step(void) {
  return (g_cs_rtt_type != 0U) && (current_demo_step_count() > 3U);
}

static uint8_t current_demo_total_step_count(void) {
  return (uint8_t)(current_demo_step_count() + (current_demo_has_mode1_timing_step() ? 1U : 0U));
}

static size_t current_demo_encoded_step_len(uint8_t step_index) {
  return (current_demo_has_mode1_timing_step() && step_index == 0U) ? 9U : 8U;
}

static size_t current_demo_total_encoded_step_bytes(void) {
  size_t total = 0U;
  const uint8_t total_steps = current_demo_total_step_count();
  for (uint8_t step_index = 0U; step_index < total_steps; ++step_index) {
    total += current_demo_encoded_step_len(step_index);
  }
  return total;
}

static uint8_t current_demo_phase_step_index(uint8_t step_index) {
  return (uint8_t)(step_index - (current_demo_has_mode1_timing_step() ? 1U : 0U));
}

static size_t append_local_demo_step(uint8_t *dst, const uint8_t *channels, uint8_t step_index) {
  if (dst == NULL || channels == NULL) {
    return 0U;
  }
  if (current_demo_has_mode1_timing_step() && step_index == 0U) {
    return append_mode1_unavailable_step(dst, channels[0], current_demo_num_antenna_paths());
  }
  const uint8_t phase_step_index = current_demo_phase_step_index(step_index);
  append_mode2_demo_step(dst, channels[phase_step_index], phase_step_index);
  return 8U;
}

static size_t append_peer_demo_step(uint8_t *dst, const uint8_t *channels, uint8_t step_index) {
  if (dst == NULL || channels == NULL) {
    return 0U;
  }
  if (current_demo_has_mode1_timing_step() && step_index == 0U) {
    return append_mode1_unavailable_step(dst, channels[0], current_demo_num_antenna_paths());
  }
  const uint8_t phase_step_index = current_demo_phase_step_index(step_index);
  append_mode2_peer_demo_step(dst, channels[phase_step_index], phase_step_index);
  return 8U;
}

static uint8_t count_demo_steps_for_budget(size_t budget, uint8_t start_step_index) {
  const uint8_t total_steps = current_demo_total_step_count();
  size_t used = 0U;
  uint8_t count = 0U;
  for (uint8_t step_index = start_step_index; step_index < total_steps; ++step_index) {
    const size_t step_len = current_demo_encoded_step_len(step_index);
    if (used + step_len > budget) {
      break;
    }
    used += step_len;
    ++count;
  }
  return count;
}

static size_t current_demo_initial_chunk_budget(void) {
#if VPR_CS_DEDICATED_IMAGE
  if (current_demo_total_encoded_step_bytes() > 32U) {
    if (g_cs_min_subevent_len <= 0x000180UL) {
      return 9U;
    }
    return 17U;
  }
#endif
  return 25U;
}

static size_t current_demo_continue_chunk_budget(uint8_t start_step_index) {
  (void)start_step_index;
#if VPR_CS_DEDICATED_IMAGE
  if (current_demo_total_encoded_step_bytes() > 32U) {
    if (g_cs_min_subevent_len <= 0x000180UL) {
      return 8U;
    }
    return 16U;
  }
#endif
  return 32U;
}

static uint16_t current_demo_acl_event_counter(void) {
  return (uint16_t)(0x1200U + (current_channel_selection_group() * 0x10U) +
                    current_step_count_group());
}

static uint16_t current_demo_frequency_compensation(void) {
  return (uint16_t)(0x0010U + ((current_channel_selection_group() & 0x0FU) << 2U) +
                    (current_step_count_group() & 0x03U));
}

static int8_t current_demo_reference_power_dbm(void) {
  int16_t dbm = (int16_t)g_cs_max_tx_power_dbm + (int16_t)g_cs_tx_power_delta;
  if (dbm < -127) {
    dbm = -127;
  }
  if (dbm > 20) {
    dbm = 20;
  }
  return (int8_t)dbm;
}

static uint8_t current_demo_num_antenna_paths(void) {
  uint8_t count = g_cs_tone_antenna_config_selection;
  if (count == 0U) {
    count = 1U;
  }
  if (count > 4U) {
    count = 4U;
  }
  return count;
}

static uint8_t fill_demo_channels_for_procedure(uint8_t *out_channels, uint8_t channel_count) {
  const uint32_t fallback_packed =
      (g_cs_demo_channels_packed != 0U) ? g_cs_demo_channels_packed : g_host_transport->reserved;
  uint8_t enabled[40];
  uint8_t count = 0U;
  uint8_t jump = 1U;
  uint8_t start = 0U;

  if (out_channels == NULL || channel_count == 0U) {
    return 0U;
  }

  for (uint8_t bit = 0U; bit < 40U; ++bit) {
    if (channel_map_bit_enabled(g_cs_channel_map, bit)) {
      enabled[count++] = bit;
    }
  }

  if (count == 0U) {
    for (uint8_t i = 0U; i < channel_count; ++i) {
      const uint8_t shift = (uint8_t)((i & 0x03U) * 8U);
      out_channels[i] = (uint8_t)((fallback_packed >> shift) & 0xFFU);
    }
    return channel_count;
  }

  if (g_cs_channel_selection_type == 1U && g_cs_ch3c_jump > 0U) {
    jump = g_cs_ch3c_jump;
  }
  start = (uint8_t)((current_channel_selection_group() * jump) % count);
  for (uint8_t i = 0U; i < channel_count; ++i) {
    out_channels[i] = enabled[(uint8_t)((start + i) % count)];
  }
  return channel_count;
}

static void update_demo_channels_from_create_config(void) {
  uint8_t channel_map[10];
  uint8_t channels[4];
  uint8_t found = 0U;
  uint8_t last = 2U;
  uint32_t packed = 0U;
  if (g_host_transport->hostLen < 32U || g_host_transport->hostData[0] != 0x01U) {
    return;
  }
  bytes_copy(channel_map, (const void *)&g_host_transport->hostData[17], sizeof(channel_map));
  for (uint8_t bit = 0U; bit < 80U && found < 4U; ++bit) {
    if (channel_map_bit_enabled(channel_map, bit)) {
      last = bit;
      channels[found++] = bit;
    }
  }
  if (found == 0U) {
    return;
  }
  for (uint8_t i = found; i < 4U; ++i) {
    channels[i] = last;
  }
  for (uint8_t i = 0U; i < 4U; ++i) {
    packed |= ((uint32_t)channels[i] << (8U * i));
  }
  g_cs_demo_channels_packed = packed;
}

static void update_defaults_from_set_default_settings(void) {
  if (g_host_transport->hostLen < 9U || g_host_transport->hostData[0] != 0x01U) {
    return;
  }
  g_cs_max_tx_power_dbm = (int8_t)g_host_transport->hostData[8];
}

static void update_create_config_from_command(void) {
  if (g_host_transport->hostLen < 32U || g_host_transport->hostData[0] != 0x01U) {
    return;
  }
  g_cs_config_id = g_host_transport->hostData[6];
  g_cs_main_mode_type = g_host_transport->hostData[8];
  g_cs_sub_mode_type = g_host_transport->hostData[9];
  g_cs_min_main_mode_steps = g_host_transport->hostData[10];
  g_cs_max_main_mode_steps = g_host_transport->hostData[11];
  g_cs_main_mode_repetition = g_host_transport->hostData[12];
  g_cs_mode0_steps = g_host_transport->hostData[13];
  g_cs_role = g_host_transport->hostData[14];
  g_cs_rtt_type = g_host_transport->hostData[15];
  g_cs_cs_sync_phy = g_host_transport->hostData[16];
  bytes_copy(g_cs_channel_map, (const void *)&g_host_transport->hostData[17], sizeof(g_cs_channel_map));
  g_cs_channel_map_repetition = g_host_transport->hostData[27];
  g_cs_channel_selection_type = g_host_transport->hostData[28];
  g_cs_ch3c_shape = g_host_transport->hostData[29];
  g_cs_ch3c_jump = g_host_transport->hostData[30];
  g_cs_enhancements1 = g_host_transport->hostData[31];
  g_cs_config_created = 1U;
  g_cs_security_enabled = 0U;
  g_cs_procedure_params_applied = 0U;
  g_cs_procedure_enabled = 0U;
  g_pending_cs_result_stage = 0U;
  g_cs_procedure_counter = 0U;
  g_cs_next_procedure_heartbeat = 0U;
  g_cs_next_peer_stage_heartbeat = 0U;
  g_cs_next_chunk_stage_heartbeat = 0U;
  g_cs_last_interval_selector = 0U;
  g_cs_last_peer_gap_ticks = 0U;
  g_cs_local_chunk_start_step = 0U;
  g_cs_peer_chunk_start_step = 0U;
  update_demo_channels_from_create_config();
}

static uint8_t validate_create_config_command(void) {
  if (g_host_transport->hostLen < 32U || g_host_transport->hostData[0] != 0x01U) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (!command_conn_handle_matches_active_link()) {
    return (g_cs_session_open != 0U) ? BLE_CS_HCI_STATUS_INVALID_PARAMS
                                     : BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }

  const uint8_t config_id = g_host_transport->hostData[6];
  const uint8_t min_steps = g_host_transport->hostData[10];
  const uint8_t max_steps = g_host_transport->hostData[11];
  const uint8_t repetition = g_host_transport->hostData[12];
  const uint8_t mode0_steps = g_host_transport->hostData[13];
  const uint8_t cs_sync_phy = g_host_transport->hostData[16];

  if (config_id == 0U || min_steps == 0U || max_steps < min_steps ||
      repetition == 0U || mode0_steps == 0U || cs_sync_phy == 0U) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (!channel_map_has_enabled_channels((const uint8_t *)&g_host_transport->hostData[17])) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  return BLE_CS_HCI_STATUS_SUCCESS;
}

static void update_procedure_params_from_command(void) {
  if (g_host_transport->hostLen < 27U || g_host_transport->hostData[0] != 0x01U) {
    return;
  }
  g_cs_config_id = g_host_transport->hostData[6];
  g_cs_max_procedure_len = read_le16(&g_host_transport->hostData[7]);
  g_cs_min_procedure_interval = read_le16(&g_host_transport->hostData[9]);
  g_cs_max_procedure_interval = read_le16(&g_host_transport->hostData[11]);
  g_cs_max_procedure_count = read_le16(&g_host_transport->hostData[13]);
  g_cs_min_subevent_len = read_le24(&g_host_transport->hostData[15]);
  g_cs_max_subevent_len = read_le24(&g_host_transport->hostData[18]);
  g_cs_tone_antenna_config_selection = g_host_transport->hostData[21];
  g_cs_phy = g_host_transport->hostData[22];
  g_cs_tx_power_delta = (int8_t)g_host_transport->hostData[23];
  g_cs_procedure_params_applied = 1U;
  g_cs_next_procedure_heartbeat = 0U;
  g_cs_next_peer_stage_heartbeat = 0U;
  g_cs_next_chunk_stage_heartbeat = 0U;
  g_cs_last_interval_selector = 0U;
  g_cs_last_peer_gap_ticks = 0U;
  g_cs_local_chunk_start_step = 0U;
  g_cs_peer_chunk_start_step = 0U;
}

static void clear_active_cs_state(void) {
  g_cs_config_created = 0U;
  g_cs_security_enabled = 0U;
  g_cs_procedure_params_applied = 0U;
  g_cs_procedure_enabled = 0U;
  g_pending_cs_result_stage = 0U;
  g_cs_procedure_counter = 0U;
  g_cs_next_procedure_heartbeat = 0U;
  g_cs_next_peer_stage_heartbeat = 0U;
  g_cs_next_chunk_stage_heartbeat = 0U;
  g_cs_last_interval_selector = 0U;
  g_cs_last_peer_gap_ticks = 0U;
  g_cs_local_chunk_start_step = 0U;
  g_cs_peer_chunk_start_step = 0U;
}

static uint8_t validate_remove_config_command(void) {
  if (g_host_transport->hostLen < 7U || g_host_transport->hostData[0] != 0x01U) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (!command_conn_handle_matches_active_link()) {
    return (g_cs_session_open != 0U) ? BLE_CS_HCI_STATUS_INVALID_PARAMS
                                     : BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_cs_config_created == 0U) {
    return BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_host_transport->hostData[6] != g_cs_config_id) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  return BLE_CS_HCI_STATUS_SUCCESS;
}

static uint8_t validate_security_enable_command(void) {
  if (g_host_transport->hostLen < 6U || g_host_transport->hostData[0] != 0x01U) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (!command_conn_handle_matches_active_link()) {
    return (g_cs_session_open != 0U) ? BLE_CS_HCI_STATUS_INVALID_PARAMS
                                     : BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_cs_config_created == 0U) {
    return BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  return BLE_CS_HCI_STATUS_SUCCESS;
}

static uint8_t validate_set_procedure_params_command(void) {
  if (g_host_transport->hostLen < 27U || g_host_transport->hostData[0] != 0x01U) {
    g_vpr_transport->lastError = 0xD1U;
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (!command_conn_handle_matches_active_link()) {
    g_vpr_transport->lastError = (g_cs_session_open != 0U) ? 0xD2U : 0xD3U;
    return (g_cs_session_open != 0U) ? BLE_CS_HCI_STATUS_INVALID_PARAMS
                                     : BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_cs_config_created == 0U || g_cs_security_enabled == 0U) {
    g_vpr_transport->lastError =
        (g_cs_config_created == 0U) ? 0xD4U : 0xD5U;
    return BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_host_transport->hostData[6] != g_cs_config_id) {
    g_vpr_transport->lastError = 0xD6U;
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  {
    const uint16_t max_procedure_len = read_le16(&g_host_transport->hostData[7]);
    const uint16_t min_interval = read_le16(&g_host_transport->hostData[9]);
    const uint16_t max_interval = read_le16(&g_host_transport->hostData[11]);
    const uint16_t max_count = read_le16(&g_host_transport->hostData[13]);
    const uint32_t min_subevent_len = read_le24(&g_host_transport->hostData[15]);
    const uint32_t max_subevent_len = read_le24(&g_host_transport->hostData[18]);
    if (max_procedure_len == 0U || min_interval == 0U || max_interval < min_interval ||
        max_count == 0U || min_subevent_len == 0U ||
        max_subevent_len < min_subevent_len) {
      g_vpr_transport->lastError = 0xD7U;
      return BLE_CS_HCI_STATUS_INVALID_PARAMS;
    }
  }
  g_vpr_transport->lastError = 0U;
  return BLE_CS_HCI_STATUS_SUCCESS;
}

static uint8_t validate_procedure_enable_command(void) {
  if (g_host_transport->hostLen < 8U || g_host_transport->hostData[0] != 0x01U) {
    g_vpr_transport->lastError = 0xE1U;
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (!command_conn_handle_matches_active_link()) {
    g_vpr_transport->lastError = (g_cs_session_open != 0U) ? 0xE2U : 0xE3U;
    return (g_cs_session_open != 0U) ? BLE_CS_HCI_STATUS_INVALID_PARAMS
                                     : BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_cs_config_created == 0U) {
    g_vpr_transport->lastError = 0xE4U;
    return BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  if (g_host_transport->hostData[6] != g_cs_config_id) {
    g_vpr_transport->lastError = 0xE5U;
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (g_host_transport->hostData[7] > 1U) {
    g_vpr_transport->lastError = 0xE6U;
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (g_host_transport->hostData[7] == 0U) {
    g_vpr_transport->lastError = 0U;
    return BLE_CS_HCI_STATUS_SUCCESS;
  }
  if (g_cs_security_enabled == 0U || g_cs_procedure_params_applied == 0U) {
    g_vpr_transport->lastError =
        (g_cs_security_enabled == 0U) ? 0xE7U : 0xE8U;
    return BLE_CS_HCI_STATUS_COMMAND_DISALLOWED;
  }
  g_vpr_transport->lastError = 0U;
  return BLE_CS_HCI_STATUS_SUCCESS;
}

static uint8_t validate_read_remote_caps_command(void) {
  if (g_host_transport->hostLen < 6U || g_host_transport->hostData[0] != 0x01U) {
    return BLE_CS_HCI_STATUS_INVALID_PARAMS;
  }
  if (g_cs_session_open == 0U) {
    return BLE_CS_HCI_STATUS_SUCCESS;
  }
  return (read_conn_handle() == g_cs_session_conn_handle) ? BLE_CS_HCI_STATUS_SUCCESS
                                                          : BLE_CS_HCI_STATUS_INVALID_PARAMS;
}

static uint32_t current_procedure_interval_ticks(void) {
  const uint32_t min_ticks =
      (g_cs_min_procedure_interval != 0U) ? (uint32_t)g_cs_min_procedure_interval : 1U;
  const uint32_t max_ticks =
      (g_cs_max_procedure_interval >= g_cs_min_procedure_interval &&
       g_cs_max_procedure_interval != 0U)
          ? (uint32_t)g_cs_max_procedure_interval
          : min_ticks;
  if (max_ticks <= min_ticks) {
    g_cs_last_interval_selector = 0U;
    return min_ticks;
  }
  if (g_cs_max_procedure_count <= 2U || g_cs_procedure_counter <= 1U) {
    g_cs_last_interval_selector = 0U;
    return min_ticks;
  }
  const uint32_t gap_index = (uint32_t)g_cs_procedure_counter - 1U;
  const uint32_t gap_span = (uint32_t)g_cs_max_procedure_count - 2U;
  uint32_t selector = (gap_index * 15U + (gap_span / 2U)) / gap_span;
  if (selector > 15U) {
    selector = 15U;
  }
  g_cs_last_interval_selector = (uint8_t)selector;
  return min_ticks + (((max_ticks - min_ticks) * selector + 7U) / 15U);
}

static uint32_t current_peer_stage_delay_ticks(void) {
  uint32_t ticks = g_cs_min_subevent_len / 256U;
  if (ticks == 0U) {
    ticks = 1U;
  }
  if (ticks > 32U) {
    ticks = 32U;
  }
  return ticks;
}

static uint32_t current_chunk_stage_delay_ticks(void) {
  uint32_t ticks = g_cs_min_subevent_len / 128U;
  if (ticks == 0U) {
    ticks = 1U;
  }
  if (ticks > 8U) {
    ticks = 8U;
  }
  return ticks;
}

static bool schedule_next_cs_procedure(void) {
  if (g_cs_procedure_enabled == 0U || g_pending_cs_result_stage != 0U ||
      g_cs_procedure_counter == 0U || g_cs_procedure_counter >= g_cs_max_procedure_count ||
      host_request_pending() ||
      (g_vpr_transport->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U) {
    return false;
  }
  if (g_vpr_transport->heartbeat < g_cs_next_procedure_heartbeat) {
    return false;
  }
  g_cs_procedure_counter = (uint16_t)(g_cs_procedure_counter + 1U);
  if (g_cs_procedure_counter == 0U) {
    g_cs_procedure_counter = 1U;
  }
  g_pending_cs_result_stage = 1U;
  g_cs_local_chunk_start_step = 0U;
  g_cs_peer_chunk_start_step = 0U;
  g_cs_next_procedure_heartbeat = 0U;
  g_cs_next_chunk_stage_heartbeat = 0U;
  return true;
}
#endif

static size_t build_remote_caps_payload(uint8_t *payload, size_t max_len, uint16_t conn_handle) {
  if (payload == NULL || max_len < 34U) {
    return 0U;
  }
  bytes_zero(payload, 34U);
  payload[0] = 0U;
  write_le16(&payload[1], conn_handle);
  payload[3] = 4U;
  write_le16(&payload[4], 6U);
  payload[6] = 4U;
  payload[7] = 4U;
  payload[8] = 0x03U;
  payload[9] = 0x01U;
  payload[10] = 0x07U;
  payload[11] = 0x02U;
  payload[12] = 0x03U;
  payload[13] = 0x04U;
  write_le16(&payload[14], 0x0001U);
  write_le16(&payload[16], 0x0001U);
  payload[18] = 0x06U;
  write_le16(&payload[19], 0x001EU);
  write_le16(&payload[21], 10U);
  write_le16(&payload[23], 20U);
  write_le16(&payload[25], 30U);
  write_le16(&payload[27], 40U);
  payload[29] = 3U;
  payload[30] = 4U;
  write_le16(&payload[31], 50U);
  payload[33] = 6U;
  return 34U;
}

static size_t build_security_complete_payload(uint8_t *payload, size_t max_len,
                                              uint16_t conn_handle) {
  if (payload == NULL || max_len < 3U) {
    return 0U;
  }
  payload[0] = 0U;
  write_le16(&payload[1], conn_handle);
  return 3U;
}

static size_t build_config_complete_payload(uint8_t *payload, size_t max_len,
                                            uint16_t conn_handle) {
  static const uint8_t k_default_channel_map[10] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x1FU,
                                                    0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
  if (payload == NULL || max_len < 33U) {
    return 0U;
  }
  bytes_zero(payload, 33U);
  payload[0] = 0U;
  write_le16(&payload[1], conn_handle);
#if VPR_CS_DEDICATED_IMAGE
  payload[3] = g_cs_config_id;
  payload[5] = g_cs_main_mode_type;
  payload[6] = g_cs_sub_mode_type;
  payload[7] = g_cs_min_main_mode_steps;
  payload[8] = g_cs_max_main_mode_steps;
  payload[9] = g_cs_main_mode_repetition;
  payload[10] = g_cs_mode0_steps;
  payload[11] = g_cs_role;
  payload[12] = g_cs_rtt_type;
  payload[13] = g_cs_cs_sync_phy;
  bytes_copy(&payload[14], g_cs_channel_map, sizeof(g_cs_channel_map));
  payload[24] = g_cs_channel_map_repetition;
  payload[25] = g_cs_channel_selection_type;
  payload[26] = g_cs_ch3c_shape;
  payload[27] = g_cs_ch3c_jump;
  payload[28] = g_cs_enhancements1;
#else
  payload[3] = 1U;
  payload[5] = BLE_CS_MAIN_MODE2;
  payload[6] = 0xFFU;
  payload[7] = 3U;
  payload[8] = 5U;
  payload[9] = 1U;
  payload[10] = 1U;
  payload[11] = 0U;
  payload[12] = 1U;
  payload[13] = 2U;
  bytes_copy(&payload[14], k_default_channel_map, sizeof(k_default_channel_map));
  payload[24] = 1U;
  payload[25] = 1U;
  payload[26] = 1U;
  payload[27] = 3U;
  payload[28] = 0x01U;
#endif
  payload[4] = 1U;
  payload[29] = 10U;
  payload[30] = 20U;
  payload[31] = 30U;
  payload[32] = 40U;
  return 33U;
}

static size_t build_procedure_enable_complete_payload(uint8_t *payload, size_t max_len,
                                                      uint16_t conn_handle) {
  if (payload == NULL || max_len < 21U) {
    return 0U;
  }
  bytes_zero(payload, 21U);
  payload[0] = 0U;
  write_le16(&payload[1], conn_handle);
#if VPR_CS_DEDICATED_IMAGE
  payload[3] = g_cs_config_id;
  payload[5] = g_cs_tone_antenna_config_selection;
  payload[6] = (uint8_t)g_cs_tx_power_delta;
  write_le24(&payload[7], g_cs_min_subevent_len);
  payload[10] = 4U;
  write_le16(&payload[11], g_cs_min_procedure_interval);
  write_le16(&payload[13], g_cs_max_procedure_interval);
  write_le16(&payload[15], g_cs_max_procedure_interval);
  write_le16(&payload[17], g_cs_max_procedure_count);
  write_le16(&payload[19], g_cs_max_procedure_len);
#else
  payload[3] = 1U;
  payload[5] = 2U;
  payload[6] = (uint8_t)(int8_t)-12;
  write_le24(&payload[7], 0x000456UL);
  payload[10] = 4U;
  write_le16(&payload[11], 100U);
  write_le16(&payload[13], 200U);
  write_le16(&payload[15], 300U);
  write_le16(&payload[17], 8U);
  write_le16(&payload[19], 12U);
#endif
  payload[4] =
#if VPR_CS_DEDICATED_IMAGE
      g_cs_procedure_enabled;
#else
      1U;
#endif
  return 21U;
}

static size_t build_demo_subevent_payload(uint8_t *payload, size_t max_len,
                                          uint16_t conn_handle, bool peer_side,
                                          bool continuation, uint8_t start_step_index,
                                          uint8_t *steps_built_out,
                                          bool *has_more_out) {
  if (payload == NULL || steps_built_out == NULL || has_more_out == NULL) {
    return 0U;
  }
  const size_t header_len = continuation ? 8U : 15U;
  if (max_len < (header_len + 8U)) {
    return 0U;
  }

  uint8_t channels[6];
  uint8_t step_count = 4U;
  uint8_t total_steps = 4U;
  uint8_t chunk_steps = 0U;
  bool has_more = false;
#if VPR_CS_DEDICATED_IMAGE
  step_count = current_demo_step_count();
  if (step_count == 0U) {
    step_count = 1U;
  }
  total_steps = current_demo_total_step_count();
  if (start_step_index >= total_steps) {
    return 0U;
  }
  size_t budget =
      continuation ? current_demo_continue_chunk_budget(start_step_index)
                   : current_demo_initial_chunk_budget();
  const size_t payload_budget = max_len - header_len;
  if (budget > payload_budget) {
    budget = payload_budget;
  }
  chunk_steps = count_demo_steps_for_budget(budget, start_step_index);
  if (chunk_steps == 0U) {
    return 0U;
  }
  has_more = ((uint8_t)(start_step_index + chunk_steps) < total_steps);
  fill_demo_channels_for_procedure(channels, step_count);
#else
  const uint32_t packed_channels = g_host_transport->reserved;
  channels[0] = (uint8_t)(packed_channels & 0xFFU);
  channels[1] = (uint8_t)((packed_channels >> 8U) & 0xFFU);
  channels[2] = (uint8_t)((packed_channels >> 16U) & 0xFFU);
  channels[3] = (uint8_t)((packed_channels >> 24U) & 0xFFU);
  if (start_step_index != 0U) {
    return 0U;
  }
  chunk_steps = 4U;
  has_more = continuation;
#endif

  bytes_zero(payload, max_len);
  write_le16(&payload[0], conn_handle);
  payload[2] =
#if VPR_CS_DEDICATED_IMAGE
      g_cs_config_id;
#else
      1U;
#endif
  if (!continuation) {
    write_le16(&payload[3],
#if VPR_CS_DEDICATED_IMAGE
               current_demo_acl_event_counter());
#else
               0x1234U);
#endif
    write_le16(&payload[5],
#if VPR_CS_DEDICATED_IMAGE
               g_cs_procedure_counter);
#else
               7U);
#endif
    write_le16(&payload[7],
#if VPR_CS_DEDICATED_IMAGE
               current_demo_frequency_compensation());
#else
               0U);
#endif
    payload[9] =
#if VPR_CS_DEDICATED_IMAGE
        (uint8_t)current_demo_reference_power_dbm();
#else
        0U;
#endif
    payload[10] = has_more ? 0x01U : 0x00U;
    payload[11] = has_more ? 0x01U : 0x00U;
    payload[12] = 0U;
    payload[13] =
#if VPR_CS_DEDICATED_IMAGE
        current_demo_num_antenna_paths();
#else
        2U;
#endif
    payload[14] = chunk_steps;
  } else {
    payload[3] = has_more ? 0x01U : 0x00U;
    payload[4] = has_more ? 0x01U : 0x00U;
    payload[5] = 0U;
    payload[6] =
#if VPR_CS_DEDICATED_IMAGE
        current_demo_num_antenna_paths();
#else
        2U;
#endif
    payload[7] = chunk_steps;
  }

  size_t offset = header_len;
  for (uint8_t i = 0U; i < chunk_steps; ++i) {
    const uint8_t step_index = (uint8_t)(start_step_index + i);
    if (peer_side) {
#if VPR_CS_DEDICATED_IMAGE
      offset += append_peer_demo_step(&payload[offset], channels, step_index);
#else
      return 0U;
#endif
    } else {
      offset += append_local_demo_step(&payload[offset], channels, step_index);
    }
  }

  *steps_built_out = chunk_steps;
  *has_more_out = has_more;
  return offset;
}

#if !VPR_CS_DEDICATED_IMAGE
static uint16_t read_opcode(void) {
  if (g_host_transport->hostLen < 4U) {
    return 0U;
  }
  if (g_host_transport->hostData[0] != 0x01U) {
    return 0U;
  }
  return (uint16_t)g_host_transport->hostData[1] |
         ((uint16_t)g_host_transport->hostData[2] << 8U);
}

static size_t build_vendor_ping_complete_payload(uint8_t *payload, size_t max_len) {
  uint32_t echoedCookie = 0U;
  if (payload == NULL || max_len < 9U) {
    return 0U;
  }
  if (g_host_transport->hostLen >= 8U) {
    echoedCookie = (uint32_t)g_host_transport->hostData[4] |
                   ((uint32_t)g_host_transport->hostData[5] << 8U) |
                   ((uint32_t)g_host_transport->hostData[6] << 16U) |
                   ((uint32_t)g_host_transport->hostData[7] << 24U);
  }
  payload[0] = 0U;
  write_le32(&payload[1], echoedCookie);
  write_le32(&payload[5], g_vpr_transport->heartbeat);
  return 9U;
}

static size_t build_vendor_info_complete_payload(uint8_t *payload, size_t max_len) {
  if (payload == NULL || max_len < 12U) {
    return 0U;
  }
  payload[0] = 0U;
  payload[1] = (uint8_t)(g_vpr_transport->status & 0xFFU);
  payload[2] = (uint8_t)(g_vpr_transport->lastError & 0xFFU);
  payload[3] = (uint8_t)(g_vpr_transport->vprFlags & 0xFFU);
  if (g_restored_from_hibernate != 0U) {
    payload[3] = (uint8_t)(payload[3] | VPR_VENDOR_TRANSPORT_FLAG_RESTORED_FROM_HIBERNATE);
  }
  write_le32(&payload[4], g_vpr_transport->heartbeat);
  write_le32(&payload[8], g_host_transport->scriptCount);
  return 12U;
}

static size_t build_vendor_capabilities_complete_payload(uint8_t *payload, size_t max_len) {
  if (payload == NULL || max_len < 11U) {
    return 0U;
  }
  payload[0] = 0U;
  payload[1] = VPR_VENDOR_SERVICE_VERSION_MAJOR;
  payload[2] = VPR_VENDOR_SERVICE_VERSION_MINOR;
  write_le32(&payload[3],
             VPR_VENDOR_OP_PING |
             VPR_VENDOR_OP_INFO |
             VPR_VENDOR_OP_FNV1A32 |
             VPR_VENDOR_OP_CAPABILITIES |
             VPR_VENDOR_OP_CRC32 |
             VPR_VENDOR_OP_CRC32C |
             VPR_VENDOR_OP_TICKER_CONFIGURE |
             VPR_VENDOR_OP_TICKER_READ_STATE |
             VPR_VENDOR_OP_ENTER_HIBERNATE |
             VPR_VENDOR_OP_TICKER_EVENT_CONFIGURE);
  write_le32(&payload[7], NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U);
  return 11U;
}

static uint32_t fnv1a32_bytes(const uint8_t *data, size_t len) {
  uint32_t hash = 2166136261UL;
  if (data == NULL) {
    return hash;
  }
  for (size_t i = 0U; i < len; ++i) {
    hash ^= (uint32_t)data[i];
    hash *= 16777619UL;
  }
  return hash;
}

static size_t build_vendor_fnv1a_complete_payload(uint8_t *payload, size_t max_len) {
  uint32_t processedLen = 0U;
  uint32_t hash = 2166136261UL;
  if (payload == NULL || max_len < 9U) {
    return 0U;
  }
  if (g_host_transport->hostLen > 4U) {
    processedLen = g_host_transport->hostLen - 4U;
    if (processedLen > (NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U)) {
      processedLen = NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U;
    }
    hash = fnv1a32_bytes((const uint8_t *)&g_host_transport->hostData[4], processedLen);
  }
  payload[0] = 0U;
  write_le32(&payload[1], hash);
  write_le32(&payload[5], processedLen);
  return 9U;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  if (data == NULL) {
    return crc ^ 0xFFFFFFFFUL;
  }
  for (size_t i = 0U; i < len; ++i) {
    crc ^= (uint32_t)data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return crc ^ 0xFFFFFFFFUL;
}

static size_t build_vendor_crc32_complete_payload(uint8_t *payload, size_t max_len) {
  uint32_t processedLen = 0U;
  uint32_t crc = 0U;
  if (payload == NULL || max_len < 9U) {
    return 0U;
  }
  if (g_host_transport->hostLen > 4U) {
    processedLen = g_host_transport->hostLen - 4U;
    if (processedLen > (NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U)) {
      processedLen = NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U;
    }
    crc = crc32_bytes((const uint8_t *)&g_host_transport->hostData[4], processedLen);
  }
  payload[0] = 0U;
  write_le32(&payload[1], crc);
  write_le32(&payload[5], processedLen);
  return 9U;
}

static uint32_t crc32c_bytes(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  if (data == NULL) {
    return crc ^ 0xFFFFFFFFUL;
  }
  for (size_t i = 0U; i < len; ++i) {
    crc ^= (uint32_t)data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0x82F63B78UL & mask);
    }
  }
  return crc ^ 0xFFFFFFFFUL;
}

static size_t build_vendor_crc32c_complete_payload(uint8_t *payload, size_t max_len) {
  uint32_t processedLen = 0U;
  uint32_t crc = 0U;
  if (payload == NULL || max_len < 9U) {
    return 0U;
  }
  if (g_host_transport->hostLen > 4U) {
    processedLen = g_host_transport->hostLen - 4U;
    if (processedLen > (NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U)) {
      processedLen = NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA - 4U;
    }
    crc = crc32c_bytes((const uint8_t *)&g_host_transport->hostData[4], processedLen);
  }
  payload[0] = 0U;
  write_le32(&payload[1], crc);
  write_le32(&payload[5], processedLen);
  return 9U;
}

static size_t build_vendor_ticker_state_payload(uint8_t *payload, size_t max_len) {
  if (payload == NULL || max_len < 14U) {
    return 0U;
  }
  payload[0] = 0U;
  payload[1] = (uint8_t)(g_ticker_enabled != 0U ? 1U : 0U);
  write_le32(&payload[2], g_ticker_period_ticks);
  write_le32(&payload[6], g_ticker_step);
  write_le32(&payload[10], g_ticker_count);
  return 14U;
}

static size_t build_vendor_ticker_configure_complete_payload(uint8_t *payload, size_t max_len) {
  if (g_host_transport->hostLen >= 13U) {
    g_ticker_enabled = g_host_transport->hostData[4] != 0U ? 1U : 0U;
    g_ticker_period_ticks = (uint32_t)g_host_transport->hostData[5] |
                            ((uint32_t)g_host_transport->hostData[6] << 8U) |
                            ((uint32_t)g_host_transport->hostData[7] << 16U) |
                            ((uint32_t)g_host_transport->hostData[8] << 24U);
    g_ticker_step = (uint32_t)g_host_transport->hostData[9] |
                    ((uint32_t)g_host_transport->hostData[10] << 8U) |
                    ((uint32_t)g_host_transport->hostData[11] << 16U) |
                    ((uint32_t)g_host_transport->hostData[12] << 24U);
    if (g_ticker_step == 0U) {
      g_ticker_step = 1U;
    }
    g_ticker_accum = 0U;
    g_ticker_count = 0U;
    if (g_ticker_period_ticks == 0U) {
      g_ticker_enabled = 0U;
    }
  }
  return build_vendor_ticker_state_payload(payload, max_len);
}

static size_t build_vendor_ticker_event_configure_complete_payload(uint8_t *payload,
                                                                   size_t max_len) {
  if (payload == NULL || max_len < 14U) {
    return 0U;
  }
  if (g_host_transport->hostLen >= 9U) {
    g_ticker_event_enabled = g_host_transport->hostData[4] != 0U ? 1U : 0U;
    g_ticker_event_emit_every = (uint32_t)g_host_transport->hostData[5] |
                                ((uint32_t)g_host_transport->hostData[6] << 8U) |
                                ((uint32_t)g_host_transport->hostData[7] << 16U) |
                                ((uint32_t)g_host_transport->hostData[8] << 24U);
    if (g_ticker_event_emit_every == 0U) {
      g_ticker_event_enabled = 0U;
    }
    g_ticker_event_last_emitted_count = g_ticker_count;
    g_ticker_event_sequence = 0U;
    clear_ticker_event_queue();
    g_ticker_event_drop_count = 0U;
  }
  payload[0] = 0U;
  payload[1] = (uint8_t)(g_ticker_event_enabled != 0U ? 1U : 0U);
  write_le32(&payload[2], g_ticker_event_emit_every);
  write_le32(&payload[6], g_ticker_event_last_emitted_count);
  write_le32(&payload[10], g_ticker_event_drop_count);
  return 14U;
}

static size_t build_vendor_ticker_event_payload(uint8_t *payload, size_t max_len,
                                                const vpr_ticker_event_entry_t *event) {
  if (payload == NULL || event == NULL || max_len < 17U) {
    return 0U;
  }
  payload[0] = event->flags;
  write_le32(&payload[1], event->count);
  write_le32(&payload[5], event->step);
  write_le32(&payload[9], event->heartbeat);
  write_le32(&payload[13], event->sequence);
  return 17U;
}

static size_t build_vendor_enter_hibernate_complete_payload(uint8_t *payload, size_t max_len) {
  if (payload == NULL || max_len < 5U) {
    return 0U;
  }
  payload[0] = 0U;
  write_le32(&payload[1], g_vpr_transport->heartbeat);
  persist_hibernate_state();
  g_pending_hibernate = 1U;
  return 5U;
}
#else
static uint16_t read_opcode(void) {
  if (g_host_transport->hostLen < 4U) {
    return 0U;
  }
  if (g_host_transport->hostData[0] != 0x01U) {
    return 0U;
  }
  return (uint16_t)g_host_transport->hostData[1] |
         ((uint16_t)g_host_transport->hostData[2] << 8U);
}
#endif

static void build_unknown_command_response(uint16_t opcode) {
  zero_vpr_data();
  g_vpr_transport->vprData[0] = 0x04U;
  g_vpr_transport->vprData[1] = 0x0EU;
  g_vpr_transport->vprData[2] = 4U;
  g_vpr_transport->vprData[3] = 1U;
  g_vpr_transport->vprData[4] = (uint8_t)(opcode & 0xFFU);
  g_vpr_transport->vprData[5] = (uint8_t)((opcode >> 8U) & 0xFFU);
  g_vpr_transport->vprData[6] = 0x01U;
  g_vpr_transport->vprLen = 7U;
}

static bool publish_builtin_response_for_opcode(uint16_t opcode) {
  uint8_t payload[40];
  uint16_t conn_handle = current_conn_handle();
  size_t offset = 0U;
  zero_vpr_data();

  switch (opcode) {
    case BLE_CS_HCI_OP_READ_REMOTE_SUPPORTED_CAPABILITIES: {
      uint8_t status = 0U;
#if VPR_CS_DEDICATED_IMAGE
      status = validate_read_remote_caps_command();
      if (status == BLE_CS_HCI_STATUS_SUCCESS && g_cs_session_open == 0U) {
        clear_active_cs_state();
        g_cs_session_conn_handle = conn_handle;
        g_cs_session_open = 1U;
      }
#endif
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, status);
      if (len == 0U) {
        return false;
      }
      offset += len;
      if (status != 0U) {
        break;
      }
      len = build_remote_caps_payload(payload, sizeof(payload), conn_handle);
      if (len == 0U) {
        return false;
      }
      len = append_h4_le_meta((uint8_t *)g_vpr_transport->vprData + offset,
                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                              BLE_CS_HCI_EVT_READ_REMOTE_SUPPORTED_CAPS_COMPLETE_V2, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case BLE_CS_HCI_OP_SET_DEFAULT_SETTINGS: {
#if VPR_CS_DEDICATED_IMAGE
      update_defaults_from_set_default_settings();
#endif
      size_t len = append_h4_command_complete((uint8_t *)g_vpr_transport->vprData + offset,
                                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                              opcode, 0U);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case BLE_CS_HCI_OP_CREATE_CONFIG: {
      uint8_t status = 0U;
#if VPR_CS_DEDICATED_IMAGE
      status = validate_create_config_command();
      if (status == 0U) {
        update_create_config_from_command();
      }
#endif
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, status);
      if (len == 0U) {
        return false;
      }
      offset += len;
      if (status != 0U) {
        break;
      }
      len = build_config_complete_payload(payload, sizeof(payload), conn_handle);
      if (len == 0U) {
        return false;
      }
      len = append_h4_le_meta((uint8_t *)g_vpr_transport->vprData + offset,
                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                              BLE_CS_HCI_EVT_CONFIG_COMPLETE, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case BLE_CS_HCI_OP_REMOVE_CONFIG: {
      uint8_t status = 0U;
#if VPR_CS_DEDICATED_IMAGE
      status = validate_remove_config_command();
      if (status == 0U) {
        clear_active_cs_state();
        g_cs_session_open = 0U;
        g_cs_session_conn_handle = 0U;
      }
#endif
      size_t len = append_h4_command_complete((uint8_t *)g_vpr_transport->vprData + offset,
                                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                              opcode, status);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case BLE_CS_HCI_OP_SECURITY_ENABLE: {
      uint8_t status = 0U;
#if VPR_CS_DEDICATED_IMAGE
      status = validate_security_enable_command();
#endif
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, status);
      if (len == 0U) {
        return false;
      }
      offset += len;
      if (status != 0U) {
        break;
      }
#if VPR_CS_DEDICATED_IMAGE
      g_cs_security_enabled = 1U;
#endif
      len = build_security_complete_payload(payload, sizeof(payload), conn_handle);
      if (len == 0U) {
        return false;
      }
      len = append_h4_le_meta((uint8_t *)g_vpr_transport->vprData + offset,
                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                              BLE_CS_HCI_EVT_SECURITY_ENABLE_COMPLETE, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case BLE_CS_HCI_OP_SET_PROCEDURE_PARAMETERS: {
      uint8_t status = 0U;
#if VPR_CS_DEDICATED_IMAGE
      status = validate_set_procedure_params_command();
      if (status == 0U) {
        update_procedure_params_from_command();
      }
#endif
      size_t len = append_h4_command_complete((uint8_t *)g_vpr_transport->vprData + offset,
                                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                              opcode, status);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case BLE_CS_HCI_OP_PROCEDURE_ENABLE: {
      uint8_t status = 0U;
      uint8_t enable = 0U;
#if VPR_CS_DEDICATED_IMAGE
      status = validate_procedure_enable_command();
      if (g_host_transport->hostLen >= 8U) {
        g_cs_config_id = g_host_transport->hostData[6];
        enable = g_host_transport->hostData[7];
        if (enable == 0U) {
          g_cs_procedure_enabled = 0U;
          g_pending_cs_result_stage = 0U;
          g_cs_next_procedure_heartbeat = 0U;
          g_cs_next_peer_stage_heartbeat = 0U;
          g_cs_next_chunk_stage_heartbeat = 0U;
          g_cs_last_interval_selector = 0U;
          g_cs_last_peer_gap_ticks = 0U;
          g_cs_local_chunk_start_step = 0U;
          g_cs_peer_chunk_start_step = 0U;
        } else if (status == 0U) {
          g_cs_procedure_enabled = 1U;
          g_cs_procedure_counter = 1U;
          g_pending_cs_result_stage = 1U;
          g_cs_next_procedure_heartbeat = 0U;
          g_cs_next_peer_stage_heartbeat = 0U;
          g_cs_next_chunk_stage_heartbeat = 0U;
          g_cs_last_interval_selector = 0U;
          g_cs_last_peer_gap_ticks = 0U;
          g_cs_local_chunk_start_step = 0U;
          g_cs_peer_chunk_start_step = 0U;
        }
      }
#endif
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, status);
      if (len == 0U) {
        return false;
      }
      offset += len;
      if (status != 0U) {
        break;
      }
      len = build_procedure_enable_complete_payload(payload, sizeof(payload), conn_handle);
      if (len == 0U) {
        return false;
      }
      len = append_h4_le_meta((uint8_t *)g_vpr_transport->vprData + offset,
                              NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                              BLE_CS_HCI_EVT_PROCEDURE_ENABLE_COMPLETE, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      if (enable != 0U) {
        g_pending_cs_result_stage = 1U;
        g_cs_next_peer_stage_heartbeat = 0U;
        g_cs_next_chunk_stage_heartbeat = 0U;
        g_cs_last_interval_selector = 0U;
        g_cs_last_peer_gap_ticks = 0U;
        g_cs_local_chunk_start_step = 0U;
        g_cs_peer_chunk_start_step = 0U;
      }
      break;
    }
#if !VPR_CS_DEDICATED_IMAGE
    case VPR_HCI_OP_VENDOR_PING: {
      size_t len = build_vendor_ping_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_GET_TRANSPORT_INFO: {
      size_t len = build_vendor_info_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_FNV1A32: {
      size_t len = build_vendor_fnv1a_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_GET_CAPABILITIES: {
      size_t len = build_vendor_capabilities_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_CRC32: {
      size_t len = build_vendor_crc32_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_CRC32C: {
      size_t len = build_vendor_crc32c_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_TICKER_CONFIGURE: {
      size_t len = build_vendor_ticker_configure_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_TICKER_READ_STATE: {
      size_t len = build_vendor_ticker_state_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_ENTER_HIBERNATE: {
      size_t len = build_vendor_enter_hibernate_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
    case VPR_HCI_OP_VENDOR_TICKER_EVENT_CONFIGURE: {
      size_t len = build_vendor_ticker_event_configure_complete_payload(payload, sizeof(payload));
      if (len == 0U) {
        return false;
      }
      len = append_h4_command_complete_payload((uint8_t *)g_vpr_transport->vprData + offset,
                                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                               opcode, payload, len);
      if (len == 0U) {
        return false;
      }
      offset += len;
      break;
    }
#endif
    default:
      return false;
  }

  g_vpr_transport->vprLen = (uint32_t)offset;
  return true;
}

static void publish_response_for_opcode(uint16_t opcode) {
  const uint32_t count =
      (g_host_transport->scriptCount <= NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_COUNT)
          ? g_host_transport->scriptCount
          : NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_COUNT;

  g_vpr_transport->lastOpcode = (uint32_t)opcode;
  for (uint32_t i = 0U; i < count; ++i) {
    volatile const Nrf54l15VprTransportScript* script = &g_host_transport->scripts[i];
    if (script->opcode != opcode) {
      continue;
    }

    if (script->responseLen > NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA) {
      g_vpr_transport->lastError = 2U;
      g_vpr_transport->status = NRF54L15_VPR_TRANSPORT_STATUS_ERROR;
      g_vpr_transport->vprLen = 0U;
      return;
    }

    zero_vpr_data();
    bytes_copy((void *)g_vpr_transport->vprData, (const void *)script->response,
               script->responseLen);
    g_vpr_transport->vprLen = script->responseLen;
    return;
  }

  if (!publish_builtin_response_for_opcode(opcode)) {
    build_unknown_command_response(opcode);
  }
}

static bool publish_pending_cs_result_packet(void) {
  uint8_t payload[40];
  uint8_t packet[96];
  size_t len = 0U;
  uint16_t conn_handle = current_conn_handle();
  uint8_t steps_built = 0U;
  bool has_more = false;
  if (g_pending_cs_result_stage == 0U ||
      (g_vpr_transport->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ||
      host_request_pending()) {
    return false;
  }
  if ((g_pending_cs_result_stage == 2U || g_pending_cs_result_stage == 5U) &&
      g_vpr_transport->heartbeat < g_cs_next_chunk_stage_heartbeat) {
    return false;
  }
  if (g_pending_cs_result_stage >= 3U &&
      g_vpr_transport->heartbeat < g_cs_next_peer_stage_heartbeat) {
    return false;
  }
  if (g_pending_cs_result_stage == 1U) {
    len = build_demo_subevent_payload(payload, sizeof(payload), conn_handle, false, false,
                                      g_cs_local_chunk_start_step, &steps_built, &has_more);
    if (len == 0U) {
      return false;
    }
    len = append_h4_le_meta(packet, sizeof(packet), BLE_CS_HCI_EVT_SUBEVENT_RESULT, payload, len);
  } else if (g_pending_cs_result_stage == 2U) {
    len = build_demo_subevent_payload(payload, sizeof(payload), conn_handle, false, true,
                                      g_cs_local_chunk_start_step, &steps_built, &has_more);
    if (len == 0U) {
      return false;
    }
    len = append_h4_le_meta(packet, sizeof(packet), BLE_CS_HCI_EVT_SUBEVENT_RESULT_CONTINUE,
                            payload, len);
  } else if (g_pending_cs_result_stage == 3U) {
#if VPR_CS_DEDICATED_IMAGE
    payload[0] = g_cs_config_id;
    write_le16(&payload[1], g_cs_procedure_counter);
    len = append_h4_vendor_event(packet, sizeof(packet),
                                 VPR_VENDOR_EVENT_CS_PEER_RESULT_SOURCE, payload, 3U);
#else
    payload[0] = 1U;
    len = append_h4_vendor_event(packet, sizeof(packet),
                                 VPR_VENDOR_EVENT_CS_PEER_RESULT_TRIGGER, payload, 1U);
#endif
#if VPR_CS_DEDICATED_IMAGE
  } else if (g_pending_cs_result_stage == 4U) {
    len = build_demo_subevent_payload(payload, sizeof(payload), conn_handle, true, false,
                                      g_cs_peer_chunk_start_step, &steps_built, &has_more);
    if (len == 0U) {
      return false;
    }
    len = append_h4_le_meta(packet, sizeof(packet), BLE_CS_HCI_EVT_SUBEVENT_RESULT, payload, len);
  } else if (g_pending_cs_result_stage == 5U) {
    len = build_demo_subevent_payload(payload, sizeof(payload), conn_handle, true, true,
                                      g_cs_peer_chunk_start_step, &steps_built, &has_more);
    if (len == 0U) {
      return false;
    }
    len = append_h4_le_meta(packet, sizeof(packet), BLE_CS_HCI_EVT_SUBEVENT_RESULT_CONTINUE,
                            payload, len);
#endif
  } else {
    return false;
  }
  if (len == 0U) {
    return false;
  }
  zero_vpr_data();
  bytes_copy((void *)g_vpr_transport->vprData, packet, len);
  g_vpr_transport->vprLen = (uint32_t)len;
  g_vpr_transport->vprSeq = g_vpr_transport->vprSeq + 1U;
  g_vpr_transport->vprFlags = NRF54L15_VPR_TRANSPORT_FLAG_PENDING;
#if VPR_CS_DEDICATED_IMAGE
  if (g_pending_cs_result_stage == 1U) {
    g_cs_local_chunk_start_step = (uint8_t)(g_cs_local_chunk_start_step + steps_built);
    if (has_more) {
      g_pending_cs_result_stage = 2U;
      g_cs_next_chunk_stage_heartbeat =
          g_vpr_transport->heartbeat + current_chunk_stage_delay_ticks();
    } else {
      const uint32_t peer_gap = current_peer_stage_delay_ticks();
      g_pending_cs_result_stage = 3U;
      g_cs_peer_chunk_start_step = 0U;
      g_cs_next_chunk_stage_heartbeat = 0U;
      g_cs_next_peer_stage_heartbeat = g_vpr_transport->heartbeat + peer_gap;
      g_cs_last_peer_gap_ticks = (peer_gap > 7U) ? 7U : (uint8_t)peer_gap;
    }
  } else if (g_pending_cs_result_stage == 2U) {
    g_cs_local_chunk_start_step = (uint8_t)(g_cs_local_chunk_start_step + steps_built);
    if (has_more) {
      g_pending_cs_result_stage = 2U;
      g_cs_next_chunk_stage_heartbeat =
          g_vpr_transport->heartbeat + current_chunk_stage_delay_ticks();
    } else {
      const uint32_t peer_gap = current_peer_stage_delay_ticks();
      g_pending_cs_result_stage = 3U;
      g_cs_peer_chunk_start_step = 0U;
      g_cs_next_chunk_stage_heartbeat = 0U;
      g_cs_next_peer_stage_heartbeat = g_vpr_transport->heartbeat + peer_gap;
      g_cs_last_peer_gap_ticks = (peer_gap > 7U) ? 7U : (uint8_t)peer_gap;
    }
  } else if (g_pending_cs_result_stage == 3U) {
    g_pending_cs_result_stage = 4U;
    g_cs_peer_chunk_start_step = 0U;
    g_cs_next_chunk_stage_heartbeat = 0U;
    g_cs_next_peer_stage_heartbeat = g_vpr_transport->heartbeat + 1U;
  } else if (g_pending_cs_result_stage == 4U) {
    g_cs_peer_chunk_start_step = (uint8_t)(g_cs_peer_chunk_start_step + steps_built);
    if (has_more) {
      g_pending_cs_result_stage = 5U;
      g_cs_next_chunk_stage_heartbeat =
          g_vpr_transport->heartbeat + current_chunk_stage_delay_ticks();
      g_cs_next_peer_stage_heartbeat = g_vpr_transport->heartbeat + 1U;
    } else {
      g_pending_cs_result_stage = 0U;
      g_cs_next_chunk_stage_heartbeat = 0U;
      g_cs_next_peer_stage_heartbeat = 0U;
      if (g_cs_procedure_enabled != 0U &&
          g_cs_procedure_counter < g_cs_max_procedure_count) {
        g_cs_next_procedure_heartbeat =
            g_vpr_transport->heartbeat + current_procedure_interval_ticks();
      } else {
        g_cs_procedure_enabled = 0U;
        g_cs_next_procedure_heartbeat = 0U;
        g_cs_last_interval_selector = 0U;
      }
    }
  } else if (g_pending_cs_result_stage == 5U) {
    g_cs_peer_chunk_start_step = (uint8_t)(g_cs_peer_chunk_start_step + steps_built);
    if (has_more) {
      g_pending_cs_result_stage = 5U;
      g_cs_next_chunk_stage_heartbeat =
          g_vpr_transport->heartbeat + current_chunk_stage_delay_ticks();
      g_cs_next_peer_stage_heartbeat = g_vpr_transport->heartbeat + 1U;
    } else if (g_cs_procedure_enabled != 0U &&
               g_cs_procedure_counter < g_cs_max_procedure_count) {
      g_cs_next_procedure_heartbeat =
          g_vpr_transport->heartbeat + current_procedure_interval_ticks();
      g_cs_next_chunk_stage_heartbeat = 0U;
      g_cs_next_peer_stage_heartbeat = 0U;
      g_pending_cs_result_stage = 0U;
    } else {
      g_cs_procedure_enabled = 0U;
      g_cs_next_procedure_heartbeat = 0U;
      g_cs_next_chunk_stage_heartbeat = 0U;
      g_cs_next_peer_stage_heartbeat = 0U;
      g_cs_last_interval_selector = 0U;
      g_pending_cs_result_stage = 0U;
    }
  } else {
    g_cs_next_chunk_stage_heartbeat = 0U;
    g_cs_next_peer_stage_heartbeat = 0U;
    g_pending_cs_result_stage = 0U;
  }
#else
  g_pending_cs_result_stage =
      (g_pending_cs_result_stage < 3U) ? (uint8_t)(g_pending_cs_result_stage + 1U) : 0U;
#endif
  return true;
}

#if !VPR_CS_DEDICATED_IMAGE
static bool publish_pending_ticker_event(void) {
  uint8_t payload[20];
  size_t len = 0U;
  if (g_ticker_event_queue_count == 0U ||
      (g_vpr_transport->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ||
      host_request_pending()) {
    return false;
  }
  const vpr_ticker_event_entry_t *event = &g_ticker_event_queue[g_ticker_event_queue_head];
  zero_vpr_data();
  len = build_vendor_ticker_event_payload(payload, sizeof(payload), event);
  if (len == 0U) {
    return false;
  }
  len = append_h4_vendor_event((uint8_t *)g_vpr_transport->vprData,
                               NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA,
                               VPR_VENDOR_EVENT_TICKER, payload, len);
  if (len == 0U) {
    return false;
  }
  g_vpr_transport->vprLen = (uint32_t)len;
  g_vpr_transport->vprSeq = g_vpr_transport->vprSeq + 1U;
  g_vpr_transport->vprFlags = NRF54L15_VPR_TRANSPORT_FLAG_PENDING;
  bytes_zero(&g_ticker_event_queue[g_ticker_event_queue_head],
             sizeof(g_ticker_event_queue[g_ticker_event_queue_head]));
  g_ticker_event_queue_head = (g_ticker_event_queue_head + 1U) % VPR_TICKER_EVENT_QUEUE_DEPTH;
  g_ticker_event_queue_count = g_ticker_event_queue_count - 1U;
  return true;
}
#endif

static bool host_request_pending(void) {
  return ((g_host_transport->hostFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U) ||
         (g_host_transport->hostLen != 0U);
}

static bool consume_host_request(uint32_t host_seq) {
  if (!host_request_pending()) {
    return false;
  }

  const uint16_t opcode = read_opcode();
  publish_response_for_opcode(opcode);
  g_vpr_transport->vprSeq = host_seq;
  g_vpr_transport->vprFlags = NRF54L15_VPR_TRANSPORT_FLAG_PENDING;
  g_host_transport->hostFlags = 0U;
  g_host_transport->hostLen = 0U;
  g_vpr_transport->status = NRF54L15_VPR_TRANSPORT_STATUS_READY;
  fence_rw();
  return true;
}

__attribute__((noreturn, used, externally_visible)) void vpr_main(void) {
  uint32_t last_seq = 0U;
#if !VPR_CS_DEDICATED_IMAGE
  const bool restored_from_hibernate = restore_hibernate_state();
  g_restored_from_hibernate = restored_from_hibernate ? 1U : 0U;
#endif
  const uint32_t nordic_ctrl =
      (VPRCSR_NORDIC_VPRNORDICCTRL_NORDICKEY_Enabled
       << VPRCSR_NORDIC_VPRNORDICCTRL_NORDICKEY_Pos) |
      VPRCSR_NORDIC_VPRNORDICCTRL_ENABLERTPERIPH_Msk;

  write_vpr_nordic_ctrl(nordic_ctrl);
  fence_rw();

  g_host_transport->magic = NRF54L15_VPR_TRANSPORT_MAGIC;
  g_host_transport->version = NRF54L15_VPR_TRANSPORT_VERSION;
  g_vpr_transport->magic = NRF54L15_VPR_TRANSPORT_MAGIC;
  g_vpr_transport->version = NRF54L15_VPR_TRANSPORT_VERSION;
  g_vpr_transport->status = NRF54L15_VPR_TRANSPORT_STATUS_READY;
  g_vpr_transport->heartbeat = 0U;
  g_vpr_transport->vprFlags = 0U;
  g_vpr_transport->vprLen = 0U;
  g_vpr_transport->lastError = 0U;
#if !VPR_CS_DEDICATED_IMAGE
  g_vpr_transport->reserved = g_restored_from_hibernate;
  if (!restored_from_hibernate) {
    g_ticker_enabled = 0U;
    g_ticker_period_ticks = 0U;
    g_ticker_step = 1U;
    g_ticker_accum = 0U;
    g_ticker_count = 0U;
    g_ticker_event_enabled = 0U;
    g_ticker_event_emit_every = 0U;
    g_ticker_event_last_emitted_count = 0U;
    g_ticker_event_sequence = 0U;
    clear_ticker_event_queue();
    g_ticker_event_drop_count = 0U;
  }
  g_pending_cs_result_stage = 0U;
  g_pending_hibernate = 0U;
#else
  reset_dedicated_cs_state();
  g_vpr_transport->reserved = current_link_state_packed();
  g_pending_cs_result_stage = 0U;
#endif
  fence_rw();

  while (1) {
    g_vpr_transport->heartbeat = g_vpr_transport->heartbeat + 1U;
#if !VPR_CS_DEDICATED_IMAGE
    if (g_ticker_enabled != 0U && g_ticker_period_ticks != 0U) {
      g_ticker_accum = g_ticker_accum + 1U;
      if (g_ticker_accum >= g_ticker_period_ticks) {
        g_ticker_accum = 0U;
        g_ticker_count = g_ticker_count + g_ticker_step;
        if (g_ticker_event_enabled != 0U && g_ticker_event_emit_every != 0U &&
            g_ticker_count >= (g_ticker_event_last_emitted_count + g_ticker_event_emit_every)) {
          (void)enqueue_ticker_event(g_ticker_count, g_ticker_step, g_vpr_transport->heartbeat);
        }
      }
    }
#endif
    fence_rw();

    const uint32_t host_seq = g_host_transport->hostSeq;
    const uint32_t host_flags = g_host_transport->hostFlags;
#if !VPR_CS_DEDICATED_IMAGE
    g_vpr_transport->reserved =
        ((host_seq & 0xFFFFU) << 16U) |
        ((host_flags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ? 0x2U : 0U) |
        0x4U;
#else
    (void)host_flags;
    g_vpr_transport->reserved = current_link_state_packed();
#endif
    fence_rw();

    if ((host_seq != last_seq) && host_request_pending()) {
      if (consume_host_request(host_seq)) {
        last_seq = host_seq;
      }
    }

    if ((g_vpr_transport->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) == 0U &&
        !host_request_pending()) {
#if !VPR_CS_DEDICATED_IMAGE
      if (!publish_pending_cs_result_packet()) {
        (void)publish_pending_ticker_event();
      }
#else
      (void)schedule_next_cs_procedure();
      (void)publish_pending_cs_result_packet();
#endif
    }

#if !VPR_CS_DEDICATED_IMAGE
    if (g_pending_hibernate != 0U &&
        (g_vpr_transport->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) == 0U) {
      g_pending_hibernate = 0U;
      fence_rw();
      write_vpr_sleepctrl(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_HIBERNATE |
                          VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_STACKONSLEEP_Msk);
      fence_rw();
      /* Nordic documents that VPR wake can fail if WFI is entered with MIE clear. */
      enable_machine_interrupts();
      __asm__ volatile("wfi");
    }
#endif
  }
}

__attribute__((naked, noreturn, section(".text.start"))) void _start(void) {
  __asm__ volatile(
      "li gp, 0\n"
      "la sp, __stack_top\n"
      "j vpr_main\n");
}
