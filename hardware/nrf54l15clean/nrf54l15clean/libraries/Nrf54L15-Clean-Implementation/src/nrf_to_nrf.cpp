#include "nrf_to_nrf.h"

#include <string.h>

using namespace xiao_nrf54l15;

namespace {

static constexpr BoardAntennaPath kCompatAntennaPath =
    BoardAntennaPath::kCeramic;

static uint8_t clampPayload(uint8_t value) {
  if (value == 0U) {
    return 1U;
  }
  if (value > ACTUAL_MAX_PAYLOAD_SIZE) {
    return ACTUAL_MAX_PAYLOAD_SIZE;
  }
  return value;
}

}  // namespace

nrf_to_nrf::nrf_to_nrf()
    : link_(),
      rawConfig_(),
      readingPipes_{},
      writingAddress_{0},
      writingPipeValid_(false),
      initialized_(false),
      poweredDown_(false),
      listening_(false),
      dynamicPayloadsEnabled_(false),
      ackPayloadsEnabled_(false),
      dynamicAckEnabled_(false),
      rxAvailable_(false),
      rxPipe_(0U),
      rxLength_(0U),
      lastPayloadLength_(0U),
      rxPayload_{0},
      ackPayloadPending_{false, false, false, false, false, false, false, false},
      ackPayloadLength_{0},
      ackPayloadData_{0},
      autoAckEnabled_{true, true, true, true, true, true, true, true},
      channel_(76U),
      payloadSize_(DEFAULT_MAX_PAYLOAD_SIZE),
      paLevel_(NRF_PA_LOW),
      retryDelaySetting_(5U),
      retryCountSetting_(15U),
      nextSequence_(1U),
      lastArc_(0U),
      activePipe_(0xFFU) {
  memset(radioData, 0, sizeof(radioData));
  rawConfig_.frequencyOffsetMhz = channel_;
  rawConfig_.addressBase0 = kDefaultAddressBase;
  rawConfig_.addressPrefix0 = kDefaultAddressPrefix;
  rawConfig_.txPowerDbm = paLevelToDbm(paLevel_);
  rawConfig_.maxPayloadLength = clampPayload(payloadSize_ + kHeaderSize);
}

bool nrf_to_nrf::begin() {
  rawConfig_.frequencyOffsetMhz = channel_;
  rawConfig_.txPowerDbm = paLevelToDbm(paLevel_);
  rawConfig_.maxPayloadLength = clampPayload(maxUserPayloadLength() + kHeaderSize);

  if (!configureLink()) {
    initialized_ = false;
    return false;
  }

  initialized_ = true;
  poweredDown_ = false;
  listening_ = false;
  rxAvailable_ = false;
  rxLength_ = 0U;
  lastPayloadLength_ = 0U;
  lastArc_ = 0U;
  nextSequence_ = 1U;
  if (activePipe_ == 0xFFU) {
    activePipe_ = selectListeningPipe();
  }
  collapseRfIfIdle();
  return true;
}

bool nrf_to_nrf::configureLink() {
  if (!link_.begin(rawConfig_)) {
    return false;
  }
  if (listening_) {
    if (!applyListeningPipe()) {
      return false;
    }
    return ensureListeningArmed();
  }
  if (writingPipeValid_) {
    return applyWritingPipe();
  }
  return true;
}

bool nrf_to_nrf::available() { return available(nullptr); }

bool nrf_to_nrf::available(uint8_t* pipe_num) {
  if (rxAvailable_) {
    if (pipe_num != nullptr) {
      *pipe_num = rxPipe_;
    }
    return true;
  }

  if (!initialized_ || poweredDown_ || !listening_) {
    return false;
  }

  if (!pollRadio()) {
    return false;
  }

  if (rxAvailable_ && pipe_num != nullptr) {
    *pipe_num = rxPipe_;
  }
  return rxAvailable_;
}

void nrf_to_nrf::read(void* buf, uint8_t len) {
  if (buf == nullptr || len == 0U) {
    clearBufferedPayload();
    return;
  }

  memset(buf, 0, len);
  if (rxAvailable_) {
    const uint8_t copyLength = (len < rxLength_) ? len : rxLength_;
    memcpy(buf, rxPayload_, copyLength);
    memcpy(radioData, rxPayload_, rxLength_);
  }
  clearBufferedPayload();
}

