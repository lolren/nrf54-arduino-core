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

#define BLE_CS_MAIN_MODE2 0x02U

#define BLE_CS_HCI_OP_READ_REMOTE_SUPPORTED_CAPABILITIES 0x208AU
#define BLE_CS_HCI_OP_SECURITY_ENABLE 0x208CU
#define BLE_CS_HCI_OP_SET_DEFAULT_SETTINGS 0x208DU
#define BLE_CS_HCI_OP_CREATE_CONFIG 0x2090U
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
static uint8_t g_cs_config_id = 1U;
static uint16_t g_cs_procedure_counter = 0U;
#endif
#if !VPR_CS_DEDICATED_IMAGE
static uint32_t g_pending_hibernate = 0U;
static uint32_t g_restored_from_hibernate = 0U;
#endif

static bool host_request_pending(void);

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

static void write_le24(uint8_t *dst, uint32_t value) {
  if (dst == NULL) {
    return;
  }
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
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

static void append_mode2_demo_step(uint8_t *dst, uint8_t channel) {
  if (dst == NULL) {
    return;
  }
  dst[0] = BLE_CS_MAIN_MODE2;
  dst[1] = channel;
  dst[2] = 5U;
  dst[3] = 0U;
  dst[4] = 0x00U;
  dst[5] = 0x04U;
  dst[6] = 0x00U;
  dst[7] = 0x00U;
}

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
  static const uint8_t channel_map[10] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x1FU,
                                          0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
  if (payload == NULL || max_len < 33U) {
    return 0U;
  }
  bytes_zero(payload, 33U);
  payload[0] = 0U;
  write_le16(&payload[1], conn_handle);
#if VPR_CS_DEDICATED_IMAGE
  payload[3] = g_cs_config_id;
#else
  payload[3] = 1U;
#endif
  payload[4] = 1U;
  payload[5] = BLE_CS_MAIN_MODE2;
  payload[6] = 0xFFU;
  payload[7] = 3U;
  payload[8] = 5U;
  payload[9] = 1U;
  payload[10] = 1U;
  payload[11] = 0U;
  payload[12] = 1U;
  payload[13] = 2U;
  bytes_copy(&payload[14], channel_map, sizeof(channel_map));
  payload[24] = 1U;
  payload[25] = 1U;
  payload[26] = 1U;
  payload[27] = 3U;
  payload[28] = 0x01U;
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
#else
  payload[3] = 1U;
#endif
  payload[4] = 1U;
  payload[5] = 2U;
  payload[6] = (uint8_t)(int8_t)-12;
  write_le24(&payload[7], 0x000456UL);
  payload[10] = 4U;
  write_le16(&payload[11], 100U);
  write_le16(&payload[13], 200U);
  write_le16(&payload[15], 300U);
  write_le16(&payload[17], 8U);
  write_le16(&payload[19], 12U);
  return 21U;
}

static size_t build_subevent_initial_payload(uint8_t *payload, size_t max_len,
                                             uint16_t conn_handle) {
  const uint32_t channels = g_host_transport->reserved;
  if (payload == NULL || max_len < 31U) {
    return 0U;
  }
  bytes_zero(payload, 31U);
  write_le16(&payload[0], conn_handle);
#if VPR_CS_DEDICATED_IMAGE
  payload[2] = g_cs_config_id;
#else
  payload[2] = 1U;
#endif
  write_le16(&payload[3], 0x1234U);
#if VPR_CS_DEDICATED_IMAGE
  write_le16(&payload[5], g_cs_procedure_counter);
#else
  write_le16(&payload[5], 7U);
#endif
  write_le16(&payload[7], 0U);
  payload[9] = 0U;
  payload[10] = 0x01U;
  payload[11] = 0x01U;
  payload[12] = 0U;
  payload[13] = 2U;
  payload[14] = 2U;
  append_mode2_demo_step(&payload[15], (uint8_t)(channels & 0xFFU));
  append_mode2_demo_step(&payload[23], (uint8_t)((channels >> 8U) & 0xFFU));
  return 31U;
}

