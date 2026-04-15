#pragma once

#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "nrf54l15_vpr_transport_shared.h"

namespace xiao_nrf54l15 {

enum class VprSleepState : uint8_t {
  kWait = VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_WAIT,
  kReset = VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_RESET,
  kSleep = VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_SLEEP,
  kDeepSleep = VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_DEEPSLEEP,
  kHibernate = VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_HIBERNATE,
};

class CtrlApMailbox {
 public:
  static void clearEvents();
  static bool pollRxReady(bool clearEvent = true);
  static bool pollTxDone(bool clearEvent = true);
  static bool rxPending();
  static bool txPending();
  static bool read(uint32_t* value);
  static bool write(uint32_t value);
  static void enableInterrupts(bool rxReady, bool txDone);

 private:
  static NRF_CTRLAPPERI_Type* regs();
};

class VprControl {
 public:
  static bool setInitPc(uint32_t address);
  static uint32_t initPc();
  static void prepareForLaunch();
  static void run();
  static bool start(uint32_t address);
  static void stop();
  static bool isRunning();
  static bool secureAccessEnabled();
  static uint32_t spuPerm();
  static void enableRtPeripherals();
  static void configureDataAccessNonCacheable();
  static void clearCache();
  static void clearBufferedSignals();
  static void clearDebugHaltState();
  static uint32_t debugStatus();
  static uint32_t haltSummary0();
  static uint32_t haltSummary1();
  static uint32_t rawNordicAxCache();
  static uint32_t rawNordicTasks();
  static uint32_t rawNordicEvents();
  static uint32_t rawNordicEventStatus();
  static uint32_t rawSleepControl();
  static uint32_t rawNordicCacheCtrl();
  static uint32_t rawMpcMemAccErrEvent();
  static uint32_t rawMpcMemAccErrAddress();
  static uint32_t rawMpcMemAccErrInfo();
  static void clearMpcMemAccErr();
  static bool configureSleepControl(VprSleepState state,
                                    bool returnToSleep = false,
                                    bool stackOnSleep = false);
  static VprSleepState sleepState();
  static bool returnToSleepEnabled();
  static bool stackOnSleepEnabled();
  static bool enableContextRestore(bool enable = true);
  static bool contextRestoreEnabled();
  static bool hartResetPulse(uint32_t spinLimit = 100000UL);
  static bool restartAfterHibernateReset();
  static bool resumeRetainedContext();
  static uint32_t rawMemconfPower0Ret2();
  static uint32_t rawMemconfPower1Ret();
  static uint32_t savedContextAddress();
  static uint32_t savedContextSize();
  static bool clearSavedContext();
  static bool readSavedContext(void* buffer, size_t len, size_t offset = 0U);

  static bool triggerTask(uint8_t index);
  static bool pollEvent(uint8_t index, bool clearEvent = true);
  static bool clearEvent(uint8_t index);
  static bool enableEventInterrupt(uint8_t index, bool enable = true);

 private:
  static NRF_VPR_Type* regs();
  static bool validTriggerIndex(uint8_t index);
};

class VprSharedTransportStream : public Stream {
 public:
  VprSharedTransportStream();

  bool resetSharedState(bool clearScripts = true);
  bool clearScripts();
  bool addScriptResponse(uint16_t opcode, const uint8_t* response, size_t len);
  bool loadFirmware(const uint8_t* image, size_t len);
  bool bootLoadedFirmware();
  bool loadFirmwareAndStart(const uint8_t* image, size_t len);
  bool loadDefaultCsTransportStubImage();
  bool loadDefaultCsTransportStub();
  bool loadDefaultCsControllerStubImage();
  bool loadDefaultCsControllerStub();
  bool restartLoadedFirmware(bool clearScripts = true, uint32_t spinLimit = 100000UL);
  bool restartAfterHibernateReset(uint32_t spinLimit = 100000UL);
  bool resumeRetainedService(uint32_t spinLimit = 100000UL);
  bool retainedHibernateStatePending() const;
  void clearRetainedHibernateState();
  bool recoverAfterHibernateFailure(bool clearScripts = false,
                                    uint32_t spinLimit = 100000UL);
  size_t writeWakeRequest(const uint8_t* buffer, size_t len);
  void stop();
  bool waitReady(uint32_t spinLimit = 100000UL);
  bool poll();

