#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nrf54l15_regs.h"
#include "xiao_nrf54l15_pins.h"

namespace xiao_nrf54l15 {

enum class GpioDirection : uint8_t {
  kInput = 0,
  kOutput = 1,
};

enum class GpioPull : uint8_t {
  kDisabled = 0,
  kPullDown = 1,
  kPullUp = 3,
};

class ClockControl {
 public:
  static bool startHfxo(bool waitForTuned = true, uint32_t spinLimit = 1000000UL);
  static void stopHfxo();
};

class Gpio {
 public:
  static bool configure(const Pin& pin, GpioDirection direction,
                        GpioPull pull = GpioPull::kDisabled);
  static bool write(const Pin& pin, bool high);
  static bool read(const Pin& pin, bool* high);
  static bool toggle(const Pin& pin);

  // Configure GPIO drive as S0D1 (open-drain style) for TWIM lines.
  static bool setDriveS0D1(const Pin& pin);
};

enum class SpiMode : uint8_t {
  kMode0 = 0,
  kMode1 = 1,
  kMode2 = 2,
  kMode3 = 3,
};

class Spim {
 public:
  explicit Spim(uint32_t base = nrf54l15::SPIM21_BASE,
                uint32_t coreClockHz = 128000000UL);

  bool begin(const Pin& sck, const Pin& mosi, const Pin& miso,
             const Pin& cs = kPinDisconnected, uint32_t hz = 4000000UL,
             SpiMode mode = SpiMode::kMode0, bool lsbFirst = false);
  void end();

  bool transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                uint32_t spinLimit = 2000000UL);

 private:
  uint32_t base_;
  uint32_t coreClockHz_;
  Pin cs_;
};

enum class TwimFrequency : uint32_t {
  k100k = nrf54l15::twim::FREQUENCY_100K,
  k250k = nrf54l15::twim::FREQUENCY_250K,
  k400k = nrf54l15::twim::FREQUENCY_400K,
  k1000k = nrf54l15::twim::FREQUENCY_1000K,
};

class Twim {
 public:
  explicit Twim(uint32_t base = nrf54l15::TWIM21_BASE);

  bool begin(const Pin& scl, const Pin& sda,
             TwimFrequency frequency = TwimFrequency::k400k);
  void end();

  bool write(uint8_t address7, const uint8_t* data, size_t len,
             uint32_t spinLimit = 2000000UL);
  bool read(uint8_t address7, uint8_t* data, size_t len,
            uint32_t spinLimit = 2000000UL);
  bool writeRead(uint8_t address7, const uint8_t* tx, size_t txLen,
                 uint8_t* rx, size_t rxLen, uint32_t spinLimit = 2000000UL);

 private:
  uint32_t base_;
};

enum class UarteBaud : uint32_t {
  k9600 = nrf54l15::uarte::BAUD_9600,
  k115200 = nrf54l15::uarte::BAUD_115200,
  k1000000 = nrf54l15::uarte::BAUD_1M,
};

class Uarte {
 public:
  explicit Uarte(uint32_t base = nrf54l15::UARTE21_BASE);

  bool begin(const Pin& txd, const Pin& rxd,
             UarteBaud baud = UarteBaud::k115200,
             bool hwFlowControl = false,
             const Pin& cts = kPinDisconnected,
             const Pin& rts = kPinDisconnected);
  void end();

  bool write(const uint8_t* data, size_t len, uint32_t spinLimit = 2000000UL);
  size_t read(uint8_t* data, size_t len, uint32_t spinLimit = 2000000UL);

 private:
  uint32_t base_;
};

enum class TimerBitWidth : uint8_t {
  k16bit = nrf54l15::timer::BITMODE_16,
  k8bit = nrf54l15::timer::BITMODE_8,
  k24bit = nrf54l15::timer::BITMODE_24,
  k32bit = nrf54l15::timer::BITMODE_32,
};

class Timer {
 public:
  using CompareCallback = void (*)(uint8_t channel, void* context);

