#include <Arduino.h>

namespace {

#ifndef SERIAL_NONBLOCKING_PROBE_SIZE
#define SERIAL_NONBLOCKING_PROBE_SIZE 384U
#endif

constexpr size_t kPayloadSize = static_cast<size_t>(SERIAL_NONBLOCKING_PROBE_SIZE);
constexpr unsigned long kBurstPeriodMs = 2000UL;

// Probe Serial1 timing while reporting results over Serial so the measured UART
// is not perturbed by its own logging traffic.
HardwareSerial& kProbeUart = Serial1;
uint8_t g_payload[kPayloadSize];
unsigned long g_nextBurstMs = 0UL;

void fillPayload() {
  for (size_t i = 0U; i < sizeof(g_payload); ++i) {
    g_payload[i] = static_cast<uint8_t>('A' + (i % 26U));
  }
}

void runBurst() {
  const unsigned long enqueueStartUs = micros();
  const size_t queued = kProbeUart.write(g_payload, sizeof(g_payload));
  const unsigned long enqueueEndUs = micros();

  kProbeUart.flush();
  const unsigned long flushEndUs = micros();
  const unsigned long enqueueUs = enqueueEndUs - enqueueStartUs;
  const unsigned long flushUs = flushEndUs - enqueueEndUs;
  const int probeFree = kProbeUart.availableForWrite();

  Serial.print("queued=");
  Serial.print(queued);
  Serial.print(" enqueue_us=");
  Serial.print(enqueueUs);
  Serial.print(" flush_us=");
  Serial.print(flushUs);
  Serial.print(" probe_free=");
  Serial.println(probeFree);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const unsigned long waitStartMs = millis();
  while (!Serial && (millis() - waitStartMs) < 1500UL) {
    delay(10);
  }

  fillPayload();
  Serial.println();
  Serial.println("SerialNonBlockingWriteProbe");
  Serial.println("DUT=Serial1, reporter=Serial");
  Serial.println("Watch enqueue_us stay much smaller than flush_us.");
  kProbeUart.begin(115200);
  g_nextBurstMs = millis();
}

void loop() {
  const unsigned long nowMs = millis();
  if (static_cast<long>(nowMs - g_nextBurstMs) < 0L) {
    delay(10);
    return;
  }

  g_nextBurstMs = nowMs + kBurstPeriodMs;
  runBurst();
}
