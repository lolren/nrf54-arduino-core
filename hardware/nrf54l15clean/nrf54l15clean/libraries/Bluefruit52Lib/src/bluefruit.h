#ifndef BLUEFRUIT_H_
#define BLUEFRUIT_H_

#include <Arduino.h>

#include "bluefruit_common.h"

class BluefruitCompatManager;
class BLEClientService;
class BLEClientCharacteristic;
class BLEClientUart;
class EddyStoneUrl;

enum {
  BANDWIDTH_AUTO = 0,
  BANDWIDTH_LOW,
  BANDWIDTH_NORMAL,
  BANDWIDTH_HIGH,
  BANDWIDTH_MAX,
};

enum CharsProperties {
  CHR_PROPS_BROADCAST = bit(0),
  CHR_PROPS_READ = bit(1),
  CHR_PROPS_WRITE_WO_RESP = bit(2),
  CHR_PROPS_WRITE = bit(3),
  CHR_PROPS_NOTIFY = bit(4),
  CHR_PROPS_INDICATE = bit(5),
};

enum BLECharsProperties {
  BLEBroadcast = 0x01,
  BLERead = 0x02,
  BLEWriteWithoutResponse = 0x04,
  BLEWrite = 0x08,
  BLENotify = 0x10,
  BLEIndicate = 0x20,
};

class BLEUuid {
 public:
  BLEUuid();
  BLEUuid(uint16_t uuid16);
  BLEUuid(const uint8_t uuid128[16]);
  BLEUuid(const char* str);

  void set(uint16_t uuid16);
  void set(const uint8_t uuid128[16]);
  void set(const char* str);

  bool get(uint16_t* uuid16) const;
  bool get(uint8_t uuid128[16]) const;
  size_t size() const;
  bool begin() const;
  String toString() const;

  bool operator==(const BLEUuid& rhs) const;
  bool operator!=(const BLEUuid& rhs) const;

  uint16_t uuid16() const;
  const uint8_t* uuid128() const;

 private:
  uint8_t size_;
  uint16_t uuid16_;
  uint8_t uuid128_[16];
};

class BLEService {
 public:
  static BLEService* lastService;

  BLEUuid uuid;

  BLEService();
  BLEService(BLEUuid bleuuid);
  virtual ~BLEService() = default;

  void setUuid(BLEUuid bleuuid);
  void setPermission(SecureMode_t read_perm, SecureMode_t write_perm);
  void getPermission(SecureMode_t* read_perm, SecureMode_t* write_perm) const;

  virtual err_t begin();

 protected:
  uint16_t _handle;
  SecureMode_t _read_perm;
  SecureMode_t _write_perm;
  bool _begun;

  friend class BLECharacteristic;
  friend class BluefruitCompatManager;
};

class BLECharacteristic {
 public:
  typedef void (*read_authorize_cb_t)(uint16_t conn_hdl, BLECharacteristic* chr,
                                      ble_gatts_evt_read_t* request);
  typedef void (*write_authorize_cb_t)(uint16_t conn_hdl, BLECharacteristic* chr,
                                       ble_gatts_evt_write_t* request);
  typedef void (*write_cb_t)(uint16_t conn_hdl, BLECharacteristic* chr,
                             uint8_t* data, uint16_t len);
  typedef void (*write_cccd_cb_t)(uint16_t conn_hdl, BLECharacteristic* chr,
                                  uint16_t value);

  BLEUuid uuid;

  BLECharacteristic();
  BLECharacteristic(BLEUuid bleuuid);
  BLECharacteristic(BLEUuid bleuuid, uint8_t properties);
  BLECharacteristic(BLEUuid bleuuid, uint8_t properties, int max_len,
                    bool fixed_len = false);
  virtual ~BLECharacteristic() = default;

  BLEService& parentService();

  void setTempMemory();
  void setUuid(BLEUuid bleuuid);
  void setProperties(uint8_t prop);
  void setPermission(SecureMode_t read_perm, SecureMode_t write_perm);
  void setMaxLen(uint16_t max_len);
  void setFixedLen(uint16_t fixed_len);
  void setBuffer(void* buf, uint16_t bufsize);

  uint16_t getMaxLen() const;
  bool isFixedLen() const;

  void setUserDescriptor(const char* descriptor);
  void setReportRefDescriptor(uint8_t id, uint8_t type);
  void setPresentationFormatDescriptor(uint8_t type, int8_t exponent,
                                       uint16_t unit, uint8_t name_space = 1,
                                       uint16_t descriptor = 0);