static size_t build_subevent_continue_payload(uint8_t *payload, size_t max_len,
                                              uint16_t conn_handle) {
  const uint32_t channels = g_host_transport->reserved;
  if (payload == NULL || max_len < 24U) {
    return 0U;
  }
  bytes_zero(payload, 24U);
  write_le16(&payload[0], conn_handle);
#if VPR_CS_DEDICATED_IMAGE
  payload[2] = g_cs_config_id;
#else
  payload[2] = 1U;
#endif
  payload[3] = 0U;
  payload[4] = 0U;
  payload[5] = 0U;
  payload[6] = 2U;
  payload[7] = 2U;
  append_mode2_demo_step(&payload[8], (uint8_t)((channels >> 16U) & 0xFFU));
  append_mode2_demo_step(&payload[16], (uint8_t)((channels >> 24U) & 0xFFU));
  return 24U;
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
  uint16_t conn_handle = read_conn_handle();
  size_t offset = 0U;
  zero_vpr_data();

  switch (opcode) {
    case BLE_CS_HCI_OP_READ_REMOTE_SUPPORTED_CAPABILITIES: {
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, 0U);
      if (len == 0U) {
        return false;
      }
      offset += len;
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
#if VPR_CS_DEDICATED_IMAGE
      if (g_host_transport->hostLen >= 7U) {
        g_cs_config_id = g_host_transport->hostData[6];
      }
#endif
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, 0U);
      if (len == 0U) {
        return false;
      }
      offset += len;
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
    case BLE_CS_HCI_OP_SECURITY_ENABLE: {
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, 0U);
      if (len == 0U) {
        return false;
      }
      offset += len;
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
#if VPR_CS_DEDICATED_IMAGE
      if (g_host_transport->hostLen >= 7U) {
        g_cs_config_id = g_host_transport->hostData[6];
      }
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
    case BLE_CS_HCI_OP_PROCEDURE_ENABLE: {
#if VPR_CS_DEDICATED_IMAGE
      if (g_host_transport->hostLen >= 8U) {
        g_cs_config_id = g_host_transport->hostData[6];
        if (g_host_transport->hostData[7] != 0U) {
          g_cs_procedure_counter =
              (uint16_t)(g_cs_procedure_counter + 1U);
          if (g_cs_procedure_counter == 0U) {
            g_cs_procedure_counter = 1U;
          }
        }
      }
#endif
      size_t len = append_h4_command_status((uint8_t *)g_vpr_transport->vprData + offset,
                                            NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA - offset,
                                            opcode, 0U);
      if (len == 0U) {
        return false;
      }
      offset += len;
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
      g_pending_cs_result_stage = 1U;
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
  uint16_t conn_handle = read_conn_handle();
  if (g_pending_cs_result_stage == 0U ||
      (g_vpr_transport->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ||
      host_request_pending()) {
    return false;
  }
  if (g_pending_cs_result_stage == 1U) {
    len = build_subevent_initial_payload(payload, sizeof(payload), conn_handle);
    if (len == 0U) {
      return false;
    }
    len = append_h4_le_meta(packet, sizeof(packet), BLE_CS_HCI_EVT_SUBEVENT_RESULT, payload, len);
  } else if (g_pending_cs_result_stage == 2U) {
    len = build_subevent_continue_payload(payload, sizeof(payload), conn_handle);
    if (len == 0U) {
      return false;
    }
    len = append_h4_le_meta(packet, sizeof(packet), BLE_CS_HCI_EVT_SUBEVENT_RESULT_CONTINUE,
                            payload, len);
  } else if (g_pending_cs_result_stage == 3U) {
    payload[0] = 1U;
    len = append_h4_vendor_event(packet, sizeof(packet),
                                 VPR_VENDOR_EVENT_CS_PEER_RESULT_TRIGGER, payload, 1U);
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
  g_pending_cs_result_stage =
      (g_pending_cs_result_stage < 3U) ? (uint8_t)(g_pending_cs_result_stage + 1U) : 0U;
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

__attribute__((noreturn)) void vpr_main(void) {
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
  g_vpr_transport->reserved = 0U;
  g_pending_cs_result_stage = 0U;
  g_cs_config_id = 1U;
  g_cs_procedure_counter = 0U;
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
    g_vpr_transport->reserved =
        ((host_seq & 0xFFFFU) << 16U) |
        ((host_flags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ? 0x2U : 0U) |
        0x4U;
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
