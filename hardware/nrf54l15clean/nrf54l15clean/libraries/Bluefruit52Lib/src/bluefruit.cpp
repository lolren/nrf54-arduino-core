#include "bluefruit.h"

#include <stdio.h>
#include <string.h>

#include <nrf54l15_hal.h>

using xiao_nrf54l15::BleConnectionInfo;
using xiao_nrf54l15::BleConnectionEvent;
using xiao_nrf54l15::BleConnectionRole;
using xiao_nrf54l15::BleDisconnectDebug;
using xiao_nrf54l15::BleDisconnectReason;
using xiao_nrf54l15::BleGattCharacteristicProperty;
using xiao_nrf54l15::BleRadio;

constexpr uint8_t kAdTypeFlags = 0x01U;
constexpr uint8_t kAdTypeIncomplete16 = 0x02U;
constexpr uint8_t kAdTypeComplete16 = 0x03U;
constexpr uint8_t kAdTypeIncomplete128 = 0x06U;
constexpr uint8_t kAdTypeComplete128 = 0x07U;
constexpr uint8_t kAdTypeShortName = 0x08U;
constexpr uint8_t kAdTypeCompleteName = 0x09U;
constexpr uint8_t kAdTypeTxPower = 0x0AU;
constexpr uint8_t kAdTypeAppearance = 0x19U;
constexpr uint8_t kAdTypeManufacturer = 0xFFU;
constexpr uint8_t kLedOnState = LOW;
constexpr uint8_t kLedOffState = HIGH;

constexpr uint16_t kUuidDfuService = 0xFE59U;
constexpr uint16_t kCompanyIdApple = 0x004CU;
constexpr uint16_t kUuidPrimaryService = 0x2800U;
constexpr uint16_t kUuidCharacteristic = 0x2803U;
constexpr uint16_t kUuidClientCharacteristicConfig = 0x2902U;

constexpr uint8_t kAttOpErrorRsp = 0x01U;
constexpr uint8_t kAttOpFindInfoReq = 0x04U;
constexpr uint8_t kAttOpFindInfoRsp = 0x05U;
constexpr uint8_t kAttOpFindByTypeValueReq = 0x06U;
constexpr uint8_t kAttOpFindByTypeValueRsp = 0x07U;
constexpr uint8_t kAttOpReadByTypeReq = 0x08U;
constexpr uint8_t kAttOpReadByTypeRsp = 0x09U;
constexpr uint8_t kAttOpReadReq = 0x0AU;
constexpr uint8_t kAttOpReadRsp = 0x0BU;
constexpr uint8_t kAttOpReadByGroupTypeReq = 0x10U;
constexpr uint8_t kAttOpReadByGroupTypeRsp = 0x11U;
constexpr uint8_t kAttOpWriteReq = 0x12U;
constexpr uint8_t kAttOpWriteRsp = 0x13U;
constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;
constexpr uint8_t kAttErrAttributeNotFound = 0x0AU;

uint8_t hid_ascii_to_keycode[128][2] = {};
uint8_t hid_keycode_to_ascii[128][2] = {};

void setHidAsciiMapping(char ascii, bool shift, uint8_t keycode) {
  const uint8_t asciiIndex = static_cast<uint8_t>(ascii);
  hid_ascii_to_keycode[asciiIndex][0] = shift ? 1U : 0U;
  hid_ascii_to_keycode[asciiIndex][1] = keycode;

  if (keycode < 128U) {
    hid_keycode_to_ascii[keycode][shift ? 1U : 0U] = asciiIndex;
  }
}

void initHidAsciiTables() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  memset(hid_ascii_to_keycode, 0, sizeof(hid_ascii_to_keycode));
  memset(hid_keycode_to_ascii, 0, sizeof(hid_keycode_to_ascii));

  setHidAsciiMapping('\b', false, HID_KEY_BACKSPACE);
  setHidAsciiMapping('\t', false, HID_KEY_TAB);
  setHidAsciiMapping('\n', false, HID_KEY_ENTER);
  setHidAsciiMapping('\r', false, HID_KEY_ENTER);
  setHidAsciiMapping(0x1BU, false, HID_KEY_ESCAPE);
  setHidAsciiMapping(' ', false, HID_KEY_SPACE);

  for (char ch = 'a'; ch <= 'z'; ++ch) {
    setHidAsciiMapping(ch, false, static_cast<uint8_t>(HID_KEY_A + (ch - 'a')));
  }
  for (char ch = 'A'; ch <= 'Z'; ++ch) {
    setHidAsciiMapping(ch, true, static_cast<uint8_t>(HID_KEY_A + (ch - 'A')));
  }
  for (char ch = '1'; ch <= '9'; ++ch) {
    setHidAsciiMapping(ch, false, static_cast<uint8_t>(HID_KEY_1 + (ch - '1')));
  }
  setHidAsciiMapping('0', false, HID_KEY_0);

  setHidAsciiMapping('!', true, HID_KEY_1);
  setHidAsciiMapping('@', true, HID_KEY_2);
  setHidAsciiMapping('#', true, HID_KEY_3);
  setHidAsciiMapping('$', true, HID_KEY_4);
  setHidAsciiMapping('%', true, HID_KEY_5);
  setHidAsciiMapping('^', true, HID_KEY_6);
  setHidAsciiMapping('&', true, HID_KEY_7);
  setHidAsciiMapping('*', true, HID_KEY_8);
  setHidAsciiMapping('(', true, HID_KEY_9);
  setHidAsciiMapping(')', true, HID_KEY_0);
  setHidAsciiMapping('-', false, HID_KEY_MINUS);
  setHidAsciiMapping('_', true, HID_KEY_MINUS);
  setHidAsciiMapping('=', false, HID_KEY_EQUAL);
  setHidAsciiMapping('+', true, HID_KEY_EQUAL);
  setHidAsciiMapping('[', false, HID_KEY_BRACKET_LEFT);
  setHidAsciiMapping('{', true, HID_KEY_BRACKET_LEFT);
  setHidAsciiMapping(']', false, HID_KEY_BRACKET_RIGHT);
  setHidAsciiMapping('}', true, HID_KEY_BRACKET_RIGHT);
  setHidAsciiMapping('\\', false, HID_KEY_BACKSLASH);
  setHidAsciiMapping('|', true, HID_KEY_BACKSLASH);
  setHidAsciiMapping(';', false, HID_KEY_SEMICOLON);
  setHidAsciiMapping(':', true, HID_KEY_SEMICOLON);
  setHidAsciiMapping('\'', false, HID_KEY_APOSTROPHE);
  setHidAsciiMapping('"', true, HID_KEY_APOSTROPHE);
  setHidAsciiMapping('`', false, HID_KEY_GRAVE);
  setHidAsciiMapping('~', true, HID_KEY_GRAVE);
  setHidAsciiMapping(',', false, HID_KEY_COMMA);
  setHidAsciiMapping('<', true, HID_KEY_COMMA);
  setHidAsciiMapping('.', false, HID_KEY_PERIOD);
  setHidAsciiMapping('>', true, HID_KEY_PERIOD);
  setHidAsciiMapping('/', false, HID_KEY_SLASH);
  setHidAsciiMapping('?', true, HID_KEY_SLASH);

  hid_keycode_to_ascii[HID_KEY_KEYPAD_DIVIDE][0] = '/';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_DIVIDE][1] = '/';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_MULTIPLY][0] = '*';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_MULTIPLY][1] = '*';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_SUBTRACT][0] = '-';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_SUBTRACT][1] = '-';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_ADD][0] = '+';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_ADD][1] = '+';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_ENTER][0] = '\r';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_ENTER][1] = '\r';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_1][0] = '1';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_2][0] = '2';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_3][0] = '3';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_4][0] = '4';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_5][0] = '5';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_5][1] = '5';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_6][0] = '6';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_7][0] = '7';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_8][0] = '8';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_9][0] = '9';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_0][0] = '0';
  hid_keycode_to_ascii[HID_KEY_KEYPAD_DECIMAL][0] = '.';
}

struct HidAsciiTableInit {
  HidAsciiTableInit() { initHidAsciiTables(); }
} g_hid_ascii_table_init;

uint16_t readLe16(const uint8_t* data) {
  if (data == nullptr) {
    return 0U;
  }
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8U);
}

