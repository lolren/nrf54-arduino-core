/*********************************************************************
 This is an example for our nRF52 based Bluefruit LE modules

 Pick one up today in the adafruit shop!

 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

/* This sketch show how to use BLEClientService and BLEClientCharacteristic
 * to implement a custom client that is used to talk with Gatt server on
 * peripheral.
 *
 * Note: you will need another feather52 running peripheral/custom_HRM sketch
 * to test with.
 */

#include <bluefruit.h>
#include <bluefruit_example_log.h>

/* HRM Service Definitions
 * Heart Rate Monitor Service:  0x180D
 * Heart Rate Measurement Char: 0x2A37 (Mandatory)
 * Body Sensor Location Char:   0x2A38 (Optional)
 */

BLEClientService        hrms(UUID16_SVC_HEART_RATE);
BLEClientCharacteristic hrmc(UUID16_CHR_HEART_RATE_MEASUREMENT);
BLEClientCharacteristic bslc(UUID16_CHR_BODY_SENSOR_LOCATION);
BluefruitExampleLogQueue<16, 96> logQueue;

void setup()
{
  Serial.begin(115200);
  while ( !Serial ) delay(10);   // for nrf52840 with native usb

  Serial.println("Bluefruit52 Central Custom HRM Example");
  Serial.println("--------------------------------------\n");

  // Initialize Bluefruit with maximum connections as Peripheral = 0, Central = 1
  // SRAM usage required by SoftDevice will increase dramatically with number of connections
  Bluefruit.begin(0, 1);

  Bluefruit.setName("Bluefruit52 Central");

  // Initialize HRM client
  hrms.begin();

  // Initialize client characteristics of HRM.
  // Note: Client Char will be added to the last service that is begin()ed.
  bslc.begin();

  // set up callback for receiving measurement
  hrmc.setNotifyCallback(hrm_notify_callback);
  hrmc.begin();

  // Increase Blink rate to different from PrPh advertising mode
  Bluefruit.setConnLedInterval(250);

  // Callbacks for Central
  Bluefruit.Central.setDisconnectCallback(disconnect_callback);
  Bluefruit.Central.setConnectCallback(connect_callback);

  /* Start Central Scanning
   * - Enable auto scan if disconnected
   * - Interval = 100 ms, window = 80 ms
   * - Don't use active scan
   * - Filter only accept HRM service
   * - Start(timeout) with timeout = 0 will scan forever (until connected)
   */
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80); // in unit of 0.625 ms
  Bluefruit.Scanner.filterUuid(hrms.uuid);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);                   // // 0 = Don't stop scanning after n seconds
}

void loop()
{
  logQueue.flush(Serial);
  yield();
}

/**
 * Callback invoked when scanner pick up an advertising data
 * @param report Structural advertising data
 */
void scan_callback(ble_gap_evt_adv_report_t* report)
{
  // Since we configure the scanner with filterUuid()
  // Scan callback only invoked for device with hrm service advertised
  // Connect to device with HRM service in advertising
  Bluefruit.Central.connect(report);
}

/**
 * Callback invoked when an connection is established
 * @param conn_handle
 */
void connect_callback(uint16_t conn_handle)
{
  logQueue.queue("Connected");
  logQueue.queue("Discovering HRM Service ...");

  // If HRM is not found, disconnect and return
  if ( !hrms.discover(conn_handle) )
  {
    logQueue.queue("Found NONE");

    // disconnect since we couldn't find HRM service
    Bluefruit.disconnect(conn_handle);

    return;
  }

  // Once HRM service is found, we continue to discover its characteristic
  logQueue.queue("Found HRM service");
  
  logQueue.queue("Discovering Measurement characteristic ...");
  if ( !hrmc.discover() )
  {
    // Measurement chr is mandatory, if it is not found (valid), then disconnect
    logQueue.queue("Measurement characteristic not found");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  logQueue.queue("Found Measurement characteristic");

  // Measurement is found, continue to look for option Body Sensor Location
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.body_sensor_location.xml
  // Body Sensor Location is optional, print out the location in text if present
  logQueue.queue("Discovering Body Sensor Location characteristic ...");
  if ( bslc.discover() )
  {
    logQueue.queue("Found Body Sensor Location characteristic");

    // Body sensor location value is 8 bit
    const char* body_str[] = { "Other", "Chest", "Wrist", "Finger", "Hand", "Ear Lobe", "Foot" };

    // Read 8-bit BSLC value from peripheral
    uint8_t loc_value = bslc.read8();

    if ( loc_value < 7 )
    {
      logQueue.queuef("Body Location Sensor: %s", body_str[loc_value]);
    }
    else
    {
      logQueue.queuef("Body Location Sensor: %u", loc_value);
    }
  }else
  {
    logQueue.queue("Body Sensor Location not found");
  }

  // Reaching here means we are ready to go, let's enable notification on measurement chr
  if ( hrmc.enableNotify() )
  {
    logQueue.queue("Ready to receive HRM Measurement value");
  }else
  {
    logQueue.queue("Couldn't enable notify for HRM Measurement");
  }
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  logQueue.queuef("Disconnected, reason = 0x%02X", reason);
}


/**
 * Hooked callback that triggered when a measurement value is sent from peripheral
 * @param chr   Pointer client characteristic that even occurred,
 *              in this example it should be hrmc
 * @param data  Pointer to received data
 * @param len   Length of received data
 */
void hrm_notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len)
{
  (void) chr;
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.heart_rate_measurement.xml
  // Measurement contains of control byte0 and measurement (8 or 16 bit) + optional field
  // if byte0's bit0 is 0 --> measurement is 8 bit, otherwise 16 bit.
  if ( len < 2 )
  {
    logQueue.queue("HRM Measurement: <short packet>");
    return;
  }

  if ( data[0] & bit(0) )
  {
    if ( len < 3 )
    {
      logQueue.queue("HRM Measurement: <short packet>");
      return;
    }
    uint16_t value;
    memcpy(&value, data+1, 2);

    logQueue.queuef("HRM Measurement: %u", value);
  }
  else
  {
    logQueue.queuef("HRM Measurement: %u", data[1]);
  }
}