bool nrf_to_nrf::write(void* buf, uint8_t len, bool multicast,
                       bool doEncryption) {
  (void)doEncryption;
  if (!initialized_ || poweredDown_ || buf == nullptr || len == 0U) {
    return false;
  }
  if (len > maxUserPayloadLength()) {
    return false;
  }

  clearBufferedPayload();
  const uint8_t sequence = nextSequence_++;
  const uint8_t* payload = static_cast<const uint8_t*>(buf);
  uint8_t attemptsUsed = 0U;
  const uint8_t totalAttempts = multicast ? 1U :
      static_cast<uint8_t>((retryCountSetting_ == 0U) ? 1U : (retryCountSetting_ + 1U));

  if (!BoardControl::enableRfPath(kCompatAntennaPath)) {
    return false;
  }

  for (uint8_t attempt = 0U; attempt < totalAttempts; ++attempt) {
    attemptsUsed = static_cast<uint8_t>(attempt + 1U);
    if (!transmitFrame(kFrameTypeData, sequence, payload, len, true)) {
      collapseRfIfIdle();
      lastArc_ = attempt;
      return false;
    }

    const uint8_t ackPipe = (activePipe_ < kMaxPipes) ? activePipe_ : 0U;
    const bool needAck = (!multicast) && autoAckEnabled_[ackPipe];
    if (!needAck) {
      lastArc_ = attempt;
      collapseRfIfIdle();
      return true;
    }

    uint8_t ackAttempts = 0U;
    if (waitForAck(sequence, &ackAttempts)) {
      lastArc_ = static_cast<uint8_t>(attempt + ackAttempts - 1U);
      collapseRfIfIdle();
      return true;
    }

    delayMicroseconds(static_cast<uint32_t>(retryDelaySetting_) * 250U + 250U);
  }

  lastArc_ = static_cast<uint8_t>(attemptsUsed - 1U);
  collapseRfIfIdle();
  return false;
}

bool nrf_to_nrf::writeFast(void* buf, uint8_t len, bool multicast) {
  return write(buf, len, multicast, true);
}

bool nrf_to_nrf::startWrite(void* buf, uint8_t len, bool multicast,
                            bool doEncryption) {
  (void)doEncryption;
  if (!initialized_ || poweredDown_ || buf == nullptr || len == 0U) {
    return false;
  }
  if (len > maxUserPayloadLength()) {
    return false;
  }
  if (!BoardControl::enableRfPath(kCompatAntennaPath)) {
    return false;
  }
  const bool ok = transmitFrame(kFrameTypeData, nextSequence_++,
                                static_cast<const uint8_t*>(buf), len, true);
  if (!multicast) {
    collapseRfIfIdle();
  }
  return ok;
}

bool nrf_to_nrf::txStandBy() { return true; }

bool nrf_to_nrf::txStandBy(uint32_t timeout, bool startTx) {
  (void)timeout;
  (void)startTx;
  return true;
}

void nrf_to_nrf::startListening(bool resetAddresses) {
  (void)resetAddresses;
  if (!initialized_ || poweredDown_) {
    return;
  }
  listening_ = true;
  clearBufferedPayload();
  (void)applyListeningPipe();
  (void)ensureListeningArmed();
}

void nrf_to_nrf::stopListening(bool setWritingPipe, bool resetAddresses) {
  (void)resetAddresses;
  listening_ = false;
  if (setWritingPipe) {
    (void)applyWritingPipe();
  }
  collapseRfIfIdle();
}

void nrf_to_nrf::stopListening(const uint8_t* txAddress, bool setWritingPipe,
                               bool resetAddresses) {
  if (txAddress != nullptr) {
    openWritingPipe(txAddress);
  }
  stopListening(setWritingPipe, resetAddresses);
}

void nrf_to_nrf::openReadingPipe(uint8_t child, const uint8_t* address) {
  if (child >= kMaxPipes || address == nullptr) {
    return;
  }
  readingPipes_[child].enabled = true;
  setAddressFromBytes(readingPipes_[child].address, address);
  if (activePipe_ == 0xFFU) {
    activePipe_ = child;
  }
  if (listening_ && child == activePipe_) {
    (void)applyListeningPipe();
  }
}