  uint32_t heartbeat() const;
  uint16_t lastOpcode() const;
  uint32_t transportStatus() const;
  uint32_t lastError() const;
  uint32_t reservedState() const;
  uint32_t reservedAuxState() const;
  uint32_t reservedMetaState() const;
  uint32_t reservedConfigState() const;
  uint32_t initPc() const;
  bool isRunning() const;
  bool secureAccessEnabled() const;
  uint32_t spuPerm() const;
  uint32_t debugStatus() const;
  uint32_t haltSummary0() const;
  uint32_t haltSummary1() const;
  uint32_t rawNordicTasks() const;
  uint32_t rawNordicEvents() const;
  uint32_t rawNordicEventStatus() const;
  uint32_t rawSleepControl() const;
  uint32_t rawNordicCacheCtrl() const;
  uint32_t rawMpcMemAccErrEvent() const;
  uint32_t rawMpcMemAccErrAddress() const;
  uint32_t rawMpcMemAccErrInfo() const;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t len) override;
  using Print::write;

 private:
  size_t writeInternal(const uint8_t* buffer, size_t len, bool allowDormantWake);
  bool pullResponse();
  volatile Nrf54l15VprTransportHostShared* hostShared() const;
  volatile Nrf54l15VprTransportVprShared* vprShared() const;

  uint8_t rxBuffer_[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA];
  size_t rxLen_;
  size_t rxIndex_;
};

struct VprControllerServiceInfo {
  uint8_t transportStatus;
  uint8_t transportError;
  uint8_t transportFlags;
  uint32_t heartbeat;
  uint32_t scriptCount;
};

struct VprControllerServiceCapabilities {
  uint8_t serviceVersionMajor;
  uint8_t serviceVersionMinor;
  uint32_t opMask;
  uint32_t maxInputLen;
};

struct VprTickerState {
  bool enabled;
  uint32_t periodTicks;
  uint32_t step;
  uint32_t count;
};

struct VprTickerEvent {
  uint8_t flags;
  uint32_t count;
  uint32_t step;
  uint32_t heartbeat;
  uint32_t sequence;
};

struct VprBleLegacyAdvertisingState {
  bool enabled;
  bool addRandomDelay;
  uint8_t channelMask;
  uint8_t lastChannelMask;
  uint32_t intervalTicks;
  uint32_t lastRandomDelayTicks;
  uint32_t eventCount;
  uint32_t droppedEvents;
};

struct VprBleLegacyAdvertisingData {
  uint8_t length;
  uint8_t bytes[31];
};

struct VprBleLegacyAdvertisingEvent {
  uint8_t flags;
  uint8_t channelMask;
  uint32_t eventCount;
  uint32_t heartbeat;
  uint32_t randomDelayTicks;
  uint32_t sequence;
};

struct VprBleConnectionState {
  bool connected;
  uint16_t connHandle;
  uint8_t role;
  bool encrypted;
  uint16_t intervalUnits;
  uint16_t latency;
  uint16_t supervisionTimeout;
  uint8_t txPhy;
  uint8_t rxPhy;
  uint32_t eventCount;
  uint32_t disconnectCount;
};

struct VprBleConnectionSharedState {
  bool hostRequestPending;
  bool restoredFromHibernate;
  bool connected;
  bool encrypted;
  bool csLinkBound;
  bool csLinkRunnable;
  bool csWorkflowConfigured;
  bool csWorkflowEnabled;
  uint16_t connHandle;
  uint8_t role;
  uint16_t intervalUnits;
  uint16_t latency;
  uint16_t supervisionTimeout;
  uint8_t txPhy;
  uint8_t rxPhy;
  uint8_t lastEventFlags;
  uint8_t lastDisconnectReason;
  uint16_t eventCount;
};

