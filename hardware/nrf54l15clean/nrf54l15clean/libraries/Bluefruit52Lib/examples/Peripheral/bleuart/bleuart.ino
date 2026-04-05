#include <bluefruit.h>

BLEDfu bledfu;
BLEDis bledis;
BLEUart bleuart;
BLEBas blebas;

void startAdv() {
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(bleuart);
  // Keep the complete short name in ADV so passive scanners can see it directly.
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void connectCallback(uint16_t conn_handle) {
  BLEConnection* connection = Bluefruit.Connection(conn_handle);
  char peer[32] = {0};
  if (connection != nullptr && connection->getPeerName(peer, sizeof(peer))) {
    Serial.print("Connected to ");
    Serial.println(peer);
  } else {
    Serial.println("Connected");
  }
}

void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  Serial.print("Disconnected, reason = 0x");
  Serial.println(reason, HEX);
}

void setup() {
  Serial.begin(115200);

  Bluefruit.autoConnLed(true);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setName("X54-NUS");
  Bluefruit.setTxPower(0);
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  bledfu.begin();
  bledis.setManufacturer("Seeed Studio");
  bledis.setModel("XIAO nRF54L15");
  bledis.setSoftwareRev("0.3.2");
  bledis.begin();

  bleuart.begin();
  blebas.begin();
  blebas.write(100U);

  startAdv();

  Serial.println("Bluefruit-style BLE UART");
}

void loop() {
  while (Serial.available() > 0) {
    delay(2);
    uint8_t buffer[32] = {0};
    const int count = Serial.readBytes(buffer, sizeof(buffer));
    if (count > 0) {
      bleuart.write(buffer, static_cast<size_t>(count));
    }
  }

  while (bleuart.available() > 0) {
    const int ch = bleuart.read();
    if (ch >= 0) {
      Serial.write(static_cast<uint8_t>(ch));
    }
  }
}