  explicit Timer(uint32_t base = nrf54l15::TIMER20_BASE,
                 uint32_t pclkHz = 16000000UL,
                 uint8_t channelCount = 6);

  bool begin(TimerBitWidth bitWidth = TimerBitWidth::k32bit,
             uint8_t prescaler = 4,
             bool counterMode = false);
  bool setFrequency(uint32_t targetHz);
  uint32_t timerHz() const;
  uint32_t ticksFromMicros(uint32_t us) const;

  void start();
  void stop();
  void clear();

  bool setCompare(uint8_t channel, uint32_t ccValue,
                  bool autoClear = false,
                  bool autoStop = false,
                  bool oneShot = false,
                  bool enableInterrupt = false);
  uint32_t capture(uint8_t channel);
  bool pollCompare(uint8_t channel, bool clearEvent = true);

  void enableInterrupt(uint8_t channel, bool enable = true);
  bool attachCompareCallback(uint8_t channel, CompareCallback callback,
                             void* context = nullptr);
  void service();

 private:
  uint32_t base_;
  uint32_t pclkHz_;
  uint8_t channelCount_;
  uint8_t prescaler_;
  CompareCallback callbacks_[8];
  void* callbackContext_[8];
};

class Pwm {
 public:
  explicit Pwm(uint32_t base = nrf54l15::PWM20_BASE);

  bool beginSingle(const Pin& outPin,
                   uint32_t frequencyHz = 1000UL,
                   uint16_t dutyPermille = 500,
                   bool activeHigh = true);
  bool setDutyPermille(uint16_t dutyPermille);
  bool setFrequency(uint32_t frequencyHz);

  bool start(uint8_t sequence = 0, uint32_t spinLimit = 2000000UL);
  bool stop(uint32_t spinLimit = 2000000UL);
  void end();

  bool pollPeriodEnd(bool clearEvent = true);

 private:
  bool configureClockAndTop(uint32_t frequencyHz);
  void updateSequenceWord();

  uint32_t base_;
  Pin outPin_;
  uint16_t dutyPermille_;
  uint16_t countertop_;
  uint8_t prescaler_;
  bool activeHigh_;
  bool configured_;
  alignas(4) uint16_t sequence_[4];
};

enum class GpiotePolarity : uint8_t {
  kNone = nrf54l15::gpiote::POLARITY_NONE,
  kLoToHi = nrf54l15::gpiote::POLARITY_LOTOHI,
  kHiToLo = nrf54l15::gpiote::POLARITY_HITOLO,
  kToggle = nrf54l15::gpiote::POLARITY_TOGGLE,
};

class Gpiote {
 public:
  using InCallback = void (*)(uint8_t channel, void* context);

  explicit Gpiote(uint32_t base = nrf54l15::GPIOTE20_BASE,
                  uint8_t channelCount = 8);

  bool configureEvent(uint8_t channel, const Pin& pin, GpiotePolarity polarity,
                      bool enableInterrupt = false);
  bool configureTask(uint8_t channel, const Pin& pin, GpiotePolarity polarity,
                     bool initialHigh = false);
  void disableChannel(uint8_t channel);

  bool triggerTaskOut(uint8_t channel);
  bool triggerTaskSet(uint8_t channel);
  bool triggerTaskClr(uint8_t channel);

  bool pollInEvent(uint8_t channel, bool clearEvent = true);
  bool pollPortEvent(bool clearEvent = true);

  void enableInterrupt(uint8_t channel, bool enable = true);
  bool attachInCallback(uint8_t channel, InCallback callback,
                        void* context = nullptr);
  void service();

 private:
  uint32_t base_;
  uint8_t channelCount_;
  InCallback callbacks_[8];
  void* callbackContext_[8];
};

enum class AdcResolution : uint8_t {
  k8bit = 0,
  k10bit = 1,
  k12bit = 2,
  k14bit = 3,
};

enum class AdcGain : uint8_t {
  k2 = 0,
  k1 = 1,
  k2over3 = 2,
  k2over4 = 3,
  k2over5 = 4,
  k2over6 = 5,
  k2over7 = 6,
  k2over8 = 7,
};

