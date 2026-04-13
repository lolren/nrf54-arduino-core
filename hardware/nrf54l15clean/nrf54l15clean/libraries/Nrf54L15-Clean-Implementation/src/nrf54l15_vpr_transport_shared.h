#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRF54L15_VPR_IMAGE_BASE 0x2003CE00UL
#define NRF54L15_VPR_IMAGE_SIZE 0x00003000UL
#define NRF54L15_VPR_TRANSPORT_HOST_BASE 0x20020000UL
#define NRF54L15_VPR_TRANSPORT_HOST_SIZE 0x00000800UL
#define NRF54L15_VPR_TRANSPORT_VPR_BASE 0x20018000UL
#define NRF54L15_VPR_TRANSPORT_VPR_SIZE 0x00000800UL
#define NRF54L15_VPR_SHARED_BASE NRF54L15_VPR_TRANSPORT_VPR_BASE
#define NRF54L15_VPR_SHARED_SIZE \
  ((NRF54L15_VPR_TRANSPORT_HOST_BASE + NRF54L15_VPR_TRANSPORT_HOST_SIZE) - \
   NRF54L15_VPR_TRANSPORT_VPR_BASE)
#define NRF54L15_VPR_CONTEXT_SAVE_BASE 0x2003FE00UL
#define NRF54L15_VPR_CONTEXT_SAVE_SIZE 0x00000200UL

#define NRF54L15_VPR_TRANSPORT_MAGIC 0x56505452UL
#define NRF54L15_VPR_TRANSPORT_VERSION 0x00000001UL

#define NRF54L15_VPR_TRANSPORT_FLAG_PENDING 0x00000001UL

#define NRF54L15_VPR_TRANSPORT_STATUS_STOPPED 0x00000000UL
#define NRF54L15_VPR_TRANSPORT_STATUS_BOOTING 0x00000001UL
#define NRF54L15_VPR_TRANSPORT_STATUS_READY 0x00000002UL
#define NRF54L15_VPR_TRANSPORT_STATUS_ERROR 0x000000EEUL

#define NRF54L15_VPR_TRANSPORT_HIBERNATE_COOKIE 0x48565052UL
#define NRF54L15_VPR_TRANSPORT_HIBERNATE_FLAG_TICKER_VALID 0x00000001UL
#define NRF54L15_VPR_TRANSPORT_HIBERNATE_FLAG_TICKER_ENABLED 0x00000002UL

#define NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA 128U
#define NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA 256U
#define NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_COUNT 8U
#define NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_RESPONSE 128U

#define NRF54L15_VPR_TRANSPORT_HOST_TO_VPR_TASK 21U
#define NRF54L15_VPR_TRANSPORT_VPR_TO_HOST_EVENT 20U

typedef struct {
  uint16_t opcode;
  uint16_t responseLen;
  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_RESPONSE];
} Nrf54l15VprTransportScript;

typedef struct {
  volatile uint32_t magic;
  volatile uint32_t version;
  volatile uint32_t hostSeq;
  volatile uint32_t hostFlags;
  volatile uint32_t hostLen;
  volatile uint32_t scriptCount;
  volatile uint32_t reserved;
  volatile uint32_t hibernateCookie;
  volatile uint32_t hibernateFlags;
  volatile uint32_t retainedTickerPeriodTicks;
  volatile uint32_t retainedTickerStep;
  volatile uint32_t retainedTickerAccum;
  volatile uint32_t retainedTickerCount;
  volatile uint8_t hostData[NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA];
  Nrf54l15VprTransportScript scripts[NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_COUNT];
} Nrf54l15VprTransportHostShared;

typedef struct {
  volatile uint32_t magic;
  volatile uint32_t version;
  volatile uint32_t status;
  volatile uint32_t heartbeat;
  volatile uint32_t vprSeq;
  volatile uint32_t vprFlags;
  volatile uint32_t vprLen;
  volatile uint32_t lastOpcode;
  volatile uint32_t lastError;
  volatile uint32_t reserved;
  volatile uint32_t reservedAux;
  volatile uint32_t reservedMeta;
  volatile uint8_t vprData[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA];
} Nrf54l15VprTransportVprShared;

static inline volatile Nrf54l15VprTransportHostShared* nrf54l15_vpr_transport_host_shared(void) {
  return (volatile Nrf54l15VprTransportHostShared*)(uintptr_t)NRF54L15_VPR_TRANSPORT_HOST_BASE;
}

static inline volatile Nrf54l15VprTransportVprShared* nrf54l15_vpr_transport_vpr_shared(void) {
  return (volatile Nrf54l15VprTransportVprShared*)(uintptr_t)NRF54L15_VPR_TRANSPORT_VPR_BASE;
}

#ifdef __cplusplus
}
#endif
