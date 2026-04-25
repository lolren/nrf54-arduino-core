#pragma once

#include <stdint.h>

#include "matter_onoff_light.h"

namespace xiao_nrf54l15 {

enum class MatterInteractionStatus : uint8_t {
  kSuccess = 0U,
  kInvalidState = 1U,
  kUnsupportedEndpoint = 2U,
  kUnsupportedCluster = 3U,
  kUnsupportedAttribute = 4U,
  kUnsupportedCommand = 5U,
  kInvalidCommandData = 6U,
  kStorageFailure = 7U,
};

enum class MatterAttributeValueType : uint8_t {
  kNone = 0U,
  kBool = 1U,
  kUint16 = 2U,
};

struct MatterAttributePath {
  MatterEndpointId endpointId =
      Nrf54MatterOnOffLightDevice::kEndpointId;
  MatterClusterId clusterId = 0U;
  uint32_t attributeId = 0U;
};

struct MatterCommandPath {
  MatterEndpointId endpointId =
      Nrf54MatterOnOffLightDevice::kEndpointId;
  MatterClusterId clusterId = 0U;
  uint32_t commandId = 0U;
};

struct MatterAttributeValue {
  MatterAttributeValueType type = MatterAttributeValueType::kNone;
  bool boolValue = false;
  uint16_t uint16Value = 0U;
};

struct MatterCommandRequest {
  MatterCommandPath path = {};
  bool hasUint16Value = false;
  uint16_t uint16Value = 0U;
};

struct MatterCommandResult {
  MatterInteractionStatus status = MatterInteractionStatus::kInvalidState;
  bool accepted = false;
  bool stateChanged = false;
  MatterOnOffLightDeviceState light = {};
};

class Nrf54MatterOnOffLightEndpoint {
 public:
  static constexpr MatterEndpointId kEndpointId =
      Nrf54MatterOnOffLightDevice::kEndpointId;
  static constexpr MatterClusterId kOnOffClusterId =
      Nrf54MatterOnOffLightDevice::kOnOffClusterId;
  static constexpr MatterClusterId kIdentifyClusterId =
      Nrf54MatterOnOffLightDevice::kIdentifyClusterId;

  static constexpr uint32_t kOnOffAttributeId =
      Nrf54MatterOnOffLightDevice::kOnOffAttributeId;
  static constexpr uint32_t kGlobalSceneControlAttributeId =
      Nrf54MatterOnOffLightDevice::kGlobalSceneControlAttributeId;
  static constexpr uint32_t kIdentifyTimeAttributeId =
      Nrf54MatterOnOffLightDevice::kIdentifyTimeAttributeId;

  static constexpr uint32_t kOffCommandId = 0x0000U;
  static constexpr uint32_t kOnCommandId = 0x0001U;
  static constexpr uint32_t kToggleCommandId = 0x0002U;
  static constexpr uint32_t kIdentifyCommandId = 0x0000U;

  Nrf54MatterOnOffLightEndpoint() = default;
  explicit Nrf54MatterOnOffLightEndpoint(Nrf54MatterOnOffLightDevice* device);

  void attach(Nrf54MatterOnOffLightDevice* device);
  void detach();
  bool attached() const;

  bool snapshot(MatterOnOffLightDeviceState* outState) const;
  bool readAttribute(const MatterAttributePath& path,
                     MatterAttributeValue* outValue,
                     MatterInteractionStatus* outStatus = nullptr) const;
  bool invokeCommand(const MatterCommandRequest& request,
                     MatterCommandResult* outResult = nullptr);

  static const char* statusName(MatterInteractionStatus status);
  static const char* clusterName(MatterClusterId clusterId);
  static const char* attributeName(MatterClusterId clusterId,
                                   uint32_t attributeId);
  static const char* commandName(MatterClusterId clusterId,
                                 uint32_t commandId);

 private:
  static void setStatus(MatterInteractionStatus* outStatus,
                        MatterInteractionStatus status);
  void fillResult(MatterCommandResult* outResult,
                  MatterInteractionStatus status,
                  bool accepted,
                  bool stateChanged) const;

  Nrf54MatterOnOffLightDevice* device_ = nullptr;
};

}  // namespace xiao_nrf54l15