struct VprBleConnectionEvent {
  uint8_t flags;
  uint16_t connHandle;
  uint8_t reason;
  uint8_t role;
  bool encrypted;
  uint16_t intervalUnits;
  uint16_t latency;
  uint16_t supervisionTimeout;
  uint8_t txPhy;
  uint8_t rxPhy;
  uint32_t eventCount;
  uint32_t disconnectCount;
  uint32_t sequence;
};

struct VprBleCsLinkState {
  bool bound;
  bool runnable;
  bool connected;
  bool encrypted;
  uint16_t connHandle;
  uint8_t role;
  uint32_t eventCount;
};

struct VprBleCsWorkflowState {
  bool linkBound;
  bool linkRunnable;
  bool configured;
  bool enabled;
  bool running;
  bool completed;
  bool connected;
  bool encrypted;
  uint16_t connHandle;
  uint8_t role;
  uint8_t configId;
  uint8_t maxProcedureCount;
  uint32_t eventCount;
  uint8_t completedProcedureCount;
  uint8_t completedConfigId;
  uint16_t nominalDistanceQ4;
  uint32_t workflowEventCount;
  uint8_t completedLocalSubeventCount;
  uint8_t completedPeerSubeventCount;
  uint8_t completedLocalStepCount;
  uint8_t completedPeerStepCount;
  uint8_t completedLocalMode1Count;
  uint8_t completedPeerMode1Count;
  uint8_t completedLocalMode2Count;
  uint8_t completedPeerMode2Count;
};

struct VprBleConnectedCsWorkflowConfig {
  uint16_t connHandle;
  uint8_t role;
  bool encrypted;
  uint16_t intervalUnits;
  uint16_t latency;
  uint16_t supervisionTimeout;
  uint8_t txPhy;
  uint8_t rxPhy;
  uint8_t configId;
  bool defaultsApplied;
  bool createConfig;
  bool securityEnabled;
  bool procedureParamsApplied;
  bool procedureEnabled;
  uint8_t maxProcedureCount;
};

struct VprBleConnectedCsWorkflowRunState {
  VprBleConnectionState configuredConnection{};
  VprBleConnectionEvent connectEvent{};
  VprBleConnectionSharedState connectedShared{};
  VprBleCsLinkState linkState{};
  VprBleCsWorkflowState startedWorkflow{};
  VprBleCsWorkflowState completedWorkflow{};
  VprBleConnectionSharedState finalShared{};
  VprBleCsWorkflowState finalWorkflow{};
};

