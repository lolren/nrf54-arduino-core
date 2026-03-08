#ifndef NRF54L15_CLEAN_NRF_TO_NRF_H_
#define NRF54L15_CLEAN_NRF_TO_NRF_H_

#include <Arduino.h>

#include "nrf54l15_hal.h"

#define NRF52_RADIO_LIBRARY
#define DEFAULT_MAX_PAYLOAD_SIZE 32U
#define ACTUAL_MAX_PAYLOAD_SIZE 127U
#define ACK_TIMEOUT_1MBPS 600U

typedef enum {
  NRF_PA_MIN = 0,
  NRF_PA_LOW,
  NRF_PA_HIGH,
  NRF_PA_MAX,
  NRF_PA_ERROR
} nrf_pa_dbm_e;

typedef enum {
  NRF_1MBPS = 0,
  NRF_2MBPS,
  NRF_250KBPS
} nrf_datarate_e;

typedef enum {
  NRF_CRC_DISABLED = 0,
  NRF_CRC_8,
  NRF_CRC_16,
  NRF_CRC_24
} nrf_crclength_e;

class nrf_to_nrf {
 public:
  nrf_to_nrf();

  bool begin();

  bool available();
  bool available(uint8_t* pipe_num);
  void read(void* buf, uint8_t len);

  bool write(void* buf, uint8_t len, bool multicast = false,
             bool doEncryption = true);
  bool writeFast(void* buf, uint8_t len, bool multicast = false);
  bool startWrite(void* buf, uint8_t len, bool multicast,
                  bool doEncryption = true);
  bool txStandBy();
  bool txStandBy(uint32_t timeout, bool startTx = false);

  void startListening(bool resetAddresses = true);
  void stopListening(bool setWritingPipe = true, bool resetAddresses = true);
  void stopListening(const uint8_t* txAddress, bool setWritingPipe = true,
                     bool resetAddresses = true);

  void openReadingPipe(uint8_t child, const uint8_t* address);
  void openReadingPipe(uint8_t child, uint64_t address);
  void openReadingPipe(uint8_t child, uint32_t base, uint32_t prefix);
  void openWritingPipe(const uint8_t* address);
  void openWritingPipe(uint64_t address);
  void openWritingPipe(uint32_t base, uint32_t prefix);

  bool writeAckPayload(uint8_t pipe, void* buf, uint8_t len);
  void enableAckPayload();
  void disableAckPayload();
  void enableDynamicAck();
  void enableDynamicPayloads(uint8_t payloadSize = ACTUAL_MAX_PAYLOAD_SIZE);
  void disableDynamicPayloads();
  uint8_t getDynamicPayloadSize();

  bool isValid();
  bool isChipConnected();

  void setChannel(uint8_t channel, bool map = false);
  uint8_t getChannel();

  void setAutoAck(bool enable);
  void setAutoAck(uint8_t pipe, bool enable);
  void setPayloadSize(uint8_t size);
  uint8_t getPayloadSize();
  void setRetries(uint8_t retryVar, uint8_t attempts);

  bool setDataRate(uint8_t speed);
  void setPALevel(uint8_t level, bool lnaEnable = false);
  uint8_t getPALevel();
  uint8_t getARC();

  void powerUp();
  void powerDown();

  uint8_t radioData[ACTUAL_MAX_PAYLOAD_SIZE + 2U];

 private:
  static constexpr uint8_t kAddressWidth = 5U;
  static constexpr uint8_t kMaxPipes = 8U;
  static constexpr uint8_t kFrameTypeData = 0x01U;
  static constexpr uint8_t kFrameTypeAck = 0x02U;
  static constexpr uint8_t kFrameMagic0 = 0x54U;
  static constexpr uint8_t kFrameMagic1 = 0x32U;
  static constexpr uint8_t kHeaderSize = 6U;
  static constexpr uint32_t kDefaultAddressBase = 0xC2C2C2C2UL;
  static constexpr uint8_t kDefaultAddressPrefix = 0xC2U;

  struct PipeConfig {
    bool enabled;
    uint8_t address[kAddressWidth];
  };

  bool configureLink();
  bool applyPipeAddress(const uint8_t* address);
  bool applyWritingPipe();
  bool applyListeningPipe();
  uint8_t selectListeningPipe() const;
  bool ensureListeningArmed();
  bool pollRadio();
  bool processIncomingFrame(const xiao_nrf54l15::RawRadioPacket& packet,
                            bool fromAckWait);
  bool sendAckForPipe(uint8_t pipe, uint8_t sequence);
  bool waitForAck(uint8_t sequence, uint8_t* outAttemptsUsed);
  bool transmitFrame(uint8_t frameType, uint8_t sequence,
                     const uint8_t* payload, uint8_t payloadLength,
                     bool forceWritingPipe);
  bool buildFrame(uint8_t frameType, uint8_t pipe, uint8_t sequence,
                  const uint8_t* payload, uint8_t payloadLength,
                  uint8_t* outFrame, uint8_t* outLength) const;
  bool parseFrame(const uint8_t* frame, uint8_t frameLength, uint8_t* outType,
                  uint8_t* outPipe, uint8_t* outSequence,
                  const uint8_t** outPayload, uint8_t* outPayloadLength) const;
  void setAddressFromBytes(uint8_t* outAddress, const uint8_t* address);
  void setAddressFromUint64(uint8_t* outAddress, uint64_t address);
  static void addressToBasePrefix(const uint8_t* address, uint32_t* outBase,
                                  uint8_t* outPrefix);
  static int8_t paLevelToDbm(uint8_t level);
  uint32_t ackTimeoutUs() const;
  uint8_t maxUserPayloadLength() const;
  void clearBufferedPayload();
  void collapseRfIfIdle();

  xiao_nrf54l15::RawRadioLink link_;
  xiao_nrf54l15::RawRadioConfig rawConfig_;
  PipeConfig readingPipes_[kMaxPipes];
  uint8_t writingAddress_[kAddressWidth];
  bool writingPipeValid_;
  bool initialized_;
  bool poweredDown_;
  bool listening_;
  bool dynamicPayloadsEnabled_;
  bool ackPayloadsEnabled_;
  bool dynamicAckEnabled_;
  bool rxAvailable_;
  uint8_t rxPipe_;
  uint8_t rxLength_;
  uint8_t lastPayloadLength_;
  uint8_t rxPayload_[ACTUAL_MAX_PAYLOAD_SIZE];
  bool ackPayloadPending_[kMaxPipes];
  uint8_t ackPayloadLength_[kMaxPipes];
  uint8_t ackPayloadData_[kMaxPipes][ACTUAL_MAX_PAYLOAD_SIZE];
  bool autoAckEnabled_[kMaxPipes];
  uint8_t channel_;
  uint8_t payloadSize_;
  uint8_t paLevel_;
  uint8_t retryDelaySetting_;
  uint8_t retryCountSetting_;
  uint8_t nextSequence_;
  uint8_t lastArc_;
  uint8_t activePipe_;
};

#endif
