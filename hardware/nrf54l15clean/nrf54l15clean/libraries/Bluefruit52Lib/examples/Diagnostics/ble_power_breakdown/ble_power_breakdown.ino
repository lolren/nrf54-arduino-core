#include <Arduino.h>
#include <bluefruit.h>

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_XIAO_NRF54LM20A_CLEAN)
#include <npm1300.h>
#define BLE_POWER_BREAKDOWN_HAS_PMIC 1
#else
#define BLE_POWER_BREAKDOWN_HAS_PMIC 0
#endif

#ifndef BLE_POWER_BREAKDOWN_SERIAL
#define BLE_POWER_BREAKDOWN_SERIAL 0
#endif

// D0: BLE initialization; D1: first explicit clock read; D2/D3/D4: stage bits.
static constexpr unsigned long kWindowMs = 10000UL;
static constexpr uint8_t kBeginPin = PIN_D0;
static constexpr uint8_t kClockPin = PIN_D1;
static constexpr uint8_t kStagePins[] = {PIN_D2, PIN_D3, PIN_D4};

static void setStage(uint8_t stage) {
  for (uint8_t bit = 0; bit < 3U; ++bit) {
    digitalWrite(kStagePins[bit], (stage & (1U << bit)) ? HIGH : LOW);
  }
}

static void failMeasurement() {
  setStage(7U);
  while (true) {
    delay(kWindowMs);
  }
}

#if BLE_POWER_BREAKDOWN_SERIAL && defined(NRF54L15_CLEAN_POWER_LOW)
extern "C" {
extern volatile uint32_t g_nrf54l15_diag_delay_outer_loops;
extern volatile uint32_t g_nrf54l15_diag_delay_wfi_entries;
extern volatile uint32_t g_nrf54l15_diag_grtc_irq_count;
extern volatile uint32_t g_nrf54l15_diag_grtc_delay_irq_count;
extern volatile uint32_t g_nrf54l15_diag_delay_skipwfi_count;
}

static uint32_t g_counts[5][5];

static void readIdleCounts(uint32_t counts[5]) {
  counts[0] = g_nrf54l15_diag_delay_outer_loops;
  counts[1] = g_nrf54l15_diag_delay_wfi_entries;
  counts[2] = g_nrf54l15_diag_grtc_irq_count;
  counts[3] = g_nrf54l15_diag_grtc_delay_irq_count;
  counts[4] = g_nrf54l15_diag_delay_skipwfi_count;
}
#endif

#if BLE_POWER_BREAKDOWN_SERIAL
static unsigned long g_beginUs;
#if BLE_POWER_BREAKDOWN_HAS_PMIC
static int32_t g_vbatMv;
static int32_t g_ibatMa;
#endif
#endif

static void idleWindow(uint8_t stage) {
  setStage(stage);
#if BLE_POWER_BREAKDOWN_SERIAL && defined(NRF54L15_CLEAN_POWER_LOW)
  uint32_t before[5];
  readIdleCounts(before);
#endif
  delay(kWindowMs);
#if BLE_POWER_BREAKDOWN_SERIAL && defined(NRF54L15_CLEAN_POWER_LOW)
  uint32_t after[5];
  readIdleCounts(after);
  for (uint8_t i = 0; i < 5U; ++i) {
    g_counts[stage - 1U][i] = after[i] - before[i];
  }
#endif
}

void setup() {
  digitalWrite(kBeginPin, LOW);
  pinMode(kBeginPin, OUTPUT);
  digitalWrite(kClockPin, LOW);
  pinMode(kClockPin, OUTPUT);
  for (uint8_t pin : kStagePins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  Bluefruit.autoConnLed(false);

  // Resolve lazy timebase startup before measuring Bluefruit.begin(). Boot
  // code may already have initialized the clock before setup() is entered.
  digitalWrite(kClockPin, HIGH);
  (void)micros();
  digitalWrite(kClockPin, LOW);
  idleWindow(1U);

#if BLE_POWER_BREAKDOWN_SERIAL
  const unsigned long beginStartUs = micros();
#endif
  digitalWrite(kBeginPin, HIGH);
  const bool begun = Bluefruit.begin();
  digitalWrite(kBeginPin, LOW);
#if BLE_POWER_BREAKDOWN_SERIAL
  g_beginUs = micros() - beginStartUs;
#endif
  if (!begun) {
    failMeasurement();
  }
  idleWindow(2U);

#if BLE_POWER_BREAKDOWN_HAS_PMIC
  if (!npm1300_is_present()) {
    failMeasurement();
  }
  const int32_t vbatMv = npm1300_read_vbat_mv();
  const int32_t ibatMa = npm1300_read_ibat_ma();
  if (vbatMv < 0 || ibatMa == -1) {
    failMeasurement();
  }
#if BLE_POWER_BREAKDOWN_SERIAL
  g_vbatMv = vbatMv;
  g_ibatMa = ibatMa;
#else
  (void)vbatMv;
  (void)ibatMa;
#endif
#endif
  idleWindow(3U);

  Bluefruit.setName("nRF54-Power");
  Bluefruit.setTxPower(0);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_ADV_NONCONN_IND);
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  if (!Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) ||
      !Bluefruit.Advertising.addName()) {
    failMeasurement();
  }
  Bluefruit.Advertising.setInterval(1600, 1600);
  Bluefruit.Advertising.setFastTimeout(0);
  if (!Bluefruit.Advertising.start(0)) {
    failMeasurement();
  }
  idleWindow(4U);
  if (!Bluefruit.Advertising.stop()) {
    failMeasurement();
  }
  idleWindow(5U);
  setStage(6U);

#if BLE_POWER_BREAKDOWN_SERIAL
  // Serial is initialized only after every capture window is complete.
  Serial.begin(115200);
  delay(1000);
  Serial.print("Bluefruit.begin including GPIO markers (us): ");
  Serial.println(g_beginUs);
#if BLE_POWER_BREAKDOWN_HAS_PMIC
  Serial.print("VBAT (mV): ");
  Serial.println(g_vbatMv);
  Serial.print("IBAT (mA): ");
  Serial.println(g_ibatMa);
#endif
#if defined(NRF54L15_CLEAN_POWER_LOW)
  Serial.println("stage,outer_loops,wfi_entries,grtc_irqs,delay_irqs,skip_wfi");
  for (uint8_t i = 0; i < 5U; ++i) {
    Serial.print(i + 1U);
    for (uint8_t counter = 0; counter < 5U; ++counter) {
      Serial.print(',');
      Serial.print(g_counts[i][counter]);
    }
    Serial.println();
  }
#else
  Serial.println("Idle counters require the Low Power profile.");
#endif
  Serial.flush();
  Serial.end();
#endif
}

void loop() {
  delay(kWindowMs);
}