void nrf_to_nrf::openReadingPipe(uint8_t child, uint64_t address) {
  uint8_t bytes[kAddressWidth] = {0};
  setAddressFromUint64(bytes, address);
  openReadingPipe(child, bytes);
}

void nrf_to_nrf::openReadingPipe(uint8_t child, uint32_t base, uint32_t prefix) {
  uint8_t bytes[kAddressWidth] = {
      static_cast<uint8_t>(base & 0xFFU),
      static_cast<uint8_t>((base >> 8) & 0xFFU),
      static_cast<uint8_t>((base >> 16) & 0xFFU),
      static_cast<uint8_t>((base >> 24) & 0xFFU),
      static_cast<uint8_t>(prefix & 0xFFU),
  };
  openReadingPipe(child, bytes);
}

void nrf_to_nrf::openWritingPipe(const uint8_t* address) {
  if (address == nullptr) {
    return;
  }
  setAddressFromBytes(writingAddress_, address);
  writingPipeValid_ = true;
  if (!listening_) {
    (void)applyWritingPipe();
  }
}

void nrf_to_nrf::openWritingPipe(uint64_t address) {
  uint8_t bytes[kAddressWidth] = {0};
  setAddressFromUint64(bytes, address);
  openWritingPipe(bytes);
}

void nrf_to_nrf::openWritingPipe(uint32_t base, uint32_t prefix) {
  uint8_t bytes[kAddressWidth] = {
      static_cast<uint8_t>(base & 0xFFU),
      static_cast<uint8_t>((base >> 8) & 0xFFU),
      static_cast<uint8_t>((base >> 16) & 0xFFU),
      static_cast<uint8_t>((base >> 24) & 0xFFU),
      static_cast<uint8_t>(prefix & 0xFFU),
  };
  openWritingPipe(bytes);
}

bool nrf_to_nrf::writeAckPayload(uint8_t pipe, void* buf, uint8_t len) {
  if (pipe >= kMaxPipes || buf == nullptr || len > maxUserPayloadLength()) {
    return false;
  }
  ackPayloadPending_[pipe] = true;
  ackPayloadLength_[pipe] = len;
  memcpy(ackPayloadData_[pipe], buf, len);
  return true;
}

void nrf_to_nrf::enableAckPayload() { ackPayloadsEnabled_ = true; }

void nrf_to_nrf::disableAckPayload() { ackPayloadsEnabled_ = false; }

void nrf_to_nrf::enableDynamicAck() { dynamicAckEnabled_ = true; }

void nrf_to_nrf::enableDynamicPayloads(uint8_t payloadSize) {
  dynamicPayloadsEnabled_ = true;
  payloadSize_ = clampPayload(payloadSize);
  rawConfig_.maxPayloadLength = clampPayload(maxUserPayloadLength() + kHeaderSize);
  if (initialized_) {
    (void)configureLink();
  }
}

void nrf_to_nrf::disableDynamicPayloads() {
  dynamicPayloadsEnabled_ = false;
  payloadSize_ = clampPayload(payloadSize_);
  rawConfig_.maxPayloadLength = clampPayload(maxUserPayloadLength() + kHeaderSize);
  if (initialized_) {
    (void)configureLink();
  }
}

uint8_t nrf_to_nrf::getDynamicPayloadSize() { return lastPayloadLength_; }

bool nrf_to_nrf::isValid() { return initialized_; }

bool nrf_to_nrf::isChipConnected() { return isValid(); }

void nrf_to_nrf::setChannel(uint8_t channel, bool map) {
  (void)map;
  channel_ = channel;
  rawConfig_.frequencyOffsetMhz = channel_;
  if (initialized_) {
    (void)link_.setFrequencyOffsetMhz(channel_);
  }
}

uint8_t nrf_to_nrf::getChannel() { return channel_; }

void nrf_to_nrf::setAutoAck(bool enable) {
  for (uint8_t i = 0U; i < kMaxPipes; ++i) {
    autoAckEnabled_[i] = enable;
  }
}

