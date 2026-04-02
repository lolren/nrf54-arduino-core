/*********************************************************************
 This is a simple custom notify peripheral example for the bundled
 Bluefruit52 compatibility layer.

 Open `central_notify` on another board to see the notifications arrive.
*********************************************************************/

#include <bluefruit.h>

static constexpr uint16_t kNotifyServiceUuid = 0xBEE0;
static constexpr uint16_t kNotifyCharUuid = 0xBEE1;
static constexpr uint32_t kNotifyPeriodMs = 1000UL;

BLEService notifyService(kNotifyServiceUuid);
BLECharacteristic notifyChar(kNotifyCharUuid);

uint32_t g_nextNotifyAtMs = 0UL;
uint32_t g_counter = 0UL;
bool g_waitingForNotifyEnable = true;

void startAdv(void);
void setupNotifyService(void);
void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
void cccd_callback(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value);

void setup()
{
  Serial.begin(115200);
  Serial.println("Bluefruit52 Notify Peripheral Example");
  Serial.println("------------------------------------");

  Bluefruit.begin(1, 0);
  Bluefruit.setName("Notify Peripheral");

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  setupNotifyService();
  startAdv();

  Serial.println("Advertising custom notify service");
}

void setupNotifyService(void)
{
  notifyService.begin();

  notifyChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  notifyChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  notifyChar.setMaxLen(20);
  notifyChar.setCccdWriteCallback(cccd_callback);
  notifyChar.begin();
  notifyChar.write("boot");
}

void startAdv(void)
{
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(notifyService);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void connect_callback(uint16_t conn_handle)
{
  (void) conn_handle;
  g_waitingForNotifyEnable = true;
  g_nextNotifyAtMs = millis();
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;
  g_waitingForNotifyEnable = true;
}

void cccd_callback(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value)
{
  (void) conn_handle;
  (void) cccd_value;

  if (chr != &notifyChar) {
    return;
  }

  g_waitingForNotifyEnable = !notifyChar.notifyEnabled();
}

void loop()
{
  const uint32_t now = millis();
  if (now < g_nextNotifyAtMs) {
    return;
  }
  g_nextNotifyAtMs = now + kNotifyPeriodMs;

  char payload[20];
  const int len = snprintf(payload, sizeof(payload), "count=%lu",
                           static_cast<unsigned long>(g_counter++));
  const uint16_t payloadLen = static_cast<uint16_t>((len > 0) ? len : 0);

  const bool notifyQueued = notifyChar.notify(payload, payloadLen);
  if (!notifyQueued && g_waitingForNotifyEnable) {
    Serial.println("Waiting for the central to enable notifications");
    g_waitingForNotifyEnable = false;
  }
}
