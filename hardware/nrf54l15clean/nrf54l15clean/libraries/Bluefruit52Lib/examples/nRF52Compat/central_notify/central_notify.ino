/*********************************************************************
 This is a simple custom notify central example for the bundled
 Bluefruit52 compatibility layer.

 Use with `notify_peripheral` to see a minimal service-discovery plus
 notification-enable flow.
*********************************************************************/

#include <bluefruit.h>

static constexpr uint16_t kNotifyServiceUuid = 0xBEE0;
static constexpr uint16_t kNotifyCharUuid = 0xBEE1;

BLEClientService notifyService(kNotifyServiceUuid);
BLEClientCharacteristic notifyChar(kNotifyCharUuid);

void scan_callback(ble_gap_evt_adv_report_t* report);
void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
void notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);

void setup()
{
  Serial.begin(115200);
  Serial.println("Bluefruit52 Central Notify Example");
  Serial.println("---------------------------------");

  Bluefruit.begin(0, 1);
  Bluefruit.setName("Notify Central");

  notifyService.begin();
  notifyChar.begin(&notifyService);
  notifyChar.setNotifyCallback(notify_callback);

  Bluefruit.Central.setConnectCallback(connect_callback);
  Bluefruit.Central.setDisconnectCallback(disconnect_callback);

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);

  Serial.println("Scanning for the custom notify service");
}

void scan_callback(ble_gap_evt_adv_report_t* report)
{
  if ( Bluefruit.Scanner.checkReportForService(report, notifyService) )
  {
    Serial.print("Notify service detected. Connecting ... ");
    Bluefruit.Central.connect(report);
  }
  else
  {
    Bluefruit.Scanner.resume();
  }
}

void connect_callback(uint16_t conn_handle)
{
  Serial.println("connected");

  Serial.print("Discovering custom notify service ... ");
  if ( !notifyService.discover(conn_handle) )
  {
    Serial.println("failed");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  Serial.println("found");

  Serial.print("Discovering notify characteristic ... ");
  if ( !notifyChar.discover() )
  {
    Serial.println("failed");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  Serial.println("found");

  Serial.print("Enabling notify ... ");
  if ( !notifyChar.enableNotify() )
  {
    Serial.println("failed");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  Serial.println("enabled");
  Serial.println("Waiting for notifications");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;

  Serial.print("Disconnected, reason = 0x");
  Serial.println(reason, HEX);
}

void notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len)
{
  (void) chr;

  Serial.print("[Notify] ");
  for (uint16_t i = 0; i < len; ++i)
  {
    const char ch = static_cast<char>(data[i]);
    if (ch >= 32 && ch <= 126)
    {
      Serial.print(ch);
    }
    else
    {
      Serial.print('.');
    }
  }
  Serial.println();
}

void loop()
{
  delay(20);
}
