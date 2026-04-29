#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr Pin kPins[] = {kPinD0, kPinD1, kPinD2, kPinD3};
static constexpr uint32_t kPwmFrequencyHz = 100UL;
static constexpr uint32_t kRefreshCount = 9UL;
static constexpr uint8_t kSeq0ToSeq1Channel = 6U;
static constexpr uint8_t kSeq1ToSeq0Channel = 7U;
static constexpr uint32_t kReportPeriodMs = 1000UL;
static constexpr uint16_t kSeq0Permille[] = {100U, 300U, 500U, 700U};
static constexpr uint16_t kSeq1Permille[] = {850U, 650U, 450U, 250U};

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static Dppic g_dppic(nrf54l15::DPPIC20_BASE);
static uint16_t g_seq0Words[sizeof(kSeq0Permille) / sizeof(kSeq0Permille[0])];
static uint16_t g_seq1Words[sizeof(kSeq1Permille) / sizeof(kSeq1Permille[0])];

static volatile uint32_t g_irqSeqStarted0 = 0U;
static volatile uint32_t g_irqSeqStarted1 = 0U;
static volatile uint32_t g_irqSeqEnd0 = 0U;
static volatile uint32_t g_irqSeqEnd1 = 0U;
static volatile uint32_t g_irqDmaEnd0 = 0U;
static volatile uint32_t g_irqDmaEnd1 = 0U;
static volatile uint32_t g_irqDmaReady0 = 0U;
static volatile uint32_t g_irqDmaReady1 = 0U;
static volatile uint32_t g_irqBusError0 = 0U;
static volatile uint32_t g_irqBusError1 = 0U;
static volatile uint32_t g_irqLastMask = 0U;
static volatile uint8_t g_readyInterruptsDisabled = 0U;
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

static void pwmIrqCallback(uint32_t irqMask, void* context) {
  (void)context;
  g_irqLastMask = irqMask;
  if ((irqMask & Pwm::irqSequenceStartedMask(0U)) != 0U) {
    ++g_irqSeqStarted0;
  }
  if ((irqMask & Pwm::irqSequenceStartedMask(1U)) != 0U) {
    ++g_irqSeqStarted1;
  }
  if ((irqMask & Pwm::irqSequenceEndMask(0U)) != 0U) {
    ++g_irqSeqEnd0;
  }
  if ((irqMask & Pwm::irqSequenceEndMask(1U)) != 0U) {
    ++g_irqSeqEnd1;
  }
  if ((irqMask & Pwm::irqDmaSequenceEndMask(0U)) != 0U) {
    ++g_irqDmaEnd0;
  }
  if ((irqMask & Pwm::irqDmaSequenceEndMask(1U)) != 0U) {
    ++g_irqDmaEnd1;
  }
  if ((irqMask & Pwm::irqDmaSequenceReadyMask(0U)) != 0U) {
    ++g_irqDmaReady0;
  }
  if ((irqMask & Pwm::irqDmaSequenceReadyMask(1U)) != 0U) {
    ++g_irqDmaReady1;
  }
  if ((irqMask & Pwm::irqDmaSequenceBusErrorMask(0U)) != 0U) {
    ++g_irqBusError0;
  }
  if ((irqMask & Pwm::irqDmaSequenceBusErrorMask(1U)) != 0U) {
    ++g_irqBusError1;
  }
  if ((irqMask & (Pwm::irqDmaSequenceReadyMask(0U) |
                  Pwm::irqDmaSequenceReadyMask(1U))) != 0U &&
      g_readyInterruptsDisabled == 0U) {
    g_pwm.enableInterruptMask(Pwm::irqDmaSequenceReadyMask(0U) |
                                  Pwm::irqDmaSequenceReadyMask(1U),
                              false);
    g_readyInterruptsDisabled = 1U;
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

static void printSnapshot() {
  noInterrupts();
  const uint32_t seqStarted0 = g_irqSeqStarted0;
  const uint32_t seqStarted1 = g_irqSeqStarted1;
  const uint32_t seqEnd0 = g_irqSeqEnd0;
  const uint32_t seqEnd1 = g_irqSeqEnd1;
  const uint32_t dmaEnd0 = g_irqDmaEnd0;
  const uint32_t dmaEnd1 = g_irqDmaEnd1;
  const uint32_t dmaReady0 = g_irqDmaReady0;
  const uint32_t dmaReady1 = g_irqDmaReady1;
  const uint32_t busError0 = g_irqBusError0;
  const uint32_t busError1 = g_irqBusError1;
  const uint32_t lastMask = g_irqLastMask;
  const uint8_t readyInterruptsDisabled = g_readyInterruptsDisabled;
  interrupts();

  Serial.print("seqstarted=");
  Serial.print(seqStarted0);
  Serial.print('/');
  Serial.print(seqStarted1);
  Serial.print(" seqend=");
  Serial.print(seqEnd0);
  Serial.print('/');
  Serial.print(seqEnd1);
  Serial.print(" dmaend=");
  Serial.print(dmaEnd0);
  Serial.print('/');
  Serial.print(dmaEnd1);
  Serial.print(" dmaready=");
  Serial.print(dmaReady0);
  Serial.print('/');
  Serial.print(dmaReady1);
  Serial.print(" buserror=");
  Serial.print(busError0);
  Serial.print('/');
  Serial.print(busError1);
  Serial.print(" ready_irq_disabled=");
  Serial.print(readyInterruptsDisabled);
  Serial.print(" last_mask=0x");
  Serial.println(lastMask, HEX);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmIrqSequenceReporter");
  Serial.println("PWM20 common-load looper. IRQ callback reports SEQSTARTED, "
                 "SEQEND, DMA END, DMA READY, and BUSERROR events.");

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

  g_pwm.setIrqCallback(pwmIrqCallback);
  if (!g_pwm.makeActive()) {
    Serial.println("PWM makeActive failed");
    while (true) {
      delay(1000);
    }
  }

  g_pwm.enableInterruptMask(Pwm::irqSequenceStartedMask(0U) |
                            Pwm::irqSequenceStartedMask(1U) |
                            Pwm::irqSequenceEndMask(0U) |
                            Pwm::irqSequenceEndMask(1U) |
                            Pwm::irqDmaSequenceEndMask(0U) |
                            Pwm::irqDmaSequenceEndMask(1U) |
                            Pwm::irqDmaSequenceReadyMask(0U) |
                            Pwm::irqDmaSequenceReadyMask(1U) |
                            Pwm::irqDmaSequenceBusErrorMask(0U) |
                            Pwm::irqDmaSequenceBusErrorMask(1U));

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
  printSequenceSummary("seq0", kSeq0Permille,
                       sizeof(kSeq0Permille) / sizeof(kSeq0Permille[0]));
  printSequenceSummary("seq1", kSeq1Permille,
                       sizeof(kSeq1Permille) / sizeof(kSeq1Permille[0]));
  g_lastReportMs = millis();
}

void loop() {
  if (g_pwm.pollRamUnderflow(true)) {
    Serial.println("ram_underflow");
  }

  const uint32_t now = millis();
  if ((now - g_lastReportMs) >= kReportPeriodMs) {
    g_lastReportMs = now;
    printSnapshot();
  }
}
