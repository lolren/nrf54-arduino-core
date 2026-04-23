#pragma once

#include <stdint.h>

#include "openthread_platform_nrf54l15.h"

#include <openthread/dataset.h>
#include <openthread/error.h>
#include <openthread/ip6.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

namespace xiao_nrf54l15 {

class Nrf54ThreadExperimental {
 public:
  using UdpReceiveCallback = void (*)(void* context,
                                      const uint8_t* payload,
                                      uint16_t length,
                                      const otMessageInfo& messageInfo);

  enum class Role : uint8_t {
    kDisabled = 0U,
    kDetached = 1U,
    kChild = 2U,
    kRouter = 3U,
    kLeader = 4U,
    kUnknown = 255U,
  };

  Nrf54ThreadExperimental() = default;

  bool begin(bool wipeSettings = true);
  void process();

  bool setActiveDataset(const otOperationalDataset& dataset);
  bool getActiveDataset(otOperationalDataset* outDataset) const;

  bool openUdp(uint16_t port,
               UdpReceiveCallback callback,
               void* callbackContext = nullptr);
  bool sendUdp(const otIp6Address& peerAddr,
               uint16_t peerPort,
               const void* payload,
               uint16_t payloadLength);
  bool getLeaderRloc(otIp6Address* outLeaderAddr) const;

  bool started() const;
  bool attached() const;
  Role role() const;
  const char* roleName() const;
  uint16_t rloc16() const;
  otError lastError() const;
  otError lastUdpError() const;
  otInstance* rawInstance() const;

  static const char* roleName(Role role);
  static void buildDemoDataset(otOperationalDataset* outDataset);

 private:
  static void handleUdpReceiveStatic(void* context,
                                     otMessage* message,
                                     const otMessageInfo* messageInfo);
  void handleUdpReceive(otMessage* message, const otMessageInfo* messageInfo);

  static Role convertRole(otDeviceRole role);

  static constexpr uint32_t kStageInitDelayMs = 2000UL;
  static constexpr uint32_t kStageDatasetApplyDelayMs = 4000UL;
  static constexpr uint32_t kStageIp6EnableDelayMs = 5000UL;
  static constexpr uint32_t kStageThreadEnableDelayMs = 6000UL;

  otInstance* instance_ = nullptr;
  otUdpSocket udpSocket_ = {};
  otOperationalDataset dataset_ = {};
  UdpReceiveCallback udpCallback_ = nullptr;
  void* udpCallbackContext_ = nullptr;

  uint32_t beginMs_ = 0;
  uint16_t udpPort_ = 0U;
  otError lastError_ = OT_ERROR_NONE;
  otError lastUdpError_ = OT_ERROR_NONE;

  bool beginCalled_ = false;
  bool settingsWiped_ = false;
  bool datasetConfigured_ = false;
  bool datasetApplied_ = false;
  bool linkConfigured_ = false;
  bool ip6Enabled_ = false;
  bool threadEnabled_ = false;
  bool udpRequested_ = false;
  bool udpOpened_ = false;
  bool wipeSettings_ = true;
};

}  // namespace xiao_nrf54l15
