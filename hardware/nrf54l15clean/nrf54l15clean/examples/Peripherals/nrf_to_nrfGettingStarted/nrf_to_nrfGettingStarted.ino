#include "nrf_to_nrf.h"

nrf_to_nrf radio;

uint8_t address[][6] = {"1Node", "2Node"};
bool radioNumber = 0;
bool role = false;
float payload = 0.0f;

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < 2000U) {
  }

  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (true) {
    }
  }

  Serial.println(F("nrf_to_nrf/GettingStarted"));
  Serial.println(F("Send '0' or '1' in the first 2 seconds to choose the node. Defaults to 0."));
  const uint32_t selectStart = millis();
  while ((millis() - selectStart) < 2000U) {
    if (Serial.available()) {
      radioNumber = (Serial.parseInt() == 1);
      break;
    }
  }

  Serial.print(F("radioNumber = "));
  Serial.println(static_cast<int>(radioNumber));
  Serial.println(F("Press 'T' for TX role, 'R' for RX role. Defaults to RX."));

  radio.setPALevel(NRF_PA_LOW);
  radio.setPayloadSize(sizeof(payload));
  radio.openWritingPipe(address[radioNumber]);
  radio.openReadingPipe(1, address[!radioNumber]);
  radio.startListening();
}

void loop() {
  if (role) {
    const unsigned long startTimer = micros();
    const bool report = radio.write(&payload, sizeof(payload));
    const unsigned long endTimer = micros();

    if (report) {
      Serial.print(F("Transmission successful! Time to transmit = "));
      Serial.print(endTimer - startTimer);
      Serial.print(F(" us. Sent: "));
      Serial.println(payload);
      payload += 0.01f;
    } else {
      Serial.println(F("Transmission failed or timed out"));
    }
    delay(1000U);
  } else {
    uint8_t pipe = 0U;
    if (radio.available(&pipe)) {
      const uint8_t bytes = radio.getPayloadSize();
      radio.read(&payload, bytes);
      Serial.print(F("Received "));
      Serial.print(bytes);
      Serial.print(F(" bytes on pipe "));
      Serial.print(pipe);
      Serial.print(F(": "));
      Serial.println(payload);
    }
  }

  if (Serial.available()) {
    const char c = toupper(Serial.read());
    if (c == 'T' && !role) {
      role = true;
      Serial.println(F("*** CHANGING TO TRANSMIT ROLE -- PRESS 'R' TO SWITCH BACK"));
      radio.stopListening();
    } else if (c == 'R' && role) {
      role = false;
      Serial.println(F("*** CHANGING TO RECEIVE ROLE -- PRESS 'T' TO SWITCH BACK"));
      radio.startListening();
    }
  }
}
