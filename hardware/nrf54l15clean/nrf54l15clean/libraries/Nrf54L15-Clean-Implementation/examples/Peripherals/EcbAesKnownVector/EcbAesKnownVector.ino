#include <Arduino.h>

#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Ecb g_ecb;

static const uint8_t kKey[16] = {
    0x4C, 0x68, 0x38, 0x41, 0x39, 0xF5, 0x74, 0xD8,
    0x36, 0xBC, 0xF3, 0x4E, 0x9D, 0xFB, 0x01, 0xBF,
};

static const uint8_t kPlaintext[16] = {
    0x02, 0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79,
    0xAC, 0xBD, 0xCE, 0xDF, 0xE0, 0xF1, 0x02, 0x13,
};

static const uint8_t kExpectedCiphertext[16] = {
    0x99, 0xAD, 0x1B, 0x52, 0x26, 0xA3, 0x7E, 0x3E,
    0x05, 0x8E, 0x3B, 0x8E, 0x27, 0xC2, 0xC6, 0x66,
};

static void printHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(300);

  uint8_t ciphertext[16] = {};
  const bool ok = g_ecb.encryptBlock(kKey, kPlaintext, ciphertext, 400000UL);
  const bool match =
      ok && (memcmp(ciphertext, kExpectedCiphertext, sizeof(ciphertext)) == 0);

  Serial.println();
  Serial.println("EcbAesKnownVector");
  Serial.println("This uses the datasheet AES-ECB sample vector.");
  Serial.print("ciphertext=");
  printHex(ciphertext, sizeof(ciphertext));
  Serial.println();
  Serial.print("errorStatus=");
  Serial.println(static_cast<unsigned long>(g_ecb.errorStatus()));
  Serial.println(match ? "PASS" : "FAIL");

  digitalWrite(LED_BUILTIN, match ? LOW : HIGH);
}

void loop() {
  delay(1000);
}