class Saadc {
 public:
  explicit Saadc(uint32_t base = nrf54l15::SAADC_BASE);

  bool begin(AdcResolution resolution = AdcResolution::k12bit,
             uint32_t spinLimit = 2000000UL);
  void end();

  // Configures one active single-ended channel and disables the others.
  bool configureSingleEnded(uint8_t channel, const Pin& pin,
                            AdcGain gain = AdcGain::k2over8,
                            uint16_t tacq = 159,
                            uint8_t tconv = 4);

  bool sampleRaw(int16_t* outRaw, uint32_t spinLimit = 2000000UL) const;
  bool sampleMilliVolts(int32_t* outMilliVolts,
                        uint32_t spinLimit = 2000000UL) const;

 private:
  uint32_t base_;
  AdcResolution resolution_;
  AdcGain gain_;
  bool configured_;
};

enum class PowerLatencyMode : uint8_t {
  kLowPower = 0,
  kConstantLatency = 1,
};

class PowerManager {
 public:
  explicit PowerManager(uint32_t powerBase = nrf54l15::POWER_BASE,
                        uint32_t resetBase = nrf54l15::RESET_BASE,
                        uint32_t regulatorsBase = nrf54l15::REGULATORS_BASE);

  void setLatencyMode(PowerLatencyMode mode);
  bool isConstantLatency() const;

  bool setRetention(uint8_t index, uint8_t value);
  bool getRetention(uint8_t index, uint8_t* value) const;

  uint32_t resetReason() const;
  void clearResetReason(uint32_t mask);

  bool enableMainDcdc(bool enable);

  [[noreturn]] void systemOff();

 private:
  NRF_POWER_Type* power_;
  NRF_RESET_Type* reset_;
  NRF_REGULATORS_Type* regulators_;
};

enum class GrtcClockSource : uint8_t {
  kLfxo = GRTC_CLKCFG_CLKSEL_LFXO,
  kSystemLfclk = GRTC_CLKCFG_CLKSEL_SystemLFCLK,
  kLflprc = GRTC_CLKCFG_CLKSEL_LFLPRC,
};

class Grtc {
 public:
  explicit Grtc(uint32_t base = nrf54l15::GRTC_BASE,
                uint8_t compareChannelCount = 12);

  bool begin(GrtcClockSource clockSource = GrtcClockSource::kSystemLfclk);
  void end();
  void start();
  void stop();
  void clear();

  uint64_t counter() const;
  bool setWakeLeadLfclk(uint8_t cycles);

  bool setCompareOffsetUs(uint8_t channel, uint32_t offsetUs,
                          bool enableChannel = true);
  bool setCompareAbsoluteUs(uint8_t channel, uint64_t timestampUs,
                            bool enableChannel = true);
  bool enableCompareChannel(uint8_t channel, bool enable = true);
  void enableCompareInterrupt(uint8_t channel, bool enable = true);
  bool pollCompare(uint8_t channel, bool clearEvent = true);
  bool clearCompareEvent(uint8_t channel);

 private:
  NRF_GRTC_Type* grtc_;
  uint8_t compareChannelCount_;
};

class TempSensor {
 public:
  explicit TempSensor(uint32_t base = nrf54l15::TEMP_BASE);

  bool sampleQuarterDegreesC(int32_t* outQuarterDegreesC,
                             uint32_t spinLimit = 200000UL) const;
  bool sampleMilliDegreesC(int32_t* outMilliDegreesC,
                           uint32_t spinLimit = 200000UL) const;

 private:
  NRF_TEMP_Type* temp_;
};

class Watchdog {
 public:
  explicit Watchdog(uint32_t base = nrf54l15::WDT31_BASE);

  bool configure(uint32_t timeoutMs, uint8_t reloadRegister = 0,
                 bool runInSleep = true, bool runInDebugHalt = false,
                 bool allowStop = false);
  void start();
  bool stop(uint32_t spinLimit = 200000UL);
  bool feed(uint8_t reloadRegister = 0xFFU);
  bool isRunning() const;
  uint32_t requestStatus() const;

