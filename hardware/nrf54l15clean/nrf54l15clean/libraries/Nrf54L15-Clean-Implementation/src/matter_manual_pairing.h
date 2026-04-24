#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xiao_nrf54l15 {

enum class MatterCommissioningFlow : uint8_t {
  kStandard = 0,
  kUserActionRequired = 1,
  kCustom = 2,
};

struct MatterManualPairingPayload {
  uint32_t setupPinCode = 0;
  uint16_t discriminator = 0;
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  MatterCommissioningFlow commissioningFlow = MatterCommissioningFlow::kStandard;
};

enum MatterRendezvousInformationFlag : uint8_t {
  kMatterRendezvousNone = 0,
  kMatterRendezvousSoftAP = 1U << 0U,
  kMatterRendezvousBLE = 1U << 1U,
  kMatterRendezvousOnNetwork = 1U << 2U,
  kMatterRendezvousWiFiPAF = 1U << 3U,
  kMatterRendezvousNFC = 1U << 4U,
  kMatterRendezvousThread = 1U << 5U,
};

struct MatterQrCodePayload {
  uint8_t version = 0;
  uint32_t setupPinCode = 0;
  uint16_t discriminator = 0;
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  uint8_t rendezvousFlags = kMatterRendezvousOnNetwork;
  MatterCommissioningFlow commissioningFlow = MatterCommissioningFlow::kStandard;
};

constexpr size_t kMatterManualPairingShortCodeLength = 11;
constexpr size_t kMatterManualPairingLongCodeLength = 21;
constexpr size_t kMatterQrCodeTextLength = 22;
constexpr size_t kMatterQrCodePayloadBytes = 11;

bool matterSetupPinValid(uint32_t setupPinCode);
bool matterDiscriminatorValid(uint16_t discriminator);
bool matterRendezvousFlagsValid(uint8_t rendezvousFlags);
bool matterManualPairingPayloadValid(const MatterManualPairingPayload& payload);
bool matterQrCodePayloadValid(const MatterQrCodePayload& payload);
size_t matterManualPairingCodeLength(const MatterManualPairingPayload& payload);

bool matterManualPairingCode(const MatterManualPairingPayload& payload,
                             char* outBuffer, size_t outBufferSize);
bool matterQrCode(const MatterQrCodePayload& payload, char* outBuffer,
                  size_t outBufferSize);

}  // namespace xiao_nrf54l15