  void setWriteCallback(write_cb_t fp, bool useAdaCallback = true);
  void setCccdWriteCallback(write_cccd_cb_t fp, bool useAdaCallback = true);
  void setReadAuthorizeCallback(read_authorize_cb_t fp,
                                bool useAdaCallback = true);
  void setWriteAuthorizeCallback(write_authorize_cb_t fp,
                                 bool useAdaCallback = true);

  virtual err_t begin();

  ble_gatts_char_handles_t handles() const;

  uint16_t write(const void* data, uint16_t len);
  uint16_t write(const char* str);
  uint16_t write8(uint8_t num);
  uint16_t write16(uint16_t num);
  uint16_t write32(uint32_t num);
  uint16_t write32(int num);
  uint16_t writeFloat(float num);

  uint16_t read(void* buffer, uint16_t bufsize, uint16_t offset = 0);
  uint8_t read8();
  uint16_t read16();
  uint32_t read32();
  float readFloat();

  uint16_t getCccd(uint16_t conn_hdl = 0);
  bool notifyEnabled();
  bool notifyEnabled(uint16_t conn_hdl);

  bool notify(const void* data, uint16_t len);
  bool notify(const char* str);
  bool notify8(uint8_t num);
  bool notify16(uint16_t num);
  bool notify32(uint32_t num);
  bool notify32(int num);
  bool notify32(float num);

  bool notify(uint16_t conn_hdl, const void* data, uint16_t len);
  bool notify(uint16_t conn_hdl, const char* str);
  bool notify8(uint16_t conn_hdl, uint8_t num);
  bool notify16(uint16_t conn_hdl, uint16_t num);
  bool notify32(uint16_t conn_hdl, uint32_t num);
  bool notify32(uint16_t conn_hdl, int num);
  bool notify32(uint16_t conn_hdl, float num);

  bool indicateEnabled();
  bool indicateEnabled(uint16_t conn_hdl);

  bool indicate(const void* data, uint16_t len);
  bool indicate(const char* str);
  bool indicate8(uint8_t num);
  bool indicate16(uint16_t num);
  bool indicate32(uint32_t num);
  bool indicate32(int num);
  bool indicate32(float num);

  bool indicate(uint16_t conn_hdl, const void* data, uint16_t len);
  bool indicate(uint16_t conn_hdl, const char* str);
  bool indicate8(uint16_t conn_hdl, uint8_t num);
  bool indicate16(uint16_t conn_hdl, uint16_t num);
  bool indicate32(uint16_t conn_hdl, uint32_t num);
  bool indicate32(uint16_t conn_hdl, int num);
  bool indicate32(uint16_t conn_hdl, float num);

 protected:
  bool _is_temp;
  uint16_t _max_len;
  bool _fixed_len;
  BLEService* _service;
  uint8_t* _userbuf;
  uint16_t _userbufsize;
  uint8_t _properties;
  SecureMode_t _read_perm;
  SecureMode_t _write_perm;
  ble_gatts_char_handles_t _handles;
  uint8_t _value[BLUEFRUIT_GATT_VALUE_MAX_LEN];
  uint8_t _value_len;
  bool _notify_enabled;
  bool _indicate_enabled;
  const char* _usr_descriptor;
  write_cb_t _wr_cb;
  write_cccd_cb_t _cccd_wr_cb;
  read_authorize_cb_t _rd_authorize_cb;
  write_authorize_cb_t _wr_authorize_cb;

  void handleWriteFromRadio(const uint8_t* data, uint16_t len);
  void pollCccdState();

  friend class BluefruitCompatManager;
};

class BLEAdvertisingData {
 public:
  explicit BLEAdvertisingData(bool scan_response = false);

  void clearData();
  bool setData(const uint8_t* data, uint8_t len);
  bool addData(uint8_t type, const void* data, uint8_t len);
  bool addFlags(uint8_t flags);
  bool addTxPower();
  bool addName();
  bool addAppearance(uint16_t appearance);
  bool addManufacturerData(const void* data, uint8_t len);
  bool addService(const BLEService& service);
  bool addService(const BLEClientService& service);
  bool addService(const BLEService& service1, const BLEService& service2);
  bool addService(const BLEService& service1, const BLEService& service2,
                  const BLEService& service3);
  bool addService(const BLEService& service1, const BLEService& service2,
                  const BLEService& service3, const BLEService& service4);
  bool addService(const BLEUuid& uuid);
  bool addUuid(const BLEUuid& uuid1, const BLEUuid& uuid2);
  bool addUuid(const BLEUuid& uuid1, const BLEUuid& uuid2, const BLEUuid& uuid3);
  bool addUuid(const BLEUuid& uuid1, const BLEUuid& uuid2, const BLEUuid& uuid3,
               const BLEUuid& uuid4);
  bool addUuid(const BLEUuid uuids[], uint8_t count);
  bool addUuid(const BLEUuid& uuid);
  uint8_t count() const;
  uint8_t* getData();