void nrf_to_nrf::setAutoAck(uint8_t pipe, bool enable) {
  if (pipe < kMaxPipes) {
    autoAckEnabled_[pipe] = enable;
  }
}

void nrf_to_nrf::setPayloadSize(uint8_t size) {
  payloadSize_ = clampPayload(size);
  rawConfig_.maxPayloadLength = clampPayload(maxUserPayloadLength() + kHeaderSize);
  if (initialized_) {
    (void)configureLink();
  }
}

uint8_t nrf_to_nrf::getPayloadSize() {
  return dynamicPayloadsEnabled_ ? rxLength_ : payloadSize_;
}

void nrf_to_nrf::setRetries(uint8_t retryVar, uint8_t attempts) {
  retryDelaySetting_ = retryVar;
  retryCountSetting_ = attempts;
}

bool nrf_to_nrf::setDataRate(uint8_t speed) {
  return (speed == NRF_1MBPS);
}

void nrf_to_nrf::setPALevel(uint8_t level, bool lnaEnable) {
  (void)lnaEnable;
  paLevel_ = (level <= NRF_PA_MAX) ? level : NRF_PA_LOW;
  rawConfig_.txPowerDbm = paLevelToDbm(paLevel_);
  if (initialized_) {
    (void)link_.setTxPowerDbm(rawConfig_.txPowerDbm);
  }
}

uint8_t nrf_to_nrf::getPALevel() { return paLevel_; }

uint8_t nrf_to_nrf::getARC() { return lastArc_; }

void nrf_to_nrf::powerUp() {
  if (!initialized_ || !poweredDown_) {
    return;
  }
  poweredDown_ = false;
  if (!configureLink()) {
    poweredDown_ = true;
    return;
  }
  if (listening_) {
    startListening();
  } else {
    collapseRfIfIdle();
  }
}

void nrf_to_nrf::powerDown() {
  if (initialized_) {
    link_.end();
  }
  poweredDown_ = true;
  collapseRfIfIdle();
}

bool nrf_to_nrf::applyPipeAddress(const uint8_t* address) {
  uint32_t base = 0U;
  uint8_t prefix = 0U;
  addressToBasePrefix(address, &base, &prefix);
  return link_.setPipe(base, prefix);
}

bool nrf_to_nrf::applyWritingPipe() {
  if (!writingPipeValid_) {
    return false;
  }
  return applyPipeAddress(writingAddress_);
}

bool nrf_to_nrf::applyListeningPipe() {
  activePipe_ = selectListeningPipe();
  if (activePipe_ >= kMaxPipes || !readingPipes_[activePipe_].enabled) {
    return false;
  }
  if (!BoardControl::enableRfPath(kCompatAntennaPath)) {
    return false;
  }
  if (!applyPipeAddress(readingPipes_[activePipe_].address)) {
    return false;
  }
  return true;
}

uint8_t nrf_to_nrf::selectListeningPipe() const {
  for (uint8_t i = 0U; i < kMaxPipes; ++i) {
    if (readingPipes_[i].enabled) {
      return i;
    }
  }
  return 0xFFU;
}

bool nrf_to_nrf::ensureListeningArmed() {
  if (!listening_) {
    return false;
  }
  if (!applyListeningPipe()) {
    return false;
  }
  if (link_.receiverArmed()) {
    return true;
  }
  return link_.armReceive();
}

bool nrf_to_nrf::pollRadio() {
  if (!ensureListeningArmed()) {
    return false;
  }

  RawRadioPacket packet{};
  const RawRadioReceiveStatus status = link_.pollReceive(&packet);
  if (status == RawRadioReceiveStatus::kIdle) {
    return false;
  }
  if (status == RawRadioReceiveStatus::kPacket) {
    return processIncomingFrame(packet, false);
  }
  if (status == RawRadioReceiveStatus::kCrcError) {
    (void)ensureListeningArmed();
    return false;
  }
  return false;
}