class VprControllerServiceHost {
 public:
  static constexpr size_t kPendingTickerEventQueueDepth = 8U;
  static constexpr size_t kPendingBleLegacyAdvertisingEventQueueDepth = 8U;
  static constexpr size_t kPendingBleConnectionEventQueueDepth = 8U;
  static constexpr size_t kPendingH4EventQueueDepth = 8U;
  static constexpr size_t kPendingH4EventMaxBytes =
      NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA;
  static constexpr uint8_t kTransportFlagRestoredFromHibernate = 0x80U;
  static constexpr uint16_t kVendorPingOpcode = 0xFCF0U;
  static constexpr uint16_t kVendorInfoOpcode = 0xFCF1U;
  static constexpr uint16_t kVendorFnv1a32Opcode = 0xFCF2U;
  static constexpr uint16_t kVendorCapabilitiesOpcode = 0xFCF3U;
  static constexpr uint16_t kVendorCrc32Opcode = 0xFCF4U;
  static constexpr uint16_t kVendorCrc32cOpcode = 0xFCF5U;
  static constexpr uint16_t kVendorTickerConfigureOpcode = 0xFCF6U;
  static constexpr uint16_t kVendorTickerReadStateOpcode = 0xFCF7U;
  static constexpr uint16_t kVendorEnterHibernateOpcode = 0xFCF8U;
  static constexpr uint16_t kVendorTickerEventConfigureOpcode = 0xFCF9U;
  static constexpr uint16_t kVendorBleLegacyAdvertisingConfigureOpcode = 0xFCFAU;
  static constexpr uint16_t kVendorBleLegacyAdvertisingReadStateOpcode = 0xFCFBU;
  static constexpr uint16_t kVendorBleLegacyAdvertisingWriteDataOpcode = 0xFCFCU;
  static constexpr uint16_t kVendorBleLegacyAdvertisingReadDataOpcode = 0xFCFDU;
  static constexpr uint16_t kVendorBleConnectionConfigureOpcode = 0xFCE0U;
  static constexpr uint16_t kVendorBleConnectionReadStateOpcode = 0xFCE1U;
  static constexpr uint16_t kVendorBleConnectionDisconnectOpcode = 0xFCE2U;
  static constexpr uint16_t kVendorBleCsLinkConfigureOpcode = 0xFCE3U;
  static constexpr uint16_t kVendorBleCsLinkReadStateOpcode = 0xFCE4U;
  static constexpr uint16_t kVendorBleCsWorkflowConfigureOpcode = 0xFCE5U;
  static constexpr uint16_t kVendorBleCsWorkflowReadStateOpcode = 0xFCE6U;
  static constexpr uint8_t kVendorEventCode = 0xFFU;
  static constexpr uint8_t kVendorEventTicker = 0xA0U;
  static constexpr uint8_t kVendorEventBleLegacyAdvertising = 0xA1U;
  static constexpr uint8_t kVendorEventBleConnection = 0xA2U;
  static constexpr uint32_t kOpPing = (1UL << 0U);
  static constexpr uint32_t kOpInfo = (1UL << 1U);
  static constexpr uint32_t kOpFnv1a32 = (1UL << 2U);
  static constexpr uint32_t kOpCapabilities = (1UL << 3U);
  static constexpr uint32_t kOpCrc32 = (1UL << 4U);
  static constexpr uint32_t kOpCrc32c = (1UL << 5U);
  static constexpr uint32_t kOpTickerConfigure = (1UL << 6U);
  static constexpr uint32_t kOpTickerReadState = (1UL << 7U);
  static constexpr uint32_t kOpEnterHibernate = (1UL << 8U);
  static constexpr uint32_t kOpTickerEventConfigure = (1UL << 9U);
  static constexpr uint32_t kOpBleLegacyAdvertisingConfigure = (1UL << 10U);
  static constexpr uint32_t kOpBleLegacyAdvertisingReadState = (1UL << 11U);
  static constexpr uint32_t kOpBleLegacyAdvertisingEvent = (1UL << 12U);
  static constexpr uint32_t kOpBleLegacyAdvertisingWriteData = (1UL << 13U);
  static constexpr uint32_t kOpBleLegacyAdvertisingReadData = (1UL << 14U);
  static constexpr uint32_t kOpBleConnectionConfigure = (1UL << 15U);
  static constexpr uint32_t kOpBleConnectionReadState = (1UL << 16U);
  static constexpr uint32_t kOpBleConnectionEvent = (1UL << 17U);
  static constexpr uint32_t kOpBleCsLinkConfigure = (1UL << 18U);
  static constexpr uint32_t kOpBleCsLinkReadState = (1UL << 19U);
  static constexpr uint32_t kOpBleCsWorkflowConfigure = (1UL << 20U);
  static constexpr uint32_t kOpBleCsWorkflowReadState = (1UL << 21U);

  explicit VprControllerServiceHost(VprSharedTransportStream* transport = nullptr);

  void attach(VprSharedTransportStream* transport);
  bool attached() const;
  bool bootDefaultService(bool rebootTransport = true);
  bool restartLoadedService(bool clearScripts = true);
  bool restartAfterHibernateReset(uint32_t spinLimit = 100000UL);
  bool recoverAfterHibernateFailure(bool clearScripts = false,
                                    uint32_t spinLimit = 100000UL);