 private:
  NRF_WDT_Type* wdt_;
  uint8_t defaultReloadRegister_;
  bool allowStop_;
};

enum class PdmEdge : uint8_t {
  kLeftRising = PDM_MODE_EDGE_LeftRising,
  kLeftFalling = PDM_MODE_EDGE_LeftFalling,
};

class Pdm {
 public:
  explicit Pdm(uint32_t base = nrf54l15::PDM20_BASE);

  bool begin(const Pin& clk, const Pin& din, bool mono = true,
             uint8_t prescalerDiv = 40,
             uint8_t ratio = PDM_RATIO_RATIO_Ratio64,
             PdmEdge edge = PdmEdge::kLeftRising);
  void end();

  bool capture(int16_t* samples, size_t sampleCount,
               uint32_t spinLimit = 4000000UL);

 private:
  NRF_PDM_Type* pdm_;
  bool configured_;
};

enum class BleAddressType : uint8_t {
  kPublic = 0,
  kRandomStatic = 1,
};

enum class BleAdvPduType : uint8_t {
  kAdvInd = 0x00,
  kAdvDirectInd = 0x01,
  kAdvNonConnInd = 0x02,
  kScanReq = 0x03,
  kScanRsp = 0x04,
  kConnectInd = 0x05,
  kAdvScanInd = 0x06,
};

enum class BleAdvertisingChannel : uint8_t {
  k37 = 37,
  k38 = 38,
  k39 = 39,
};

struct BleScanPacket {
  BleAdvertisingChannel channel;
  int8_t rssiDbm;
  uint8_t pduHeader;
  uint8_t length;
  const uint8_t* payload;
};

struct BleAdvInteraction {
  BleAdvertisingChannel channel;
  bool receivedScanRequest;
  bool scanResponseTransmitted;
  bool receivedConnectInd;
  bool peerAddressRandom;
  int8_t rssiDbm;
  uint8_t peerAddress[6];
};

struct BleConnectionInfo {
  uint8_t peerAddress[6];
  bool peerAddressRandom;
  uint32_t accessAddress;
  uint32_t crcInit;
  uint16_t intervalUnits;
  uint16_t latency;
  uint16_t supervisionTimeoutUnits;
  uint8_t channelMap[5];
  uint8_t channelCount;
  uint8_t hopIncrement;
  uint8_t sleepClockAccuracy;
};

struct BleConnectionEvent {
  bool eventStarted;
  bool packetReceived;
  bool crcOk;
  bool emptyAckTransmitted;
  bool packetIsNew;
  bool terminateInd;
  bool llControlPacket;
  bool attPacket;
  bool txPacketSent;
  uint16_t eventCounter;
  uint8_t dataChannel;
  int8_t rssiDbm;
  uint8_t llid;
  uint8_t llControlOpcode;
  uint8_t attOpcode;
  uint8_t payloadLength;
  uint8_t txLlid;
  uint8_t txPayloadLength;
  const uint8_t* payload;
  const uint8_t* txPayload;
};

// Minimal BLE LL radio block (legacy ADV + passive scan) implemented on RADIO.
// This class intentionally avoids a full host/controller stack.
class BleRadio {
 public:
  explicit BleRadio(uint32_t radioBase = nrf54l15::RADIO_BASE,
                    uint32_t ficrBase = nrf54l15::FICR_BASE);

  bool begin(int8_t txPowerDbm = 0);
  void end();

  bool setTxPowerDbm(int8_t dbm);
  bool selectExternalAntenna(bool external);
  bool loadAddressFromFicr(bool forceRandomStatic = true);
  bool setDeviceAddress(const uint8_t address[6],
                        BleAddressType type = BleAddressType::kRandomStatic);