bool nrf_to_nrf::processIncomingFrame(const RawRadioPacket& packet,
                                      bool fromAckWait) {
  uint8_t frameType = 0U;
  uint8_t pipe = 0U;
  uint8_t sequence = 0U;
  const uint8_t* payload = nullptr;
  uint8_t payloadLength = 0U;
  if (!parseFrame(packet.payload, packet.length, &frameType, &pipe, &sequence,
                  &payload, &payloadLength)) {
    if (listening_ && !fromAckWait) {
      (void)ensureListeningArmed();
    }
    return false;
  }

  if (frameType == kFrameTypeAck) {
    rxPipe_ = pipe;
    rxLength_ = payloadLength;
    lastPayloadLength_ = payloadLength;
    rxAvailable_ = (payloadLength > 0U);
    if (payloadLength > 0U) {
      memcpy(rxPayload_, payload, payloadLength);
      memcpy(radioData, payload, payloadLength);
    }
    if (listening_ && !fromAckWait) {
      (void)ensureListeningArmed();
    }
    return true;
  }

  if (frameType != kFrameTypeData) {
    if (listening_ && !fromAckWait) {
      (void)ensureListeningArmed();
    }
    return false;
  }

  rxPipe_ = activePipe_;
  rxLength_ = payloadLength;
  lastPayloadLength_ = payloadLength;
  rxAvailable_ = true;
  memcpy(rxPayload_, payload, payloadLength);
  memcpy(radioData, payload, payloadLength);

  if (listening_ && autoAckEnabled_[activePipe_]) {
    (void)sendAckForPipe(activePipe_, sequence);
    (void)ensureListeningArmed();
  } else if (listening_ && !fromAckWait) {
    (void)ensureListeningArmed();
  }

  return true;
}

bool nrf_to_nrf::sendAckForPipe(uint8_t pipe, uint8_t sequence) {
  const bool havePayload = ackPayloadsEnabled_ && pipe < kMaxPipes &&
                           ackPayloadPending_[pipe];
  const uint8_t* payload = havePayload ? ackPayloadData_[pipe] : nullptr;
  const uint8_t length = havePayload ? ackPayloadLength_[pipe] : 0U;

  if (!BoardControl::enableRfPath(kCompatAntennaPath)) {
    return false;
  }
  const bool ok = transmitFrame(kFrameTypeAck, sequence, payload, length, true);
  if (havePayload && ok) {
    ackPayloadPending_[pipe] = false;
    ackPayloadLength_[pipe] = 0U;
  }
  return ok;
}

bool nrf_to_nrf::waitForAck(uint8_t sequence, uint8_t* outAttemptsUsed) {
  if (outAttemptsUsed != nullptr) {
    *outAttemptsUsed = 0U;
  }
  if (selectListeningPipe() == 0xFFU) {
    return false;
  }
  if (!applyListeningPipe()) {
    return false;
  }

  RawRadioPacket packet{};
  const RawRadioReceiveStatus status =
      link_.waitForReceive(&packet, ackTimeoutUs());
  if (outAttemptsUsed != nullptr) {
    *outAttemptsUsed = 1U;
  }
  if (status != RawRadioReceiveStatus::kPacket) {
    return false;
  }

  uint8_t frameType = 0U;
  uint8_t pipe = 0U;
  uint8_t ackSequence = 0U;
  const uint8_t* payload = nullptr;
  uint8_t payloadLength = 0U;
  if (!parseFrame(packet.payload, packet.length, &frameType, &pipe, &ackSequence,
                  &payload, &payloadLength)) {
    return false;
  }
  if (frameType != kFrameTypeAck || ackSequence != sequence) {
    return false;
  }

  rxPipe_ = pipe;
  rxLength_ = payloadLength;
  rxAvailable_ = (payloadLength > 0U);
  if (payloadLength > 0U) {
    memcpy(rxPayload_, payload, payloadLength);
    memcpy(radioData, payload, payloadLength);
  }
  return true;
}

bool nrf_to_nrf::transmitFrame(uint8_t frameType, uint8_t sequence,
                               const uint8_t* payload, uint8_t payloadLength,
                               bool forceWritingPipe) {
  uint8_t frame[ACTUAL_MAX_PAYLOAD_SIZE + kHeaderSize] = {0};
  uint8_t frameLength = 0U;
  const uint8_t pipe = (frameType == kFrameTypeAck) ? activePipe_ : 0U;
  if (!buildFrame(frameType, pipe, sequence, payload, payloadLength, frame,
                  &frameLength)) {
    return false;
  }
  if (forceWritingPipe && !applyWritingPipe()) {
    return false;
  }
  return link_.transmit(frame, frameLength);
}