 protected:
  uint8_t data_[31];
  uint8_t len_;
  bool scan_response_;
  bool dirty_;

  friend class BluefruitCompatManager;
};

class BLEAdvertising : public BLEAdvertisingData {
 public:
  typedef void (*stop_callback_t)();
  typedef void (*slow_callback_t)();

  BLEAdvertising();

  void restartOnDisconnect(bool enable);
  void setInterval(uint16_t fast, uint16_t slow = 0);
  void setIntervalMS(uint16_t fast_ms, uint16_t slow_ms = 0);
  void setFastTimeout(uint16_t seconds);
  void setType(uint8_t type);
  void setSlowCallback(slow_callback_t cb);
  void setStopCallback(stop_callback_t cb);
  uint16_t getInterval() const;
  bool setBeacon(class BLEBeacon& beacon);
  bool setBeacon(class EddyStoneUrl& beacon);

  bool start(uint16_t timeout = 0);
  bool stop();
  bool isRunning() const;

 private:
  bool restart_on_disconnect_;
  uint16_t interval_fast_;
  uint16_t interval_slow_;
  uint16_t fast_timeout_s_;
  uint16_t stop_timeout_s_;
  uint8_t adv_type_;
  bool running_;
  slow_callback_t slow_callback_;
  stop_callback_t stop_callback_;

  friend class BluefruitCompatManager;
};

class BLEBeacon {
 public:
  BLEBeacon();
  explicit BLEBeacon(const uint8_t uuid128[16]);
  BLEBeacon(const uint8_t uuid128[16], uint16_t major, uint16_t minor, int8_t rssi);

  void setManufacturer(uint16_t manufacturer);
  void setUuid(const uint8_t uuid128[16]);
  void setMajorMinor(uint16_t major, uint16_t minor);
  void setRssiAt1m(int8_t rssi);

  bool start();
  bool start(BLEAdvertising& adv);

 private:
  void reset();

  uint16_t manufacturer_;
  uint8_t uuid128_[16];
  bool uuid_valid_;
  uint16_t major_be_;
  uint16_t minor_be_;
  int8_t rssi_at_1m_;
};

class EddyStoneUrl {
 public:
  EddyStoneUrl();
  EddyStoneUrl(int8_t rssiAt0m, const char* url = nullptr);

  void setUrl(const char* url);
  void setRssi(int8_t rssiAt0m);

  bool start();
  bool start(BLEAdvertising& adv);

 private:
  int8_t rssi_;
  const char* url_;
};

class BLEPeriph {
 public:
  BLEPeriph();

  void setConnectCallback(ble_connect_callback_t fp);
  void setDisconnectCallback(ble_disconnect_callback_t fp);
  bool setConnInterval(uint16_t min_interval, uint16_t max_interval = 0);
  bool setConnIntervalMS(uint16_t min_ms, uint16_t max_ms);
  bool setConnSlaveLatency(uint16_t latency);
  bool setConnSupervisionTimeout(uint16_t timeout);
  bool setConnSupervisionTimeoutMS(uint16_t timeout_ms);
  void clearBonds();

 private:
  ble_connect_callback_t connect_callback_;
  ble_disconnect_callback_t disconnect_callback_;
  uint16_t conn_interval_min_;
  uint16_t conn_interval_max_;
  uint16_t conn_latency_;
  uint16_t conn_supervision_timeout_;

  friend class BluefruitCompatManager;
};

class BLEConnection {
 public:
  BLEConnection();

  uint16_t handle() const;
  bool connected() const;
  bool bonded() const;
  bool secured() const;
  bool getPeerName(char* name, uint16_t bufsize) const;
  bool disconnect() const;
  bool requestPHY() { return false; }
  bool requestDataLengthUpdate();
  bool requestMtuExchange(uint16_t mtu);
  bool requestPairing() const;
  bool monitorRssi(uint8_t threshold = 0xFFU) const;
  int8_t getRssi() const;
  uint16_t getConnectionInterval() const;
  uint16_t getSlaveLatency() const;
  uint16_t getSupervisionTimeout() const;
  uint16_t getDataLength() const;
  uint16_t getMtu() const;

 private:
  uint16_t handle_;

  friend class BluefruitCompatManager;
};

class BLECentral {
 public:
  BLECentral();

  void setConnectCallback(ble_connect_callback_t fp);
  void setDisconnectCallback(ble_disconnect_callback_t fp);
  template <typename T>
  void setConnectCallback(T fp) {
    setConnectCallback(static_cast<ble_connect_callback_t>(fp));
  }
  template <typename T>
  void setDisconnectCallback(T fp) {
    setDisconnectCallback(static_cast<ble_disconnect_callback_t>(fp));
  }
  bool connect(const ble_gap_evt_adv_report_t* report);
  bool connected() const;
  void clearBonds();

