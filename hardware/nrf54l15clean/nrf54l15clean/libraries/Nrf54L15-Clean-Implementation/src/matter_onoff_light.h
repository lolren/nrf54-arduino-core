#pragma once

#include <Preferences.h>
#include <stdint.h>

#include "matter_foundation_target.h"

namespace xiao_nrf54l15 {

enum class MatterOnOffLightStartUpBehavior : uint8_t {
  kForceOff = 0U,
  kForceOn = 1U,
  kTogglePrevious = 2U,
  kRestorePrevious = 0xFFU,
};

struct MatterOnOffLightDeviceState {
  bool on = false;
  bool identifying = false;
  bool persistentStorageOpen = false;
  uint16_t identifyTimeSeconds = 0U;
  MatterOnOffLightStartUpBehavior startUpBehavior =
      MatterOnOffLightStartUpBehavior::kRestorePrevious;
};

struct MatterOnOffLightPersistentState {
  uint32_t magic = 0U;
  uint16_t version = 0U;
  uint8_t flags = 0U;
  uint8_t startUpBehavior = 0U;
};

class Nrf54MatterOnOffLightDevice {
 public:
  using OnChangeCallback = void (*)(void* context, bool on);
  using IdentifyCallback =
      void (*)(void* context, bool active, uint16_t remainingSeconds);

  static constexpr MatterEndpointId kEndpointId =
      Nrf54MatterOnOffLightFoundation::kPrimaryEndpointId;
  static constexpr MatterClusterId kOnOffClusterId =
      Nrf54MatterOnOffLightFoundation::kOnOffClusterId;
  static constexpr MatterClusterId kIdentifyClusterId =
      Nrf54MatterOnOffLightFoundation::kIdentifyClusterId;
  static constexpr uint32_t kOnOffAttributeId = 0x0000U;
  static constexpr uint32_t kGlobalSceneControlAttributeId = 0x4000U;
  static constexpr uint32_t kIdentifyTimeAttributeId = 0x0000U;

  Nrf54MatterOnOffLightDevice() = default;

  bool begin(const char* storageNamespace = "matter_onoff",
             bool restoreState = true);
  void end();
  void process();

  bool setOn(bool on, bool persist = true);
  bool toggle(bool persist = true);
  bool on() const;

  bool setStartUpBehavior(MatterOnOffLightStartUpBehavior behavior,
                          bool persist = true);
  MatterOnOffLightStartUpBehavior startUpBehavior() const;

  bool setIdentifyTimeSeconds(uint16_t seconds);
  void stopIdentify();
  bool identifying() const;
  uint16_t identifyTimeSeconds() const;

  bool savePersistentState();
  bool clearPersistentState();
  bool snapshot(MatterOnOffLightDeviceState* outState) const;

  bool readOnOffAttribute(bool* outOn) const;
  bool readGlobalSceneControlAttribute(bool* outEnabled) const;
  bool readIdentifyTimeAttribute(uint16_t* outSeconds) const;

  void setOnChangeCallback(OnChangeCallback callback,
                           void* context = nullptr);
  void setIdentifyCallback(IdentifyCallback callback,
                           void* context = nullptr);

  static bool persistentStateValid(
      const MatterOnOffLightPersistentState& state);
  static bool startUpBehaviorValid(MatterOnOffLightStartUpBehavior behavior);
  static const char* startUpBehaviorName(
      MatterOnOffLightStartUpBehavior behavior);

 private:
  static constexpr uint32_t kPersistentStateMagic = 0x4D4F4F46UL;
  static constexpr uint16_t kPersistentStateVersion = 1U;
  static constexpr uint8_t kPersistentFlagOn = 0x01U;
  static constexpr char kPersistentStateKey[] = "state";

  bool loadPersistentState(MatterOnOffLightPersistentState* outState);
  bool applyStartUpBehavior(bool previousOn);
  void notifyOnChange();
  void notifyIdentifyChange();

  Preferences prefs_;
  bool storageOpen_ = false;
  bool on_ = false;
  bool globalSceneControl_ = true;
  uint32_t identifyEndMs_ = 0U;
  MatterOnOffLightStartUpBehavior startUpBehavior_ =
      MatterOnOffLightStartUpBehavior::kRestorePrevious;
  OnChangeCallback onChangeCallback_ = nullptr;
  void* onChangeContext_ = nullptr;
  IdentifyCallback identifyCallback_ = nullptr;
  void* identifyContext_ = nullptr;
};

}  // namespace xiao_nrf54l15
