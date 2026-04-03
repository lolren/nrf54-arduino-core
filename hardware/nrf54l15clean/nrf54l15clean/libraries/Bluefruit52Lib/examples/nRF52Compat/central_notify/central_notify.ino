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

static constexpr uint8_t kLogQueueDepth = 8;
static constexpr size_t kLogLineLen = 64;

char g_logQueue[kLogQueueDepth][kLogLineLen];
uint8_t g_logHead = 0U;
uint8_t g_logTail = 0U;
uint8_t g_logCount = 0U;
uint32_t g_droppedLogLines = 0UL;

void scan_callback(ble_gap_evt_adv_report_t* report);
void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
void notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
void queue_log_line(const char* text);
void queue_notify_line(const uint8_t* data, uint16_t len);
void flush_log_queue(void);

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
    queue_log_line("Notify service detected. Connecting ...");
    Bluefruit.Central.connect(report);
  }
  else
  {
    Bluefruit.Scanner.resume();
  }
}

void connect_callback(uint16_t conn_handle)
{
  queue_log_line("connected");

  queue_log_line("Discovering custom notify service ...");
  if ( !notifyService.discover(conn_handle) )
  {
    queue_log_line("Custom notify service discovery failed");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  queue_log_line("Custom notify service found");

  queue_log_line("Discovering notify characteristic ...");
  if ( !notifyChar.discover() )
  {
    queue_log_line("Notify characteristic discovery failed");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  queue_log_line("Notify characteristic found");

  queue_log_line("Enabling notify ...");
  if ( !notifyChar.enableNotify() )
  {
    queue_log_line("Enable notify failed");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  queue_log_line("Notify enabled");
  queue_log_line("Waiting for notifications");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;

  char line[kLogLineLen];
  snprintf(line, sizeof(line), "Disconnected, reason = 0x%02X",
           static_cast<unsigned int>(reason));
  queue_log_line(line);
}

void notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len)
{
  (void) chr;
  queue_notify_line(data, len);
}

void loop()
{
  flush_log_queue();
  delay(20);
}

void queue_log_line(const char* text)
{
  if (text == nullptr)
  {
    return;
  }

  if (g_logCount >= kLogQueueDepth)
  {
    ++g_droppedLogLines;
    return;
  }

  snprintf(g_logQueue[g_logHead], sizeof(g_logQueue[g_logHead]), "%s", text);
  g_logHead = static_cast<uint8_t>((g_logHead + 1U) % kLogQueueDepth);
  ++g_logCount;
}

void queue_notify_line(const uint8_t* data, uint16_t len)
{
  char line[kLogLineLen];
  size_t pos = static_cast<size_t>(snprintf(line, sizeof(line), "[Notify] "));
  if (pos >= sizeof(line))
  {
    line[sizeof(line) - 1U] = '\0';
    queue_log_line(line);
    return;
  }

  for (uint16_t i = 0; i < len && pos < (sizeof(line) - 1U); ++i)
  {
    const char ch = static_cast<char>(data[i]);
    line[pos++] = (ch >= 32 && ch <= 126) ? ch : '.';
  }

  line[pos] = '\0';
  queue_log_line(line);
}

void flush_log_queue(void)
{
  if (g_droppedLogLines != 0UL)
  {
    Serial.print("[Log] dropped ");
    Serial.print(static_cast<unsigned long>(g_droppedLogLines));
    Serial.println(" lines");
    g_droppedLogLines = 0UL;
  }

  while (g_logCount != 0U)
  {
    Serial.println(g_logQueue[g_logTail]);
    g_logTail = static_cast<uint8_t>((g_logTail + 1U) % kLogQueueDepth);
    --g_logCount;
  }
}