  bool setAdvertisingPduType(BleAdvPduType type);
  bool setAdvertisingChannelSelectionAlgorithm2(bool enabled);
  bool setAdvertisingData(const uint8_t* data, size_t len);
  bool setAdvertisingName(const char* name, bool includeFlags = true);
  bool buildAdvertisingPacket();
  bool setGattDeviceName(const char* name);
  bool setGattBatteryLevel(uint8_t percent);

  bool setScanResponseData(const uint8_t* data, size_t len);
  bool setScanResponseName(const char* name);
  bool buildScanResponsePacket();

  bool advertiseOnce(BleAdvertisingChannel channel,
                     uint32_t spinLimit = 600000UL);
  bool advertiseEvent(uint32_t interChannelDelayUs = 350U,
                      uint32_t spinLimit = 600000UL);

  // Advertise and listen for SCAN_REQ / CONNECT_IND on a single channel.
  bool advertiseInteractOnce(BleAdvertisingChannel channel,
                             BleAdvInteraction* interaction,
                             uint32_t requestListenSpinLimit = 250000UL,
                             uint32_t spinLimit = 700000UL);
  bool advertiseInteractEvent(BleAdvInteraction* interaction,
                              uint32_t interChannelDelayUs = 350U,
                              uint32_t requestListenSpinLimit = 250000UL,
                              uint32_t spinLimit = 700000UL);

  bool isConnected() const;
  bool getConnectionInfo(BleConnectionInfo* info) const;
  bool disconnect(uint32_t spinLimit = 300000UL);
  bool pollConnectionEvent(BleConnectionEvent* event = nullptr,
                           uint32_t spinLimit = 400000UL);

  bool scanOnce(BleAdvertisingChannel channel, BleScanPacket* packet,
                uint32_t spinLimit = 900000UL);
  bool scanCycle(BleScanPacket* packet, uint32_t perChannelSpinLimit = 300000UL);

 private:
  bool configureBle1M();
  bool waitDisabled(uint32_t spinLimit);
  bool waitForEnd(uint32_t spinLimit);
  bool setAdvertisingChannel(BleAdvertisingChannel channel);
  bool setDataChannel(uint8_t dataChannel);
  bool handleRequestAndMaybeRespond(BleAdvertisingChannel channel,
                                    BleAdvInteraction* interaction,
                                    uint32_t requestListenSpinLimit,
                                    uint32_t spinLimit);
  bool startConnectionFromConnectInd(const uint8_t* payload, uint8_t length,
                                     bool peerAddressRandom);
  bool buildLlControlResponse(const uint8_t* payload, uint8_t length,
                              uint8_t* outPayload, uint8_t* outLength,
                              bool* terminateInd);
  bool buildAttResponse(const uint8_t* attRequest, uint16_t requestLength,
                        uint8_t* outAttResponse, uint16_t* outAttResponseLength);
  bool buildL2capResponse(const uint8_t* l2capPayload, uint8_t l2capPayloadLength,
                          uint8_t* outPayload, uint8_t* outPayloadLength);
  bool buildL2capAttResponse(const uint8_t* l2capPayload, uint8_t l2capPayloadLength,
                             uint8_t* outPayload, uint8_t* outPayloadLength);
  bool buildL2capSignalingResponse(const uint8_t* l2capPayload,
                                   uint8_t l2capPayloadLength,
                                   uint8_t* outPayload,
                                   uint8_t* outPayloadLength);
  bool buildL2capSmpResponse(const uint8_t* l2capPayload,
                             uint8_t l2capPayloadLength,
                             uint8_t* outPayload,
                             uint8_t* outPayloadLength);
  bool buildAttErrorResponse(uint8_t requestOpcode, uint16_t handle,
                             uint8_t errorCode, uint8_t* outAttResponse,
                             uint16_t* outAttResponseLength) const;
  uint8_t readAttributeValue(uint16_t handle, uint16_t offset, uint8_t* outValue,
                             uint8_t maxLen) const;
  void updateNextConnectionEventTime();
  uint8_t selectNextDataChannel();
  void restoreAdvertisingLinkDefaults();
  static uint32_t txPowerRegFromDbm(int8_t dbm);