  bool sendHciCommand(uint16_t opcode,
                      const uint8_t* params,
                      size_t paramsLen,
                      uint8_t* response,
                      size_t responseSize,
                      size_t* responseLen);
  bool ping(uint32_t cookie,
            uint32_t* echoedCookie = nullptr,
            uint32_t* heartbeat = nullptr);
  bool readTransportInfo(VprControllerServiceInfo* info);
  bool readCapabilities(VprControllerServiceCapabilities* caps);
  bool hashFnv1a32(const uint8_t* data,
                   size_t len,
                   uint32_t* hash,
                   uint32_t* processedLen = nullptr);
  bool crc32(const uint8_t* data,
             size_t len,
             uint32_t* crc,
             uint32_t* processedLen = nullptr);
  bool crc32c(const uint8_t* data,
              size_t len,
              uint32_t* crc,
              uint32_t* processedLen = nullptr);
  bool configureTicker(bool enabled,
                       uint32_t periodTicks,
                       uint32_t step,
                       VprTickerState* state = nullptr);
  bool readTickerState(VprTickerState* state);
  bool configureTickerEvents(bool enabled,
                             uint32_t emitEveryCount,
                             uint32_t* appliedEmitEveryCount = nullptr,
                             uint32_t* droppedEvents = nullptr);
  bool waitTickerEvent(VprTickerEvent* event, uint32_t timeoutMs = 5000UL);
  bool configureBleLegacyAdvertising(bool enabled,
                                     uint32_t intervalTicks,
                                     uint8_t channelMask,
                                     bool addRandomDelay,
                                     VprBleLegacyAdvertisingState* state = nullptr);
  bool readBleLegacyAdvertisingState(VprBleLegacyAdvertisingState* state);
  bool writeBleLegacyAdvertisingData(const uint8_t* data,
                                     size_t len,
                                     VprBleLegacyAdvertisingData* applied = nullptr);
  bool readBleLegacyAdvertisingData(VprBleLegacyAdvertisingData* data);
  bool waitBleLegacyAdvertisingEvent(VprBleLegacyAdvertisingEvent* event,
                                     uint32_t timeoutMs = 5000UL);
  bool configureBleConnection(uint16_t connHandle,
                              uint8_t role,
                              bool encrypted,
                              uint16_t intervalUnits,
                              uint16_t latency,
                              uint16_t supervisionTimeout,
                              uint8_t txPhy,
                              uint8_t rxPhy,
                              VprBleConnectionState* state = nullptr);
  bool readBleConnectionState(VprBleConnectionState* state);
  bool disconnectBleConnection(uint16_t connHandle,
                               uint8_t reason,
                               VprBleConnectionState* state = nullptr);
  bool readBleConnectionSharedState(VprBleConnectionSharedState* state);
  bool configureBleCsLink(bool bound,
                          uint16_t connHandle,
                          VprBleCsLinkState* state = nullptr);
  bool readBleCsLinkState(VprBleCsLinkState* state);
  bool configureBleCsWorkflow(uint8_t configId,
                              bool defaultsApplied,
                              bool createConfig,
                              bool securityEnabled,
                              bool procedureParamsApplied,
                              bool procedureEnabled,
                              uint8_t maxProcedureCount,
                              VprBleCsWorkflowState* state = nullptr);
  bool readBleCsWorkflowState(VprBleCsWorkflowState* state);
  bool beginFreshBleConnectedCsWorkflow(
      const VprBleConnectedCsWorkflowConfig& config,
      VprBleConnectedCsWorkflowRunState* state = nullptr,
      bool rebootService = true,
      uint32_t timeoutMs = 5000UL);
  bool disconnectBleConnectionAndWait(
      uint16_t connHandle,
      uint8_t reason,
      VprBleConnectionSharedState* state = nullptr,
      uint32_t timeoutMs = 5000UL);
  bool runFreshBleConnectedCsWorkflow(
      const VprBleConnectedCsWorkflowConfig& config,
      uint8_t disconnectReason,
      VprBleConnectedCsWorkflowRunState* state = nullptr,
      bool rebootService = true,
      uint32_t timeoutMs = 5000UL);
  bool waitBleCsWorkflowCompleted(uint8_t minCompletedProcedureCount,
                                  VprBleCsWorkflowState* state = nullptr,
                                  uint32_t timeoutMs = 5000UL);
  bool waitBleConnectionSharedState(bool connected,
                                    uint16_t minEventCount,
                                    VprBleConnectionSharedState* state = nullptr,
                                    uint32_t timeoutMs = 5000UL);
  bool waitBleConnectionEvent(VprBleConnectionEvent* event,
                              uint32_t timeoutMs = 5000UL);
  bool popPendingH4Event(uint8_t* packet, size_t packetSize, size_t* packetLen);
  uint32_t pendingH4EventDropCount() const;
  uint32_t pendingTickerEventDropCount() const;
  uint32_t pendingBleLegacyAdvertisingEventDropCount() const;
  uint32_t pendingBleConnectionEventDropCount() const;
  bool enterHibernate();
  bool probe(uint32_t cookie,
             VprControllerServiceInfo* info = nullptr,
             uint32_t* echoedCookie = nullptr,
             uint32_t* heartbeat = nullptr);

