#include "nrf_to_nrf.h"

nrf_to_nrf radio;

uint8_t address[][6] = {"1Node", "2Node"};
bool radioNumber = 0;
bool role = false;

struct PayloadStruct {
  char message[7];
  uint8_t counter;
};

PayloadStruct payload;

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

  Serial.println(F("nrf_to_nrf/AcknowledgementPayloads"));
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
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.openWritingPipe(address[radioNumber]);
  radio.openReadingPipe(1, address[!radioNumber]);

  if (role) {
    memcpy(payload.message, "Hello ", 6);
    payload.message[6] = '\0';
    payload.counter = 0U;
    radio.stopListening();
  } else {
    memcpy(payload.message, "World ", 6);
    payload.message[6] = '\0';
    payload.counter = 0U;
    radio.writeAckPayload(1, &payload, sizeof(payload));
    radio.startListening();
  }
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
      Serial.print(payload.message);
      Serial.print(payload.counter);
      uint8_t pipe = 0U;
      if (radio.available(&pipe)) {
        PayloadStruct received{};
        radio.read(&received, sizeof(received));
        Serial.print(F(" Received "));
        Serial.print(radio.getDynamicPayloadSize());
        Serial.print(F(" bytes on pipe "));
        Serial.print(pipe);
        Serial.print(F(": "));
        Serial.print(received.message);
        Serial.println(received.counter);
        payload.counter = received.counter + 1U;
      } else {
        Serial.println(F(" Received: an empty ACK packet"));
      }
    } else {
      Serial.println(F("Transmission failed or timed out"));
    }

    delay(1000U);
  } else {
    uint8_t pipe = 0U;
    if (radio.available(&pipe)) {
      const uint8_t bytes = radio.getDynamicPayloadSize();
      PayloadStruct received{};
      radio.read(&received, sizeof(received));
      Serial.print(F("Received "));
      Serial.print(bytes);
      Serial.print(F(" bytes on pipe "));
      Serial.print(pipe);
      Serial.print(F(": "));
      Serial.print(received.message);
      Serial.print(received.counter);
      Serial.print(F(" Sent: "));
      Serial.print(payload.message);
      Serial.println(payload.counter);
      payload.counter = received.counter + 1U;
      radio.writeAckPayload(1, &payload, sizeof(payload));
    }
  }

  if (Serial.available()) {
    const char c = toupper(Serial.read());
    if (c == 'T' && !role) {
      role = true;
      Serial.println(F("*** CHANGING TO TRANSMIT ROLE -- PRESS 'R' TO SWITCH BACK"));
      memcpy(payload.message, "Hello ", 6);
      payload.counter = 0U;
      radio.stopListening();
    } else if (c == 'R' && role) {
      role = false;
      Serial.println(F("*** CHANGING TO RECEIVE ROLE -- PRESS 'T' TO SWITCH BACK"));
      memcpy(payload.message, "World ", 6);
      payload.counter = 0U;
      radio.writeAckPayload(1, &payload, sizeof(payload));
      radio.startListening();
    }
  }
}
