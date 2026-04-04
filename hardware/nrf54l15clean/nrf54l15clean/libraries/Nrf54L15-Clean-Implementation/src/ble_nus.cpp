#include "ble_nus.h"

#include <string.h>

namespace xiao_nrf54l15 {

namespace {

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
      debugNotificationSentCount_(0U),
      debugNotificationRetiredCount_(0U),
      lastQueueResult_(0U),
      initialized_(false),
      connected_(false),
      txNotificationInFlight_(false),
      txNotificationAwaitingAck_(false) {}

bool BleNordicUart::begin() {
  if (initialized_) {
    return true;
  }

  // NUS is an interactive streaming service; prefer a short connection
  // interval so centrals are nudged toward a responsive, high-throughput link.
  ble_.setPreferredConnectionParameters(6U, 12U, 0U, 500U);
  // Disable CSA#2 advertisement — matches the default used by all connectable
  // example sketches and avoids a Qualcomm/Sony discoverability issue where
  // ADV_IND with the ChSel bit set is not shown in some BLE scanner apps.
  ble_.setAdvertisingChannelSelectionAlgorithm2(false);

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

  // Advertise the NUS service UUID so Android BLE serial apps can discover this
  // device by UUID scan filter.  The GAP device name is automatically moved to
  // the scan response, so it still appears when the central performs an active scan.
  ble_.setAdvertisingServiceUuid128(kServiceUuid128);

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

  if (event == nullptr) {
    BleConnectionEvent deferred{};
    while (ble_.consumeDeferredConnectionEvent(&deferred)) {
      service(&deferred);
    }
  }

  // The HAL retains a small custom-notification queue. Once this NUS TX
  // characteristic has anything pending in that controller-owned path, keep
  // the sketch-side ring blocked only until the HAL reports that queue empty
  // again; link-layer retransmission is then owned entirely by the controller.
  if (txNotificationInFlight_) {
    const bool stillQueued =
        ble_.isCustomGattNotificationQueued(txValueHandle_, false);
    txNotificationAwaitingAck_ = stillQueued;
    if (!stillQueued) {
      txChunkLength_ = 0U;
      txNotificationInFlight_ = false;
      txNotificationAwaitingAck_ = false;
      ++debugNotificationRetiredCount_;
    }
  }

  // If CCCD was disabled while a notification was still queued in the HAL,
  // drop the controller-handoff state and let future writes re-arm it once
  // the central enables notifications again.
  if (txNotificationInFlight_ && ble_.isConnected() && !isNotifyEnabled()) {
    txChunkLength_ = 0U;
    txNotificationInFlight_ = false;
    txNotificationAwaitingAck_ = false;
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
  while (queueNextNotification()) {
  }
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

uint16_t BleNordicUart::debugTxCount() const {
  return txCount_;
}

bool BleNordicUart::debugTxNotificationInFlight() const {
  return txNotificationInFlight_;
}

bool BleNordicUart::debugTxNotificationAwaitingAck() const {
  return txNotificationAwaitingAck_;
}

uint8_t BleNordicUart::debugLastQueueResult() const {
  return lastQueueResult_;
}

uint32_t BleNordicUart::debugNotificationSentCount() const {
  return debugNotificationSentCount_;
}

uint32_t BleNordicUart::debugNotificationRetiredCount() const {
  return debugNotificationRetiredCount_;
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

uint8_t BleNordicUart::maxPayloadLength() const {
  return notificationValueLimit();
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
  txNotificationAwaitingAck_ = false;
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
  while (written < size) {
    if (txCount_ >= kTxBufferSize) {
      const uint16_t beforeService = txCount_;
      service();
      if (txCount_ >= kTxBufferSize && txCount_ >= beforeService) {
        break;
      }
    }

    size_t batch = size - written;
    const size_t freeSpace = static_cast<size_t>(kTxBufferSize - txCount_);
    if (batch > freeSpace) {
      batch = freeSpace;
    }
    if (batch == 0U) {
      break;
    }

    for (size_t i = 0U; i < batch; ++i) {
      txBuffer_[txHead_] = buffer[written++];
      txHead_ = advanceIndex(txHead_, static_cast<uint16_t>(kTxBufferSize));
      ++txCount_;
    }

    if (ble_.isConnected() && isNotifyEnabled() &&
        txCount_ >= static_cast<uint16_t>(notificationValueLimit())) {
      service();
    }
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
  if (!initialized_) {
    lastQueueResult_ = 1U;
    return false;
  }
  if (!ble_.isConnected()) {
    lastQueueResult_ = 2U;
    return false;
  }
  if (txCount_ == 0U) {
    lastQueueResult_ = 3U;
    return false;
  }
  if (!isNotifyEnabled()) {
    lastQueueResult_ = 4U;
    return false;
  }

  const size_t chunkLength = copyTxChunk(txChunk_, sizeof(txChunk_));
  if (chunkLength == 0U || chunkLength > kMaxPayloadLength) {
    lastQueueResult_ = 5U;
    return false;
  }
  if (!ble_.setCustomGattCharacteristicValue(
          txValueHandle_, txChunk_, static_cast<uint8_t>(chunkLength))) {
    lastQueueResult_ = 6U;
    return false;
  }
  if (!ble_.notifyCustomGattCharacteristic(txValueHandle_, false)) {
    lastQueueResult_ = 7U;
    return false;
  }

  uint16_t advance = static_cast<uint16_t>(chunkLength);
  while (advance > 0U && txCount_ > 0U) {
    txTail_ = advanceIndex(txTail_, static_cast<uint16_t>(kTxBufferSize));
    --txCount_;
    --advance;
  }
  txChunkLength_ = static_cast<uint8_t>(chunkLength);
  txNotificationInFlight_ = true;
  txNotificationAwaitingAck_ =
      ble_.isCustomGattNotificationQueued(txValueHandle_, false);
  ++debugNotificationSentCount_;
  lastQueueResult_ = 8U;
  return true;
}

uint8_t BleNordicUart::notificationValueLimit() const {
  const uint8_t limit = ble_.maxNotificationValueLength();
  return (limit > 0U) ? limit : 1U;
}

size_t BleNordicUart::copyTxChunk(uint8_t* outChunk, size_t maxLength) const {
  if (outChunk == nullptr || maxLength == 0U || txCount_ == 0U) {
    return 0U;
  }

  size_t chunkLength = txCount_;
  if (chunkLength > maxLength) {
    chunkLength = maxLength;
  }
  const uint8_t valueLimit = notificationValueLimit();
  if (chunkLength > valueLimit) {
    chunkLength = valueLimit;
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