 private:
  bool readH4Event(uint8_t* packet,
                   size_t packetSize,
                   size_t* packetLen,
                   uint32_t timeoutMs = 5000UL);
  static bool parseCommandComplete(const uint8_t* packet,
                                   size_t packetLen,
                                   uint16_t expectedOpcode,
                                   const uint8_t** payload,
                                   size_t* payloadLen);
  static bool parseCommandStatus(const uint8_t* packet,
                                 size_t packetLen,
                                 uint16_t expectedOpcode,
                                 const uint8_t** payload,
                                 size_t* payloadLen);
  static bool parseVendorEvent(const uint8_t* packet,
                               size_t packetLen,
                               uint8_t expectedSubevent,
                               const uint8_t** payload,
                               size_t* payloadLen);
  void clearPendingEvents();
  bool pushPendingH4Event(const uint8_t* packet, size_t packetLen);
  bool pushPendingTickerEvent(const VprTickerEvent& event);
  bool pushPendingBleLegacyAdvertisingEvent(
      const VprBleLegacyAdvertisingEvent& event);
  bool pushPendingBleConnectionEvent(const VprBleConnectionEvent& event);
  bool stashAsyncEvent(const uint8_t* packet, size_t packetLen);
  bool popPendingTickerEvent(VprTickerEvent* event);
  bool popPendingBleLegacyAdvertisingEvent(VprBleLegacyAdvertisingEvent* event);
  bool popPendingBleConnectionEvent(VprBleConnectionEvent* event);
  static uint32_t readLe32(const uint8_t* data);

  VprSharedTransportStream* transport_;
  uint8_t pendingH4Events_[kPendingH4EventQueueDepth][kPendingH4EventMaxBytes];
  size_t pendingH4EventLens_[kPendingH4EventQueueDepth];
  size_t pendingH4EventHead_;
  size_t pendingH4EventTail_;
  size_t pendingH4EventCount_;
  uint32_t pendingH4EventDropped_;
  VprTickerEvent pendingTickerEvents_[kPendingTickerEventQueueDepth];
  size_t pendingTickerEventHead_;
  size_t pendingTickerEventTail_;
  size_t pendingTickerEventCount_;
  uint32_t pendingTickerEventDropped_;
  VprBleLegacyAdvertisingEvent
      pendingBleLegacyAdvertisingEvents_[kPendingBleLegacyAdvertisingEventQueueDepth];
  size_t pendingBleLegacyAdvertisingEventHead_;
  size_t pendingBleLegacyAdvertisingEventTail_;
  size_t pendingBleLegacyAdvertisingEventCount_;
  uint32_t pendingBleLegacyAdvertisingEventDropped_;
  VprBleConnectionEvent
      pendingBleConnectionEvents_[kPendingBleConnectionEventQueueDepth];
  size_t pendingBleConnectionEventHead_;
  size_t pendingBleConnectionEventTail_;
  size_t pendingBleConnectionEventCount_;
  uint32_t pendingBleConnectionEventDropped_;
};

}  // namespace xiao_nrf54l15
