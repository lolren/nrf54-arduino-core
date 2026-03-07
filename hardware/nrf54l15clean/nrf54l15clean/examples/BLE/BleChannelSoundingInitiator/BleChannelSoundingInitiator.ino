#include <Arduino.h>

#include <math.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

struct ChannelStats {
  uint32_t hits;
  uint32_t rspHits;
  int32_t advRssiSum;
  int32_t rspRssiSum;
};

static BleRadio g_ble;
static bool g_ready = false;
static ChannelStats g_stats[3] = {};
static uint32_t g_lastLogMs = 0;
static uint32_t g_scanHits = 0;
static uint32_t g_targetHits = 0;
static uint32_t g_missWindows = 0;
static uint8_t g_lastChannel = 0;
static int32_t g_targetAdvRssiSum = 0;
static uint32_t g_targetAdvRssiCount = 0;
static int32_t g_targetRspRssiSum = 0;
static uint32_t g_targetRspRssiCount = 0;

static constexpr uint32_t kAdvListenSpinPerChannel = 1200000UL;
static constexpr uint32_t kScanRspListenSpin = 300000UL;
static constexpr float kRefRssiAtOneMeterDbm = -59.0f;
static constexpr float kPathLossExponent = 2.0f;

// Must match CoreBleChannelSoundingReflector address.
static const uint8_t kReflectorAddress[6] = {0x5A, 0x15, 0x54, 0x15, 0xDE, 0xC0};

static bool addressMatches(const uint8_t* a, const uint8_t* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  for (size_t i = 0; i < 6U; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

static void printAddress(const uint8_t* addr) {
  if (addr == nullptr) {
    return;
  }
  for (int i = 5; i >= 0; --i) {
    if (addr[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(addr[i], HEX);
    if (i > 0) {
      Serial.print(':');
    }
  }
}

static int channelIndex(BleAdvertisingChannel channel) {
  switch (channel) {
    case BleAdvertisingChannel::k37:
      return 0;
    case BleAdvertisingChannel::k38:
      return 1;
    case BleAdvertisingChannel::k39:
      return 2;
    default:
      return -1;
  }
}

static void printChannelSummary(uint8_t channelNum, const ChannelStats& stats) {
  Serial.print(" ch");
  Serial.print(channelNum);
  Serial.print(" n=");
  Serial.print(stats.hits);
  if (stats.hits == 0U) {
    Serial.print(" adv=na rsp=na");
    return;
  }

  const int32_t advAvg = stats.advRssiSum / static_cast<int32_t>(stats.hits);
  Serial.print(" adv=");
  Serial.print(advAvg);
  Serial.print("dBm");

  if (stats.rspHits == 0U) {
    Serial.print(" rsp=na");
    return;
  }
  const int32_t rspAvg = stats.rspRssiSum / static_cast<int32_t>(stats.rspHits);
  Serial.print(" rsp=");
  Serial.print(rspAvg);
  Serial.print("dBm");
}

static uint8_t bestChannelByAdvAverage() {
  int bestIdx = -1;
  int32_t bestAvg = -127;

  for (int i = 0; i < 3; ++i) {
    if (g_stats[i].hits == 0U) {
      continue;
    }
    const int32_t avg = g_stats[i].advRssiSum / static_cast<int32_t>(g_stats[i].hits);
    if (bestIdx < 0 || avg > bestAvg) {
      bestIdx = i;
      bestAvg = avg;
    }
  }

  if (bestIdx < 0) {
    return 0;
  }
  return static_cast<uint8_t>(37 + bestIdx);
}

static int32_t estimateDistanceMmFromRssi(int32_t avgRssiDbm) {
  if (avgRssiDbm >= 0) {
    return -1;
  }
  const float exponent =
      (kRefRssiAtOneMeterDbm - static_cast<float>(avgRssiDbm)) /
      (10.0f * kPathLossExponent);
  const float meters = powf(10.0f, exponent);
  const int32_t mm = static_cast<int32_t>(meters * 1000.0f + 0.5f);
  if (mm < 0) {
    return -1;
  }
  return mm;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nCoreBleChannelSoundingInitiator start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  g_ready = g_ble.begin(-8);
  Serial.print("BLE init: ");
  Serial.print(g_ready ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("target_reflector=");
  printAddress(kReflectorAddress);
  Serial.print("\r\n");
  Serial.print("distance_model=rssi ref_1m=");
  Serial.print(kRefRssiAtOneMeterDbm);
  Serial.print("dBm n=");
  Serial.print(kPathLossExponent);
  Serial.print("\r\n");
  Serial.print("pair_with=CoreBleChannelSoundingReflector\r\n");
  if (!g_ready) {
    Serial.print("Hint: enable Tools -> BLE Support = Enabled\r\n");
  }
}

void loop() {
  if (!g_ready) {
    delay(250);
    return;
  }

  BleActiveScanResult result{};
  const bool got = g_ble.scanActiveCycle(&result, kAdvListenSpinPerChannel,
                                         kScanRspListenSpin);
  if (!got) {
    ++g_missWindows;
  } else {
    ++g_scanHits;

    const bool target = addressMatches(result.advertiserAddress, kReflectorAddress);
    if (target) {
      ++g_targetHits;
      g_lastChannel = static_cast<uint8_t>(result.channel);
      const int idx = channelIndex(result.channel);
      if (idx >= 0 && idx < 3) {
        ++g_stats[idx].hits;
        g_stats[idx].advRssiSum += static_cast<int32_t>(result.advRssiDbm);
        ++g_targetAdvRssiCount;
        g_targetAdvRssiSum += static_cast<int32_t>(result.advRssiDbm);
        if (result.scanResponseReceived) {
          ++g_stats[idx].rspHits;
          g_stats[idx].rspRssiSum += static_cast<int32_t>(result.scanRspRssiDbm);
          ++g_targetRspRssiCount;
          g_targetRspRssiSum += static_cast<int32_t>(result.scanRspRssiDbm);
        }
      }
      Gpio::write(kPinUserLed, (g_targetHits & 0x1U) == 0U);
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = now;
    Serial.print("t=");
    Serial.print(now);
    Serial.print(" hits=");
    Serial.print(g_scanHits);
    Serial.print(" target=");
    Serial.print(g_targetHits);
    Serial.print(" misses=");
    Serial.print(g_missWindows);
    Serial.print(" last_ch=");
    Serial.print(g_lastChannel);
    printChannelSummary(37, g_stats[0]);
    printChannelSummary(38, g_stats[1]);
    printChannelSummary(39, g_stats[2]);
    const uint8_t best = bestChannelByAdvAverage();
    if (best != 0U) {
      Serial.print(" best=");
      Serial.print(best);
    }
    if (g_targetAdvRssiCount > 0U) {
      const int32_t advAvg =
          g_targetAdvRssiSum / static_cast<int32_t>(g_targetAdvRssiCount);
      const int32_t mm = estimateDistanceMmFromRssi(advAvg);
      if (mm > 0) {
        Serial.print(" dist_cm=");
        Serial.print(mm / 10);
        Serial.print(" dist_mm=");
        Serial.print(mm);
      }
    }
    if (g_targetRspRssiCount > 0U) {
      const int32_t rspAvg =
          g_targetRspRssiSum / static_cast<int32_t>(g_targetRspRssiCount);
      Serial.print(" rsp_avg=");
      Serial.print(rspAvg);
      Serial.print("dBm");
    }
    Serial.print("\r\n");
  }

  delay(1);
}
