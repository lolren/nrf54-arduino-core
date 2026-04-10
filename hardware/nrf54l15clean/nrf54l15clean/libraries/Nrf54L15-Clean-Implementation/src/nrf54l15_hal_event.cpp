#include "nrf54l15_hal.h"

namespace xiao_nrf54l15 {

namespace {

inline bool validEguChannel(uint8_t channel, uint8_t channelCount) {
  return channel < channelCount && channel < EGU_TASKS_TRIGGER_MaxCount;
}

inline uint32_t eguInterruptMask(uint8_t channel) {
  return (1UL << channel);
}

inline uint32_t dppiConfigValue(uint8_t dppiChannel, bool enable) {
  uint32_t value =
      (static_cast<uint32_t>(dppiChannel) << EGU_PUBLISH_TRIGGERED_CHIDX_Pos);
  if (enable) {
    value |= EGU_PUBLISH_TRIGGERED_EN_Msk;
  }
  return value;
}

}  // namespace

Egu::Egu(uint32_t base, uint8_t channelCount)
    : egu_(reinterpret_cast<NRF_EGU_Type*>(base)),
      channelCount_(channelCount > EGU_TASKS_TRIGGER_MaxCount
                        ? EGU_TASKS_TRIGGER_MaxCount
                        : channelCount) {}

bool Egu::trigger(uint8_t channel) {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return false;
  }
  egu_->TASKS_TRIGGER[channel] = EGU_TASKS_TRIGGER_TASKS_TRIGGER_Trigger;
  return true;
}

bool Egu::pollTriggered(uint8_t channel, bool clearEvent) {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return false;
  }
  const bool generated =
      ((egu_->EVENTS_TRIGGERED[channel] &
        EGU_EVENTS_TRIGGERED_EVENTS_TRIGGERED_Msk) >>
       EGU_EVENTS_TRIGGERED_EVENTS_TRIGGERED_Pos) ==
      EGU_EVENTS_TRIGGERED_EVENTS_TRIGGERED_Generated;
  if (generated && clearEvent) {
    egu_->EVENTS_TRIGGERED[channel] = 0U;
  }
  return generated;
}

void Egu::clearEvent(uint8_t channel) {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return;
  }
  egu_->EVENTS_TRIGGERED[channel] = 0U;
}

void Egu::clearAllEvents() {
  if (egu_ == nullptr) {
    return;
  }
  for (uint8_t channel = 0; channel < channelCount_; ++channel) {
    egu_->EVENTS_TRIGGERED[channel] = 0U;
  }
}

void Egu::enableInterrupt(uint8_t channel, bool enable) {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return;
  }
  const uint32_t mask = eguInterruptMask(channel);
  if (enable) {
    egu_->INTENSET = mask;
  } else {
    egu_->INTENCLR = mask;
  }
}

bool Egu::configurePublish(uint8_t channel, uint8_t dppiChannel, bool enable) {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return false;
  }
  egu_->PUBLISH_TRIGGERED[channel] = dppiConfigValue(dppiChannel, enable);
  return true;
}

bool Egu::configureSubscribe(uint8_t channel, uint8_t dppiChannel, bool enable) {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return false;
  }
  egu_->SUBSCRIBE_TRIGGER[channel] = dppiConfigValue(dppiChannel, enable);
  return true;
}

volatile uint32_t* Egu::publishTriggeredConfigRegister(uint8_t channel) const {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return nullptr;
  }
  return &egu_->PUBLISH_TRIGGERED[channel];
}

volatile uint32_t* Egu::subscribeTriggerConfigRegister(uint8_t channel) const {
  if (egu_ == nullptr || !validEguChannel(channel, channelCount_)) {
    return nullptr;
  }
  return &egu_->SUBSCRIBE_TRIGGER[channel];
}

}  // namespace xiao_nrf54l15