 private:
  ble_connect_callback_t connect_callback_;
  ble_disconnect_callback_t disconnect_callback_;

  friend class BluefruitCompatManager;
};

class BLEScanner {
 public:
  typedef void (*rx_callback_t)(ble_gap_evt_adv_report_t* report);

  BLEScanner();

  void setRxCallback(rx_callback_t fp);
  template <typename T>
  void setRxCallback(T fp) {
    setRxCallback(static_cast<rx_callback_t>(fp));
  }
  void setInterval(uint16_t interval, uint16_t window = 0);
  void setIntervalMS(uint16_t interval_ms, uint16_t window_ms = 0);
  void useActiveScan(bool enabled);
  void restartOnDisconnect(bool enabled);
  void filterUuid(const BLEUuid& uuid);
  void filterService(const BLEUuid& uuid);
  void filterService(const BLEClientService& service);
  void filterMSD(uint16_t company_id);
  void filterRssi(int8_t min_rssi_dbm);
  void start(uint16_t timeout);
  bool stop();
  void resume();
  bool checkReportForUuid(const ble_gap_evt_adv_report_t* report,
                          const BLEUuid& uuid) const;
  bool checkReportForUuid(const ble_gap_evt_adv_report_t* report,
                          const uint8_t uuid128[16]) const;
  bool checkReportForService(const ble_gap_evt_adv_report_t* report,
                             const BLEUuid& uuid) const;
  bool checkReportForService(const ble_gap_evt_adv_report_t* report,
                             const BLEClientService& service) const;
  bool checkReportForService(const ble_gap_evt_adv_report_t* report,
                             const BLEClientUart& client_uart) const;
  int parseReportByType(const ble_gap_evt_adv_report_t* report, uint8_t type,
                        void* buffer, uint8_t buffer_len) const;

 private:
  rx_callback_t rx_callback_;
  uint16_t interval_;
  uint16_t window_;
  uint16_t timeout_s_;
  bool active_scan_;
  bool restart_on_disconnect_;
  bool running_;
  bool paused_;
  bool has_filter_uuid_;
  BLEUuid filter_uuid_;
  bool has_filter_msd_;
  uint16_t filter_msd_company_;
  bool has_filter_rssi_;
  int8_t filter_rssi_dbm_;

  friend class BluefruitCompatManager;
};

class BLEDiscovery {};
class BLEGatt {};

class BLESecurity {
 public:
  template <typename T>
  void setSecuredCallback(T) {}
  template <typename T>
  void setPairPasskeyCallback(T) {}
  template <typename T>
  void setPairCompleteCallback(T) {}
  void setIOCaps(uint8_t) {}
  void setIOCaps(bool, bool, bool) {}
  void setPIN(const char*) {}
};

enum {
  ANCS_CAT_OTHER,
  ANCS_CAT_INCOMING_CALL,
  ANCS_CAT_MISSED_CALL,
  ANCS_CAT_VOICE_MAIL,
  ANCS_CAT_SOCIAL,
  ANCS_CAT_SCHEDULE,
  ANCS_CAT_EMAIL,
  ANCS_CAT_NEWS,
  ANCS_CAT_HEALTH_AND_FITNESS,
  ANCS_CAT_BUSSINESS_AND_FINANCE,
  ANCS_CAT_LOCATION,
  ANCS_CAT_ENTERTAINMENT,
};

enum {
  ANCS_EVT_NOTIFICATION_ADDED,
  ANCS_EVT_NOTIFICATION_MODIFIED,
  ANCS_EVT_NOTIFICATION_REMOVED,
};

enum {
  ANCS_CMD_GET_NOTIFICATION_ATTR,
  ANCS_CMD_GET_APP_ATTR,
  ANCS_CMD_PERFORM_NOTIFICATION_ACTION,
};

enum {
  ANCS_ATTR_APP_IDENTIFIER,
  ANCS_ATTR_TITLE,
  ANCS_ATTR_SUBTITLE,
  ANCS_ATTR_MESSAGE,
  ANCS_ATTR_MESSAGE_SIZE,
  ANCS_ATTR_DATE,
  ANCS_ATTR_POSITIVE_ACTION_LABEL,
  ANCS_ATTR_NEGATIVE_ACTION_LABEL,
  ANCS_ATTR_INVALID,
};

enum {
  ANCS_ACTION_POSITIVE,
  ANCS_ACTION_NEGATIVE,
};

enum {
  ANCS_APP_ATTR_DISPLAY_NAME,
  ANCS_APP_ATTR_INVALID,
};

