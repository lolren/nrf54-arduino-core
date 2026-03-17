#include "ble_nus.h"

#include <string.h>

namespace xiao_nrf54l15 {

namespace {

constexpr uint16_t kBleAttCid = 0x0004U;
constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;

inline uint16_t readLe16Local(const uint8_t* value) {
  if (value == nullptr) {
    return 0U;
  }
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(value[1] << 8U);
}

inline uint16_t advanceIndex(uint16_t index, uint16_t capacity) {
  ++index;
  if (index >= capacity) {
    index = 0U;
  }
  return index;
}

}  // namespace

const uint8_t BleNordicUart::kServiceUuid128[16] = {
    0x6E, 0x40, 0x00, 0x01, 0xB5, 0xA3, 0xF3, 0x93,
    0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E};

const uint8_t BleNordicUart::kRxCharacteristicUuid128[16] = {
    0x6E, 0x40, 0x00, 0x02, 0xB5, 0xA3, 0xF3, 0x93,
    0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E};

const uint8_t BleNordicUart::kTxCharacteristicUuid128[16] = {
    0x6E, 0x40, 0x00, 0x03, 0xB5, 0xA3, 0xF3, 0x93,
    0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E};

BleNordicUart::BleNordicUart(BleRadio& ble)
    : ble_(ble),
      serviceHandle_(0U),
      rxValueHandle_(0U),
      txValueHandle_(0U),
      txCccdHandle_(0U),
      rxHead_(0U),
      rxTail_(0U),
      rxCount_(0U),
      txHead_(0U),
      txTail_(0U),
      txCount_(0U),
      rxBuffer_{0},
      txBuffer_{0},
      txChunk_{0},
      txChunkLength_(0U),
      rxDroppedBytes_(0U),
      txDroppedBytes_(0U),
      initialized_(false),
      connected_(false),
      txNotificationInFlight_(false) {}

bool BleNordicUart::begin() {
  if (initialized_) {
    return true;
  }

  uint16_t serviceHandle = 0U;
  uint16_t rxValueHandle = 0U;
  uint16_t txValueHandle = 0U;
  uint16_t txCccdHandle = 0U;

  const uint8_t rxProperties =
      static_cast<uint8_t>(kBleGattPropWrite | kBleGattPropWriteNoRsp);
  const uint8_t txProperties = static_cast<uint8_t>(kBleGattPropNotify);

  if (!ble_.addCustomGattService128(kServiceUuid128, &serviceHandle)) {
    return false;
  }
  if (!ble_.addCustomGattCharacteristic128(serviceHandle, kRxCharacteristicUuid128,
                                           rxProperties, nullptr, 0U,
                                           &rxValueHandle, nullptr)) {
    return false;
  }
  if (!ble_.addCustomGattCharacteristic128(serviceHandle, kTxCharacteristicUuid128,
                                           txProperties, nullptr, 0U,
                                           &txValueHandle, &txCccdHandle)) {
    return false;
  }
  if (!ble_.setCustomGattWriteHandler(rxValueHandle, onRxWriteThunk, this)) {
    return false;
  }

  serviceHandle_ = serviceHandle;
  rxValueHandle_ = rxValueHandle;
  txValueHandle_ = txValueHandle;
  txCccdHandle_ = txCccdHandle;
  clear();
  initialized_ = true;
  connected_ = ble_.isConnected();
  return true;
}

void BleNordicUart::end() {
  if (rxValueHandle_ != 0U) {
    ble_.setCustomGattWriteHandler(rxValueHandle_, nullptr, nullptr);
  }
  serviceHandle_ = 0U;
  rxValueHandle_ = 0U;
  txValueHandle_ = 0U;
  txCccdHandle_ = 0U;
  clear();
  initialized_ = false;
  connected_ = false;
}

void BleNordicUart::service(const BleConnectionEvent* event) {
  if (!initialized_) {
    return;
  }

  // Only retire the in-flight notification when the HAL confirms it was sent
  // as a *fresh* (non-retransmitted) PDU and the payload matches. On a
  // retransmit event txPayload still points to the last fresh payload, so
  // checking only txPacketSent would fire prematurely and advance txTail_
  // before the peer has even ACKed the packet.
  if (event != nullptr && txNotificationInFlight_ &&
      event->freshTxAllowed &&
      eventSentNotificationForHandle(event, txValueHandle_)) {
    uint8_t advance = txChunkLength_;
    if (advance > txCount_) {
      advance = static_cast<uint8_t>(txCount_);
    }
    while (advance > 0U) {
      txTail_ = advanceIndex(txTail_, static_cast<uint16_t>(kTxBufferSize));
      --txCount_;
      --advance;
    }
    txChunkLength_ = 0U;
    txNotificationInFlight_ = false;
  }

  if (event != nullptr && event->terminateInd) {
    resetSessionState();
    connected_ = false;
    return;
  }

  if (!ble_.isConnected()) {
    if (connected_) {
      resetSessionState();
    }
    connected_ = false;
    return;
  }

  connected_ = true;
  queueNextNotification();
}

bool BleNordicUart::initialized() const {
  return initialized_;
}

bool BleNordicUart::isConnected() const {
  return ble_.isConnected();
}

bool BleNordicUart::isNotifyEnabled() const {
  if (!initialized_ || txValueHandle_ == 0U) {
    return false;
  }
  return ble_.isCustomGattCccdEnabled(txValueHandle_, false);
}

bool BleNordicUart::hasPendingTx() const {
  return (txCount_ != 0U) || txNotificationInFlight_;
}

uint16_t BleNordicUart::serviceHandle() const {
  return serviceHandle_;
}

uint16_t BleNordicUart::rxValueHandle() const {
  return rxValueHandle_;
}

uint16_t BleNordicUart::txValueHandle() const {
  return txValueHandle_;
}

uint16_t BleNordicUart::txCccdHandle() const {
  return txCccdHandle_;
}

uint32_t BleNordicUart::rxDroppedBytes() const {
  return rxDroppedBytes_;
}

uint32_t BleNordicUart::txDroppedBytes() const {
  return txDroppedBytes_;
}

void BleNordicUart::clear() {
  clearRx();
  clearTx();
}

void BleNordicUart::clearRx() {
  rxHead_ = 0U;
  rxTail_ = 0U;
  rxCount_ = 0U;
}

void BleNordicUart::clearTx() {
  txHead_ = 0U;
  txTail_ = 0U;
  txCount_ = 0U;
  txChunkLength_ = 0U;
  txNotificationInFlight_ = false;
}

int BleNordicUart::available() {
  return static_cast<int>(rxCount_);
}

int BleNordicUart::read() {
  if (rxCount_ == 0U) {
    return -1;
  }
  const uint8_t value = rxBuffer_[rxTail_];
  rxTail_ = advanceIndex(rxTail_, static_cast<uint16_t>(kRxBufferSize));
  --rxCount_;
  return value;
}

int BleNordicUart::peek() {
  if (rxCount_ == 0U) {
    return -1;
  }
  return rxBuffer_[rxTail_];
}

void BleNordicUart::flush() {
  service();
}

int BleNordicUart::availableForWrite() {
  return static_cast<int>(kTxBufferSize - txCount_);
}

size_t BleNordicUart::write(uint8_t value) {
  return write(&value, 1U);
}

size_t BleNordicUart::write(const uint8_t* buffer, size_t size) {
  if (buffer == nullptr || size == 0U) {
    return 0U;
  }

  size_t written = 0U;
  while (written < size && txCount_ < kTxBufferSize) {
    txBuffer_[txHead_] = buffer[written];
    txHead_ = advanceIndex(txHead_, static_cast<uint16_t>(kTxBufferSize));
    ++txCount_;
    ++written;
  }

  if (written < size) {
    txDroppedBytes_ += static_cast<uint32_t>(size - written);
  }

  if (written > 0U) {
    service();
  }
  return written;
}

void BleNordicUart::onRxWriteThunk(uint16_t valueHandle, const uint8_t* value,
                                   uint8_t valueLength, bool withResponse,
                                   void* context) {
  (void)valueHandle;
  (void)withResponse;
  BleNordicUart* self = static_cast<BleNordicUart*>(context);
  if (self == nullptr) {
    return;
  }
  self->onRxWrite(value, valueLength);
}

bool BleNordicUart::eventSentNotificationForHandle(const BleConnectionEvent* event,
                                                   uint16_t valueHandle) {
  if (event == nullptr || !event->txPacketSent || event->txPayload == nullptr ||
      event->txPayloadLength < 7U) {
    return false;
  }

  const uint8_t* payload = event->txPayload;
  if (readLe16Local(&payload[2]) != kBleAttCid) {
    return false;
  }
  if (payload[4] != kAttOpHandleValueNtf) {
    return false;
  }
  return (readLe16Local(&payload[5]) == valueHandle);
}

void BleNordicUart::onRxWrite(const uint8_t* value, uint8_t valueLength) {
  if (valueLength == 0U || value == nullptr) {
    return;
  }

  for (uint8_t i = 0U; i < valueLength; ++i) {
    if (rxCount_ >= kRxBufferSize) {
      ++rxDroppedBytes_;
      continue;
    }
    rxBuffer_[rxHead_] = value[i];
    rxHead_ = advanceIndex(rxHead_, static_cast<uint16_t>(kRxBufferSize));
    ++rxCount_;
  }
}

bool BleNordicUart::queueNextNotification() {
  if (!initialized_ || !ble_.isConnected() || txNotificationInFlight_ ||
      txCount_ == 0U || !isNotifyEnabled()) {
    return false;
  }

  const size_t chunkLength = copyTxChunk(txChunk_, sizeof(txChunk_));
  if (chunkLength == 0U || chunkLength > kMaxPayloadLength) {
    return false;
  }
  if (!ble_.setCustomGattCharacteristicValue(
          txValueHandle_, txChunk_, static_cast<uint8_t>(chunkLength))) {
    return false;
  }
  if (!ble_.notifyCustomGattCharacteristic(txValueHandle_, false)) {
    return false;
  }

  txChunkLength_ = static_cast<uint8_t>(chunkLength);
  txNotificationInFlight_ = true;
  return true;
}

size_t BleNordicUart::copyTxChunk(uint8_t* outChunk, size_t maxLength) const {
  if (outChunk == nullptr || maxLength == 0U || txCount_ == 0U) {
    return 0U;
  }

  size_t chunkLength = txCount_;
  if (chunkLength > maxLength) {
    chunkLength = maxLength;
  }

  uint16_t index = txTail_;
  for (size_t i = 0U; i < chunkLength; ++i) {
    outChunk[i] = txBuffer_[index];
    index = advanceIndex(index, static_cast<uint16_t>(kTxBufferSize));
  }
  return chunkLength;
}

void BleNordicUart::resetSessionState() {
  clear();
}

}  // namespace xiao_nrf54l15
