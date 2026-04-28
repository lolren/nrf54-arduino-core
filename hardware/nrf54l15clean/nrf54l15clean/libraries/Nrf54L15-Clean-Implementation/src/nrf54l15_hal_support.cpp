#include "nrf54l15_hal_support_internal.h"

#include <cmsis.h>
#include <string.h>

namespace xiao_nrf54l15::hal_internal {
using namespace nrf54l15;

namespace {

double adcGainValue(AdcGain gain) {
  switch (gain) {
    case AdcGain::k2:
      return 2.0;
    case AdcGain::k1:
      return 1.0;
    case AdcGain::k2over3:
      return 2.0 / 3.0;
    case AdcGain::k2over4:
      return 2.0 / 4.0;
    case AdcGain::k2over5:
      return 2.0 / 5.0;
    case AdcGain::k2over6:
      return 2.0 / 6.0;
    case AdcGain::k2over7:
      return 2.0 / 7.0;
    case AdcGain::k2over8:
    default:
      return 2.0 / 8.0;
  }
}

uint8_t adcResolutionBits(AdcResolution resolution) {
  return static_cast<uint8_t>(8U + (static_cast<uint8_t>(resolution) * 2U));
}

}  // namespace

uint32_t gpioBaseForPort(uint8_t port) {
  switch (port) {
    case 0:
      return GPIO_P0_BASE;
    case 1:
      return GPIO_P1_BASE;
    case 2:
      return GPIO_P2_BASE;
    default:
      return 0;
  }
}

bool waitForEvent(uint32_t base, uint32_t eventOffset, uint32_t spinLimit) {
  while (spinLimit-- > 0U) {
    if (reg32(base + eventOffset) != 0U) {
      return true;
    }
  }
  return false;
}

bool waitForEventOrError(uint32_t base, uint32_t eventOffset,
                         uint32_t errorOffset, uint32_t spinLimit) {
  while (spinLimit-- > 0U) {
    if (reg32(base + eventOffset) != 0U) {
      return true;
    }
    if (reg32(base + errorOffset) != 0U) {
      return false;
    }
  }
  return false;
}

void clearEvent(uint32_t base, uint32_t eventOffset) {
  reg32(base + eventOffset) = 0U;
}

bool waitForNonZero(volatile uint32_t* reg, uint32_t spinLimit) {
  if (reg == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (*reg != 0U) {
      return true;
    }
  }
  return false;
}

bool tryAllocateHighestSetBit(uint32_t mask, uint8_t* outBit) {
  if (outBit == nullptr || mask == 0U) {
    return false;
  }
  *outBit =
      static_cast<uint8_t>(31U - static_cast<uint32_t>(__builtin_clz(mask)));
  return true;
}

int32_t adcRawToMilliVolts(int16_t raw, AdcResolution resolution,
                           AdcGain gain, bool differential) {
  const uint32_t bits = adcResolutionBits(resolution);
  uint32_t exponent = bits;
  if (differential && exponent > 0U) {
    --exponent;
  }

  const double scale = static_cast<double>(1UL << exponent);
  const double mv =
      (static_cast<double>(raw) * 900.0) / (adcGainValue(gain) * scale);
  return static_cast<int32_t>(mv >= 0.0 ? (mv + 0.5) : (mv - 0.5));
}

uint32_t saadcPselValue(const Pin& pin) {
  uint32_t psel = 0U;
  psel |= (static_cast<uint32_t>(pin.pin) << saadc::CH_PSEL_PIN_Pos);
  psel |= (static_cast<uint32_t>(pin.port) << saadc::CH_PSEL_PORT_Pos);
  psel |= (saadc::CH_PSEL_CONNECT_ANALOG << saadc::CH_PSEL_CONNECT_Pos);
  return psel;
}

uint32_t spimPrescaler(uint32_t coreHz, uint32_t targetHz, uint32_t minDivisor) {
  if (coreHz == 0U || targetHz == 0U || minDivisor == 0U) {
    return 0U;
  }

  uint32_t divisor = coreHz / targetHz;
  if ((coreHz % targetHz) != 0U) {
    ++divisor;
  }

  if (divisor < minDivisor) {
    divisor = minDivisor;
  }
  if ((divisor & 1U) != 0U) {
    ++divisor;
  }
  if (divisor > 126U) {
    divisor = 126U;
  }

  return divisor;
}

void clearTwimState(uint32_t base) {
  clearEvent(base, twim::EVENTS_STOPPED);
  clearEvent(base, twim::EVENTS_ERROR);
  clearEvent(base, twim::EVENTS_LASTRX);
  clearEvent(base, twim::EVENTS_LASTTX);
  clearEvent(base, twim::EVENTS_DMA_RX_END);
  clearEvent(base, twim::EVENTS_DMA_TX_END);
  reg32(base + twim::ERRORSRC) = twim::ERRORSRC_ALL;
}

uint32_t absDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

uint32_t timerCompareEventOffset(uint8_t channel) {
  return timer::EVENTS_COMPARE +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerCaptureTaskOffset(uint8_t channel) {
  return timer::TASKS_CAPTURE +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerCcOffset(uint8_t channel) {
  return timer::CC + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerOneShotOffset(uint8_t channel) {
  return timer::ONESHOTEN + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerPublishCompareOffset(uint8_t channel) {
  return timer::PUBLISH_COMPARE +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerSubscribeCaptureOffset(uint8_t channel) {
  return timer::SUBSCRIBE_CAPTURE +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerCompareIntMask(uint8_t channel) {
  return (1UL << (16U + static_cast<uint32_t>(channel)));
}

bool computePwmTiming(uint32_t targetHz, PwmTiming* timing) {
  if (timing == nullptr || targetHz == 0U) {
    return false;
  }

  uint32_t bestError = 0xFFFFFFFFUL;
  PwmTiming best{0, 0, 0};
  bool found = false;

  for (uint8_t prescaler = 0; prescaler <= 7U; ++prescaler) {
    const uint32_t pwmClk = 16000000UL >> prescaler;
    if (pwmClk == 0U) {
      continue;
    }

    uint32_t top = (pwmClk + (targetHz / 2U)) / targetHz;
    if (top < 3U) {
      top = 3U;
    }
    if (top > 32767U) {
      continue;
    }

    const uint32_t actualHz = pwmClk / top;
    const uint32_t error = absDiffU32(actualHz, targetHz);

    if (!found || error < bestError) {
      found = true;
      bestError = error;
      best.prescaler = prescaler;
      best.countertop = static_cast<uint16_t>(top);
      best.actualHz = actualHz;
      if (error == 0U) {
        break;
      }
    }
  }

  if (!found) {
    return false;
  }

  *timing = best;
  return true;
}

uint32_t pwmTaskSeqStartOffset(uint8_t sequence) {
  return pwm::TASKS_DMA_SEQ_START + (static_cast<uint32_t>(sequence) * 8U);
}

uint32_t pwmEventSeqStartedOffset(uint8_t sequence) {
  return pwm::EVENTS_SEQSTARTED +
         (static_cast<uint32_t>(sequence) * sizeof(uint32_t));
}

uint32_t pwmEventSeqEndOffset(uint8_t sequence) {
  return pwm::EVENTS_SEQEND + (static_cast<uint32_t>(sequence) * sizeof(uint32_t));
}

uint32_t pwmEventDmaSeqEndOffset(uint8_t sequence) {
  return pwm::EVENTS_DMA_SEQ_END + (static_cast<uint32_t>(sequence) * 0x0CU);
}

uint32_t pwmDmaSeqPtrOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_PTR +
         (static_cast<uint32_t>(sequence) * pwm::DMA_SEQ_STRIDE);
}

uint32_t pwmDmaSeqMaxCntOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_MAXCNT +
         (static_cast<uint32_t>(sequence) * pwm::DMA_SEQ_STRIDE);
}

uint32_t pwmDmaSeqAmountOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_AMOUNT +
         (static_cast<uint32_t>(sequence) * pwm::DMA_SEQ_STRIDE);
}

uint32_t pwmDmaSeqCurrentAmountOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_CURRENTAMOUNT +
         (static_cast<uint32_t>(sequence) * pwm::DMA_SEQ_STRIDE);
}

uint32_t pwmDmaSeqBusErrorAddressOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_BUSERRORADDRESS +
         (static_cast<uint32_t>(sequence) * pwm::DMA_SEQ_STRIDE);
}

uint32_t gpioteInEventOffset(uint8_t channel) {
  return gpiote::EVENTS_IN + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteTaskOutOffset(uint8_t channel) {
  return gpiote::TASKS_OUT + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteTaskSetOffset(uint8_t channel) {
  return gpiote::TASKS_SET + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteTaskClrOffset(uint8_t channel) {
  return gpiote::TASKS_CLR + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteConfigOffset(uint8_t channel) {
  return gpiote::CONFIG + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteSubscribeOutOffset(uint8_t channel) {
  return gpiote::SUBSCRIBE_OUT +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteSubscribeSetOffset(uint8_t channel) {
  return gpiote::SUBSCRIBE_SET +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteSubscribeClrOffset(uint8_t channel) {
  return gpiote::SUBSCRIBE_CLR +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

}  // namespace xiao_nrf54l15::hal_internal