bool nrf_to_nrf::buildFrame(uint8_t frameType, uint8_t pipe, uint8_t sequence,
                            const uint8_t* payload, uint8_t payloadLength,
                            uint8_t* outFrame, uint8_t* outLength) const {
  if (outFrame == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }
  if (payloadLength > maxUserPayloadLength()) {
    return false;
  }

  outFrame[0] = kFrameMagic0;
  outFrame[1] = kFrameMagic1;
  outFrame[2] = frameType;
  outFrame[3] = pipe;
  outFrame[4] = sequence;
  outFrame[5] = payloadLength;
  if (payloadLength > 0U) {
    memcpy(&outFrame[kHeaderSize], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderSize + payloadLength);
  return true;
}

bool nrf_to_nrf::parseFrame(const uint8_t* frame, uint8_t frameLength,
                            uint8_t* outType, uint8_t* outPipe,
                            uint8_t* outSequence, const uint8_t** outPayload,
                            uint8_t* outPayloadLength) const {
  if (frame == nullptr || frameLength < kHeaderSize) {
    return false;
  }
  if (frame[0] != kFrameMagic0 || frame[1] != kFrameMagic1) {
    return false;
  }
  const uint8_t payloadLength = frame[5];
  if (static_cast<uint8_t>(kHeaderSize + payloadLength) != frameLength) {
    return false;
  }
  if (outType != nullptr) {
    *outType = frame[2];
  }
  if (outPipe != nullptr) {
    *outPipe = frame[3];
  }
  if (outSequence != nullptr) {
    *outSequence = frame[4];
  }
  if (outPayload != nullptr) {
    *outPayload = &frame[kHeaderSize];
  }
  if (outPayloadLength != nullptr) {
    *outPayloadLength = payloadLength;
  }
  return true;
}

void nrf_to_nrf::setAddressFromBytes(uint8_t* outAddress,
                                     const uint8_t* address) {
  if (outAddress == nullptr || address == nullptr) {
    return;
  }
  memcpy(outAddress, address, kAddressWidth);
}

void nrf_to_nrf::setAddressFromUint64(uint8_t* outAddress, uint64_t address) {
  if (outAddress == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < kAddressWidth; ++i) {
    outAddress[i] = static_cast<uint8_t>((address >> (8U * i)) & 0xFFU);
  }
}

void nrf_to_nrf::addressToBasePrefix(const uint8_t* address, uint32_t* outBase,
                                     uint8_t* outPrefix) {
  if (address == nullptr || outBase == nullptr || outPrefix == nullptr) {
    return;
  }
  *outBase = static_cast<uint32_t>(address[0]) |
             (static_cast<uint32_t>(address[1]) << 8U) |
             (static_cast<uint32_t>(address[2]) << 16U) |
             (static_cast<uint32_t>(address[3]) << 24U);
  *outPrefix = address[4];
}

int8_t nrf_to_nrf::paLevelToDbm(uint8_t level) {
  switch (level) {
    case NRF_PA_MIN:
      return -12;
    case NRF_PA_LOW:
      return 2;
    case NRF_PA_HIGH:
      return 6;
    case NRF_PA_MAX:
      return 8;
    default:
      return -8;
  }
}

uint32_t nrf_to_nrf::ackTimeoutUs() const {
  return ACK_TIMEOUT_1MBPS + static_cast<uint32_t>(retryDelaySetting_) * 250U;
}

uint8_t nrf_to_nrf::maxUserPayloadLength() const {
  return dynamicPayloadsEnabled_ ? ACTUAL_MAX_PAYLOAD_SIZE : payloadSize_;
}

void nrf_to_nrf::clearBufferedPayload() {
  rxAvailable_ = false;
  rxPipe_ = 0U;
  memset(rxPayload_, 0, sizeof(rxPayload_));
}

void nrf_to_nrf::collapseRfIfIdle() {
  if (!listening_) {
    BoardControl::collapseRfPathIdle();
  }
}