void writeLe16(uint8_t* data, uint16_t value) {
  if (data == nullptr) {
    return;
  }
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

uint8_t clampValueLen(uint16_t len) {
  if (len > BleRadio::kCustomGattMaxValueLength) {
    return BleRadio::kCustomGattMaxValueLength;
  }
  return static_cast<uint8_t>(len);
}

class ScopedBluefruitUserCallback {
 public:
  ScopedBluefruitUserCallback() { ++depth(); }

  ~ScopedBluefruitUserCallback() {
    if (depth() != 0U) {
      --depth();
    }
  }

  ScopedBluefruitUserCallback(const ScopedBluefruitUserCallback&) = delete;
  ScopedBluefruitUserCallback& operator=(const ScopedBluefruitUserCallback&) = delete;

  static bool active() { return depth() != 0U; }

 private:
  static uint8_t& depth() {
    static uint8_t value = 0U;
    return value;
  }
};

template <typename Callback, typename... Args>
void invokeBluefruitUserCallback(Callback callback, Args... args) {
  if (callback == nullptr) {
    return;
  }
  ScopedBluefruitUserCallback scope;
  callback(args...);
}

bool timeReachedUs(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

uint16_t byteSwap16(uint16_t value) {
  return static_cast<uint16_t>((value >> 8U) | (value << 8U));
}

bool uuidMatches(const BLEUuid& uuid, const uint8_t* data, uint8_t dataLen) {
  if (data == nullptr) {
    return false;
  }
  if (uuid.size() == 2U && dataLen == 2U) {
    return uuid.uuid16() == readLe16(data);
  }
  if (uuid.size() == 16U && dataLen == 16U) {
    if (memcmp(uuid.uuid128(), data, 16U) == 0) {
      return true;
    }
    const uint8_t* expected = uuid.uuid128();
    for (uint8_t i = 0U; i < 16U; ++i) {
      if (expected[i] != data[15U - i]) {
        return false;
      }
    }
    return true;
  }
  return false;
}

bool adDataHasUuid(const uint8_t* data, uint16_t len, const BLEUuid& uuid) {
  if (data == nullptr || len == 0U || !uuid.begin()) {
    return false;
  }

  uint16_t offset = 0U;
  while (offset < len) {
    const uint8_t fieldLen = data[offset];
    if (fieldLen == 0U || static_cast<uint16_t>(offset + fieldLen) >= (len + 1U)) {
      break;
    }

    const uint8_t type = data[offset + 1U];
    const uint8_t valueLen = static_cast<uint8_t>(fieldLen - 1U);
    const uint8_t* value = &data[offset + 2U];
    if ((type == BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE ||
         type == BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE) &&
        uuid.size() == 2U) {
      for (uint8_t pos = 0U; (pos + 2U) <= valueLen; pos = static_cast<uint8_t>(pos + 2U)) {
        if (uuidMatches(uuid, &value[pos], 2U)) {
          return true;
        }
      }
    }
    if ((type == BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_MORE_AVAILABLE ||
         type == BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE) &&
        uuid.size() == 16U) {
      for (uint8_t pos = 0U; (pos + 16U) <= valueLen;
           pos = static_cast<uint8_t>(pos + 16U)) {
        if (uuidMatches(uuid, &value[pos], 16U)) {
          return true;
        }
      }
    }
    offset = static_cast<uint16_t>(offset + fieldLen + 1U);
  }
  return false;
}

bool adDataHasMsdCompany(const uint8_t* data, uint16_t len, uint16_t companyId) {
  if (data == nullptr || len == 0U) {
    return false;
  }

  uint16_t offset = 0U;
  while (offset < len) {
    const uint8_t fieldLen = data[offset];
    if (fieldLen == 0U || static_cast<uint16_t>(offset + fieldLen) >= (len + 1U)) {
      break;
    }
    const uint8_t type = data[offset + 1U];
    if (type == BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA && fieldLen >= 3U &&
        readLe16(&data[offset + 2U]) == companyId) {
      return true;
    }
    offset = static_cast<uint16_t>(offset + fieldLen + 1U);
  }
  return false;
}

int copyAdFieldByType(const uint8_t* data, uint16_t len, uint8_t type,
                      void* buffer, uint8_t bufferLen) {
  if (data == nullptr || buffer == nullptr || bufferLen == 0U) {
    return 0;
  }

  uint16_t offset = 0U;
  while (offset < len) {
    const uint8_t fieldLen = data[offset];
    if (fieldLen == 0U || static_cast<uint16_t>(offset + fieldLen) >= (len + 1U)) {
      break;
    }
    const uint8_t fieldType = data[offset + 1U];
    if (fieldType == type) {
      const uint8_t valueLen = static_cast<uint8_t>(fieldLen - 1U);
      const uint8_t copyLen = min<uint8_t>(valueLen, bufferLen);
      memcpy(buffer, &data[offset + 2U], copyLen);
      return copyLen;
    }
    offset = static_cast<uint16_t>(offset + fieldLen + 1U);
  }
  return 0;
}

int hexDigitValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

bool parseUuidString(const char* str, uint8_t outUuid[16], uint8_t* outSize,
                     uint16_t* outUuid16) {
  if (str == nullptr || outSize == nullptr || outUuid16 == nullptr) {
    return false;
  }

  const size_t len = strlen(str);
  if (len == 4U) {
    char* end = nullptr;
    const unsigned long value = strtoul(str, &end, 16);
    if (end == nullptr || *end != '\0' || value > 0xFFFFUL) {
      return false;
    }
    *outSize = 2U;
    *outUuid16 = static_cast<uint16_t>(value);
    return true;
  }

  char hex[32] = {0};
  size_t used = 0U;
  for (size_t i = 0U; i < len; ++i) {
    const char c = str[i];
    if (c == '-') {
      continue;
    }
    if (hexDigitValue(c) < 0 || used >= sizeof(hex)) {
      return false;
    }
    hex[used++] = c;
  }
  if (used != 32U) {
    return false;
  }

  uint8_t bigEndian[16] = {0};
  for (size_t i = 0U; i < 16U; ++i) {
    const int hi = hexDigitValue(hex[i * 2U]);
    const int lo = hexDigitValue(hex[i * 2U + 1U]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    bigEndian[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  for (size_t i = 0U; i < 16U; ++i) {
    outUuid[i] = bigEndian[15U - i];
  }
  *outSize = 16U;
  *outUuid16 = 0U;
  return true;
}

uint8_t mapProperties(uint8_t properties) {
  uint8_t mapped = 0U;
  if ((properties & CHR_PROPS_READ) != 0U) {
    mapped |= xiao_nrf54l15::kBleGattPropRead;
  }
  if ((properties & CHR_PROPS_WRITE_WO_RESP) != 0U) {
    mapped |= xiao_nrf54l15::kBleGattPropWriteNoRsp;
  }
  if ((properties & CHR_PROPS_WRITE) != 0U) {
    mapped |= xiao_nrf54l15::kBleGattPropWrite;
  }
  if ((properties & CHR_PROPS_NOTIFY) != 0U) {
    mapped |= xiao_nrf54l15::kBleGattPropNotify;
  }
  if ((properties & CHR_PROPS_INDICATE) != 0U) {
    mapped |= xiao_nrf54l15::kBleGattPropIndicate;
  }
  return mapped;
}

uint8_t disconnectReasonToHci(const BleDisconnectDebug& debug) {
  switch (static_cast<BleDisconnectReason>(debug.reason)) {
    case BleDisconnectReason::kApi:
      return 0x16U;
    case BleDisconnectReason::kSupervisionTimeout:
      return 0x08U;
    case BleDisconnectReason::kPeerTerminate:
      return (debug.errorCode != 0U) ? debug.errorCode : 0x13U;
    case BleDisconnectReason::kMicFailure:
      return 0x3DU;
    case BleDisconnectReason::kInternalTerminate:
      return (debug.errorCode != 0U) ? debug.errorCode : 0x13U;
    case BleDisconnectReason::kNone:
    default:
      return 0x13U;
  }
}

class BluefruitCompatManager {
 public:
  BluefruitCompatManager()
      : started_(false),
        last_connected_(false),
        last_connection_role_(BleConnectionRole::kNone),
        next_adv_due_us_(0U),
        adv_started_ms_(0UL),
        scan_rsp_name_added_(false),
        characteristic_count_(0U),
        client_characteristic_count_(0U),
        pending_connect_valid_(false),
        pending_connect_random_(0U),
        central_sync_procedure_depth_(0U),
        central_data_length_request_pending_(false),
        central_mtu_request_pending_(false),
        central_link_config_not_before_ms_(0UL),
        last_connect_attempt_ms_(0UL),
        scan_report_data_{0},
        scan_report_len_(0U) {
    memset(characteristics_, 0, sizeof(characteristics_));
    memset(client_characteristics_, 0, sizeof(client_characteristics_));
    memset(&scan_report_, 0, sizeof(scan_report_));
    memset(pending_connect_address_, 0, sizeof(pending_connect_address_));
  }

  bool begin(uint8_t prph_count, uint8_t central_count) {
    (void)prph_count;
    (void)central_count;
    if (started_) {
      return true;
    }

    if (!radio_.begin()) {
      return false;
    }
    radio_.loadAddressFromFicr();
    radio_.setAdvertisingPduType(xiao_nrf54l15::BleAdvPduType::kAdvInd);
    radio_.setAdvertisingChannelSelectionAlgorithm2(false);
    radio_.clearCustomGatt();
    radio_.setGattDeviceName(Bluefruit.device_name_);
    radio_.setGattAppearance(Bluefruit.appearance_);
    radio_.setPreferredConnectionParameters(Bluefruit.Periph.conn_interval_min_,
                                            Bluefruit.Periph.conn_interval_max_,
                                            Bluefruit.Periph.conn_latency_,
                                            Bluefruit.Periph.conn_supervision_timeout_);
    radio_.setCustomGattWriteCallback(&BluefruitCompatManager::gattWriteThunk, this);

    if (Bluefruit.auto_conn_led_) {
      pinMode(LED_BUILTIN, OUTPUT);
      digitalWrite(LED_BUILTIN, kLedOffState);
    }

    started_ = true;
    last_connected_ = radio_.isConnected();
    last_connection_role_ = radio_.connectionRole();
    return true;
  }

  BleRadio& radio() { return radio_; }
  const BleRadio& radio() const { return radio_; }

  void beginCentralSyncProcedure() {
    if (central_sync_procedure_depth_ < 0xFFU) {
      ++central_sync_procedure_depth_;
    }
  }

  void endCentralSyncProcedure() {
    if (central_sync_procedure_depth_ > 0U) {
      --central_sync_procedure_depth_;
    }
  }

  bool centralSyncProcedureActive() const { return central_sync_procedure_depth_ != 0U; }

  bool registerCharacteristic(BLECharacteristic* characteristic) {
    if (characteristic == nullptr || characteristic_count_ >= kMaxCharacteristics) {
      return false;
    }
    characteristics_[characteristic_count_++] = characteristic;
    return true;
  }

  bool registerClientCharacteristic(BLEClientCharacteristic* characteristic) {
    if (characteristic == nullptr) {
      return false;
    }
    for (uint8_t i = 0U; i < client_characteristic_count_; ++i) {
      if (client_characteristics_[i] == characteristic) {
        return true;
      }
    }
    if (client_characteristic_count_ >= kMaxClientCharacteristics) {
      return false;
    }
    client_characteristics_[client_characteristic_count_++] = characteristic;
    return true;
  }

  void markAdvertisingDirty() {
    Bluefruit.Advertising.dirty_ = true;
    Bluefruit.ScanResponse.dirty_ = true;
  }

  void advertisingStarted() {
    adv_started_ms_ = millis();
    next_adv_due_us_ = 0U;
  }

  BLEConnection* connection() { return &connection_; }

  bool queueCentralConnect(const ble_gap_evt_adv_report_t* report) {
    if (report == nullptr) {
      return false;
    }
    memcpy(pending_connect_address_, report->peer_addr.addr, sizeof(pending_connect_address_));
    pending_connect_random_ =
        (report->peer_addr.addr_type == BLE_GAP_ADDR_TYPE_RANDOM_STATIC) ? 1U : 0U;
    pending_connect_valid_ = true;
    Bluefruit.Scanner.paused_ = true;
    return maybeConnectCentral();
  }

  void handleCentralConnectionEvent(const BleConnectionEvent& event) {
    if (!(event.packetReceived && event.crcOk && event.attPacket && event.payload != nullptr &&
          event.payloadLength >= 7U)) {
      return;
    }
    const uint8_t attOpcode = event.payload[4];
    if (attOpcode != kAttOpHandleValueNtf) {
      return;
    }

    const uint16_t valueHandle = readLe16(&event.payload[5]);
    const uint16_t valueLength =
        (event.payloadLength >= 7U) ? static_cast<uint16_t>(event.payloadLength - 7U) : 0U;
    const uint8_t* value = &event.payload[7];
    for (uint8_t i = 0U; i < client_characteristic_count_; ++i) {
      BLEClientCharacteristic* characteristic = client_characteristics_[i];
      if (characteristic == nullptr || !characteristic->discovered_) {
        continue;
      }
      if (characteristic->value_handle_ == valueHandle) {
        characteristic->handleNotify(value, valueLength);
        return;
      }
    }
  }

  void idleService() {
    if (!started_) {
      return;
    }

    if (!radio_.isConnected()) {
      maybeAdvertise();
      maybeScanOrConnect();
    }

    const bool connected = radio_.isConnected();
    if (connected != last_connected_) {
      handleConnectionEdge(connected);
      last_connected_ = connected;
    }

    if (connected) {
      if (radio_.connectionRole() == BleConnectionRole::kPeripheral) {
        for (uint8_t i = 0U; i < characteristic_count_; ++i) {
          if (characteristics_[i] != nullptr) {
            characteristics_[i]->pollCccdState();
          }
        }
      } else if (radio_.connectionRole() == BleConnectionRole::kCentral) {
        processCentralBackgroundEvents(4U);
      }
    }
  }

 private:
  static constexpr uint8_t kMaxCharacteristics = 24U;
  static constexpr uint8_t kMaxClientCharacteristics = 16U;

  BleRadio radio_;
  bool started_;
  bool last_connected_;
  BleConnectionRole last_connection_role_;
  uint32_t next_adv_due_us_;
  unsigned long adv_started_ms_;
  bool scan_rsp_name_added_;
  BLECharacteristic* characteristics_[kMaxCharacteristics];
  uint8_t characteristic_count_;
  BLEClientCharacteristic* client_characteristics_[kMaxClientCharacteristics];
  uint8_t client_characteristic_count_;
  BLEConnection connection_;
  ble_gap_evt_adv_report_t scan_report_;
  uint8_t scan_report_data_[31];
  uint8_t scan_report_len_;
  bool pending_connect_valid_;
  uint8_t pending_connect_address_[6];
  uint8_t pending_connect_random_;
  uint8_t central_sync_procedure_depth_;
  bool central_data_length_request_pending_;
  bool central_mtu_request_pending_;
  unsigned long central_link_config_not_before_ms_;
  unsigned long last_connect_attempt_ms_;

  static void gattWriteThunk(uint16_t valueHandle, const uint8_t* value,
                             uint8_t valueLength, bool withResponse, void* context) {
    (void)withResponse;
    auto* self = static_cast<BluefruitCompatManager*>(context);
    if (self != nullptr) {
      self->handleGattWrite(valueHandle, value, valueLength);
    }
  }

  void handleGattWrite(uint16_t valueHandle, const uint8_t* value, uint8_t valueLength) {
    for (uint8_t i = 0U; i < characteristic_count_; ++i) {
      BLECharacteristic* characteristic = characteristics_[i];
      if (characteristic == nullptr) {
        continue;
      }
      if (characteristic->_handles.value_handle == valueHandle) {
        characteristic->handleWriteFromRadio(value, valueLength);
        return;
      }
    }
  }

  static void fillReportType(uint8_t pduHeader, ble_gap_adv_report_type_t* outType) {
    if (outType == nullptr) {
      return;
    }
    memset(outType, 0, sizeof(*outType));
    switch (pduHeader & 0x0FU) {
      case 0x00U:
        outType->connectable = 1U;
        outType->scannable = 1U;
        break;
      case 0x01U:
        outType->connectable = 1U;
        outType->directed = 1U;
        break;
      case 0x02U:
        break;
      case 0x04U:
      case 0x06U:
        outType->scannable = 1U;
        break;
      default:
        break;
    }
  }

  bool buildScanReport(const xiao_nrf54l15::BleScanPacket& packet) {
    if (packet.payload == nullptr || packet.length < 6U) {
      return false;
    }
    memset(&scan_report_, 0, sizeof(scan_report_));
    memcpy(scan_report_.peer_addr.addr, packet.payload, 6U);
    scan_report_.peer_addr.addr_type =
        ((packet.pduHeader >> 6U) & 0x1U) ? BLE_GAP_ADDR_TYPE_RANDOM_STATIC
                                          : BLE_GAP_ADDR_TYPE_PUBLIC;
    scan_report_.rssi = packet.rssiDbm;
    fillReportType(packet.pduHeader, &scan_report_.type);

    scan_report_len_ = static_cast<uint8_t>(packet.length - 6U);
    if (scan_report_len_ > sizeof(scan_report_data_)) {
      scan_report_len_ = sizeof(scan_report_data_);
    }
    if (scan_report_len_ > 0U) {
      memcpy(scan_report_data_, &packet.payload[6], scan_report_len_);
    }
    scan_report_.data.p_data = scan_report_data_;
    scan_report_.data.len = scan_report_len_;
    return true;
  }

  bool buildScanReport(const xiao_nrf54l15::BleActiveScanResult& result) {
    if (result.advPayloadLength < 6U) {
      return false;
    }
    memset(&scan_report_, 0, sizeof(scan_report_));
    memcpy(scan_report_.peer_addr.addr, result.advertiserAddress, 6U);
    scan_report_.peer_addr.addr_type =
        result.advertiserAddressRandom ? BLE_GAP_ADDR_TYPE_RANDOM_STATIC
                                       : BLE_GAP_ADDR_TYPE_PUBLIC;
    scan_report_.rssi = result.advRssiDbm;
    fillReportType(result.advHeader, &scan_report_.type);

    scan_report_len_ = result.advDataLength();
    if (scan_report_len_ > sizeof(scan_report_data_)) {
      scan_report_len_ = sizeof(scan_report_data_);
    }
    if (scan_report_len_ > 0U && result.advData() != nullptr) {
      memcpy(scan_report_data_, result.advData(), scan_report_len_);
    }
    scan_report_.data.p_data = scan_report_data_;
    scan_report_.data.len = scan_report_len_;
    return true;
  }

  bool currentReportMatchesFilters() const {
    if (Bluefruit.Scanner.has_filter_rssi_ &&
        scan_report_.rssi < Bluefruit.Scanner.filter_rssi_dbm_) {
      return false;
    }
    if (Bluefruit.Scanner.has_filter_uuid_ &&
        !adDataHasUuid(scan_report_.data.p_data, scan_report_.data.len,
                       Bluefruit.Scanner.filter_uuid_)) {
      return false;
    }
    if (Bluefruit.Scanner.has_filter_msd_ &&
        !adDataHasMsdCompany(scan_report_.data.p_data, scan_report_.data.len,
                             Bluefruit.Scanner.filter_msd_company_)) {
      return false;
    }
    return true;
  }

  bool maybeConnectCentral() {
    if (!pending_connect_valid_) {
      return false;
    }
    const unsigned long now = millis();
    if ((now - last_connect_attempt_ms_) < 250UL) {
      return false;
    }
    last_connect_attempt_ms_ = now;
    uint16_t interval_units = Bluefruit.central_conn_interval_;
    if (interval_units < 6U) {
      interval_units = 6U;
    } else if (interval_units > 3200U) {
      interval_units = 3200U;
    }
    uint16_t supervision_timeout_units = Bluefruit.central_supervision_timeout_;
    if (supervision_timeout_units < 10U) {
      supervision_timeout_units = 10U;
    } else if (supervision_timeout_units > 3200U) {
      supervision_timeout_units = 3200U;
    }
    if (!radio_.initiateConnection(pending_connect_address_, pending_connect_random_ != 0U,
                                   interval_units, supervision_timeout_units, 9U,
                                   1200000UL)) {
      return false;
    }
    pending_connect_valid_ = false;
    return true;
  }

  void maybeScanOrConnect() {
    if (pending_connect_valid_) {
      (void)maybeConnectCentral();
      return;
    }
    if (!Bluefruit.Scanner.running_ || Bluefruit.Scanner.paused_ ||
        Bluefruit.Scanner.rx_callback_ == nullptr) {
      return;
    }

    bool gotReport = false;
    if (Bluefruit.Scanner.active_scan_) {
      xiao_nrf54l15::BleActiveScanResult result{};
      if (radio_.scanActiveCycle(&result, 300000UL, 200000UL)) {
        gotReport = buildScanReport(result);
      }
    } else {
      xiao_nrf54l15::BleScanPacket packet{};
      if (radio_.scanCycle(&packet, 300000UL)) {
        gotReport = buildScanReport(packet);
      }
    }
    if (!gotReport || !currentReportMatchesFilters()) {
      return;
    }

    Bluefruit.Scanner.paused_ = true;
    invokeBluefruitUserCallback(Bluefruit.Scanner.rx_callback_, &scan_report_);
    if (pending_connect_valid_) {
      (void)maybeConnectCentral();
    }
  }

  void maybeApplyCentralLinkConfig() {
    if (!radio_.isConnected() || radio_.connectionRole() != BleConnectionRole::kCentral) {
      return;
    }
    if (centralSyncProcedureActive()) {
      return;
    }
    if (static_cast<int32_t>(millis() - central_link_config_not_before_ms_) < 0) {
      return;
    }

    if (central_data_length_request_pending_) {
      if ((radio_.currentDataLength() >=
           static_cast<uint16_t>(BleRadio::kCustomGattMaxValueLength + 7U)) ||
          radio_.requestDataLengthUpdate()) {
        central_data_length_request_pending_ = false;
      }
    }

    if (central_mtu_request_pending_) {
      const uint16_t mtu = Bluefruit.central_requested_mtu_;
      if ((mtu <= 23U) || (radio_.currentAttMtu() >= mtu) ||
          radio_.requestAttMtuExchange(mtu)) {
        central_mtu_request_pending_ = false;
      }
    }
  }

  void processCentralBackgroundEvents(uint8_t maxEvents) {
    maybeApplyCentralLinkConfig();
    for (uint8_t i = 0U; i < maxEvents; ++i) {
      BleConnectionEvent event{};
      if (!radio_.consumeDeferredConnectionEvent(&event)) {
        break;
      }
      handleCentralConnectionEvent(event);
    }
  }

  bool applyAdvertisingPayloads() {
    if (!radio_.setAdvertisingPduType(
            static_cast<xiao_nrf54l15::BleAdvPduType>(Bluefruit.Advertising.adv_type_))) {
      return false;
    }
    if (!radio_.setAdvertisingData(Bluefruit.Advertising.data_, Bluefruit.Advertising.len_)) {
      return false;
    }
    if (!radio_.setScanResponseData(Bluefruit.ScanResponse.data_, Bluefruit.ScanResponse.len_)) {
      return false;
    }
    Bluefruit.Advertising.dirty_ = false;
    Bluefruit.ScanResponse.dirty_ = false;
    return true;
  }

  void maybeAdvertise() {
    if (!Bluefruit.Advertising.running_) {
      return;
    }

    if (Bluefruit.Advertising.stop_timeout_s_ != 0U) {
      const unsigned long elapsedMs = millis() - adv_started_ms_;
      if (elapsedMs >= (static_cast<unsigned long>(Bluefruit.Advertising.stop_timeout_s_) *
                        1000UL)) {
        Bluefruit.Advertising.running_ = false;
        if (Bluefruit.Advertising.stop_callback_ != nullptr) {
          invokeBluefruitUserCallback(Bluefruit.Advertising.stop_callback_);
        }
        return;
      }
    }

    if (Bluefruit.Advertising.dirty_ || Bluefruit.ScanResponse.dirty_) {
      if (!applyAdvertisingPayloads()) {
        return;
      }
    }

    uint16_t intervalUnits = Bluefruit.Advertising.interval_fast_;
    if (Bluefruit.Advertising.fast_timeout_s_ != 0U) {
      const unsigned long elapsedMs = millis() - adv_started_ms_;
      if (elapsedMs >= (static_cast<unsigned long>(Bluefruit.Advertising.fast_timeout_s_) *
                        1000UL)) {
        intervalUnits = (Bluefruit.Advertising.interval_slow_ != 0U)
                            ? Bluefruit.Advertising.interval_slow_
                            : Bluefruit.Advertising.interval_fast_;
      }
    }
    if (intervalUnits == 0U) {
      intervalUnits = 32U;
    }

    const uint32_t nowUs = micros();
    if (next_adv_due_us_ != 0U && !timeReachedUs(nowUs, next_adv_due_us_)) {
      return;
    }

    radio_.advertiseInteractEvent(nullptr);
    next_adv_due_us_ = nowUs + (static_cast<uint32_t>(intervalUnits) * 625UL);
  }

  void handleConnectionEdge(bool connected) {
    if (connected) {
      connection_.handle_ = 0U;
      last_connection_role_ = radio_.connectionRole();
      pending_connect_valid_ = false;
      if (Bluefruit.auto_conn_led_) {
        digitalWrite(LED_BUILTIN, kLedOnState);
      }
      if (last_connection_role_ == BleConnectionRole::kPeripheral) {
        for (uint8_t i = 0U; i < characteristic_count_; ++i) {
          if (characteristics_[i] != nullptr) {
            characteristics_[i]->_notify_enabled = false;
            characteristics_[i]->_indicate_enabled = false;
          }
        }
        if (Bluefruit.Periph.connect_callback_ != nullptr) {
          invokeBluefruitUserCallback(Bluefruit.Periph.connect_callback_, 0U);
        }
        if (!Bluefruit.Advertising.restart_on_disconnect_) {
          Bluefruit.Advertising.running_ = false;
        }
      } else if (last_connection_role_ == BleConnectionRole::kCentral) {
        Bluefruit.Scanner.paused_ = true;
        central_data_length_request_pending_ = Bluefruit.central_request_data_length_;
        central_mtu_request_pending_ = Bluefruit.central_request_mtu_;
        central_link_config_not_before_ms_ = millis() + 1000UL;
        if (Bluefruit.Central.connect_callback_ != nullptr) {
          invokeBluefruitUserCallback(Bluefruit.Central.connect_callback_, 0U);
        }
      }
      return;
    }

    BleDisconnectDebug debug{};
    uint8_t reason = 0x13U;
    if (radio_.getDisconnectDebug(&debug)) {
      reason = disconnectReasonToHci(debug);
    }
    connection_.handle_ = INVALID_CONNECTION_HANDLE;
    if (last_connection_role_ == BleConnectionRole::kPeripheral) {
      for (uint8_t i = 0U; i < characteristic_count_; ++i) {
        if (characteristics_[i] != nullptr) {
          const bool hadNotify = characteristics_[i]->_notify_enabled;
          const bool hadIndicate = characteristics_[i]->_indicate_enabled;
          characteristics_[i]->_notify_enabled = false;
          characteristics_[i]->_indicate_enabled = false;
          if ((hadNotify || hadIndicate) && characteristics_[i]->_cccd_wr_cb != nullptr) {
            invokeBluefruitUserCallback(characteristics_[i]->_cccd_wr_cb, 0U,
                                        characteristics_[i], 0U);
          }
        }
      }
    }
    if (Bluefruit.auto_conn_led_) {
      digitalWrite(LED_BUILTIN, kLedOffState);
    }
    if (last_connection_role_ == BleConnectionRole::kPeripheral) {
      if (Bluefruit.Periph.disconnect_callback_ != nullptr) {
        invokeBluefruitUserCallback(Bluefruit.Periph.disconnect_callback_, 0U, reason);
      }
      if (Bluefruit.Advertising.restart_on_disconnect_) {
        Bluefruit.Advertising.running_ = true;
        adv_started_ms_ = millis();
        next_adv_due_us_ = 0U;
      }
    } else if (last_connection_role_ == BleConnectionRole::kCentral) {
      central_data_length_request_pending_ = false;
      central_mtu_request_pending_ = false;
      central_link_config_not_before_ms_ = 0UL;
      if (Bluefruit.Central.disconnect_callback_ != nullptr) {
        invokeBluefruitUserCallback(Bluefruit.Central.disconnect_callback_, 0U, reason);
      }
      if (Bluefruit.Scanner.restart_on_disconnect_ && Bluefruit.Scanner.running_) {
        Bluefruit.Scanner.paused_ = false;
      }
    }
    last_connection_role_ = BleConnectionRole::kNone;
  }
};

BluefruitCompatManager& manager() {
  static BluefruitCompatManager instance;
  return instance;
}

class ScopedCentralSyncProcedure {
 public:
  ScopedCentralSyncProcedure() { manager().beginCentralSyncProcedure(); }
  ~ScopedCentralSyncProcedure() { manager().endCentralSyncProcedure(); }
};

BLEService* BLEService::lastService = nullptr;
BLEClientService* BLEClientService::lastService = nullptr;
AdafruitBluefruit Bluefruit;
const uint8_t BLEUART_UUID_SERVICE[16] = {0x9EU, 0xCAU, 0xDCU, 0x24U, 0x0EU, 0xE5U,
                                          0xA9U, 0xE0U, 0x93U, 0xF3U, 0xA3U, 0xB5U,
                                          0x01U, 0x00U, 0x40U, 0x6EU};
const uint8_t BLEUART_UUID_CHR_RXD[16] = {0x9EU, 0xCAU, 0xDCU, 0x24U, 0x0EU, 0xE5U,
                                          0xA9U, 0xE0U, 0x93U, 0xF3U, 0xA3U, 0xB5U,
                                          0x02U, 0x00U, 0x40U, 0x6EU};
const uint8_t BLEUART_UUID_CHR_TXD[16] = {0x9EU, 0xCAU, 0xDCU, 0x24U, 0x0EU, 0xE5U,
                                          0xA9U, 0xE0U, 0x93U, 0xF3U, 0xA3U, 0xB5U,
                                          0x03U, 0x00U, 0x40U, 0x6EU};

enum class AttWaitOutcome : uint8_t {
  kResponse = 0,
  kError = 1,
  kDisconnected = 2,
  kTimeout = 3,
};

struct AttWaitResult {
  AttWaitOutcome outcome;
  BleConnectionEvent event;
  uint8_t errorCode;
};

bool centralReady(uint16_t connHandle) {
  return connHandle == 0U && manager().radio().isConnected() &&
         manager().radio().connectionRole() == BleConnectionRole::kCentral;
}

bool nextCentralEvent(BleConnectionEvent* event, uint32_t timeoutMs = 800UL) {
  if (event == nullptr) {
    return false;
  }

  const unsigned long startMs = millis();
  while ((millis() - startMs) < timeoutMs) {
    if (manager().radio().consumeDeferredConnectionEvent(event)) {
      return true;
    }
    if (!manager().radio().isConnected()) {
      return false;
    }
    if (manager().radio().pollConnectionEvent(event, 120000UL)) {
      return true;
    }
  }
  return false;
}

template <typename QueueAttempt>
bool queueCentralAttProcedure(QueueAttempt queueAttempt, uint32_t timeoutMs = 1000UL) {
  if (!manager().radio().isConnected()) {
    return false;
  }

  const unsigned long startMs = millis();
  while ((millis() - startMs) < timeoutMs) {
    if (queueAttempt()) {
      return true;
    }
    if (!manager().radio().isConnected()) {
      return false;
    }

    BleConnectionEvent event{};
    if (!nextCentralEvent(&event, 24UL)) {
      yield();
      continue;
    }

    if (event.terminateInd) {
      return false;
    }
    manager().handleCentralConnectionEvent(event);
  }

  return false;
}

AttWaitResult waitForAttOpcode(uint8_t requestOpcode, uint8_t responseOpcode,
                               uint32_t timeoutMs = 1000UL) {
  AttWaitResult result{};
  result.outcome = AttWaitOutcome::kTimeout;
  result.errorCode = 0U;

  const unsigned long startMs = millis();
  while ((millis() - startMs) < timeoutMs) {
    BleConnectionEvent event{};
    if (!nextCentralEvent(&event, 120UL)) {
      if (!manager().radio().isConnected()) {
        result.outcome = AttWaitOutcome::kDisconnected;
        return result;
      }
      continue;
    }

    if (event.terminateInd) {
      result.outcome = AttWaitOutcome::kDisconnected;
      result.event = event;
      return result;
    }

    if (!(event.packetReceived && event.crcOk && event.attPacket &&
          event.payload != nullptr && event.payloadLength >= 5U)) {
      manager().handleCentralConnectionEvent(event);
      continue;
    }

    const uint8_t attOpcode = event.payload[4];
    if (attOpcode == kAttOpHandleValueNtf) {
      manager().handleCentralConnectionEvent(event);
      continue;
    }

    if (attOpcode == kAttOpErrorRsp && event.payloadLength >= 9U) {
      if (event.payload[5] == requestOpcode) {
        result.outcome = AttWaitOutcome::kError;
        result.event = event;
        result.errorCode = event.payload[8];
        return result;
      }
      manager().handleCentralConnectionEvent(event);
      continue;
    }

    if (attOpcode == responseOpcode) {
      result.outcome = AttWaitOutcome::kResponse;
      result.event = event;
      return result;
    }

    manager().handleCentralConnectionEvent(event);
  }

  return result;
}

bool queueServiceDiscoveryRequest(const BLEUuid& uuid, bool reverse128 = false) {
  if (uuid.size() == 2U) {
    uint8_t request[9] = {kAttOpFindByTypeValueReq, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    writeLe16(&request[1], 0x0001U);
    writeLe16(&request[3], 0xFFFFU);
    writeLe16(&request[5], kUuidPrimaryService);
    writeLe16(&request[7], uuid.uuid16());
    return queueCentralAttProcedure(
        [&]() { return manager().radio().queueAttRequest(request, sizeof(request)); });
  }
  if (uuid.size() == 16U) {
    uint8_t request[23] = {0};
    request[0] = kAttOpFindByTypeValueReq;
    writeLe16(&request[1], 0x0001U);
    writeLe16(&request[3], 0xFFFFU);
    writeLe16(&request[5], kUuidPrimaryService);
    for (uint8_t i = 0U; i < 16U; ++i) {
      request[7U + i] = reverse128 ? uuid.uuid128()[15U - i] : uuid.uuid128()[i];
    }
    return queueCentralAttProcedure(
        [&]() { return manager().radio().queueAttRequest(request, sizeof(request)); });
  }
  return false;
}

bool discoverServiceRangeSync(const BLEUuid& uuid, uint16_t* startHandle,
                              uint16_t* endHandle) {
  ScopedCentralSyncProcedure scopedProcedure;
  if (startHandle == nullptr || endHandle == nullptr) {
    return false;
  }
  if (!queueServiceDiscoveryRequest(uuid, false)) {
    return false;
  }

  const AttWaitResult wait =
      waitForAttOpcode(kAttOpFindByTypeValueReq, kAttOpFindByTypeValueRsp);
  if (wait.outcome != AttWaitOutcome::kResponse || wait.event.payloadLength < 9U) {
    if (uuid.size() != 16U) {
      return false;
    }
  } else {
    *startHandle = readLe16(&wait.event.payload[5]);
    *endHandle = readLe16(&wait.event.payload[7]);
    if ((*startHandle != 0U) && (*endHandle >= *startHandle)) {
      return true;
    }
  }

  if (uuid.size() == 16U) {
    if (!queueServiceDiscoveryRequest(uuid, true)) {
      return false;
    }

    const AttWaitResult reversedWait =
        waitForAttOpcode(kAttOpFindByTypeValueReq, kAttOpFindByTypeValueRsp);
    if (reversedWait.outcome == AttWaitOutcome::kResponse &&
        reversedWait.event.payloadLength >= 9U) {
      *startHandle = readLe16(&reversedWait.event.payload[5]);
      *endHandle = readLe16(&reversedWait.event.payload[7]);
      if ((*startHandle != 0U) && (*endHandle >= *startHandle)) {
        return true;
      }
    }
  } else {
    return false;
  }

  uint16_t searchStart = 0x0001U;
  while (searchStart <= 0xFFFFU) {
    uint8_t request[7] = {kAttOpReadByGroupTypeReq, 0U, 0U, 0U, 0U, 0U, 0U};
    writeLe16(&request[1], searchStart);
    writeLe16(&request[3], 0xFFFFU);
    writeLe16(&request[5], kUuidPrimaryService);
    if (!queueCentralAttProcedure(
            [&]() { return manager().radio().queueAttRequest(request, sizeof(request)); })) {
      return false;
    }

    const AttWaitResult groupWait =
        waitForAttOpcode(kAttOpReadByGroupTypeReq, kAttOpReadByGroupTypeRsp);
    if (groupWait.outcome == AttWaitOutcome::kError &&
        groupWait.errorCode == kAttErrAttributeNotFound) {
      return false;
    }
    if (groupWait.outcome != AttWaitOutcome::kResponse || groupWait.event.payloadLength < 10U) {
      return false;
    }

    const uint8_t entryLen = groupWait.event.payload[5];
    if (entryLen < 6U) {
      return false;
    }

    uint16_t lastEndHandle = searchStart;
    for (uint8_t offset = 6U;
         (offset + static_cast<uint8_t>(entryLen - 1U)) < groupWait.event.payloadLength;
         offset = static_cast<uint8_t>(offset + entryLen)) {
      const uint16_t candidateStart = readLe16(&groupWait.event.payload[offset]);
      const uint16_t candidateEnd = readLe16(&groupWait.event.payload[offset + 2U]);
      const uint8_t uuidLen = static_cast<uint8_t>(entryLen - 4U);
      const uint8_t* candidateUuid = &groupWait.event.payload[offset + 4U];
      lastEndHandle = candidateEnd;
      if (uuidMatches(uuid, candidateUuid, uuidLen)) {
        *startHandle = candidateStart;
        *endHandle = candidateEnd;
        return (*startHandle != 0U) && (*endHandle >= *startHandle);
      }
    }

    if (lastEndHandle == 0xFFFFU || lastEndHandle < searchStart) {
      break;
    }
    searchStart = static_cast<uint16_t>(lastEndHandle + 1U);
  }

  return false;
}

bool discoverCharacteristicSync(uint16_t serviceStartHandle, uint16_t serviceEndHandle,
                                const BLEUuid& uuid, uint16_t* declHandle,
                                uint16_t* valueHandle, uint16_t* endHandle) {
  ScopedCentralSyncProcedure scopedProcedure;
  if (declHandle == nullptr || valueHandle == nullptr || endHandle == nullptr ||
      serviceStartHandle == 0U || serviceEndHandle == 0U) {
    return false;
  }

  bool found = false;
  uint16_t searchStart = serviceStartHandle;
  *declHandle = 0U;
  *valueHandle = 0U;
  *endHandle = 0U;

  while (searchStart <= serviceEndHandle) {
    uint8_t request[7] = {kAttOpReadByTypeReq, 0U, 0U, 0U, 0U, 0U, 0U};
    writeLe16(&request[1], searchStart);
    writeLe16(&request[3], serviceEndHandle);
    writeLe16(&request[5], kUuidCharacteristic);
    if (!queueCentralAttProcedure(
            [&]() { return manager().radio().queueAttRequest(request, sizeof(request)); })) {
      return false;
    }

    const AttWaitResult wait =
        waitForAttOpcode(kAttOpReadByTypeReq, kAttOpReadByTypeRsp);
    if (wait.outcome == AttWaitOutcome::kError &&
        wait.errorCode == kAttErrAttributeNotFound) {
      break;
    }
    if (wait.outcome != AttWaitOutcome::kResponse || wait.event.payloadLength < 7U) {
      return false;
    }

    const uint8_t entryLen = wait.event.payload[5];
    if (entryLen < 7U) {
      return false;
    }

    uint16_t lastDecl = searchStart;
    bool anyEntry = false;
    for (uint8_t offset = 6U;
         (offset + static_cast<uint8_t>(entryLen - 1U)) < wait.event.payloadLength;
         offset = static_cast<uint8_t>(offset + entryLen)) {
      anyEntry = true;
      const uint16_t candidateDecl = readLe16(&wait.event.payload[offset]);
      const uint16_t candidateValue = readLe16(&wait.event.payload[offset + 3U]);
      const uint8_t uuidLen = static_cast<uint8_t>(entryLen - 5U);
      const uint8_t* candidateUuid = &wait.event.payload[offset + 5U];

      if (found && candidateDecl > *declHandle) {
        *endHandle = static_cast<uint16_t>(candidateDecl - 1U);
        return true;
      }

      if (!found && uuidMatches(uuid, candidateUuid, uuidLen)) {
        found = true;
        *declHandle = candidateDecl;
        *valueHandle = candidateValue;
        *endHandle = serviceEndHandle;
      }

      lastDecl = candidateDecl;
    }

    if (found) {
      return true;
    }

    if (!anyEntry || lastDecl >= serviceEndHandle) {
      break;
    }
    searchStart = static_cast<uint16_t>(lastDecl + 1U);
  }

  return found;
}

bool discoverCccdHandleSync(uint16_t valueHandle, uint16_t endHandle, uint16_t* cccdHandle) {
  ScopedCentralSyncProcedure scopedProcedure;
  if (cccdHandle == nullptr || valueHandle == 0U || endHandle <= valueHandle) {
    return false;
  }

  uint16_t searchStart = static_cast<uint16_t>(valueHandle + 1U);
  while (searchStart <= endHandle) {
    uint8_t request[5] = {kAttOpFindInfoReq, 0U, 0U, 0U, 0U};
    writeLe16(&request[1], searchStart);
    writeLe16(&request[3], endHandle);
    if (!queueCentralAttProcedure(
            [&]() { return manager().radio().queueAttRequest(request, sizeof(request)); })) {
      return false;
    }

    const AttWaitResult wait = waitForAttOpcode(kAttOpFindInfoReq, kAttOpFindInfoRsp);
    if (wait.outcome == AttWaitOutcome::kError &&
        wait.errorCode == kAttErrAttributeNotFound) {
      return false;
    }
    if (wait.outcome != AttWaitOutcome::kResponse || wait.event.payloadLength < 6U) {
      return false;
    }

    const uint8_t format = wait.event.payload[5];
    uint16_t lastHandle = searchStart;
    if (format == 0x01U) {
      for (uint8_t offset = 6U; (offset + 3U) < wait.event.payloadLength;
           offset = static_cast<uint8_t>(offset + 4U)) {
        const uint16_t handle = readLe16(&wait.event.payload[offset]);
        const uint16_t uuid16 = readLe16(&wait.event.payload[offset + 2U]);
        if (uuid16 == kUuidClientCharacteristicConfig) {
          *cccdHandle = handle;
          return true;
        }
        lastHandle = handle;
      }
    } else if (format == 0x02U) {
      for (uint8_t offset = 6U; (offset + 17U) < wait.event.payloadLength;
           offset = static_cast<uint8_t>(offset + 18U)) {
        lastHandle = readLe16(&wait.event.payload[offset]);
      }
    } else {
      return false;
    }

    if (lastHandle >= endHandle) {
      return false;
    }
    searchStart = static_cast<uint16_t>(lastHandle + 1U);
  }

  return false;
}

uint16_t readHandleSync(uint16_t handle, uint8_t* buffer, uint16_t bufferLen) {
  ScopedCentralSyncProcedure scopedProcedure;
  if (handle == 0U || buffer == nullptr || bufferLen == 0U ||
      !queueCentralAttProcedure(
          [&]() { return manager().radio().queueAttReadRequest(handle); })) {
    return 0U;
  }

  const AttWaitResult wait = waitForAttOpcode(kAttOpReadReq, kAttOpReadRsp);
  if (wait.outcome != AttWaitOutcome::kResponse || wait.event.payloadLength < 5U) {
    return 0U;
  }

  const uint16_t valueLen = static_cast<uint16_t>(wait.event.payloadLength - 5U);
  const uint16_t copyLen = min<uint16_t>(bufferLen, valueLen);
  if (copyLen > 0U) {
    memcpy(buffer, &wait.event.payload[5], copyLen);
  }
  return copyLen;
}

bool writeHandleSync(uint16_t handle, const uint8_t* data, uint8_t dataLen,
                     bool withResponse = true) {
  ScopedCentralSyncProcedure scopedProcedure;
  if (!queueCentralAttProcedure([&]() {
        return manager().radio().queueAttWriteRequest(handle, data, dataLen, withResponse);
      })) {
    return false;
  }
  if (!withResponse) {
    return true;
  }

  const AttWaitResult wait = waitForAttOpcode(kAttOpWriteReq, kAttOpWriteRsp);
  return wait.outcome == AttWaitOutcome::kResponse;
}

extern "C" void nrf54l15_bluefruit_compat_idle_service(void) {
  if (ScopedBluefruitUserCallback::active()) {
    return;
  }
  manager().idleService();
}

BLEUuid::BLEUuid() : size_(0U), uuid16_(0U), uuid128_{0} {}

BLEUuid::BLEUuid(uint16_t uuid16) : BLEUuid() { set(uuid16); }

BLEUuid::BLEUuid(const uint8_t uuid128[16]) : BLEUuid() { set(uuid128); }

BLEUuid::BLEUuid(const char* str) : BLEUuid() { set(str); }

void BLEUuid::set(uint16_t uuid16) {
  size_ = 2U;
  uuid16_ = uuid16;
  memset(uuid128_, 0, sizeof(uuid128_));
}

void BLEUuid::set(const uint8_t uuid128[16]) {
  if (uuid128 == nullptr) {
    size_ = 0U;
    uuid16_ = 0U;
    memset(uuid128_, 0, sizeof(uuid128_));
    return;
  }
  size_ = 16U;
  uuid16_ = 0U;
  memcpy(uuid128_, uuid128, sizeof(uuid128_));
}

void BLEUuid::set(const char* str) {
  uint8_t parsed[16] = {0};
  uint8_t parsedSize = 0U;
  uint16_t parsed16 = 0U;
  if (!parseUuidString(str, parsed, &parsedSize, &parsed16)) {
    size_ = 0U;
    uuid16_ = 0U;
    memset(uuid128_, 0, sizeof(uuid128_));
    return;
  }
  if (parsedSize == 2U) {
    set(parsed16);
  } else {
    set(parsed);
  }
}

bool BLEUuid::get(uint16_t* uuid16) const {
  if (uuid16 == nullptr || size_ != 2U) {
    return false;
  }
  *uuid16 = uuid16_;
  return true;
}

bool BLEUuid::get(uint8_t uuid128[16]) const {
  if (uuid128 == nullptr || size_ != 16U) {
    return false;
  }
  memcpy(uuid128, uuid128_, sizeof(uuid128_));
  return true;
}

size_t BLEUuid::size() const { return size_; }

bool BLEUuid::begin() const { return size_ == 2U || size_ == 16U; }

String BLEUuid::toString() const {
  if (size_ == 2U) {
    char buffer[5] = {0};
    snprintf(buffer, sizeof(buffer), "%04X", uuid16_);
    return String(buffer);
  }
  if (size_ != 16U) {
    return String();
  }
  uint8_t bigEndian[16] = {0};
  for (size_t i = 0U; i < 16U; ++i) {
    bigEndian[i] = uuid128_[15U - i];
  }
  char buffer[37] = {0};
  snprintf(buffer, sizeof(buffer),
           "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
           bigEndian[0], bigEndian[1], bigEndian[2], bigEndian[3], bigEndian[4],
           bigEndian[5], bigEndian[6], bigEndian[7], bigEndian[8], bigEndian[9],
           bigEndian[10], bigEndian[11], bigEndian[12], bigEndian[13], bigEndian[14],
           bigEndian[15]);
  return String(buffer);
}

bool BLEUuid::operator==(const BLEUuid& rhs) const {
  if (size_ != rhs.size_) {
    return false;
  }
  if (size_ == 2U) {
    return uuid16_ == rhs.uuid16_;
  }
  if (size_ == 16U) {
    return memcmp(uuid128_, rhs.uuid128_, sizeof(uuid128_)) == 0;
  }
  return true;
}

bool BLEUuid::operator!=(const BLEUuid& rhs) const { return !(*this == rhs); }

uint16_t BLEUuid::uuid16() const { return uuid16_; }

const uint8_t* BLEUuid::uuid128() const { return uuid128_; }

BLEService::BLEService()
    : uuid(), _handle(0U), _read_perm(SECMODE_OPEN), _write_perm(SECMODE_OPEN),
      _begun(false) {}

BLEService::BLEService(BLEUuid bleuuid)
    : uuid(bleuuid), _handle(0U), _read_perm(SECMODE_OPEN), _write_perm(SECMODE_OPEN),
      _begun(false) {}

void BLEService::setUuid(BLEUuid bleuuid) { uuid = bleuuid; }

void BLEService::setPermission(SecureMode_t read_perm, SecureMode_t write_perm) {
  _read_perm = read_perm;
  _write_perm = write_perm;
}

void BLEService::getPermission(SecureMode_t* read_perm, SecureMode_t* write_perm) const {
  if (read_perm != nullptr) {
    *read_perm = _read_perm;
  }
  if (write_perm != nullptr) {
    *write_perm = _write_perm;
  }
}

err_t BLEService::begin() {
  if (!manager().begin(1U, 0U)) {
    return ERROR_INVALID_STATE;
  }
  if (_begun) {
    lastService = this;
    return ERROR_NONE;
  }

  bool ok = false;
  if (uuid.size() == 2U) {
    ok = manager().radio().addCustomGattService(uuid.uuid16(), &_handle);
  } else if (uuid.size() == 16U) {
    ok = manager().radio().addCustomGattService128(uuid.uuid128(), &_handle);
  }
  if (!ok) {
    return ERROR_INVALID_STATE;
  }

  _begun = true;
  lastService = this;
  return ERROR_NONE;
}

BLECharacteristic::BLECharacteristic()
    : uuid(),
      _is_temp(false),
      _max_len(BleRadio::kCustomGattMaxValueLength),
      _fixed_len(false),
      _service(nullptr),
      _userbuf(nullptr),
      _userbufsize(0U),
      _properties(0U),
      _read_perm(SECMODE_OPEN),
      _write_perm(SECMODE_OPEN),
      _handles{0U, 0U, 0U, 0U},
      _value{0},
      _value_len(0U),
      _notify_enabled(false),
      _indicate_enabled(false),
      _usr_descriptor(nullptr),
      _wr_cb(nullptr),
      _cccd_wr_cb(nullptr),
      _rd_authorize_cb(nullptr),
      _wr_authorize_cb(nullptr) {}

BLECharacteristic::BLECharacteristic(BLEUuid bleuuid) : BLECharacteristic() { uuid = bleuuid; }

BLECharacteristic::BLECharacteristic(BLEUuid bleuuid, uint8_t properties)
    : BLECharacteristic(bleuuid) {
  _properties = properties;
}

BLECharacteristic::BLECharacteristic(BLEUuid bleuuid, uint8_t properties, int max_len,
                                     bool fixed_len)
    : BLECharacteristic(bleuuid, properties) {
  setMaxLen(static_cast<uint16_t>(max_len));
  if (fixed_len) {
    setFixedLen(static_cast<uint16_t>(max_len));
  }
}

BLEService& BLECharacteristic::parentService() { return *_service; }

void BLECharacteristic::setTempMemory() { _is_temp = true; }

void BLECharacteristic::setUuid(BLEUuid bleuuid) { uuid = bleuuid; }

void BLECharacteristic::setProperties(uint8_t prop) { _properties = prop; }

void BLECharacteristic::setPermission(SecureMode_t read_perm, SecureMode_t write_perm) {
  _read_perm = read_perm;
  _write_perm = write_perm;
}

void BLECharacteristic::setMaxLen(uint16_t max_len) {
  _max_len = (max_len > BleRadio::kCustomGattMaxValueLength)
                 ? BleRadio::kCustomGattMaxValueLength
                 : max_len;
}

void BLECharacteristic::setFixedLen(uint16_t fixed_len) {
  setMaxLen(fixed_len);
  _fixed_len = true;
}

void BLECharacteristic::setBuffer(void* buf, uint16_t bufsize) {
  _userbuf = static_cast<uint8_t*>(buf);
  _userbufsize = bufsize;
}

uint16_t BLECharacteristic::getMaxLen() const { return _max_len; }

bool BLECharacteristic::isFixedLen() const { return _fixed_len; }

void BLECharacteristic::setUserDescriptor(const char* descriptor) { _usr_descriptor = descriptor; }

void BLECharacteristic::setReportRefDescriptor(uint8_t id, uint8_t type) {
  (void)id;
  (void)type;
}

void BLECharacteristic::setPresentationFormatDescriptor(uint8_t type, int8_t exponent,
                                                        uint16_t unit, uint8_t name_space,
                                                        uint16_t descriptor) {
  (void)type;
  (void)exponent;
  (void)unit;
  (void)name_space;
  (void)descriptor;
}

void BLECharacteristic::setWriteCallback(write_cb_t fp, bool useAdaCallback) {
  (void)useAdaCallback;
  _wr_cb = fp;
}

void BLECharacteristic::setCccdWriteCallback(write_cccd_cb_t fp, bool useAdaCallback) {
  (void)useAdaCallback;
  _cccd_wr_cb = fp;
}

void BLECharacteristic::setReadAuthorizeCallback(read_authorize_cb_t fp, bool useAdaCallback) {
  (void)useAdaCallback;
  _rd_authorize_cb = fp;
}

void BLECharacteristic::setWriteAuthorizeCallback(write_authorize_cb_t fp,
                                                  bool useAdaCallback) {
  (void)useAdaCallback;
  _wr_authorize_cb = fp;
}

err_t BLECharacteristic::begin() {
  if (!manager().begin(1U, 0U)) {
    return ERROR_INVALID_STATE;
  }
  if (_handles.value_handle != 0U) {
    return ERROR_NONE;
  }

  _service = BLEService::lastService;
  if (_service == nullptr || !_service->_begun) {
    return ERROR_INVALID_STATE;
  }

  uint16_t valueHandle = 0U;
  uint16_t cccdHandle = 0U;
  const uint8_t initialLen = clampValueLen(_value_len);
  const uint8_t properties = mapProperties(_properties);
  bool ok = false;
  if (uuid.size() == 2U) {
    ok = manager().radio().addCustomGattCharacteristic(
        _service->_handle, uuid.uuid16(), properties, _value, initialLen, &valueHandle,
        &cccdHandle);
  } else if (uuid.size() == 16U) {
    ok = manager().radio().addCustomGattCharacteristic128(
        _service->_handle, uuid.uuid128(), properties, _value, initialLen, &valueHandle,
        &cccdHandle);
  }
  if (!ok) {
    return ERROR_INVALID_STATE;
  }

  _handles.value_handle = valueHandle;
  _handles.decl_handle = (valueHandle > 0U) ? static_cast<uint16_t>(valueHandle - 1U) : 0U;
  _handles.cccd_handle = cccdHandle;
  if (!manager().registerCharacteristic(this)) {
    return ERROR_NO_MEM;
  }
  return ERROR_NONE;
}

ble_gatts_char_handles_t BLECharacteristic::handles() const { return _handles; }

uint16_t BLECharacteristic::write(const void* data, uint16_t len) {
  if (data == nullptr && len > 0U) {
    return 0U;
  }
  const uint8_t toWrite = clampValueLen((_fixed_len && _max_len > 0U) ? _max_len : len);
  memset(_value, 0, sizeof(_value));
  if (data != nullptr && toWrite > 0U) {
    memcpy(_value, data, min<uint8_t>(toWrite, clampValueLen(len)));
  }
  _value_len = toWrite;
  if (_userbuf != nullptr && _userbufsize > 0U) {
    const uint16_t copied = min<uint16_t>(_userbufsize, _value_len);
    memcpy(_userbuf, _value, copied);
  }
  if (_handles.value_handle != 0U) {
    manager().radio().setCustomGattCharacteristicValue(_handles.value_handle, _value, _value_len);
  }
  return _value_len;
}

uint16_t BLECharacteristic::write(const char* str) {
  return write(str, (str != nullptr) ? static_cast<uint16_t>(strlen(str)) : 0U);
}

uint16_t BLECharacteristic::write8(uint8_t num) { return write(&num, sizeof(num)); }

uint16_t BLECharacteristic::write16(uint16_t num) { return write(&num, sizeof(num)); }

uint16_t BLECharacteristic::write32(uint32_t num) { return write(&num, sizeof(num)); }

uint16_t BLECharacteristic::write32(int num) { return write(&num, sizeof(num)); }

uint16_t BLECharacteristic::writeFloat(float num) { return write(&num, sizeof(num)); }

uint16_t BLECharacteristic::read(void* buffer, uint16_t bufsize, uint16_t offset) {
  if (buffer == nullptr || bufsize == 0U) {
    return 0U;
  }
  if (_handles.value_handle != 0U) {
    uint8_t len = sizeof(_value);
    if (manager().radio().getCustomGattCharacteristicValue(_handles.value_handle, _value, &len)) {
      _value_len = len;
    }
  }
  if (offset >= _value_len) {
    return 0U;
  }
  const uint16_t toCopy = min<uint16_t>(bufsize, static_cast<uint16_t>(_value_len - offset));
  memcpy(buffer, &_value[offset], toCopy);
  return toCopy;
}

uint8_t BLECharacteristic::read8() {
  uint8_t value = 0U;
  read(&value, sizeof(value));
  return value;
}

uint16_t BLECharacteristic::read16() {
  uint16_t value = 0U;
  read(&value, sizeof(value));
  return value;
}

uint32_t BLECharacteristic::read32() {
  uint32_t value = 0UL;
  read(&value, sizeof(value));
  return value;
}

float BLECharacteristic::readFloat() {
  float value = 0.0f;
  read(&value, sizeof(value));
  return value;
}

uint16_t BLECharacteristic::getCccd(uint16_t conn_hdl) {
  (void)conn_hdl;
  uint16_t value = 0U;
  if (notifyEnabled()) {
    value |= 0x0001U;
  }
  if (indicateEnabled()) {
    value |= 0x0002U;
  }
  return value;
}

bool BLECharacteristic::notifyEnabled() { return notifyEnabled(0U); }

bool BLECharacteristic::notifyEnabled(uint16_t conn_hdl) {
  (void)conn_hdl;
  return (_handles.value_handle != 0U) &&
         manager().radio().isCustomGattCccdEnabled(_handles.value_handle, false);
}

bool BLECharacteristic::notify(const void* data, uint16_t len) {
  return notify(0U, data, len);
}

bool BLECharacteristic::notify(const char* str) {
  return notify(0U, str, (str != nullptr) ? static_cast<uint16_t>(strlen(str)) : 0U);
}

bool BLECharacteristic::notify8(uint8_t num) { return notify(&num, sizeof(num)); }

bool BLECharacteristic::notify16(uint16_t num) { return notify(&num, sizeof(num)); }

bool BLECharacteristic::notify32(uint32_t num) { return notify(&num, sizeof(num)); }

bool BLECharacteristic::notify32(int num) { return notify(&num, sizeof(num)); }

bool BLECharacteristic::notify32(float num) { return notify(&num, sizeof(num)); }

bool BLECharacteristic::notify(uint16_t conn_hdl, const void* data, uint16_t len) {
  (void)conn_hdl;
  if (_handles.value_handle == 0U) {
    return false;
  }
  const uint16_t written = write(data, len);
  if (len > 0U && written == 0U) {
    return false;
  }
  return manager().radio().notifyCustomGattCharacteristic(_handles.value_handle, false);
}

bool BLECharacteristic::notify(uint16_t conn_hdl, const char* str) {
  return notify(conn_hdl, str, (str != nullptr) ? static_cast<uint16_t>(strlen(str)) : 0U);
}

bool BLECharacteristic::notify8(uint16_t conn_hdl, uint8_t num) {
  return notify(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::notify16(uint16_t conn_hdl, uint16_t num) {
  return notify(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::notify32(uint16_t conn_hdl, uint32_t num) {
  return notify(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::notify32(uint16_t conn_hdl, int num) {
  return notify(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::notify32(uint16_t conn_hdl, float num) {
  return notify(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::indicateEnabled() { return indicateEnabled(0U); }

bool BLECharacteristic::indicateEnabled(uint16_t conn_hdl) {
  (void)conn_hdl;
  return (_handles.value_handle != 0U) &&
         manager().radio().isCustomGattCccdEnabled(_handles.value_handle, true);
}

bool BLECharacteristic::indicate(const void* data, uint16_t len) {
  return indicate(0U, data, len);
}

bool BLECharacteristic::indicate(const char* str) {
  return indicate(0U, str, (str != nullptr) ? static_cast<uint16_t>(strlen(str)) : 0U);
}

bool BLECharacteristic::indicate8(uint8_t num) { return indicate(&num, sizeof(num)); }

bool BLECharacteristic::indicate16(uint16_t num) { return indicate(&num, sizeof(num)); }

bool BLECharacteristic::indicate32(uint32_t num) { return indicate(&num, sizeof(num)); }

bool BLECharacteristic::indicate32(int num) { return indicate(&num, sizeof(num)); }

bool BLECharacteristic::indicate32(float num) { return indicate(&num, sizeof(num)); }

bool BLECharacteristic::indicate(uint16_t conn_hdl, const void* data, uint16_t len) {
  (void)conn_hdl;
  if (_handles.value_handle == 0U ||
      manager().radio().isCustomGattNotificationQueued(_handles.value_handle, true)) {
    return false;
  }
  const uint16_t written = write(data, len);
  if (len > 0U && written == 0U) {
    return false;
  }
  return manager().radio().notifyCustomGattCharacteristic(_handles.value_handle, true);
}

bool BLECharacteristic::indicate(uint16_t conn_hdl, const char* str) {
  return indicate(conn_hdl, str, (str != nullptr) ? static_cast<uint16_t>(strlen(str)) : 0U);
}

bool BLECharacteristic::indicate8(uint16_t conn_hdl, uint8_t num) {
  return indicate(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::indicate16(uint16_t conn_hdl, uint16_t num) {
  return indicate(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::indicate32(uint16_t conn_hdl, uint32_t num) {
  return indicate(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::indicate32(uint16_t conn_hdl, int num) {
  return indicate(conn_hdl, &num, sizeof(num));
}

bool BLECharacteristic::indicate32(uint16_t conn_hdl, float num) {
  return indicate(conn_hdl, &num, sizeof(num));
}

void BLECharacteristic::handleWriteFromRadio(const uint8_t* data, uint16_t len) {
  const uint8_t toCopy = clampValueLen(len);
  memset(_value, 0, sizeof(_value));
  if (data != nullptr && toCopy > 0U) {
    memcpy(_value, data, toCopy);
  }
  _value_len = toCopy;
  if (_userbuf != nullptr && _userbufsize > 0U) {
    const uint16_t copied = min<uint16_t>(_userbufsize, _value_len);
    memcpy(_userbuf, _value, copied);
  }
  if (_wr_cb != nullptr) {
    invokeBluefruitUserCallback(_wr_cb, 0U, this, _value, _value_len);
  }
}

void BLECharacteristic::pollCccdState() {
  if (_handles.value_handle == 0U || _cccd_wr_cb == nullptr) {
    return;
  }
  const bool notify = manager().radio().isCustomGattCccdEnabled(_handles.value_handle, false);
  const bool indicate = manager().radio().isCustomGattCccdEnabled(_handles.value_handle, true);
  if (notify == _notify_enabled && indicate == _indicate_enabled) {
    return;
  }
  _notify_enabled = notify;
  _indicate_enabled = indicate;
  uint16_t cccd = 0U;
  if (notify) {
    cccd |= 0x0001U;
  }
  if (indicate) {
    cccd |= 0x0002U;
  }
  invokeBluefruitUserCallback(_cccd_wr_cb, 0U, this, cccd);
}

BLEAdvertisingData::BLEAdvertisingData(bool scan_response)
    : data_{0}, len_(0U), scan_response_(scan_response), dirty_(true) {}

void BLEAdvertisingData::clearData() {
  memset(data_, 0, sizeof(data_));
  len_ = 0U;
  dirty_ = true;
}

bool BLEAdvertisingData::setData(const uint8_t* data, uint8_t len) {
  if ((data == nullptr && len > 0U) || len > sizeof(data_)) {
    return false;
  }
  memset(data_, 0, sizeof(data_));
  if (len > 0U) {
    memcpy(data_, data, len);
  }
  len_ = len;
  dirty_ = true;
  return true;
}

bool BLEAdvertisingData::addData(uint8_t type, const void* data, uint8_t len) {
  if ((data == nullptr && len > 0U) || (len_ + len + 2U) > sizeof(data_)) {
    return false;
  }
  data_[len_++] = static_cast<uint8_t>(len + 1U);
  data_[len_++] = type;
  if (len > 0U) {
    memcpy(&data_[len_], data, len);
    len_ = static_cast<uint8_t>(len_ + len);
  }
  dirty_ = true;
  return true;
}

bool BLEAdvertisingData::addFlags(uint8_t flags) { return addData(kAdTypeFlags, &flags, 1U); }

bool BLEAdvertisingData::addTxPower() {
  const int8_t txPower = Bluefruit.getTxPower();
  return addData(kAdTypeTxPower, &txPower, 1U);
}

bool BLEAdvertisingData::addName() {
  char name[CFG_MAX_DEVNAME_LEN + 1] = {0};
  const uint8_t nameLen = Bluefruit.getName(name, sizeof(name));
  if (nameLen == 0U) {
    return false;
  }
  const uint8_t maxLen = static_cast<uint8_t>((sizeof(data_) > (len_ + 2U))
                                                  ? (sizeof(data_) - len_ - 2U)
                                                  : 0U);
  if (maxLen == 0U) {
    return false;
  }
  const uint8_t actualLen = (nameLen > maxLen) ? maxLen : nameLen;
  const uint8_t type = (actualLen == nameLen) ? kAdTypeCompleteName : kAdTypeShortName;
  return addData(type, name, actualLen);
}

bool BLEAdvertisingData::addAppearance(uint16_t appearance) {
  return addData(kAdTypeAppearance, &appearance, sizeof(appearance));
}

bool BLEAdvertisingData::addManufacturerData(const void* data, uint8_t len) {
  return addData(kAdTypeManufacturer, data, len);
}

bool BLEAdvertisingData::addService(const BLEService& service) { return addService(service.uuid); }

bool BLEAdvertisingData::addService(const BLEClientService& service) {
  return addService(service.uuid);
}

bool BLEAdvertisingData::addService(const BLEService& service1, const BLEService& service2) {
  return addService(service1) && addService(service2);
}

bool BLEAdvertisingData::addService(const BLEService& service1, const BLEService& service2,
                                    const BLEService& service3) {
  return addService(service1) && addService(service2) && addService(service3);
}

bool BLEAdvertisingData::addService(const BLEService& service1, const BLEService& service2,
                                    const BLEService& service3,
                                    const BLEService& service4) {
  return addService(service1) && addService(service2) && addService(service3) &&
         addService(service4);
}

bool BLEAdvertisingData::addService(const BLEUuid& uuid) { return addUuid(uuid); }

bool BLEAdvertisingData::addUuid(const BLEUuid& uuid) {
  if (uuid.size() == 2U) {
    const uint16_t uuid16 = uuid.uuid16();
    return addData(kAdTypeComplete16, &uuid16, sizeof(uuid16));
  }
  if (uuid.size() == 16U) {
    return addData(kAdTypeComplete128, uuid.uuid128(), 16U);
  }
  return false;
}

bool BLEAdvertisingData::addUuid(const BLEUuid& uuid1, const BLEUuid& uuid2) {
  return addUuid(uuid1) && addUuid(uuid2);
}

bool BLEAdvertisingData::addUuid(const BLEUuid& uuid1, const BLEUuid& uuid2,
                                 const BLEUuid& uuid3) {
  return addUuid(uuid1) && addUuid(uuid2) && addUuid(uuid3);
}

bool BLEAdvertisingData::addUuid(const BLEUuid& uuid1, const BLEUuid& uuid2,
                                 const BLEUuid& uuid3, const BLEUuid& uuid4) {
  return addUuid(uuid1) && addUuid(uuid2) && addUuid(uuid3) && addUuid(uuid4);
}

bool BLEAdvertisingData::addUuid(const BLEUuid uuids[], uint8_t count) {
  if (uuids == nullptr) {
    return false;
  }
  for (uint8_t i = 0U; i < count; ++i) {
    if (!addUuid(uuids[i])) {
      return false;
    }
  }
  return true;
}

uint8_t BLEAdvertisingData::count() const { return len_; }

uint8_t* BLEAdvertisingData::getData() { return data_; }

BLEAdvertising::BLEAdvertising()
    : BLEAdvertisingData(false),
      restart_on_disconnect_(true),
      interval_fast_(32U),
      interval_slow_(244U),
      fast_timeout_s_(30U),
      stop_timeout_s_(0U),
      adv_type_(static_cast<uint8_t>(xiao_nrf54l15::BleAdvPduType::kAdvInd)),
      running_(false),
      slow_callback_(nullptr),
      stop_callback_(nullptr) {}

void BLEAdvertising::restartOnDisconnect(bool enable) { restart_on_disconnect_ = enable; }

void BLEAdvertising::setInterval(uint16_t fast, uint16_t slow) {
  interval_fast_ = fast;
  interval_slow_ = slow;
}

void BLEAdvertising::setIntervalMS(uint16_t fast_ms, uint16_t slow_ms) {
  setInterval(MS1000TO625(fast_ms), (slow_ms == 0U) ? 0U : MS1000TO625(slow_ms));
}

void BLEAdvertising::setFastTimeout(uint16_t seconds) { fast_timeout_s_ = seconds; }

void BLEAdvertising::setType(uint8_t type) { adv_type_ = type; }

void BLEAdvertising::setSlowCallback(slow_callback_t cb) { slow_callback_ = cb; }

void BLEAdvertising::setStopCallback(stop_callback_t cb) { stop_callback_ = cb; }

uint16_t BLEAdvertising::getInterval() const { return interval_fast_; }

bool BLEAdvertising::setBeacon(BLEBeacon& beacon) { return beacon.start(*this); }

bool BLEAdvertising::setBeacon(EddyStoneUrl& beacon) { return beacon.start(*this); }

bool BLEAdvertising::start(uint16_t timeout) {
  if (!manager().begin(1U, 0U)) {
    return false;
  }
  stop_timeout_s_ = timeout;
  running_ = true;
  manager().markAdvertisingDirty();
  manager().advertisingStarted();
  return true;
}

bool BLEAdvertising::stop() {
  running_ = false;
  return true;
}

bool BLEAdvertising::isRunning() const { return running_; }

BLEBeacon::BLEBeacon() { reset(); }

BLEBeacon::BLEBeacon(const uint8_t uuid128[16]) {
  reset();
  setUuid(uuid128);
}

BLEBeacon::BLEBeacon(const uint8_t uuid128[16], uint16_t major, uint16_t minor, int8_t rssi) {
  reset();
  setUuid(uuid128);
  setMajorMinor(major, minor);
  setRssiAt1m(rssi);
}

void BLEBeacon::reset() {
  manufacturer_ = kCompanyIdApple;
  memset(uuid128_, 0, sizeof(uuid128_));
  uuid_valid_ = false;
  major_be_ = 0U;
  minor_be_ = 0U;
  rssi_at_1m_ = -54;
}

void BLEBeacon::setManufacturer(uint16_t manufacturer) { manufacturer_ = manufacturer; }

void BLEBeacon::setUuid(const uint8_t uuid128[16]) {
  if (uuid128 == nullptr) {
    memset(uuid128_, 0, sizeof(uuid128_));
    uuid_valid_ = false;
    return;
  }
  memcpy(uuid128_, uuid128, sizeof(uuid128_));
  uuid_valid_ = true;
}

void BLEBeacon::setMajorMinor(uint16_t major, uint16_t minor) {
  major_be_ = byteSwap16(major);
  minor_be_ = byteSwap16(minor);
}

void BLEBeacon::setRssiAt1m(int8_t rssi) { rssi_at_1m_ = rssi; }

bool BLEBeacon::start() { return start(Bluefruit.Advertising); }

bool BLEBeacon::start(BLEAdvertising& adv) {
  if (!uuid_valid_) {
    return false;
  }

  struct {
    uint16_t manufacturer;
    uint8_t beacon_type;
    uint8_t beacon_len;
    uint8_t uuid128[16];
    uint16_t major;
    uint16_t minor;
    int8_t rssi_at_1m;
  } payload{};

  payload.manufacturer = manufacturer_;
  payload.beacon_type = 0x02U;
  payload.beacon_len = 21U;
  memcpy(payload.uuid128, uuid128_, sizeof(payload.uuid128));
  payload.major = major_be_;
  payload.minor = minor_be_;
  payload.rssi_at_1m = rssi_at_1m_;

  adv.clearData();
  return adv.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) &&
         adv.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, &payload,
                     sizeof(payload));
}

namespace {

constexpr const char* kEddystonePrefixes[] = {
    "http://www.",
    "https://www.",
    "http://",
    "https://",
};

constexpr const char* kEddystoneExpansions[] = {
    ".com/",
    ".org/",
    ".edu/",
    ".net/",
    ".info/",
    ".biz/",
    ".gov/",
    ".com",
    ".org",
    ".edu",
    ".net",
    ".info",
    ".biz",
    ".gov",
};

const char* findEddystoneExpansion(const char* url, uint8_t* index) {
  if (url == nullptr || index == nullptr) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < (sizeof(kEddystoneExpansions) / sizeof(kEddystoneExpansions[0]));
       ++i) {
    const char* match = strstr(url, kEddystoneExpansions[i]);
    if (match != nullptr) {
      *index = i;
      return match;
    }
  }
  return nullptr;
}

}  // namespace

EddyStoneUrl::EddyStoneUrl() : rssi_(0), url_(nullptr) {}

EddyStoneUrl::EddyStoneUrl(int8_t rssiAt0m, const char* url) : rssi_(rssiAt0m), url_(url) {}

void EddyStoneUrl::setUrl(const char* url) { url_ = url; }

void EddyStoneUrl::setRssi(int8_t rssiAt0m) { rssi_ = rssiAt0m; }

bool EddyStoneUrl::start() { return start(Bluefruit.Advertising); }

bool EddyStoneUrl::start(BLEAdvertising& adv) {
  if (url_ == nullptr) {
    return false;
  }

  struct ATTR_PACKED {
    uint16_t eddystone_uuid;
    uint8_t frame_type;
    int8_t rssi_at_0m;
    uint8_t url_scheme;
    uint8_t encoded_url[17];
  } payload{};

  payload.eddystone_uuid = UUID16_SVC_EDDYSTONE;
  payload.frame_type = 0x10U;
  payload.rssi_at_0m = rssi_;
  payload.url_scheme = 0xFFU;

  const char* url = url_;
  for (uint8_t i = 0U; i < (sizeof(kEddystonePrefixes) / sizeof(kEddystonePrefixes[0])); ++i) {
    const size_t prefixLen = strlen(kEddystonePrefixes[i]);
    if (strncmp(url, kEddystonePrefixes[i], prefixLen) == 0) {
      payload.url_scheme = i;
      url += prefixLen;
      break;
    }
  }
  if (payload.url_scheme == 0xFFU) {
    return false;
  }

  uint8_t encodedLen = 0U;
  while (*url != '\0') {
    uint8_t expansionCode = 0U;
    const char* expansion = findEddystoneExpansion(url, &expansionCode);
    const size_t copyLen =
        (expansion != nullptr) ? static_cast<size_t>(expansion - url) : strlen(url);
    if ((encodedLen + copyLen + ((expansion != nullptr) ? 1U : 0U)) >
        sizeof(payload.encoded_url)) {
      return false;
    }

    memcpy(&payload.encoded_url[encodedLen], url, copyLen);
    encodedLen = static_cast<uint8_t>(encodedLen + copyLen);
    url += copyLen;

    if (expansion != nullptr) {
      payload.encoded_url[encodedLen++] = expansionCode;
      url += strlen(kEddystoneExpansions[expansionCode]);
    }
  }

  adv.clearData();
  return adv.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) &&
         adv.addUuid(UUID16_SVC_EDDYSTONE) &&
         adv.addData(BLE_GAP_AD_TYPE_SERVICE_DATA, &payload,
                     static_cast<uint8_t>(5U + encodedLen));
}

BLEPeriph::BLEPeriph()
    : connect_callback_(nullptr),
      disconnect_callback_(nullptr),
      conn_interval_min_(BLE_GAP_CONN_MIN_INTERVAL_DFLT),
      conn_interval_max_(BLE_GAP_CONN_MAX_INTERVAL_DFLT),
      conn_latency_(BLE_GAP_CONN_SLAVE_LATENCY),
      conn_supervision_timeout_(BLE_GAP_CONN_SUPERVISION_TIMEOUT_MS / 10U) {}

void BLEPeriph::setConnectCallback(ble_connect_callback_t fp) { connect_callback_ = fp; }

void BLEPeriph::setDisconnectCallback(ble_disconnect_callback_t fp) {
  disconnect_callback_ = fp;
}

bool BLEPeriph::setConnInterval(uint16_t min_interval, uint16_t max_interval) {
  conn_interval_min_ = min_interval;
  conn_interval_max_ = (max_interval == 0U) ? min_interval : max_interval;
  manager().radio().setPreferredConnectionParameters(conn_interval_min_, conn_interval_max_,
                                                     conn_latency_,
                                                     conn_supervision_timeout_);
  return true;
}

bool BLEPeriph::setConnIntervalMS(uint16_t min_ms, uint16_t max_ms) {
  return setConnInterval(MS100TO125(min_ms), MS100TO125(max_ms));
}

bool BLEPeriph::setConnSlaveLatency(uint16_t latency) {
  conn_latency_ = latency;
  manager().radio().setPreferredConnectionParameters(conn_interval_min_, conn_interval_max_,
                                                     conn_latency_,
                                                     conn_supervision_timeout_);
  return true;
}

bool BLEPeriph::setConnSupervisionTimeout(uint16_t timeout) {
  conn_supervision_timeout_ = timeout;
  manager().radio().setPreferredConnectionParameters(conn_interval_min_, conn_interval_max_,
                                                     conn_latency_,
                                                     conn_supervision_timeout_);
  return true;
}

bool BLEPeriph::setConnSupervisionTimeoutMS(uint16_t timeout_ms) {
  return setConnSupervisionTimeout(timeout_ms / 10U);
}

void BLEPeriph::clearBonds() {
  (void)manager().radio().clearBondRecord();
}

BLEConnection::BLEConnection() : handle_(INVALID_CONNECTION_HANDLE) {}

uint16_t BLEConnection::handle() const { return handle_; }

bool BLEConnection::connected() const { return Bluefruit.connected(handle_); }

bool BLEConnection::bonded() const { return connected() && manager().radio().hasBondRecord(); }

bool BLEConnection::secured() const {
  return connected() && manager().radio().isConnectionEncrypted();
}

bool BLEConnection::getPeerName(char* name, uint16_t bufsize) const {
  if (name == nullptr || bufsize == 0U || !manager().radio().isConnected()) {
    return false;
  }
  BleConnectionInfo info{};
  if (!manager().radio().getConnectionInfo(&info)) {
    return false;
  }
  snprintf(name, bufsize, "%02X:%02X:%02X:%02X:%02X:%02X", info.peerAddress[5],
           info.peerAddress[4], info.peerAddress[3], info.peerAddress[2], info.peerAddress[1],
           info.peerAddress[0]);
  return true;
}

bool BLEConnection::disconnect() const { return Bluefruit.disconnect(handle_); }

uint16_t BLEConnection::getConnectionInterval() const {
  if (!connected()) {
    return 0U;
  }
  BleConnectionInfo info{};
  return manager().radio().getConnectionInfo(&info) ? info.intervalUnits : 0U;
}

uint16_t BLEConnection::getSlaveLatency() const {
  if (!connected()) {
    return 0U;
  }
  BleConnectionInfo info{};
  return manager().radio().getConnectionInfo(&info) ? info.latency : 0U;
}

uint16_t BLEConnection::getSupervisionTimeout() const {
  if (!connected()) {
    return 0U;
  }
  BleConnectionInfo info{};
  return manager().radio().getConnectionInfo(&info) ? info.supervisionTimeoutUnits : 0U;
}

uint16_t BLEConnection::getDataLength() const {
  return connected() ? manager().radio().currentDataLength() : 0U;
}

bool BLEConnection::requestDataLengthUpdate() {
  if (!connected()) {
    return false;
  }
  return manager().radio().requestDataLengthUpdate();
}

bool BLEConnection::requestMtuExchange(uint16_t mtu) {
  if (!connected()) {
    return false;
  }
  return manager().radio().requestAttMtuExchange(mtu);
}

uint16_t BLEConnection::getMtu() const {
  if (!connected()) {
    return 23U;
  }
  return manager().radio().currentAttMtu();
}

bool BLEConnection::requestPairing() const {
  if (!connected()) {
    return false;
  }
  if (manager().radio().connectionRole() == BleConnectionRole::kPeripheral) {
    return manager().radio().sendSmpSecurityRequest();
  }
  return false;
}

BLECentral::BLECentral() : connect_callback_(nullptr), disconnect_callback_(nullptr) {}

void BLECentral::setConnectCallback(ble_connect_callback_t fp) { connect_callback_ = fp; }

void BLECentral::setDisconnectCallback(ble_disconnect_callback_t fp) {
  disconnect_callback_ = fp;
}

bool BLECentral::connect(const ble_gap_evt_adv_report_t* report) {
  if (!manager().begin(0U, 1U)) {
    return false;
  }
  return manager().queueCentralConnect(report);
}

bool BLECentral::connected() const {
  return manager().radio().isConnected() &&
         manager().radio().connectionRole() == BleConnectionRole::kCentral;
}

void BLECentral::clearBonds() {
  (void)manager().radio().clearBondRecord();
}

BLEScanner::BLEScanner()
    : rx_callback_(nullptr),
      interval_(160U),
      window_(80U),
      timeout_s_(0U),
      active_scan_(false),
      restart_on_disconnect_(false),
      running_(false),
      paused_(false),
      has_filter_uuid_(false),
      filter_uuid_(),
      has_filter_msd_(false),
      filter_msd_company_(0U),
      has_filter_rssi_(false),
      filter_rssi_dbm_(-127) {}

void BLEScanner::setRxCallback(rx_callback_t fp) { rx_callback_ = fp; }

void BLEScanner::setInterval(uint16_t interval, uint16_t window) {
  interval_ = interval;
  window_ = (window == 0U) ? interval : window;
}

void BLEScanner::useActiveScan(bool enabled) { active_scan_ = enabled; }

void BLEScanner::restartOnDisconnect(bool enabled) { restart_on_disconnect_ = enabled; }

void BLEScanner::filterUuid(const BLEUuid& uuid) {
  filter_uuid_ = uuid;
  has_filter_uuid_ = uuid.begin();
}

void BLEScanner::filterService(const BLEUuid& uuid) { filterUuid(uuid); }

void BLEScanner::filterService(const BLEClientService& service) { filterUuid(service.uuid); }

void BLEScanner::filterMSD(uint16_t company_id) {
  filter_msd_company_ = company_id;
  has_filter_msd_ = true;
}

void BLEScanner::filterRssi(int8_t min_rssi_dbm) {
  filter_rssi_dbm_ = min_rssi_dbm;
  has_filter_rssi_ = true;
}

void BLEScanner::start(uint16_t timeout) {
  if (!manager().begin(0U, 1U)) {
    return;
  }
  timeout_s_ = timeout;
  running_ = true;
  paused_ = false;
}

void BLEScanner::resume() { paused_ = false; }

bool BLEScanner::checkReportForUuid(const ble_gap_evt_adv_report_t* report,
                                    const BLEUuid& uuid) const {
  return report != nullptr && adDataHasUuid(report->data.p_data, report->data.len, uuid);
}

bool BLEScanner::checkReportForUuid(const ble_gap_evt_adv_report_t* report,
                                    const uint8_t uuid128[16]) const {
  return (uuid128 != nullptr) && checkReportForUuid(report, BLEUuid(uuid128));
}

bool BLEScanner::checkReportForService(const ble_gap_evt_adv_report_t* report,
                                       const BLEUuid& uuid) const {
  return checkReportForUuid(report, uuid);
}

bool BLEScanner::checkReportForService(const ble_gap_evt_adv_report_t* report,
                                       const BLEClientService& service) const {
  return checkReportForService(report, service.uuid);
}

bool BLEScanner::checkReportForService(const ble_gap_evt_adv_report_t* report,
                                       const BLEClientUart& client_uart) const {
  return checkReportForService(report, client_uart.serviceUuid());
}

int BLEScanner::parseReportByType(const ble_gap_evt_adv_report_t* report, uint8_t type,
                                  void* buffer, uint8_t buffer_len) const {
  if (report == nullptr) {
    return 0;
  }
  return copyAdFieldByType(report->data.p_data, report->data.len, type, buffer, buffer_len);
}

BLEClientCharacteristic::BLEClientCharacteristic()
    : uuid(),
      begun_(false),
      discovered_(false),
      notify_callback_(nullptr),
      service_(nullptr),
      conn_handle_(INVALID_CONNECTION_HANDLE),
      decl_handle_(0U),
      value_handle_(0U),
      end_handle_(0U),
      cccd_handle_(0U),
      last_value_{0},
      last_value_len_(0U) {}

BLEClientCharacteristic::BLEClientCharacteristic(BLEUuid bleuuid)
    : BLEClientCharacteristic() {
  uuid = bleuuid;
}

bool BLEClientCharacteristic::begin() {
  return begin(nullptr);
}

bool BLEClientCharacteristic::begin(BLEClientService* parent_service) {
  if (begun_) {
    return true;
  }
  if (!manager().begin(0U, 1U)) {
    return false;
  }
  service_ = (parent_service != nullptr) ? parent_service : BLEClientService::lastService;
  if (service_ == nullptr || !manager().registerClientCharacteristic(this)) {
    return false;
  }
  begun_ = true;
  return true;
}

BLEClientService& BLEClientCharacteristic::parentService() { return *service_; }

const BLEClientService& BLEClientCharacteristic::parentService() const { return *service_; }

void BLEClientCharacteristic::resetDiscovery() {
  discovered_ = false;
  conn_handle_ = INVALID_CONNECTION_HANDLE;
  decl_handle_ = 0U;
  value_handle_ = 0U;
  end_handle_ = 0U;
  cccd_handle_ = 0U;
  last_value_len_ = 0U;
}

bool BLEClientCharacteristic::discover(uint16_t conn_hdl) {
  if (!begin() || service_ == nullptr || !service_->discover(conn_hdl)) {
    resetDiscovery();
    return false;
  }
  return discover();
}

bool BLEClientCharacteristic::discover() {
  if (!begin() || service_ == nullptr || !service_->discovered_) {
    resetDiscovery();
    return false;
  }

  resetDiscovery();
  if (!discoverCharacteristicSync(service_->start_handle_, service_->end_handle_, uuid,
                                  &decl_handle_, &value_handle_, &end_handle_)) {
    return false;
  }
  conn_handle_ = service_->conn_handle_;
  discovered_ = true;
  return true;
}

uint16_t BLEClientCharacteristic::read(void* buffer, uint16_t len) {
  if (!discovered_ || buffer == nullptr || len == 0U) {
    return 0U;
  }
  uint8_t scratch[sizeof(last_value_)] = {0};
  const uint16_t readLen = readHandleSync(value_handle_, scratch, sizeof(scratch));
  if (readLen == 0U) {
    return 0U;
  }
  last_value_len_ =
      static_cast<uint8_t>(min<uint16_t>(readLen, sizeof(last_value_)));
  memcpy(last_value_, scratch, last_value_len_);
  const uint16_t copyLen = min<uint16_t>(len, readLen);
  memcpy(buffer, scratch, copyLen);
  return copyLen;
}

uint8_t BLEClientCharacteristic::read8() {
  uint8_t value = 0U;
  (void)read(&value, sizeof(value));
  return value;
}

uint16_t BLEClientCharacteristic::write(const void* buffer, uint16_t len) {
  if (!discovered_ || buffer == nullptr || len == 0U) {
    return 0U;
  }
  return writeHandleSync(value_handle_, static_cast<const uint8_t*>(buffer), len, true) ? len
                                                                                          : 0U;
}

uint16_t BLEClientCharacteristic::write8(uint8_t value) {
  return write(&value, sizeof(value));
}

bool BLEClientCharacteristic::enableNotify() {
  if (!discovered_) {
    return false;
  }
  if (cccd_handle_ == 0U &&
      !discoverCccdHandleSync(value_handle_, end_handle_, &cccd_handle_)) {
    return false;
  }
  const uint8_t cccdValue[2] = {0x01U, 0x00U};
  return writeHandleSync(cccd_handle_, cccdValue, sizeof(cccdValue), true);
}

bool BLEClientCharacteristic::disableNotify() {
  if (!discovered_) {
    return false;
  }
  if (cccd_handle_ == 0U &&
      !discoverCccdHandleSync(value_handle_, end_handle_, &cccd_handle_)) {
    return false;
  }
  const uint8_t cccdValue[2] = {0x00U, 0x00U};
  return writeHandleSync(cccd_handle_, cccdValue, sizeof(cccdValue), true);
}

void BLEClientCharacteristic::handleNotify(const uint8_t* data, uint16_t len) {
  const uint8_t copyLen = min<uint16_t>(len, sizeof(last_value_));
  if (copyLen > 0U && data != nullptr) {
    memcpy(last_value_, data, copyLen);
  }
  last_value_len_ = copyLen;
  if (notify_callback_ != nullptr) {
    invokeBluefruitUserCallback(notify_callback_, this, last_value_, last_value_len_);
  }
}

BLEClientService::BLEClientService()
    : uuid(),
      begun_(false),
      discovered_(false),
      conn_handle_(INVALID_CONNECTION_HANDLE),
      start_handle_(0U),
      end_handle_(0U) {}

BLEClientService::BLEClientService(BLEUuid bleuuid)
    : BLEClientService() {
  uuid = bleuuid;
}

bool BLEClientService::begin() {
  if (!manager().begin(0U, 1U)) {
    return false;
  }
  begun_ = true;
  lastService = this;
  return true;
}

void BLEClientService::resetDiscovery() {
  discovered_ = false;
  conn_handle_ = INVALID_CONNECTION_HANDLE;
  start_handle_ = 0U;
  end_handle_ = 0U;
}

bool BLEClientService::discover(uint16_t conn_hdl) {
  resetDiscovery();
  if ((!begun_ && !begin()) || !centralReady(conn_hdl)) {
    return false;
  }
  if (!discoverServiceRangeSync(uuid, &start_handle_, &end_handle_)) {
    return false;
  }
  conn_handle_ = conn_hdl;
  discovered_ = true;
  lastService = this;
  return true;
}

BLEClientUart* BLEClientUart::instances_[BLEClientUart::kMaxInstances] = {};
uint8_t BLEClientUart::instance_count_ = 0U;

BLEClientUart::BLEClientUart()
    : service_(BLEUART_UUID_SERVICE),
      txd_(BLEUART_UUID_CHR_TXD),
      rxd_(BLEUART_UUID_CHR_RXD),
      discovered_(false),
      rx_callback_(nullptr),
      rx_fifo_{0},
      rx_head_(0U),
      rx_tail_(0U),
      rx_count_(0U) {
  if (instance_count_ < kMaxInstances) {
    instances_[instance_count_++] = this;
  }
}

bool BLEClientUart::begin() {
  txd_.setNotifyCallback(txdNotifyThunk);
  return service_.begin() && txd_.begin(&service_) && rxd_.begin(&service_);
}

bool BLEClientUart::discover(uint16_t conn_hdl) {
  discovered_ = false;
  rx_head_ = 0U;
  rx_tail_ = 0U;
  rx_count_ = 0U;
  if (!begin() || !service_.discover(conn_hdl) || !txd_.discover() || !rxd_.discover()) {
    return false;
  }
  discovered_ = true;
  return true;
}

bool BLEClientUart::enableTXD() { return discovered_ && txd_.enableNotify(); }

int BLEClientUart::read() {
  if (rx_count_ == 0U) {
    return -1;
  }
  const int value = rx_fifo_[rx_tail_];
  rx_tail_ = static_cast<uint16_t>((rx_tail_ + 1U) % kRxFifoDepth);
  --rx_count_;
  return value;
}

int BLEClientUart::read(uint8_t* buffer, size_t size) {
  if (buffer == nullptr || size == 0U) {
    return 0;
  }

  size_t copied = 0U;
  while (copied < size) {
    const int value = read();
    if (value < 0) {
      break;
    }
    buffer[copied++] = static_cast<uint8_t>(value);
  }
  return static_cast<int>(copied);
}

int BLEClientUart::available() { return rx_count_; }

int BLEClientUart::peek() {
  if (rx_count_ == 0U) {
    return -1;
  }
  return rx_fifo_[rx_tail_];
}

void BLEClientUart::flush() {}

size_t BLEClientUart::write(uint8_t value) { return write(&value, 1U); }

size_t BLEClientUart::write(const uint8_t* buffer, size_t size) {
  if (!discovered_ || buffer == nullptr || size == 0U || !centralReady(0U) ||
      rxd_.value_handle_ == 0U) {
    return 0U;
  }

  size_t sent = 0U;
  while (sent < size) {
    const uint8_t chunk = min<uint16_t>(BleRadio::kCustomGattMaxValueLength,
                                        static_cast<uint16_t>(size - sent));
    if (!writeHandleSync(rxd_.value_handle_, &buffer[sent], chunk, true)) {
      break;
    }
    sent += chunk;
  }
  return sent;
}

const BLEUuid& BLEClientUart::serviceUuid() const { return service_.uuid; }

void BLEClientUart::handleTxdNotify(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  (void)chr;
  for (uint16_t i = 0U; i < len; ++i) {
    if (rx_count_ >= kRxFifoDepth) {
      break;
    }
    rx_fifo_[rx_head_] = data[i];
    rx_head_ = static_cast<uint16_t>((rx_head_ + 1U) % kRxFifoDepth);
    ++rx_count_;
  }
  if (rx_callback_ != nullptr) {
    invokeBluefruitUserCallback(rx_callback_, *this);
  }
}

void BLEClientUart::txdNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  for (uint8_t i = 0U; i < instance_count_; ++i) {
    BLEClientUart* instance = instances_[i];
    if (instance != nullptr && (&instance->txd_ == chr)) {
      instance->handleTxdNotify(chr, data, len);
      return;
    }
  }
}

BLEClientDis::BLEClientDis()
    : BLEClientService(UUID16_SVC_DEVICE_INFORMATION),
      manufacturer_(UUID16_CHR_MANUFACTURER_NAME_STRING),
      model_(UUID16_CHR_MODEL_NUMBER_STRING) {}

bool BLEClientDis::begin() {
  return BLEClientService::begin() && manufacturer_.begin() && model_.begin();
}

bool BLEClientDis::discover(uint16_t conn_hdl) {
  if (!begin() || !BLEClientService::discover(conn_hdl)) {
    return false;
  }
  (void)manufacturer_.discover();
  (void)model_.discover();
  return true;
}

bool BLEClientDis::getManufacturer(char* buffer, uint16_t len) {
  if (buffer == nullptr || len == 0U) {
    return false;
  }
  buffer[0] = '\0';
  const uint16_t readLen =
      manufacturer_.discovered() ? manufacturer_.read(buffer, static_cast<uint16_t>(len - 1U))
                                 : 0U;
  buffer[readLen] = '\0';
  return readLen > 0U;
}

bool BLEClientDis::getModel(char* buffer, uint16_t len) {
  if (buffer == nullptr || len == 0U) {
    return false;
  }
  buffer[0] = '\0';
  const uint16_t readLen =
      model_.discovered() ? model_.read(buffer, static_cast<uint16_t>(len - 1U)) : 0U;
  buffer[readLen] = '\0';
  return readLen > 0U;
}

BLEClientBas::BLEClientBas()
    : BLEClientService(UUID16_SVC_BATTERY), battery_(UUID16_CHR_BATTERY_LEVEL) {}

bool BLEClientBas::begin() {
  return BLEClientService::begin() && battery_.begin();
}

bool BLEClientBas::discover(uint16_t conn_hdl) {
  if (!begin() || !BLEClientService::discover(conn_hdl)) {
    return false;
  }
  (void)battery_.discover();
  return true;
}

uint8_t BLEClientBas::read() {
  return battery_.discovered() ? battery_.read8() : 0U;
}

BLEClientCts::BLEClientCts()
    : BLEClientService(UUID16_SVC_CURRENT_TIME),
      current_time_(UUID16_CHR_CURRENT_TIME),
      local_info_(UUID16_CHR_LOCAL_TIME_INFORMATION),
      adjust_callback_(nullptr),
      Time{},
      LocalInfo{} {}

bool BLEClientCts::begin() {
  current_time_.setNotifyCallback(currentTimeNotifyThunk);
  return BLEClientService::begin() && current_time_.begin(this) && local_info_.begin(this);
}

bool BLEClientCts::discover(uint16_t conn_hdl) {
  if (!begin() || !BLEClientService::discover(conn_hdl)) {
    return false;
  }
  (void)current_time_.discover();
  (void)local_info_.discover();
  return current_time_.discovered();
}

bool BLEClientCts::getCurrentTime() {
  return current_time_.discovered() &&
         current_time_.read(&Time, sizeof(Time)) == sizeof(Time);
}

bool BLEClientCts::getLocalTimeInfo() {
  if (!local_info_.discovered()) {
    memset(&LocalInfo, 0, sizeof(LocalInfo));
    return false;
  }
  return local_info_.read(&LocalInfo, sizeof(LocalInfo)) == sizeof(LocalInfo);
}

bool BLEClientCts::enableAdjust() { return current_time_.enableNotify(); }

void BLEClientCts::handleCurrentTimeNotify(uint8_t* data, uint16_t len) {
  if (data == nullptr || len == 0U) {
    return;
  }
  const uint16_t copyLen = min<uint16_t>(len, sizeof(Time));
  memcpy(&Time, data, copyLen);
  if (copyLen < sizeof(Time)) {
    memset(reinterpret_cast<uint8_t*>(&Time) + copyLen, 0, sizeof(Time) - copyLen);
  }
  if (adjust_callback_ != nullptr) {
    invokeBluefruitUserCallback(adjust_callback_, Time.adjust_reason);
  }
}

void BLEClientCts::currentTimeNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data,
                                          uint16_t len) {
  if (chr == nullptr) {
    return;
  }
  static_cast<BLEClientCts&>(chr->parentService()).handleCurrentTimeNotify(data, len);
}

namespace {
const uint8_t kAncsServiceUuid[16] = {0xD0U, 0x00U, 0x2DU, 0x12U, 0x1EU, 0x4BU, 0x0FU, 0xA4U,
                                      0x99U, 0x4EU, 0xCEU, 0xB5U, 0x31U, 0xF4U, 0x05U, 0x79U};
const uint8_t kAncsControlUuid[16] = {0xD9U, 0xD9U, 0xAAU, 0xFDU, 0xBDU, 0x9BU, 0x21U, 0x98U,
                                      0xA8U, 0x49U, 0xE1U, 0x45U, 0xF3U, 0xD8U, 0xD1U, 0x69U};
const uint8_t kAncsNotificationUuid[16] = {0xBDU, 0x1DU, 0xA2U, 0x99U, 0xE6U, 0x25U, 0x58U, 0x8CU,
                                           0xD9U, 0x42U, 0x01U, 0x63U, 0x0DU, 0x12U, 0xBFU, 0x9FU};
const uint8_t kAncsDataUuid[16] = {0xFBU, 0x7BU, 0x7CU, 0xCEU, 0x6AU, 0xB3U, 0x44U, 0xBEU,
                                   0xB5U, 0x4BU, 0xD6U, 0x24U, 0xE9U, 0xC6U, 0xEAU, 0x22U};
}  // namespace

BLEAncs::BLEAncs()
    : BLEClientService(kAncsServiceUuid),
      control_(kAncsControlUuid),
      notification_(kAncsNotificationUuid),
      data_(kAncsDataUuid),
      notification_callback_(nullptr) {}

bool BLEAncs::begin() {
  notification_.setNotifyCallback(notificationThunk);
  data_.setNotifyCallback(dataThunk);
  return BLEClientService::begin() && control_.begin(this) && notification_.begin(this) &&
         data_.begin(this);
}

bool BLEAncs::discover(uint16_t conn_handle) {
  if (!begin() || !BLEClientService::discover(conn_handle)) {
    return false;
  }
  const bool foundControl = control_.discover();
  const bool foundNotification = notification_.discover();
  const bool foundData = data_.discover();
  return foundControl && foundNotification && foundData;
}

void BLEAncs::setNotificationCallback(notification_callback_t fp) {
  notification_callback_ = fp;
}

bool BLEAncs::enableNotification() {
  return data_.enableNotify() && notification_.enableNotify();
}

bool BLEAncs::disableNotification() {
  bool ok = true;
  if (notification_.discovered()) {
    ok = notification_.disableNotify() && ok;
  }
  if (data_.discovered()) {
    ok = data_.disableNotify() && ok;
  }
  return ok;
}

uint16_t BLEAncs::getAttribute(uint32_t uid, uint8_t attr, void* buffer, uint16_t bufsize) {
  (void)uid;
  (void)attr;
  if (buffer != nullptr && bufsize > 0U) {
    memset(buffer, 0, bufsize);
  }
  return 0U;
}

uint16_t BLEAncs::getAppAttribute(const char* appid, uint8_t attr, void* buffer,
                                  uint16_t bufsize) {
  (void)appid;
  (void)attr;
  if (buffer != nullptr && bufsize > 0U) {
    memset(buffer, 0, bufsize);
  }
  return 0U;
}

bool BLEAncs::performAction(uint32_t uid, uint8_t actionid) {
  if (!control_.discovered()) {
    return false;
  }
  uint8_t payload[6] = {ANCS_CMD_PERFORM_NOTIFICATION_ACTION, 0U, 0U, 0U, 0U, actionid};
  memcpy(&payload[1], &uid, sizeof(uid));
  return control_.write(payload, sizeof(payload)) == sizeof(payload);
}

uint16_t BLEAncs::getAppName(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAppAttribute("", ANCS_APP_ATTR_DISPLAY_NAME, buffer, bufsize);
}

uint16_t BLEAncs::getAppID(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_APP_IDENTIFIER, buffer, bufsize);
}

uint16_t BLEAncs::getTitle(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_TITLE, buffer, bufsize);
}

uint16_t BLEAncs::getSubtitle(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_SUBTITLE, buffer, bufsize);
}

uint16_t BLEAncs::getMessage(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_MESSAGE, buffer, bufsize);
}

uint16_t BLEAncs::getMessageSize(uint32_t uid) {
  uint16_t value = 0U;
  (void)getAttribute(uid, ANCS_ATTR_MESSAGE_SIZE, &value, sizeof(value));
  return value;
}

uint16_t BLEAncs::getDate(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_DATE, buffer, bufsize);
}

uint16_t BLEAncs::getPosActionLabel(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_POSITIVE_ACTION_LABEL, buffer, bufsize);
}

uint16_t BLEAncs::getNegActionLabel(uint32_t uid, void* buffer, uint16_t bufsize) {
  return getAttribute(uid, ANCS_ATTR_NEGATIVE_ACTION_LABEL, buffer, bufsize);
}

bool BLEAncs::actPositive(uint32_t uid) { return performAction(uid, ANCS_ACTION_POSITIVE); }

bool BLEAncs::actNegative(uint32_t uid) { return performAction(uid, ANCS_ACTION_NEGATIVE); }

void BLEAncs::handleNotification(uint8_t* data, uint16_t len) {
  if (notification_callback_ == nullptr || data == nullptr || len < sizeof(AncsNotification_t)) {
    return;
  }
  invokeBluefruitUserCallback(notification_callback_,
                              reinterpret_cast<AncsNotification_t*>(data));
}

void BLEAncs::notificationThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  if (chr == nullptr) {
    return;
  }
  static_cast<BLEAncs&>(chr->parentService()).handleNotification(data, len);
}

void BLEAncs::dataThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
  (void)chr;
  (void)data;
  (void)len;
}

BLEClientHidAdafruit::BLEClientHidAdafruit()
    : BLEClientService(UUID16_SVC_HUMAN_INTERFACE_DEVICE),
      keyboard_callback_(nullptr),
      mouse_callback_(nullptr),
      gamepad_callback_(nullptr),
      last_keyboard_report_{0},
      last_mouse_report_{0},
      last_gamepad_report_{0},
      protocol_mode_(UUID16_CHR_PROTOCOL_MODE),
      hid_info_(UUID16_CHR_HID_INFORMATION),
      hid_control_(UUID16_CHR_HID_CONTROL_POINT),
      keyboard_boot_input_(UUID16_CHR_BOOT_KEYBOARD_INPUT_REPORT),
      keyboard_boot_output_(UUID16_CHR_BOOT_KEYBOARD_OUTPUT_REPORT),
      mouse_boot_input_(UUID16_CHR_BOOT_MOUSE_INPUT_REPORT),
      gamepad_report_(UUID16_CHR_REPORT) {}

bool BLEClientHidAdafruit::begin() {
  keyboard_boot_input_.setNotifyCallback(keyboardNotifyThunk);
  mouse_boot_input_.setNotifyCallback(mouseNotifyThunk);
  gamepad_report_.setNotifyCallback(gamepadNotifyThunk);
  return BLEClientService::begin() && protocol_mode_.begin(this) && hid_info_.begin(this) &&
         hid_control_.begin(this) && keyboard_boot_input_.begin(this) &&
         keyboard_boot_output_.begin(this) && mouse_boot_input_.begin(this) &&
         gamepad_report_.begin(this);
}

bool BLEClientHidAdafruit::discover(uint16_t conn_handle) {
  if (!begin() || !BLEClientService::discover(conn_handle)) {
    return false;
  }
  (void)protocol_mode_.discover();
  (void)hid_info_.discover();
  (void)hid_control_.discover();
  (void)keyboard_boot_input_.discover();
  (void)keyboard_boot_output_.discover();
  (void)mouse_boot_input_.discover();
  (void)gamepad_report_.discover();
  return hid_info_.discovered() && hid_control_.discovered() &&
         (keyboardPresent() || mousePresent() || gamepadPresent());
}

bool BLEClientHidAdafruit::getHidInfo(uint8_t info[4]) {
  return (info != nullptr) && (hid_info_.read(info, 4U) == 4U);
}

uint8_t BLEClientHidAdafruit::getCountryCode(void) {
  uint8_t info[4] = {0};
  return getHidInfo(info) ? info[2] : 0U;
}

bool BLEClientHidAdafruit::setBootMode(bool boot) {
  return protocol_mode_.discovered() && protocol_mode_.write8(boot ? 0U : 1U) == 1U;
}

bool BLEClientHidAdafruit::keyboardPresent(void) {
  return keyboard_boot_input_.discovered() && keyboard_boot_output_.discovered() &&
         protocol_mode_.discovered();
}

bool BLEClientHidAdafruit::enableKeyboard(void) { return keyboard_boot_input_.enableNotify(); }

bool BLEClientHidAdafruit::disableKeyboard(void) { return keyboard_boot_input_.disableNotify(); }

void BLEClientHidAdafruit::getKeyboardReport(hid_keyboard_report_t* report) {
  if (report != nullptr) {
    memcpy(report, &last_keyboard_report_, sizeof(last_keyboard_report_));
  }
}

bool BLEClientHidAdafruit::mousePresent(void) {
  return mouse_boot_input_.discovered() && protocol_mode_.discovered();
}

bool BLEClientHidAdafruit::enableMouse(void) { return mouse_boot_input_.enableNotify(); }

bool BLEClientHidAdafruit::disableMouse(void) { return mouse_boot_input_.disableNotify(); }

void BLEClientHidAdafruit::getMouseReport(hid_mouse_report_t* report) {
  if (report != nullptr) {
    memcpy(report, &last_mouse_report_, sizeof(last_mouse_report_));
  }
}

bool BLEClientHidAdafruit::gamepadPresent(void) { return gamepad_report_.discovered(); }

bool BLEClientHidAdafruit::enableGamepad(void) { return gamepad_report_.enableNotify(); }

bool BLEClientHidAdafruit::disableGamepad(void) { return gamepad_report_.disableNotify(); }

void BLEClientHidAdafruit::getGamepadReport(hid_gamepad_report_t* report) {
  if (report != nullptr) {
    memcpy(report, &last_gamepad_report_, sizeof(last_gamepad_report_));
  }
}

void BLEClientHidAdafruit::setKeyboardReportCallback(kbd_callback_t fp) {
  keyboard_callback_ = fp;
}

void BLEClientHidAdafruit::setMouseReportCallback(mse_callback_t fp) {
  mouse_callback_ = fp;
}

void BLEClientHidAdafruit::setGamepadReportCallback(gpd_callback_t fp) {
  gamepad_callback_ = fp;
}

void BLEClientHidAdafruit::handleKeyboardInput(uint8_t* data, uint16_t len) {
  memset(&last_keyboard_report_, 0, sizeof(last_keyboard_report_));
  if (data != nullptr) {
    memcpy(&last_keyboard_report_, data, min<uint16_t>(len, sizeof(last_keyboard_report_)));
  }
  if (keyboard_callback_ != nullptr) {
    invokeBluefruitUserCallback(keyboard_callback_, &last_keyboard_report_);
  }
}

void BLEClientHidAdafruit::handleMouseInput(uint8_t* data, uint16_t len) {
  memset(&last_mouse_report_, 0, sizeof(last_mouse_report_));
  if (data != nullptr) {
    memcpy(&last_mouse_report_, data, min<uint16_t>(len, sizeof(last_mouse_report_)));
  }
  if (mouse_callback_ != nullptr) {
    invokeBluefruitUserCallback(mouse_callback_, &last_mouse_report_);
  }
}

void BLEClientHidAdafruit::handleGamepadInput(uint8_t* data, uint16_t len) {
  memset(&last_gamepad_report_, 0, sizeof(last_gamepad_report_));
  if (data != nullptr) {
    memcpy(&last_gamepad_report_, data, min<uint16_t>(len, sizeof(last_gamepad_report_)));
  }
  if (gamepad_callback_ != nullptr) {
    invokeBluefruitUserCallback(gamepad_callback_, &last_gamepad_report_);
  }
}

void BLEClientHidAdafruit::keyboardNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data,
                                               uint16_t len) {
  if (chr != nullptr) {
    static_cast<BLEClientHidAdafruit&>(chr->parentService()).handleKeyboardInput(data, len);
  }
}

void BLEClientHidAdafruit::mouseNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data,
                                            uint16_t len) {
  if (chr != nullptr) {
    static_cast<BLEClientHidAdafruit&>(chr->parentService()).handleMouseInput(data, len);
  }
}

void BLEClientHidAdafruit::gamepadNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data,
                                              uint16_t len) {
  if (chr != nullptr) {
    static_cast<BLEClientHidAdafruit&>(chr->parentService()).handleGamepadInput(data, len);
  }
}

BLEHidAdafruit::BLEHidAdafruit()
    : BLEService(UUID16_SVC_HUMAN_INTERFACE_DEVICE),
      mouse_buttons_(0U),
      keyboard_led_callback_(nullptr) {}

err_t BLEHidAdafruit::begin() { return BLEService::begin(); }

void BLEHidAdafruit::setKeyboardLedCallback(kbd_led_cb_t fp) {
  keyboard_led_callback_ = fp;
}

bool BLEHidAdafruit::keyboardReport(hid_keyboard_report_t* report) {
  return keyboardReport(BLE_CONN_HANDLE_INVALID, report);
}

bool BLEHidAdafruit::keyboardReport(uint8_t modifier, uint8_t keycode[6]) {
  return keyboardReport(BLE_CONN_HANDLE_INVALID, modifier, keycode);
}

bool BLEHidAdafruit::keyPress(char ch) { return keyPress(BLE_CONN_HANDLE_INVALID, ch); }

bool BLEHidAdafruit::keyRelease(void) { return keyRelease(BLE_CONN_HANDLE_INVALID); }

bool BLEHidAdafruit::keySequence(const char* str, int interval) {
  return keySequence(BLE_CONN_HANDLE_INVALID, str, interval);
}

bool BLEHidAdafruit::keyboardReport(uint16_t conn_hdl, hid_keyboard_report_t* report) {
  (void)conn_hdl;
  return report != nullptr && Bluefruit.connected();
}

bool BLEHidAdafruit::keyboardReport(uint16_t conn_hdl, uint8_t modifier, uint8_t keycode[6]) {
  hid_keyboard_report_t report{};
  report.modifier = modifier;
  if (keycode != nullptr) {
    memcpy(report.keycode, keycode, sizeof(report.keycode));
  }
  return keyboardReport(conn_hdl, &report);
}

bool BLEHidAdafruit::keyPress(uint16_t conn_hdl, char ch) {
  const uint8_t ascii = static_cast<uint8_t>(ch);
  if (ascii >= 128U) {
    return false;
  }
  hid_keyboard_report_t report{};
  report.modifier = hid_ascii_to_keycode[ascii][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0U;
  report.keycode[0] = hid_ascii_to_keycode[ascii][1];
  return keyboardReport(conn_hdl, &report);
}

bool BLEHidAdafruit::keyRelease(uint16_t conn_hdl) {
  hid_keyboard_report_t report{};
  return keyboardReport(conn_hdl, &report);
}

bool BLEHidAdafruit::keySequence(uint16_t conn_hdl, const char* str, int interval) {
  if (str == nullptr) {
    return false;
  }
  for (const char* cursor = str; *cursor != '\0'; ++cursor) {
    if (!keyPress(conn_hdl, *cursor)) {
      return false;
    }
    delay(interval);
    if (!keyRelease(conn_hdl)) {
      return false;
    }
    delay(interval);
  }
  return true;
}

bool BLEHidAdafruit::consumerReport(uint16_t usage_code) {
  return consumerReport(BLE_CONN_HANDLE_INVALID, usage_code);
}

bool BLEHidAdafruit::consumerKeyPress(uint16_t usage_code) {
  return consumerKeyPress(BLE_CONN_HANDLE_INVALID, usage_code);
}

bool BLEHidAdafruit::consumerKeyRelease(void) {
  return consumerKeyRelease(BLE_CONN_HANDLE_INVALID);
}

bool BLEHidAdafruit::consumerReport(uint16_t conn_hdl, uint16_t usage_code) {
  (void)conn_hdl;
  (void)usage_code;
  return Bluefruit.connected();
}

bool BLEHidAdafruit::consumerKeyPress(uint16_t conn_hdl, uint16_t usage_code) {
  return consumerReport(conn_hdl, usage_code);
}

bool BLEHidAdafruit::consumerKeyRelease(uint16_t conn_hdl) {
  return consumerReport(conn_hdl, 0U);
}

bool BLEHidAdafruit::mouseReport(hid_mouse_report_t* report) {
  return mouseReport(BLE_CONN_HANDLE_INVALID, report);
}

bool BLEHidAdafruit::mouseReport(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  return mouseReport(BLE_CONN_HANDLE_INVALID, buttons, x, y, wheel, pan);
}

bool BLEHidAdafruit::mouseButtonPress(uint8_t buttons) {
  return mouseButtonPress(BLE_CONN_HANDLE_INVALID, buttons);
}

bool BLEHidAdafruit::mouseButtonRelease(void) {
  return mouseButtonRelease(BLE_CONN_HANDLE_INVALID);
}

bool BLEHidAdafruit::mouseMove(int8_t x, int8_t y) {
  return mouseMove(BLE_CONN_HANDLE_INVALID, x, y);
}

bool BLEHidAdafruit::mouseScroll(int8_t scroll) {
  return mouseScroll(BLE_CONN_HANDLE_INVALID, scroll);
}

bool BLEHidAdafruit::mousePan(int8_t pan) { return mousePan(BLE_CONN_HANDLE_INVALID, pan); }

bool BLEHidAdafruit::mouseReport(uint16_t conn_hdl, hid_mouse_report_t* report) {
  (void)conn_hdl;
  return report != nullptr && Bluefruit.connected();
}

bool BLEHidAdafruit::mouseReport(uint16_t conn_hdl, uint8_t buttons, int8_t x, int8_t y,
                                 int8_t wheel, int8_t pan) {
  hid_mouse_report_t report{buttons, x, y, wheel, pan};
  return mouseReport(conn_hdl, &report);
}

bool BLEHidAdafruit::mouseButtonPress(uint16_t conn_hdl, uint8_t buttons) {
  mouse_buttons_ |= buttons;
  return mouseReport(conn_hdl, mouse_buttons_, 0, 0, 0, 0);
}

bool BLEHidAdafruit::mouseButtonRelease(uint16_t conn_hdl) {
  mouse_buttons_ = 0U;
  return mouseReport(conn_hdl, mouse_buttons_, 0, 0, 0, 0);
}

bool BLEHidAdafruit::mouseMove(uint16_t conn_hdl, int8_t x, int8_t y) {
  return mouseReport(conn_hdl, mouse_buttons_, x, y, 0, 0);
}

bool BLEHidAdafruit::mouseScroll(uint16_t conn_hdl, int8_t scroll) {
  return mouseReport(conn_hdl, mouse_buttons_, 0, 0, scroll, 0);
}

bool BLEHidAdafruit::mousePan(uint16_t conn_hdl, int8_t pan) {
  return mouseReport(conn_hdl, mouse_buttons_, 0, 0, 0, pan);
}

BLEHidGamepad::BLEHidGamepad() : BLEService(UUID16_SVC_HUMAN_INTERFACE_DEVICE) {}

err_t BLEHidGamepad::begin() { return BLEService::begin(); }

bool BLEHidGamepad::report(hid_gamepad_report_t* report) {
  return this->report(BLE_CONN_HANDLE_INVALID, report);
}

bool BLEHidGamepad::report(uint16_t conn_hdl, hid_gamepad_report_t* report) {
  (void)conn_hdl;
  return report != nullptr && Bluefruit.connected();
}

BLEDis::BLEDis()
    : BLEService(UUID16_SVC_DEVICE_INFORMATION), values_{nullptr}, lengths_{0}, auto_serial_{0} {}

void BLEDis::setSystemID(const char* system_id, uint8_t length) {
  values_[0] = system_id;
  lengths_[0] = length;
}

void BLEDis::setModel(const char* model, uint8_t length) {
  values_[1] = model;
  lengths_[1] = length;
}

void BLEDis::setSerialNum(const char* serial_num, uint8_t length) {
  values_[2] = serial_num;
  lengths_[2] = length;
}

void BLEDis::setFirmwareRev(const char* firmware_rev, uint8_t length) {
  values_[3] = firmware_rev;
  lengths_[3] = length;
}

void BLEDis::setHardwareRev(const char* hw_rev, uint8_t length) {
  values_[4] = hw_rev;
  lengths_[4] = length;
}

void BLEDis::setSoftwareRev(const char* sw_rev, uint8_t length) {
  values_[5] = sw_rev;
  lengths_[5] = length;
}

void BLEDis::setManufacturer(const char* manufacturer, uint8_t length) {
  values_[6] = manufacturer;
  lengths_[6] = length;
}

void BLEDis::setRegCertList(const char* reg_cert_list, uint8_t length) {
  values_[7] = reg_cert_list;
  lengths_[7] = length;
}

void BLEDis::setPNPID(const char* pnp_id, uint8_t length) {
  values_[8] = pnp_id;
  lengths_[8] = length;
}

err_t BLEDis::begin() {
  static const uint16_t kCharacteristicUuids[9] = {
      UUID16_CHR_SYSTEM_ID,        UUID16_CHR_MODEL_NUMBER_STRING,
      UUID16_CHR_SERIAL_NUMBER_STRING, UUID16_CHR_FIRMWARE_REVISION_STRING,
      UUID16_CHR_HARDWARE_REVISION_STRING, UUID16_CHR_SOFTWARE_REVISION_STRING,
      UUID16_CHR_MANUFACTURER_NAME_STRING, UUID16_CHR_REGULATORY_CERT_DATA_LIST,
      UUID16_CHR_PNP_ID};
  static constexpr char kDefaultManufacturer[] = "Seeed Studio";
  static constexpr char kDefaultModel[] = "XIAO nRF54L15";
  static constexpr char kDefaultHardwareRev[] = "XIAO nRF54L15";

  const err_t status = BLEService::begin();
  if (status != ERROR_NONE) {
    return status;
  }

  if (values_[1] == nullptr || lengths_[1] == 0U) {
    setModel(kDefaultModel);
  }
  if (values_[2] == nullptr || lengths_[2] == 0U) {
    snprintf(auto_serial_, sizeof(auto_serial_), "%08" PRIX32 "%08" PRIX32,
             NRF_FICR->INFO.DEVICEID[1], NRF_FICR->INFO.DEVICEID[0]);
    setSerialNum(auto_serial_);
  }
  if (values_[3] == nullptr || lengths_[3] == 0U) {
    setFirmwareRev(NRF54L15_CLEAN_CORE_VERSION_STRING);
  }
  if (values_[4] == nullptr || lengths_[4] == 0U) {
    setHardwareRev(kDefaultHardwareRev);
  }
  if (values_[5] == nullptr || lengths_[5] == 0U) {
    setSoftwareRev(NRF54L15_CLEAN_CORE_VERSION_STRING);
  }
  if (values_[6] == nullptr || lengths_[6] == 0U) {
    setManufacturer(kDefaultManufacturer);
  }

  for (uint8_t i = 0U; i < 9U; ++i) {
    if (values_[i] == nullptr || lengths_[i] == 0U) {
      continue;
    }
    const uint8_t len = clampValueLen(lengths_[i]);
    if (!manager().radio().addCustomGattCharacteristic(
            _handle, kCharacteristicUuids[i], xiao_nrf54l15::kBleGattPropRead,
            reinterpret_cast<const uint8_t*>(values_[i]), len, nullptr, nullptr)) {
      return ERROR_INVALID_STATE;
    }
  }
  return ERROR_NONE;
}

BLEDfu::BLEDfu() : BLEService(kUuidDfuService) {}

err_t BLEDfu::begin() {
  _begun = true;
  lastService = this;
  return ERROR_NONE;
}

BLEBas::BLEBas() : BLEService(UUID16_SVC_BATTERY) {}

err_t BLEBas::begin() {
  _begun = true;
  lastService = this;
  return ERROR_NONE;
}

bool BLEBas::write(uint8_t level) { return manager().radio().setGattBatteryLevel(level); }

bool BLEBas::notify(uint8_t level) { return notify(0U, level); }

bool BLEBas::notify(uint16_t conn_hdl, uint8_t level) {
  (void)conn_hdl;
  return write(level);
}

BLEUart::BLEUart(uint16_t fifo_depth)
    : BLEService(BLEUART_UUID_SERVICE),
      _txd(BLEUART_UUID_CHR_TXD),
      _rxd(BLEUART_UUID_CHR_RXD),
      _rx_fifo_depth((fifo_depth == 0U) ? 1U : fifo_depth),
      _rx_fifo(new uint8_t[(fifo_depth == 0U) ? 1U : fifo_depth]),
      _rx_head(0U),
      _rx_tail(0U),
      _rx_count(0U),
      _tx_buffered(false),
      _rx_cb(nullptr),
      _notify_cb(nullptr),
      _overflow_cb(nullptr) {}

err_t BLEUart::begin() {
  const err_t status = BLEService::begin();
  if (status != ERROR_NONE) {
    return status;
  }

  _txd.setProperties(CHR_PROPS_NOTIFY);
  _txd.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  _txd.setMaxLen(BleRadio::kCustomGattMaxValueLength);
  _txd.setCccdWriteCallback(bleuart_txd_cccd_cb);
  if (_txd.begin() != ERROR_NONE) {
    return ERROR_INVALID_STATE;
  }

  _rxd.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  _rxd.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  _rxd.setMaxLen(BleRadio::kCustomGattMaxValueLength);
  _rxd.setWriteCallback(bleuart_rxd_cb);
  if (_rxd.begin() != ERROR_NONE) {
    return ERROR_INVALID_STATE;
  }

  return ERROR_NONE;
}

bool BLEUart::notifyEnabled() { return _txd.notifyEnabled(); }

bool BLEUart::notifyEnabled(uint16_t conn_hdl) { return _txd.notifyEnabled(conn_hdl); }

void BLEUart::setRxCallback(rx_callback_t fp, bool deferred) {
  (void)deferred;
  _rx_cb = fp;
}

void BLEUart::setRxOverflowCallback(rx_overflow_callback_t fp) { _overflow_cb = fp; }

void BLEUart::setNotifyCallback(notify_callback_t fp) { _notify_cb = fp; }

void BLEUart::bufferTXD(bool enable) { _tx_buffered = enable; }

bool BLEUart::flushTXD() { return flushTXD(0U); }

bool BLEUart::flushTXD(uint16_t conn_hdl) {
  (void)conn_hdl;
  return true;
}

uint8_t BLEUart::read8() { return static_cast<uint8_t>(read()); }

uint16_t BLEUart::read16() {
  uint16_t value = 0U;
  read(reinterpret_cast<uint8_t*>(&value), sizeof(value));
  return value;
}

uint32_t BLEUart::read32() {
  uint32_t value = 0UL;
  read(reinterpret_cast<uint8_t*>(&value), sizeof(value));
  return value;
}

int BLEUart::read() {
  if (_rx_count == 0U) {
    return -1;
  }
  const int value = _rx_fifo[_rx_tail];
  _rx_tail = static_cast<uint16_t>((_rx_tail + 1U) % _rx_fifo_depth);
  --_rx_count;
  return value;
}

int BLEUart::read(uint8_t* buf, size_t size) {
  if (buf == nullptr || size == 0U) {
    return 0;
  }
  size_t copied = 0U;
  while (copied < size) {
    const int ch = read();
    if (ch < 0) {
      break;
    }
    buf[copied++] = static_cast<uint8_t>(ch);
  }
  return static_cast<int>(copied);
}

size_t BLEUart::write(uint8_t b) { return write(0U, &b, 1U); }

size_t BLEUart::write(const uint8_t* content, size_t len) { return write(0U, content, len); }

size_t BLEUart::write(uint16_t conn_hdl, uint8_t b) { return write(conn_hdl, &b, 1U); }

size_t BLEUart::write(uint16_t conn_hdl, const uint8_t* content, size_t len) {
  BLEConnection* conn = Bluefruit.Connection(conn_hdl);
  if (conn == nullptr || content == nullptr || len == 0U || !notifyEnabled(conn_hdl)) {
    return 0U;
  }

  size_t sent = 0U;
  uint32_t stalledAtMs = 0U;
  while (sent < len) {
    const uint16_t mtu = conn->getMtu();
    const uint16_t maxChunk =
        (mtu > 3U) ? static_cast<uint16_t>(mtu - 3U) : 20U;
    const uint16_t chunk =
        min<uint16_t>(min<uint16_t>(BleRadio::kCustomGattMaxValueLength, maxChunk),
                      static_cast<uint16_t>(len - sent));
    if (_txd.notify(conn_hdl, &content[sent], chunk)) {
      sent += chunk;
      stalledAtMs = 0U;
      continue;
    }

    if (!Bluefruit.connected() || !notifyEnabled(conn_hdl)) {
      break;
    }

    if (stalledAtMs == 0U) {
      stalledAtMs = millis();
    } else if ((millis() - stalledAtMs) > 1000UL) {
      break;
    }

    yield();
  }
  return sent;
}

int BLEUart::available() { return _rx_count; }

int BLEUart::peek() {
  if (_rx_count == 0U) {
    return -1;
  }
  return _rx_fifo[_rx_tail];
}

void BLEUart::flush() {}

void BLEUart::handleRx(uint16_t conn_hdl, const uint8_t* data, uint16_t len) {
  uint16_t dropped = 0U;
  for (uint16_t i = 0U; i < len; ++i) {
    if (_rx_count >= _rx_fifo_depth) {
      ++dropped;
      continue;
    }
    _rx_fifo[_rx_head] = data[i];
    _rx_head = static_cast<uint16_t>((_rx_head + 1U) % _rx_fifo_depth);
    ++_rx_count;
  }
  if (dropped > 0U && _overflow_cb != nullptr) {
    invokeBluefruitUserCallback(_overflow_cb, conn_hdl, dropped);
  }
  if (_rx_cb != nullptr) {
    invokeBluefruitUserCallback(_rx_cb, conn_hdl);
  }
}

void BLEUart::bleuart_rxd_cb(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data,
                             uint16_t len) {
  auto& service = static_cast<BLEUart&>(chr->parentService());
  service.handleRx(conn_hdl, data, len);
}

void BLEUart::bleuart_txd_cccd_cb(uint16_t conn_hdl, BLECharacteristic* chr, uint16_t value) {
  auto& service = static_cast<BLEUart&>(chr->parentService());
  if (service._notify_cb != nullptr) {
    invokeBluefruitUserCallback(service._notify_cb, conn_hdl, (value & 0x0001U) != 0U);
  }
}

AdafruitBluefruit::AdafruitBluefruit()
    : Periph(),
      Central(),
      Security(),
      Gatt(),
      Advertising(),
      ScanResponse(true),
      Scanner(),
      Discovery(),
      device_name_{0},
      tx_power_(0),
      appearance_(0),
      auto_conn_led_(false),
      conn_led_interval_ms_(500UL),
      central_conn_interval_(24U),
      central_supervision_timeout_(200U),
      central_requested_mtu_(23U),
      central_request_data_length_(false),
      central_request_mtu_(false) {
  strncpy(device_name_, "XIAO nRF54L15", sizeof(device_name_) - 1U);
}

void AdafruitBluefruit::configServiceChanged(bool changed) { (void)changed; }

void AdafruitBluefruit::configUuid128Count(uint8_t uuid128_max) { (void)uuid128_max; }

void AdafruitBluefruit::configAttrTableSize(uint32_t attr_table_size) {
  (void)attr_table_size;
}

static void applyPeripheralConnHint(BLEPeriph& periph, uint16_t mtu_max,
                                    uint16_t event_len) {
  uint16_t min_interval = 24U;
  uint16_t max_interval = 40U;

  if (mtu_max >= 247U || event_len >= 100U) {
    min_interval = 6U;
    max_interval = 12U;
  } else if (mtu_max >= 128U || event_len >= 6U) {
    min_interval = 12U;
    max_interval = 24U;
  } else if (event_len <= 2U) {
    min_interval = 40U;
    max_interval = 80U;
  }

  periph.setConnInterval(min_interval, max_interval);
  periph.setConnSlaveLatency(0U);
  periph.setConnSupervisionTimeout(500U);
}

void AdafruitBluefruit::configPrphConn(uint16_t mtu_max, uint16_t event_len,
                                       uint8_t hvn_qsize, uint8_t wrcmd_qsize) {
  (void)hvn_qsize;
  (void)wrcmd_qsize;
  applyPeripheralConnHint(Periph, mtu_max, event_len);
}

void AdafruitBluefruit::configCentralConn(uint16_t mtu_max, uint16_t event_len,
                                          uint8_t hvn_qsize, uint8_t wrcmd_qsize) {
  (void)hvn_qsize;
  (void)wrcmd_qsize;

  uint16_t interval_units = 24U;
  if (mtu_max >= 247U || event_len >= 100U) {
    interval_units = 6U;
  } else if (mtu_max >= 128U || event_len >= 6U) {
    interval_units = 12U;
  } else if (event_len <= 2U) {
    interval_units = 40U;
  }

  uint16_t requested_mtu = mtu_max;
  if (requested_mtu < 23U) {
    requested_mtu = 23U;
  } else if (requested_mtu > 247U) {
    requested_mtu = 247U;
  }

  central_conn_interval_ = interval_units;
  central_supervision_timeout_ = 200U;
  central_requested_mtu_ = requested_mtu;
  central_request_mtu_ = (requested_mtu > 23U);
  central_request_data_length_ = central_request_mtu_ || (event_len >= 6U);
}

void AdafruitBluefruit::configPrphBandwidth(uint8_t bw) {
  switch (bw) {
    case BANDWIDTH_LOW:
      configPrphConn(23U, 2U, 1U, 1U);
      break;

    case BANDWIDTH_AUTO:
    case BANDWIDTH_NORMAL:
      configPrphConn(23U, 3U, 1U, 1U);
      break;

    case BANDWIDTH_HIGH:
      configPrphConn(128U, 6U, 2U, 1U);
      break;

    case BANDWIDTH_MAX:
      configPrphConn(247U, 100U, 3U, 1U);
      break;

    default:
      break;
  }
}

void AdafruitBluefruit::configCentralBandwidth(uint8_t bw) {
  switch (bw) {
    case BANDWIDTH_LOW:
      configCentralConn(23U, 2U, 1U, 1U);
      break;

    case BANDWIDTH_AUTO:
    case BANDWIDTH_NORMAL:
      configCentralConn(23U, 3U, 1U, 1U);
      break;

    case BANDWIDTH_HIGH:
      configCentralConn(128U, 6U, 2U, 1U);
      break;

    case BANDWIDTH_MAX:
      configCentralConn(247U, 100U, 3U, 1U);
      break;

    default:
      break;
  }
}

bool AdafruitBluefruit::begin(uint8_t prph_count, uint8_t central_count) {
  return manager().begin(prph_count, central_count);
}

void AdafruitBluefruit::setName(const char* str) {
  if (str == nullptr) {
    return;
  }
  strncpy(device_name_, str, CFG_MAX_DEVNAME_LEN);
  device_name_[CFG_MAX_DEVNAME_LEN] = '\0';
  manager().radio().setGattDeviceName(device_name_);
  manager().markAdvertisingDirty();
}

uint8_t AdafruitBluefruit::getName(char* name, uint16_t bufsize) {
  if (name == nullptr || bufsize == 0U) {
    return 0U;
  }
  const size_t len = strlen(device_name_);
  const uint16_t copyLen = min<uint16_t>(bufsize - 1U, static_cast<uint16_t>(len));
  memcpy(name, device_name_, copyLen);
  name[copyLen] = '\0';
  return static_cast<uint8_t>(copyLen);
}

bool AdafruitBluefruit::setTxPower(int8_t power) {
  tx_power_ = power;
  manager().markAdvertisingDirty();
  return manager().radio().setTxPowerDbm(power);
}

int8_t AdafruitBluefruit::getTxPower() const { return tx_power_; }

bool AdafruitBluefruit::setAppearance(uint16_t appearance) {
  appearance_ = appearance;
  return manager().radio().setGattAppearance(appearance);
}

uint16_t AdafruitBluefruit::getAppearance() const { return appearance_; }

void AdafruitBluefruit::autoConnLed(bool enabled) {
  auto_conn_led_ = enabled;
  if (enabled) {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN,
                 manager().radio().isConnected() ? kLedOnState : kLedOffState);
  }
}

void AdafruitBluefruit::setConnLedInterval(uint32_t ms) { conn_led_interval_ms_ = ms; }

uint8_t AdafruitBluefruit::connected() const { return manager().radio().isConnected() ? 1U : 0U; }

bool AdafruitBluefruit::connected(uint16_t conn_hdl) const {
  return (conn_hdl == 0U) && manager().radio().isConnected();
}

uint8_t AdafruitBluefruit::getConnectedHandles(uint16_t* hdl_list, uint8_t max_count) const {
  if (hdl_list == nullptr || max_count == 0U || !manager().radio().isConnected()) {
    return 0U;
  }
  hdl_list[0] = 0U;
  return 1U;
}

uint16_t AdafruitBluefruit::connHandle() const {
  return manager().radio().isConnected() ? 0U : INVALID_CONNECTION_HANDLE;
}

bool AdafruitBluefruit::disconnect(uint16_t conn_hdl) {
  if (conn_hdl != 0U || !manager().radio().isConnected()) {
    return false;
  }
  return manager().radio().disconnect();
}

void AdafruitBluefruit::setRssiCallback(void (*fp)(uint16_t conn_hdl, int8_t rssi)) {
  (void)fp;
}

BLEConnection* AdafruitBluefruit::Connection(uint16_t conn_hdl) {
  if (conn_hdl != 0U || !manager().radio().isConnected()) {
    return nullptr;
  }
  return manager().connection();
}