  NRF_RADIO_Type* radio_;
  NRF_FICR_Type* ficr_;
  bool initialized_;
  BleAddressType addressType_;
  BleAdvPduType pduType_;
  bool useChSel2_;
  bool externalAntenna_;
  uint8_t address_[6];
  uint8_t advData_[31];
  size_t advDataLen_;
  uint8_t scanRspData_[31];
  size_t scanRspDataLen_;
  alignas(4) uint8_t txPacket_[2 + 6 + 31];
  alignas(4) uint8_t scanRspPacket_[2 + 6 + 31];
  alignas(4) uint8_t rxPacket_[2 + 255];
  alignas(4) uint8_t connectionTxPayload_[255];

  bool connected_;
  uint8_t connectionPeerAddress_[6];
  bool connectionPeerAddressRandom_;
  uint32_t connectionAccessAddress_;
  uint32_t connectionCrcInit_;
  uint16_t connectionIntervalUnits_;
  uint16_t connectionLatency_;
  uint16_t connectionTimeoutUnits_;
  uint8_t connectionChannelMap_[5];
  uint8_t connectionChannelCount_;
  uint8_t connectionHop_;
  uint8_t connectionSca_;
  uint8_t connectionChanUse_;
  uint8_t connectionExpectedRxSn_;
  uint8_t connectionTxSn_;
  uint16_t connectionEventCounter_;
  uint32_t connectionNextEventUs_;
  uint16_t connectionAttMtu_;
  uint8_t connectionLastTxLlid_;
  uint8_t connectionLastTxLength_;
  bool connectionUpdatePending_;
  uint16_t connectionUpdateInstant_;
  uint16_t connectionPendingIntervalUnits_;
  uint16_t connectionPendingLatency_;
  uint16_t connectionPendingTimeoutUnits_;
  bool connectionChannelMapPending_;
  uint16_t connectionChannelMapInstant_;
  uint8_t connectionPendingChannelMap_[5];
  uint8_t connectionPendingChannelCount_;
  bool connectionServiceChangedIndicationsEnabled_;
  bool connectionServiceChangedIndicationPending_;
  bool connectionServiceChangedIndicationAwaitingConfirm_;
  bool connectionBatteryNotificationsEnabled_;
  bool connectionBatteryNotificationPending_;
  bool connectionPreparedWriteActive_;
  uint16_t connectionPreparedWriteHandle_;
  uint8_t connectionPreparedWriteValue_[2];
  uint8_t connectionPreparedWriteMask_;
  uint8_t smpPairingState_;
  uint8_t smpPairingReq_[7];
  uint8_t smpPairingRsp_[7];
  uint8_t smpPeerConfirm_[16];
  uint8_t smpPeerRandom_[16];
  uint8_t smpLocalRandom_[16];
  uint8_t smpStk_[16];
  bool smpStkValid_;
  bool connectionEncSessionValid_;
  bool connectionEncRxEnabled_;
  bool connectionEncTxEnabled_;
  bool connectionEncStartReqPending_;
  bool connectionEncAwaitingStartRsp_;
  bool connectionEncEnableTxOnNextEvent_;
  uint64_t connectionEncRxCounter_;
  uint64_t connectionEncTxCounter_;
  uint8_t connectionEncSessionKey_[16];
  uint8_t connectionEncIv_[8];
  bool connectionLastTxWasEncrypted_;
  uint8_t connectionLastTxEncryptedLength_;
  uint8_t connectionLastTxEncryptedPayload_[31];
  uint8_t scanCycleStartIndex_;
  uint8_t gapDeviceName_[31];
  uint8_t gapDeviceNameLen_;
  uint16_t gapAppearance_;
  uint16_t gapPpcpIntervalMin_;
  uint16_t gapPpcpIntervalMax_;
  uint16_t gapPpcpLatency_;
  uint16_t gapPpcpTimeout_;
  uint8_t gapBatteryLevel_;
};

}  // namespace xiao_nrf54l15