struct ATTR_PACKED AncsNotification_t {
  uint8_t eventID;
  struct ATTR_PACKED {
    uint8_t silent : 1;
    uint8_t important : 1;
    uint8_t preExisting : 1;
    uint8_t positiveAction : 1;
    uint8_t NegativeAction : 1;
  } eventFlags;
  uint8_t categoryID;
  uint8_t categoryCount;
  uint32_t uid;
};

class BLEClientCharacteristic {
 public:
  typedef void (*notify_cb_t)(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);

  BLEUuid uuid;

  BLEClientCharacteristic();
  explicit BLEClientCharacteristic(BLEUuid bleuuid);

  bool begin();
  bool begin(BLEClientService* parent_service);
  bool discover(uint16_t conn_hdl);
  bool discover();
  bool discovered() const { return discovered_; }
  void setNotifyCallback(notify_cb_t fp) { notify_callback_ = fp; }
  BLEClientService& parentService();
  const BLEClientService& parentService() const;
  uint16_t read(void* buffer, uint16_t len);
  uint8_t read8();
  uint16_t write(const void* buffer, uint16_t len);
  uint16_t write8(uint8_t value);
  bool enableNotify();
  bool disableNotify();

 protected:
  bool begun_;
  bool discovered_;
  notify_cb_t notify_callback_;
  BLEClientService* service_;
  uint16_t conn_handle_;
  uint16_t decl_handle_;
  uint16_t value_handle_;
  uint16_t end_handle_;
  uint16_t cccd_handle_;
  uint8_t last_value_[BLUEFRUIT_GATT_VALUE_MAX_LEN];
  uint8_t last_value_len_;

  void handleNotify(const uint8_t* data, uint16_t len);
  void resetDiscovery();

  friend class BluefruitCompatManager;
  friend class BLEClientUart;
  friend class BLEClientDis;
  friend class BLEClientBas;
};

class BLEClientService {
 public:
  static BLEClientService* lastService;

  BLEUuid uuid;

  BLEClientService();
  explicit BLEClientService(BLEUuid bleuuid);

  bool begin();
  bool discover(uint16_t conn_hdl);
  bool discovered() const { return discovered_; }
  uint16_t connHandle() const { return conn_handle_; }

 protected:
  bool begun_;
  bool discovered_;
  uint16_t conn_handle_;
  uint16_t start_handle_;
  uint16_t end_handle_;

  void resetDiscovery();

  friend class BLEClientCharacteristic;
  friend class BLEClientUart;
  friend class BLEClientDis;
  friend class BLEClientBas;
};
class BLEClientUart : public Stream {
 public:
  typedef void (*rx_callback_t)(BLEClientUart& uart_svc);

  BLEClientUart();

  bool begin();
  bool discover(uint16_t conn_hdl);
  bool discovered() const { return discovered_; }
  void setRxCallback(rx_callback_t fp) { rx_callback_ = fp; }
  bool enableTXD();
  uint16_t connHandle() const { return service_.conn_handle_; }
  int read() override;
  int read(uint8_t* buffer, size_t size);
  int read(char* buffer, size_t size) {
    return read(reinterpret_cast<uint8_t*>(buffer), size);
  }
  int available() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size);
  using Print::write;

  const BLEUuid& serviceUuid() const;

 private:
  static constexpr uint16_t kRxFifoDepth = 256U;
  static constexpr uint8_t kMaxInstances = 4U;

  BLEClientService service_;
  BLEClientCharacteristic txd_;
  BLEClientCharacteristic rxd_;
  static BLEClientUart* instances_[kMaxInstances];
  static uint8_t instance_count_;
  bool discovered_;
  rx_callback_t rx_callback_;
  uint8_t rx_fifo_[kRxFifoDepth];
  uint16_t rx_head_;
  uint16_t rx_tail_;
  uint16_t rx_count_;

  void handleTxdNotify(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
  static void txdNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
};
class BLEClientDis : public BLEClientService {
 public:
  BLEClientDis();

  bool begin();
  bool discover(uint16_t conn_hdl);
  bool getManufacturer(char* buffer, uint16_t len);
  bool getModel(char* buffer, uint16_t len);

 private:
  BLEClientCharacteristic manufacturer_;
  BLEClientCharacteristic model_;
};
class BLEClientBas : public BLEClientService {
 public:
  BLEClientBas();

  bool begin();
  bool discover(uint16_t conn_hdl);
  uint8_t read();

 private:
  BLEClientCharacteristic battery_;
};
class BLEClientCts : public BLEClientService {
 public:
  typedef void (*adjust_callback_t)(uint8_t reason);

  struct ATTR_PACKED CurrentTimeData {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
    uint8_t subsecond;
    uint8_t adjust_reason;
  } Time;

