#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr Pin kPins[] = {kPinD0, kPinD1, kPinD2, kPinD3};
static constexpr uint32_t kPwmFrequencyHz = 100UL;
static constexpr uint32_t kRefreshCount = 9UL;
static constexpr uint8_t kSeq0ToSeq1Channel = 4U;
static constexpr uint8_t kSeq1ToSeq0Channel = 5U;
static constexpr uint16_t kSeq0Permille[] = {100U, 300U, 500U, 700U};
static constexpr uint16_t kSeq1Permille[] = {850U, 650U, 450U, 250U};

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static Dppic g_dppic(nrf54l15::DPPIC20_BASE);
static uint16_t g_seq0Words[sizeof(kSeq0Permille) / sizeof(kSeq0Permille[0])];
static uint16_t g_seq1Words[sizeof(kSeq1Permille) / sizeof(kSeq1Permille[0])];
static uint32_t g_seq0StartedCount = 0U;
static uint32_t g_seq1StartedCount = 0U;
static uint32_t g_seq0EndCount = 0U;
static uint32_t g_seq1EndCount = 0U;
static uint32_t g_seq0DmaEndCount = 0U;
static uint32_t g_seq1DmaEndCount = 0U;
static uint32_t g_lastReportMs = 0U;

static void buildSequenceWords() {
  const uint16_t top = g_pwm.countertop();
  for (uint8_t i = 0U; i < (sizeof(kSeq0Permille) / sizeof(kSeq0Permille[0])); ++i) {
    g_seq0Words[i] = Pwm::encodeSequenceWordPermille(kSeq0Permille[i], top, true);
  }
  for (uint8_t i = 0U; i < (sizeof(kSeq1Permille) / sizeof(kSeq1Permille[0])); ++i) {
    g_seq1Words[i] = Pwm::encodeSequenceWordPermille(kSeq1Permille[i], top, true);
  }
}

static void printSequenceSummary(const char* label, const uint16_t* values, size_t count) {
  Serial.print(label);
  Serial.print('=');
  for (size_t i = 0U; i < count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(values[i]);
  }
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmDppiSequenceTelemetry");
  Serial.println("PWM20 DPPI looper with sequence and DMA telemetry.");

  if (!g_pwm.beginRaw(kPins, 4U, kPwmFrequencyHz, Pwm::DecoderLoad::kCommon,
                      Pwm::DecoderMode::kRefreshCount)) {
    Serial.println("PWM beginRaw failed");
    while (true) {
      delay(1000);
    }
  }

  buildSequenceWords();
  if (!g_pwm.setSequence(
          0U, g_seq0Words,
          static_cast<uint16_t>(sizeof(g_seq0Words) / sizeof(g_seq0Words[0])),
          kRefreshCount) ||
      !g_pwm.setSequence(
          1U, g_seq1Words,
          static_cast<uint16_t>(sizeof(g_seq1Words) / sizeof(g_seq1Words[0])),
          kRefreshCount)) {
    Serial.println("PWM setSequence failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_dppic.connect(g_pwm.publishSequenceEndConfigRegister(0U),
                       g_pwm.subscribeSequenceStartConfigRegister(1U),
                       kSeq0ToSeq1Channel) ||
      !g_dppic.connect(g_pwm.publishSequenceEndConfigRegister(1U),
                       g_pwm.subscribeSequenceStartConfigRegister(0U),
                       kSeq1ToSeq0Channel)) {
    Serial.println("DPPIC connect failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_pwm.start(0U)) {
    Serial.println("PWM start failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("pwm_hz=");
  Serial.println(kPwmFrequencyHz);
  Serial.print("refresh_count=");
  Serial.println(kRefreshCount);
  Serial.print("countertop=");
  Serial.println(g_pwm.countertop());
  Serial.print("seq0_cfg_bytes=");
  Serial.print(g_pwm.sequenceConfiguredBytes(0U));
  Serial.print(" seq1_cfg_bytes=");
  Serial.println(g_pwm.sequenceConfiguredBytes(1U));
  printSequenceSummary("seq0", kSeq0Permille,
                       sizeof(kSeq0Permille) / sizeof(kSeq0Permille[0]));
  printSequenceSummary("seq1", kSeq1Permille,
                       sizeof(kSeq1Permille) / sizeof(kSeq1Permille[0]));
  g_lastReportMs = millis();
}

void loop() {
  if (g_pwm.pollSequenceStarted(0U, true)) {
    ++g_seq0StartedCount;
  }
  if (g_pwm.pollSequenceStarted(1U, true)) {
    ++g_seq1StartedCount;
  }
  if (g_pwm.pollSequenceEnd(0U, true)) {
    ++g_seq0EndCount;
  }
  if (g_pwm.pollSequenceEnd(1U, true)) {
    ++g_seq1EndCount;
  }
  if (g_pwm.pollDmaSequenceEnd(0U, true)) {
    ++g_seq0DmaEndCount;
  }
  if (g_pwm.pollDmaSequenceEnd(1U, true)) {
    ++g_seq1DmaEndCount;
  }
  if (g_pwm.pollRamUnderflow(true)) {
    Serial.println("ram_underflow");
  }

  const uint32_t now = millis();
  if ((now - g_lastReportMs) >= 1000UL) {
    g_lastReportMs = now;
    Serial.print("seq0_started=");
    Serial.print(g_seq0StartedCount);
    Serial.print(" seq1_started=");
    Serial.print(g_seq1StartedCount);
    Serial.print(" seq0_end=");
    Serial.print(g_seq0EndCount);
    Serial.print(" seq1_end=");
    Serial.print(g_seq1EndCount);
    Serial.print(" dma0_end=");
    Serial.print(g_seq0DmaEndCount);
    Serial.print(" dma1_end=");
    Serial.print(g_seq1DmaEndCount);
    Serial.print(" amt0=");
    Serial.print(g_pwm.sequenceTransferredBytes(0U));
    Serial.print(" cur0=");
    Serial.print(g_pwm.sequenceCurrentTransferredBytes(0U));
    Serial.print(" amt1=");
    Serial.print(g_pwm.sequenceTransferredBytes(1U));
    Serial.print(" cur1=");
    Serial.print(g_pwm.sequenceCurrentTransferredBytes(1U));
    Serial.print(" bus0=0x");
    Serial.print(g_pwm.sequenceBusErrorAddress(0U), HEX);
    Serial.print(" bus1=0x");
    Serial.println(g_pwm.sequenceBusErrorAddress(1U), HEX);
  }
}
