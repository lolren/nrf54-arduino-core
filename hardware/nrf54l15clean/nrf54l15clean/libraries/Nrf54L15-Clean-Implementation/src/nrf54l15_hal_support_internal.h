#pragma once

#include "nrf54l15_hal.h"

namespace xiao_nrf54l15::hal_internal {

uint32_t gpioBaseForPort(uint8_t port);

bool waitForEvent(uint32_t base, uint32_t eventOffset, uint32_t spinLimit);
bool waitForEventOrError(uint32_t base, uint32_t eventOffset,
                         uint32_t errorOffset, uint32_t spinLimit);
void clearEvent(uint32_t base, uint32_t eventOffset);
bool waitForNonZero(volatile uint32_t* reg, uint32_t spinLimit);
bool tryAllocateHighestSetBit(uint32_t mask, uint8_t* outBit);

int32_t adcRawToMilliVolts(int16_t raw, AdcResolution resolution,
                           AdcGain gain, bool differential);
uint32_t saadcPselValue(const Pin& pin);

uint32_t spimPrescaler(uint32_t coreHz, uint32_t targetHz, uint32_t minDivisor);
void clearTwimState(uint32_t base);

uint32_t absDiffU32(uint32_t a, uint32_t b);

uint32_t timerCompareEventOffset(uint8_t channel);
uint32_t timerCaptureTaskOffset(uint8_t channel);
uint32_t timerCcOffset(uint8_t channel);
uint32_t timerOneShotOffset(uint8_t channel);
uint32_t timerPublishCompareOffset(uint8_t channel);
uint32_t timerSubscribeCaptureOffset(uint8_t channel);
uint32_t timerCompareIntMask(uint8_t channel);

struct PwmTiming {
  uint8_t prescaler;
  uint16_t countertop;
  uint32_t actualHz;
};

bool computePwmTiming(uint32_t targetHz, PwmTiming* timing);

uint32_t pwmTaskSeqStartOffset(uint8_t sequence);
uint32_t pwmEventSeqStartedOffset(uint8_t sequence);
uint32_t pwmEventSeqEndOffset(uint8_t sequence);
uint32_t pwmEventDmaSeqEndOffset(uint8_t sequence);
uint32_t pwmEventDmaSeqReadyOffset(uint8_t sequence);
uint32_t pwmEventDmaSeqBusErrorOffset(uint8_t sequence);
uint32_t pwmDmaSeqPtrOffset(uint8_t sequence);
uint32_t pwmDmaSeqMaxCntOffset(uint8_t sequence);
uint32_t pwmDmaSeqAmountOffset(uint8_t sequence);
uint32_t pwmDmaSeqCurrentAmountOffset(uint8_t sequence);
uint32_t pwmDmaSeqBusErrorAddressOffset(uint8_t sequence);

uint32_t gpioteInEventOffset(uint8_t channel);
uint32_t gpioteTaskOutOffset(uint8_t channel);
uint32_t gpioteTaskSetOffset(uint8_t channel);
uint32_t gpioteTaskClrOffset(uint8_t channel);
uint32_t gpioteConfigOffset(uint8_t channel);
uint32_t gpioteSubscribeOutOffset(uint8_t channel);
uint32_t gpioteSubscribeSetOffset(uint8_t channel);
uint32_t gpioteSubscribeClrOffset(uint8_t channel);

}  // namespace xiao_nrf54l15::hal_internal