  struct ATTR_PACKED LocalTimeInfoData {
    int8_t timezone;
    uint8_t dst_offset;
  } LocalInfo;

  BLEClientCts();

  bool begin();
  bool discover(uint16_t conn_hdl);
  bool getCurrentTime();
  bool getLocalTimeInfo();
  bool enableAdjust();
  void setAdjustCallback(adjust_callback_t fp) { adjust_callback_ = fp; }

 private:
  BLEClientCharacteristic current_time_;
  BLEClientCharacteristic local_info_;
  adjust_callback_t adjust_callback_;

  void handleCurrentTimeNotify(uint8_t* data, uint16_t len);
  static void currentTimeNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data,
                                     uint16_t len);
};
class BLEAncs : public BLEClientService {
 public:
  typedef void (*notification_callback_t)(AncsNotification_t* notif);

  BLEAncs();

  bool begin();
  bool discover(uint16_t conn_handle);
  void setNotificationCallback(notification_callback_t fp);
  bool enableNotification();
  bool disableNotification();
  uint16_t getAttribute(uint32_t uid, uint8_t attr, void* buffer, uint16_t bufsize);
  uint16_t getAppAttribute(const char* appid, uint8_t attr, void* buffer, uint16_t bufsize);
  bool performAction(uint32_t uid, uint8_t actionid);
  uint16_t getAppName(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getAppID(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getTitle(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getSubtitle(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getMessage(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getMessageSize(uint32_t uid);
  uint16_t getDate(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getPosActionLabel(uint32_t uid, void* buffer, uint16_t bufsize);
  uint16_t getNegActionLabel(uint32_t uid, void* buffer, uint16_t bufsize);
  bool actPositive(uint32_t uid);
  bool actNegative(uint32_t uid);

 private:
  BLEClientCharacteristic control_;
  BLEClientCharacteristic notification_;
  BLEClientCharacteristic data_;
  notification_callback_t notification_callback_;

  void handleNotification(uint8_t* data, uint16_t len);
  static void notificationThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
  static void dataThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
};

class BLEClientHidAdafruit : public BLEClientService {
 public:
  typedef void (*kbd_callback_t)(hid_keyboard_report_t* report);
  typedef void (*mse_callback_t)(hid_mouse_report_t* report);
  typedef void (*gpd_callback_t)(hid_gamepad_report_t* report);

  BLEClientHidAdafruit();

  bool begin();
  bool discover(uint16_t conn_handle);
  bool getHidInfo(uint8_t info[4]);
  uint8_t getCountryCode(void);
  bool setBootMode(bool boot);
  bool keyboardPresent(void);
  bool enableKeyboard(void);
  bool disableKeyboard(void);
  void getKeyboardReport(hid_keyboard_report_t* report);
  bool mousePresent(void);
  bool enableMouse(void);
  bool disableMouse(void);
  void getMouseReport(hid_mouse_report_t* report);
  bool gamepadPresent(void);
  bool enableGamepad(void);
  bool disableGamepad(void);
  void getGamepadReport(hid_gamepad_report_t* report);
  void setKeyboardReportCallback(kbd_callback_t fp);
  void setMouseReportCallback(mse_callback_t fp);
  void setGamepadReportCallback(gpd_callback_t fp);

 private:
  kbd_callback_t keyboard_callback_;
  mse_callback_t mouse_callback_;
  gpd_callback_t gamepad_callback_;
  hid_keyboard_report_t last_keyboard_report_;
  hid_mouse_report_t last_mouse_report_;
  hid_gamepad_report_t last_gamepad_report_;
  BLEClientCharacteristic protocol_mode_;
  BLEClientCharacteristic hid_info_;
  BLEClientCharacteristic hid_control_;
  BLEClientCharacteristic keyboard_boot_input_;
  BLEClientCharacteristic keyboard_boot_output_;
  BLEClientCharacteristic mouse_boot_input_;
  BLEClientCharacteristic gamepad_report_;

  void handleKeyboardInput(uint8_t* data, uint16_t len);
  void handleMouseInput(uint8_t* data, uint16_t len);
  void handleGamepadInput(uint8_t* data, uint16_t len);
  static void keyboardNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
  static void mouseNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
  static void gamepadNotifyThunk(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
};

class BLEHidAdafruit : public BLEService {
 public:
  typedef void (*kbd_led_cb_t)(uint16_t conn_hdl, uint8_t leds_bitmap);

  BLEHidAdafruit();

  err_t begin() override;
  void setKeyboardLedCallback(kbd_led_cb_t fp);
  bool keyboardReport(hid_keyboard_report_t* report);
  bool keyboardReport(uint8_t modifier, uint8_t keycode[6]);
  bool keyPress(char ch);
  bool keyRelease(void);
  bool keySequence(const char* str, int interval = 5);
  bool keyboardReport(uint16_t conn_hdl, hid_keyboard_report_t* report);
  bool keyboardReport(uint16_t conn_hdl, uint8_t modifier, uint8_t keycode[6]);
  bool keyPress(uint16_t conn_hdl, char ch);
  bool keyRelease(uint16_t conn_hdl);
  bool keySequence(uint16_t conn_hdl, const char* str, int interval = 5);
  bool consumerReport(uint16_t usage_code);
  bool consumerKeyPress(uint16_t usage_code);
  bool consumerKeyRelease(void);
  bool consumerReport(uint16_t conn_hdl, uint16_t usage_code);
  bool consumerKeyPress(uint16_t conn_hdl, uint16_t usage_code);
  bool consumerKeyRelease(uint16_t conn_hdl);
  bool mouseReport(hid_mouse_report_t* report);
  bool mouseReport(uint8_t buttons, int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0);
  bool mouseButtonPress(uint8_t buttons);
  bool mouseButtonRelease(void);
  bool mouseMove(int8_t x, int8_t y);
  bool mouseScroll(int8_t scroll);
  bool mousePan(int8_t pan);
  bool mouseReport(uint16_t conn_hdl, hid_mouse_report_t* report);
  bool mouseReport(uint16_t conn_hdl, uint8_t buttons, int8_t x, int8_t y,
                   int8_t wheel = 0, int8_t pan = 0);
  bool mouseButtonPress(uint16_t conn_hdl, uint8_t buttons);
  bool mouseButtonRelease(uint16_t conn_hdl);
  bool mouseMove(uint16_t conn_hdl, int8_t x, int8_t y);
  bool mouseScroll(uint16_t conn_hdl, int8_t scroll);
  bool mousePan(uint16_t conn_hdl, int8_t pan);

 private:
  uint8_t mouse_buttons_;
  kbd_led_cb_t keyboard_led_callback_;
};

class BLEHidGamepad : public BLEService {
 public:
  BLEHidGamepad();

  err_t begin() override;
  bool report(hid_gamepad_report_t* report);
  bool report(uint16_t conn_hdl, hid_gamepad_report_t* report);
};

class BLEDis : public BLEService {
 public:
  BLEDis();

  void setSystemID(const char* system_id, uint8_t length);
  void setModel(const char* model, uint8_t length);
  void setSerialNum(const char* serial_num, uint8_t length);
  void setFirmwareRev(const char* firmware_rev, uint8_t length);
  void setHardwareRev(const char* hw_rev, uint8_t length);
  void setSoftwareRev(const char* sw_rev, uint8_t length);
  void setManufacturer(const char* manufacturer, uint8_t length);
  void setRegCertList(const char* reg_cert_list, uint8_t length);
  void setPNPID(const char* pnp_id, uint8_t length);

  void setSystemID(const char* system_id) { setSystemID(system_id, strlen(system_id)); }
  void setModel(const char* model) { setModel(model, strlen(model)); }
  void setSerialNum(const char* serial_num) { setSerialNum(serial_num, strlen(serial_num)); }
  void setFirmwareRev(const char* firmware_rev) { setFirmwareRev(firmware_rev, strlen(firmware_rev)); }
  void setHardwareRev(const char* hw_rev) { setHardwareRev(hw_rev, strlen(hw_rev)); }
  void setSoftwareRev(const char* sw_rev) { setSoftwareRev(sw_rev, strlen(sw_rev)); }
  void setManufacturer(const char* manufacturer) { setManufacturer(manufacturer, strlen(manufacturer)); }
  void setRegCertList(const char* reg_cert_list) { setRegCertList(reg_cert_list, strlen(reg_cert_list)); }
  void setPNPID(const char* pnp_id) { setPNPID(pnp_id, strlen(pnp_id)); }

 err_t begin() override;

 private:
  const char* values_[9];
  uint8_t lengths_[9];
  char auto_serial_[17];
};

class BLEDfu : public BLEService {
 public:
  BLEDfu();
  err_t begin() override;
};

class BLEBas : public BLEService {
 public:
  BLEBas();

  err_t begin() override;
  bool write(uint8_t level);
  bool notify(uint8_t level);
  bool notify(uint16_t conn_hdl, uint8_t level);
};

class BLEUart : public BLEService, public Stream {
 public:
  typedef void (*rx_callback_t)(uint16_t conn_hdl);
  typedef void (*notify_callback_t)(uint16_t conn_hdl, bool enabled);
  typedef void (*rx_overflow_callback_t)(uint16_t conn_hdl, uint16_t leftover);

  explicit BLEUart(uint16_t fifo_depth = 256);

  err_t begin() override;

  bool notifyEnabled();
  bool notifyEnabled(uint16_t conn_hdl);

  void setRxCallback(rx_callback_t fp, bool deferred = true);
  void setRxOverflowCallback(rx_overflow_callback_t fp);
  void setNotifyCallback(notify_callback_t fp);

  void bufferTXD(bool enable);
  bool flushTXD();
  bool flushTXD(uint16_t conn_hdl);

  uint8_t read8();
  uint16_t read16();
  uint32_t read32();

  int read() override;
  int read(uint8_t* buf, size_t size);
  int read(char* buf, size_t size) { return read(reinterpret_cast<uint8_t*>(buf), size); }

  size_t write(uint8_t b) override;
  size_t write(const uint8_t* content, size_t len);
  size_t write(uint16_t conn_hdl, uint8_t b);
  size_t write(uint16_t conn_hdl, const uint8_t* content, size_t len);

  int available() override;
  int peek() override;
  void flush() override;

  using Print::write;

 private:
  BLECharacteristic _txd;
  BLECharacteristic _rxd;
  uint16_t _rx_fifo_depth;
  uint8_t* _rx_fifo;
  uint16_t _rx_head;
  uint16_t _rx_tail;
  uint16_t _rx_count;
  bool _tx_buffered;
  rx_callback_t _rx_cb;
  notify_callback_t _notify_cb;
  rx_overflow_callback_t _overflow_cb;

  void handleRx(uint16_t conn_hdl, const uint8_t* data, uint16_t len);
  static void bleuart_rxd_cb(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data,
                             uint16_t len);
  static void bleuart_txd_cccd_cb(uint16_t conn_hdl, BLECharacteristic* chr,
                                  uint16_t value);
};

extern const uint8_t BLEUART_UUID_SERVICE[16];
extern const uint8_t BLEUART_UUID_CHR_RXD[16];
extern const uint8_t BLEUART_UUID_CHR_TXD[16];

class AdafruitBluefruit {
 public:
  AdafruitBluefruit();

  BLEPeriph Periph;
  BLECentral Central;
  BLESecurity Security;
  BLEGatt Gatt;
  BLEAdvertising Advertising;
  BLEAdvertisingData ScanResponse;
  BLEScanner Scanner;
  BLEDiscovery Discovery;

  void configServiceChanged(bool changed);
  void configUuid128Count(uint8_t uuid128_max);
  void configAttrTableSize(uint32_t attr_table_size);
  void configPrphConn(uint16_t mtu_max, uint16_t event_len, uint8_t hvn_qsize,
                      uint8_t wrcmd_qsize);
  void configCentralConn(uint16_t mtu_max, uint16_t event_len, uint8_t hvn_qsize,
                         uint8_t wrcmd_qsize);
  void configPrphBandwidth(uint8_t bw);
  void configCentralBandwidth(uint8_t bw);

  bool begin(uint8_t prph_count = 1, uint8_t central_count = 0);

  ble_gap_addr_t getAddr();
  uint8_t getAddr(uint8_t mac[6]);
  bool setAddr(ble_gap_addr_t* gap_addr);
  void setName(const char* str);
  uint8_t getName(char* name, uint16_t bufsize);
  bool setTxPower(int8_t power);
  int8_t getTxPower() const;
  bool setAppearance(uint16_t appearance);
  uint16_t getAppearance() const;
  void autoConnLed(bool enabled);
  void setConnLedInterval(uint32_t ms);

  uint8_t connected() const;
  bool connected(uint16_t conn_hdl) const;
  uint8_t getConnectedHandles(uint16_t* hdl_list, uint8_t max_count) const;
  uint16_t connHandle() const;
  bool disconnect(uint16_t conn_hdl);
  uint16_t getMaxMtu(uint8_t role);
  void setRssiCallback(void (*fp)(uint16_t conn_hdl, int8_t rssi));
  BLEConnection* Connection(uint16_t conn_hdl);

 private:
  char device_name_[CFG_MAX_DEVNAME_LEN + 1];
  int8_t tx_power_;
  uint16_t appearance_;
  bool auto_conn_led_;
  uint32_t conn_led_interval_ms_;
  uint16_t periph_requested_mtu_;
  uint16_t central_conn_interval_;
  uint16_t central_supervision_timeout_;
  uint16_t central_requested_mtu_;
  bool central_request_data_length_;
  bool central_request_mtu_;
  void (*rssi_callback_)(uint16_t conn_hdl, int8_t rssi);

  friend class BluefruitCompatManager;
};

extern AdafruitBluefruit Bluefruit;

#endif  // BLUEFRUIT_H_
