#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nrf54l15_regs.h"
#include "xiao_nrf54l15_pins.h"

extern "C" void nrf54l15_clean_ble_idle_service(void);
extern "C" void nrf54l15_clean_ble_yield_service(void);
extern "C" uint32_t nrf54l15_clean_ble_idle_sleep_cap_us(void);

namespace xiao_nrf54l15 {

extern "C" void nrf54l15_ble_idle_service(void);
extern "C" void nrf54l15_ble_grtc_irq_service(void);
extern "C" void nrf54l15_ble_clock_irq_service(void);
extern "C" uint32_t nrf54l15_ble_grtc_reserved_cc_mask(void);
extern "C" void PendSV_Handler(void);

uint64_t hardwareUniqueId64();
uint64_t zigbeeFactoryEui64();

enum class CpuFrequency : uint32_t {
  k64MHz = 64000000UL,
  k128MHz = 128000000UL,
};

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
  static bool setCpuFrequency(CpuFrequency frequency);
  static CpuFrequency cpuFrequency();
  static bool enableIdleCpuScaling(CpuFrequency idleFrequency = CpuFrequency::k64MHz);
  static void disableIdleCpuScaling();
  static bool idleCpuScalingEnabled();
  static CpuFrequency idleCpuFrequency();
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
  bool setFrequency(uint32_t hz);
  void end();

  bool transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                uint32_t spinLimit = 2000000UL);

 private:
  uint32_t base_;
  uint32_t coreClockHz_;
  Pin cs_;
};

class Spis {
 public:
  explicit Spis(uint32_t base = nrf54l15::SPIS00_BASE);

  bool begin(const Pin& sck, const Pin& mosi, const Pin& miso, const Pin& csn,
             SpiMode mode = SpiMode::kMode0, bool lsbFirst = false,
             uint8_t defaultChar = 0xFFU, uint8_t overReadChar = 0x00U,
             bool autoAcquireAfterEnd = true);
  bool acquire(uint32_t spinLimit = 200000UL);
  bool setBuffers(uint8_t* rx, size_t rxLen, const uint8_t* tx, size_t txLen,
                  uint32_t spinLimit = 200000UL);
  bool releaseTransaction();
  bool pollAcquired(bool clearEvent = true);
  bool pollEnd(bool clearEvent = true);
  size_t receivedBytes() const;
  size_t transmittedBytes() const;
  bool overflowed() const;
  bool overread() const;
  void clearStatus();
  void end();

 private:
  NRF_SPIS_Type* spis_;
  bool active_;
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
  bool setFrequency(TwimFrequency frequency);
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
  uint32_t ccValue(uint8_t channel) const;
  bool pollCompare(uint8_t channel, bool clearEvent = true);
  volatile uint32_t* publishCompareConfigRegister(uint8_t channel) const;
  volatile uint32_t* subscribeStartConfigRegister() const;
  volatile uint32_t* subscribeStopConfigRegister() const;
  volatile uint32_t* subscribeCountConfigRegister() const;
  volatile uint32_t* subscribeClearConfigRegister() const;
  volatile uint32_t* subscribeCaptureConfigRegister(uint8_t channel) const;

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
  enum class DecoderLoad : uint8_t {
    kCommon = nrf54l15::pwm::DECODER_LOAD_COMMON,
    kGrouped = nrf54l15::pwm::DECODER_LOAD_GROUPED,
    kIndividual = nrf54l15::pwm::DECODER_LOAD_INDIVIDUAL,
    kWaveForm = nrf54l15::pwm::DECODER_LOAD_WAVEFORM,
  };

  enum class DecoderMode : uint8_t {
    kRefreshCount = nrf54l15::pwm::DECODER_MODE_REFRESHCOUNT,
    kNextStep = nrf54l15::pwm::DECODER_MODE_NEXTSTEP,
  };

  enum class CounterMode : uint8_t {
    kUp = nrf54l15::pwm::MODE_UP,
    kUpDown = nrf54l15::pwm::MODE_UPDOWN,
  };

  struct ChannelConfig {
    Pin outPin;
    uint16_t dutyPermille;
    bool activeHigh;
  };

  explicit Pwm(uint32_t base = nrf54l15::PWM20_BASE);
  static constexpr uint8_t maxChannelCount() { return 4U; }
  static uint16_t clampCountertop(uint16_t countertop);
  static uint16_t encodeSequenceWordTicks(uint16_t pulseTicks,
                                          bool activeHigh = true);
  static uint16_t encodeSequenceWordPermille(uint16_t dutyPermille,
                                             uint16_t countertop,
                                             bool activeHigh = true);

  bool beginChannels(const ChannelConfig* channels, uint8_t channelCount,
                     uint32_t frequencyHz = 1000UL);
  bool beginRaw(const Pin* outPins, uint8_t channelCount,
                uint32_t frequencyHz = 1000UL,
                DecoderLoad load = DecoderLoad::kIndividual,
                DecoderMode mode = DecoderMode::kRefreshCount,
                uint8_t idleOutMask = 0U,
                CounterMode counterMode = CounterMode::kUp);

  bool beginSingle(const Pin& outPin,
                   uint32_t frequencyHz = 1000UL,
                   uint16_t dutyPermille = 500,
                   bool activeHigh = true);
  bool setDutyPermille(uint16_t dutyPermille);
  bool setDutyPermille(uint8_t channel, uint16_t dutyPermille);
  bool setActiveHigh(uint8_t channel, bool activeHigh);
  bool setSequence(uint8_t sequence, const uint16_t* words, uint16_t wordCount,
                   uint32_t refreshCount = 0U, uint32_t endDelay = 0U);
  bool setLoopCount(uint16_t loopCount);
  bool setCounterMode(CounterMode mode);
  bool setFrequency(uint32_t frequencyHz);
  bool triggerNextStep();
  bool channelConfigured(uint8_t channel) const;
  uint8_t configuredChannelMask() const;
  uint16_t countertop() const;
  uint8_t prescaler() const;
  DecoderLoad decoderLoad() const;
  DecoderMode decoderMode() const;
  CounterMode counterMode() const;
  volatile uint32_t* publishStoppedConfigRegister() const;
  volatile uint32_t* publishSequenceStartedConfigRegister(uint8_t sequence) const;
  volatile uint32_t* publishSequenceEndConfigRegister(uint8_t sequence) const;
  volatile uint32_t* publishPeriodEndConfigRegister() const;
  volatile uint32_t* publishLoopsDoneConfigRegister() const;
  volatile uint32_t* publishRamUnderflowConfigRegister() const;
  volatile uint32_t* publishCompareMatchConfigRegister(uint8_t channel) const;
  volatile uint32_t* subscribeStopConfigRegister() const;
  volatile uint32_t* subscribeNextStepConfigRegister() const;
  volatile uint32_t* subscribeSequenceStartConfigRegister(uint8_t sequence) const;

  bool start(uint8_t sequence = 0, uint32_t spinLimit = 2000000UL);
  bool stop(uint32_t spinLimit = 2000000UL);
  void end();

  bool pollPeriodEnd(bool clearEvent = true);
  bool pollCompareMatch(uint8_t channel, bool clearEvent = true);
  bool pollSequenceEnd(uint8_t sequence, bool clearEvent = true);
  bool pollLoopsDone(bool clearEvent = true);
  bool pollRamUnderflow(bool clearEvent = true);

 private:
  bool applyOutputRouting(const Pin* outPins, uint8_t channelCount,
                         uint8_t idleOutMask);
  bool configureClockAndTop(uint32_t frequencyHz);
  void disconnectAllOutputs();
  void updateSequenceWords();

  uint32_t base_;
  Pin outPins_[4];
  uint16_t dutyPermille_[4];
  uint8_t configuredMask_;
  uint8_t activeHighMask_;
  uint16_t countertop_;
  uint8_t prescaler_;
  uint8_t sequenceConfiguredMask_;
  DecoderLoad loadMode_;
  DecoderMode mode_;
  CounterMode counterMode_;
  bool highLevelManaged_;
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
  volatile uint32_t* subscribeTaskOutConfigRegister(uint8_t channel) const;
  volatile uint32_t* subscribeTaskSetConfigRegister(uint8_t channel) const;
  volatile uint32_t* subscribeTaskClrConfigRegister(uint8_t channel) const;

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

class Dppic {
 public:
  explicit Dppic(uint32_t base = nrf54l15::DPPIC20_BASE);

  bool enableChannel(uint8_t channel, bool enable = true);
  bool channelEnabled(uint8_t channel) const;
  bool configureChannelGroup(uint8_t group, uint32_t channelMask) const;
  bool configurePublish(volatile uint32_t* publishRegister,
                        uint8_t channel,
                        bool enable = true) const;
  bool configureSubscribe(volatile uint32_t* subscribeRegister,
                          uint8_t channel,
                          bool enable = true) const;
  bool connect(volatile uint32_t* publishRegister,
               volatile uint32_t* subscribeRegister,
               uint8_t channel,
               bool enableChannel = true) const;
  bool disconnectPublish(volatile uint32_t* publishRegister) const;
  bool disconnectSubscribe(volatile uint32_t* subscribeRegister) const;

 private:
  NRF_DPPIC_Type* dppic_;
};

class Egu {
 public:
  explicit Egu(uint32_t base = nrf54l15::EGU20_BASE,
               uint8_t channelCount = 16);

  bool trigger(uint8_t channel);
  bool pollTriggered(uint8_t channel, bool clearEvent = true);
  void clearEvent(uint8_t channel);
  void clearAllEvents();
  void enableInterrupt(uint8_t channel, bool enable = true);
  bool configurePublish(uint8_t channel, uint8_t dppiChannel,
                        bool enable = true);
  bool configureSubscribe(uint8_t channel, uint8_t dppiChannel,
                          bool enable = true);
  volatile uint32_t* publishTriggeredConfigRegister(uint8_t channel) const;
  volatile uint32_t* subscribeTriggerConfigRegister(uint8_t channel) const;

 private:
  NRF_EGU_Type* egu_;
  uint8_t channelCount_;
};

class CracenRng {
 public:
  explicit CracenRng(uint32_t controlBase = nrf54l15::CRACEN_BASE,
                     uint32_t coreBase = nrf54l15::CRACENCORE_BASE);

  bool begin(uint32_t spinLimit = 400000UL);
  bool beginNonBlocking();
  void end();
  bool fill(void* data, size_t length, uint32_t spinLimit = 400000UL);
  bool randomWord(uint32_t* outWord, uint32_t spinLimit = 400000UL);
  bool tryRandomWord(uint32_t* outWord);
  uint32_t availableWords() const;
  uint32_t status() const;
  bool healthy() const;
  bool active() const;
  void clearEvent();

 private:
  bool ensureDataAvailable(uint32_t spinLimit);

  NRF_CRACEN_Type* cracen_;
  NRF_CRACENCORE_Type* core_;
  bool active_;
};

enum class KmuRevocationPolicy : uint32_t {
  kReserved = 0U,
  kRotating = 1U,
  kLocked = 2U,
  kRevoked = 3U,
};

struct alignas(16) KmuProvisionSource {
  uint32_t value[4];
  uint32_t revocationPolicy;
  uint32_t destination;
  uint32_t metadata;
};

class Kmu {
 public:
  explicit Kmu(uint32_t base = nrf54l15::KMU_BASE,
               uint32_t rramcBase = nrf54l15::RRAMC_BASE);

  bool ready() const;
  void clearEvents();
  bool pollProvisioned(bool clearEvent = true);
  bool pollPushed(bool clearEvent = true);
  bool pollRevoked(bool clearEvent = true);
  bool pollMetadataRead(bool clearEvent = true);
  bool pollPushBlocked(bool clearEvent = true);
  bool pollError(bool clearEvent = true);
  bool readMetadata(uint8_t slot, uint32_t* metadata,
                    uint32_t spinLimit = 600000UL);
  bool provision(uint8_t slot, const KmuProvisionSource& source,
                 uint32_t spinLimit = 600000UL);
  bool push(uint8_t slot, uint32_t spinLimit = 600000UL);
  bool revoke(uint8_t slot, uint32_t spinLimit = 600000UL);
  bool pushBlock(uint8_t slot, uint32_t spinLimit = 600000UL);

 private:
  bool waitReady(uint32_t spinLimit) const;
  bool performSimpleTask(uint8_t slot,
                         volatile uint32_t& task,
                         volatile uint32_t& successEvent,
                         uint32_t spinLimit);
  bool enableRramWrite(uint32_t* previousConfig, uint32_t spinLimit) const;
  void restoreRramWrite(uint32_t previousConfig, uint32_t spinLimit) const;

  NRF_KMU_Type* kmu_;
  NRF_RRAMC_Type* rramc_;
};

class CracenIkg {
 public:
  explicit CracenIkg(uint32_t controlBase = nrf54l15::CRACEN_BASE,
                     uint32_t coreBase = nrf54l15::CRACENCORE_BASE);

  bool begin(uint32_t spinLimit = 600000UL);
  void end();
  bool active() const;
  void clearEvent();

  uint32_t status() const;
  uint32_t pkeStatus() const;
  uint32_t hwConfig() const;
  uint8_t symmetricKeyCapacity() const;
  uint8_t privateKeyCapacity() const;

  bool okay() const;
  bool seedError() const;
  bool entropyError() const;
  bool catastrophicError() const;
  bool ctrDrbgBusy() const;
  bool symmetricKeysStored() const;
  bool privateKeysStored() const;

  bool markSeedValid(bool valid = true);
  bool lockSeed();
  bool lockProtectedRam();
  bool softResetKeys(uint32_t spinLimit = 600000UL);
  bool initInput();
  bool writeNonce(const uint32_t* words, size_t wordCount);
  bool writePersonalization(const uint32_t* words, size_t wordCount);
  bool setReseedInterval(uint64_t interval);
  bool start(uint32_t spinLimit = 600000UL);

 private:
  bool waitReady(uint32_t spinLimit) const;
  bool waitGenerationComplete(uint32_t spinLimit) const;

  NRF_CRACEN_Type* cracen_;
  NRF_CRACENCORE_Type* core_;
  bool active_;
};

class Tampc {
 public:
  explicit Tampc(uint32_t base = nrf54l15::TAMPC_BASE);

  uint32_t status() const;
  bool tamperDetected() const;
  bool writeErrorDetected() const;
  bool pollTamper(bool clearEvent = true);
  bool pollWriteError(bool clearEvent = true);
  void clearEvents();
  void enableInterrupts(bool tamper, bool writeError);
  bool pendingTamperInterrupt() const;
  bool pendingWriteErrorInterrupt() const;
  bool setInternalResetOnTamper(bool enable, bool lock = false);
  bool setExternalResetOnTamper(bool enable, bool lock = false);
  bool setEraseProtect(bool enable, bool lock = false);
  bool setCracenTamperMonitor(bool enable, bool lock = false);
  bool setActiveShieldMonitor(bool enable, bool lock = false);
  bool setGlitchSlowMonitor(bool enable, bool lock = false);
  bool setGlitchFastMonitor(bool enable, bool lock = false);
  bool setActiveShieldChannelMask(uint8_t channelMask);
  uint8_t activeShieldChannelMask() const;
  bool activeShieldChannelEnabled(uint8_t channel) const;
  bool setActiveShieldChannelEnabled(uint8_t channel, bool enable = true);

  bool internalResetOnTamperEnabled() const;
  bool externalResetOnTamperEnabled() const;
  bool eraseProtectEnabled() const;
  bool cracenTamperMonitorEnabled() const;
  bool activeShieldMonitorEnabled() const;
  bool glitchSlowMonitorEnabled() const;
  bool glitchFastMonitorEnabled() const;

  bool domainDbgenEnabled(uint8_t domain = 0U) const;
  bool domainNidenEnabled(uint8_t domain = 0U) const;
  bool domainSpidenEnabled(uint8_t domain = 0U) const;
  bool domainSpnidenEnabled(uint8_t domain = 0U) const;
  bool apDbgenEnabled(uint8_t ap = 0U) const;

  bool setDomainDbgen(bool enable, bool lock = false, uint8_t domain = 0U);
  bool setDomainNiden(bool enable, bool lock = false, uint8_t domain = 0U);
  bool setDomainSpiden(bool enable, bool lock = false, uint8_t domain = 0U);
  bool setDomainSpniden(bool enable, bool lock = false, uint8_t domain = 0U);
  bool setApDbgen(bool enable, bool lock = false, uint8_t ap = 0U);

  bool protectStatusError() const;
  bool cracenTamperStatusError() const;
  bool activeShieldStatusError() const;
  bool glitchSlowStatusError() const;
  bool glitchFastStatusError() const;
  bool intResetStatusError() const;
  bool extResetStatusError() const;
  bool eraseProtectStatusError() const;
  bool domainDbgenStatusError(uint8_t domain = 0U) const;
  bool domainNidenStatusError(uint8_t domain = 0U) const;
  bool domainSpidenStatusError(uint8_t domain = 0U) const;
  bool domainSpnidenStatusError(uint8_t domain = 0U) const;
  bool apDbgenStatusError(uint8_t ap = 0U) const;

 private:
  bool writeProtectedControl(volatile uint32_t& ctrlRegister,
                             uint32_t valueHigh,
                             uint32_t valueLow,
                             bool enable,
                             bool lock);
  bool protectedSignalEnabled(const volatile uint32_t& ctrlRegister) const;
  bool protectedStatusError(const volatile uint32_t& statusRegister) const;
  bool validDomainIndex(uint8_t domain) const;
  bool validApIndex(uint8_t ap) const;

  NRF_TAMPC_Type* tampc_;
};

class Aar {
 public:
  explicit Aar(uint32_t base = nrf54l15::AAR00_BASE);

  bool resolveFirst(const uint8_t address[6],
                    const uint8_t* irks,
                    size_t irkCount,
                    bool* outResolved,
                    uint16_t* outIndex = nullptr,
                    uint32_t spinLimit = 200000UL);
  bool resolveSingle(const uint8_t address[6],
                     const uint8_t irk[16],
                     bool* outResolved,
                     uint32_t spinLimit = 200000UL);
  uint32_t errorStatus() const;
  uint32_t resolvedAmountBytes() const;
  void clearEvents();

 private:
  NRF_AAR_Type* aar_;
};

class Ecb {
 public:
  explicit Ecb(uint32_t base = nrf54l15::ECB00_BASE);

  bool encryptBlock(const uint8_t key[16],
                    const uint8_t plaintext[16],
                    uint8_t ciphertext[16],
                    uint32_t spinLimit = 200000UL);
  bool encryptBlockInPlace(const uint8_t key[16],
                           uint8_t block[16],
                           uint32_t spinLimit = 200000UL);
  uint32_t errorStatus() const;
  void clearEvents();

 private:
  NRF_ECB_Type* ecb_;
};

enum class CcmBleDataRate : uint8_t {
  k125Kbit = CCM_MODE_DATARATE_125Kbit,
  k250Kbit = CCM_MODE_DATARATE_250Kbit,
  k500Kbit = CCM_MODE_DATARATE_500Kbit,
  k1Mbit = CCM_MODE_DATARATE_1Mbit,
  k2Mbit = CCM_MODE_DATARATE_2Mbit,
  k4Mbit = CCM_MODE_DATARATE_4Mbit,
};

class Ccm {
 public:
  explicit Ccm(uint32_t base = nrf54l15::CCM00_BASE);

  bool encryptBlePacket(const uint8_t key[16],
                        const uint8_t iv[8],
                        uint64_t counter,
                        uint8_t direction,
                        uint8_t header,
                        const uint8_t* plaintext,
                        uint8_t plaintextLen,
                        uint8_t* outCipherWithMic,
                        uint8_t* outCipherWithMicLen,
                        CcmBleDataRate dataRate = CcmBleDataRate::k125Kbit,
                        uint8_t adataMask = 0xE3U,
                        uint32_t spinLimit = 200000UL);
  bool decryptBlePacket(const uint8_t key[16],
                        const uint8_t iv[8],
                        uint64_t counter,
                        uint8_t direction,
                        uint8_t header,
                        const uint8_t* cipherWithMic,
                        uint8_t cipherWithMicLen,
                        uint8_t* outPlaintext,
                        uint8_t* outPlaintextLen,
                        bool* outMacValid = nullptr,
                        CcmBleDataRate dataRate = CcmBleDataRate::k125Kbit,
                        uint8_t adataMask = 0xE3U,
                        uint32_t spinLimit = 200000UL);
  uint32_t errorStatus() const;
  bool macStatus() const;
  void clearEvents();

 private:
  NRF_CCM_Type* ccm_;
};

enum class CompReference : uint8_t {
  kInt1V2 = COMP_REFSEL_REFSEL_Int1V2,
  kVdd = COMP_REFSEL_REFSEL_VDD,
  kExternalAref = COMP_REFSEL_REFSEL_ARef,
};

enum class CompSpeedMode : uint8_t {
  kLowPower = COMP_MODE_SP_Low,
  kNormal = COMP_MODE_SP_Normal,
  kHighSpeed = COMP_MODE_SP_High,
};

enum class CompCurrentSource : uint8_t {
  kDisabled = COMP_ISOURCE_ISOURCE_Off,
  k2uA5 = COMP_ISOURCE_ISOURCE_Ien2uA5,
  k5uA = COMP_ISOURCE_ISOURCE_Ien5uA,
  k10uA = COMP_ISOURCE_ISOURCE_Ien10uA,
};

class Comp {
 public:
  explicit Comp(uint32_t base = nrf54l15::COMP_BASE);

  bool beginThreshold(const Pin& inputPin,
                      uint16_t thresholdPermille = 500U,
                      uint16_t hysteresisPermille = 0U,
                      CompReference reference = CompReference::kVdd,
                      CompSpeedMode speed = CompSpeedMode::kLowPower,
                      const Pin& externalReferencePin = kPinDisconnected,
                      CompCurrentSource currentSource = CompCurrentSource::kDisabled,
                      uint32_t spinLimit = 200000UL);
  bool beginSingleEnded(const Pin& inputPin,
                        CompReference reference = CompReference::kVdd,
                        uint16_t thresholdPermille = 500U,
                        uint16_t hysteresisPermille = 0U,
                        CompSpeedMode speed = CompSpeedMode::kLowPower,
                        const Pin& externalReferencePin = kPinDisconnected,
                        CompCurrentSource currentSource = CompCurrentSource::kDisabled,
                        uint32_t spinLimit = 200000UL);
  bool beginDifferential(const Pin& positivePin,
                         const Pin& negativePin,
                         CompSpeedMode speed = CompSpeedMode::kLowPower,
                         bool hysteresis = false,
                         CompCurrentSource currentSource = CompCurrentSource::kDisabled,
                         uint32_t spinLimit = 200000UL);
  bool setThresholdWindowPermille(uint16_t lowPermille, uint16_t highPermille);
  void setCurrentSource(CompCurrentSource currentSource);
  bool sample(uint32_t spinLimit = 200000UL) const;
  bool resultAbove() const;
  bool pollReady(bool clearEvent = true);
  bool pollUp(bool clearEvent = true);
  bool pollDown(bool clearEvent = true);
  bool pollCross(bool clearEvent = true);
  void clearEvents();
  void end();

 private:
  NRF_COMP_Type* comp_;
  bool active_;
};

enum class LpcompReference : uint8_t {
  k1over8Vdd = LPCOMP_REFSEL_REFSEL_Ref1_8Vdd,
  k2over8Vdd = LPCOMP_REFSEL_REFSEL_Ref2_8Vdd,
  k3over8Vdd = LPCOMP_REFSEL_REFSEL_Ref3_8Vdd,
  k4over8Vdd = LPCOMP_REFSEL_REFSEL_Ref4_8Vdd,
  k5over8Vdd = LPCOMP_REFSEL_REFSEL_Ref5_8Vdd,
  k6over8Vdd = LPCOMP_REFSEL_REFSEL_Ref6_8Vdd,
  k7over8Vdd = LPCOMP_REFSEL_REFSEL_Ref7_8Vdd,
  kExternalAref = LPCOMP_REFSEL_REFSEL_ARef,
  k1over16Vdd = LPCOMP_REFSEL_REFSEL_Ref1_16Vdd,
  k3over16Vdd = LPCOMP_REFSEL_REFSEL_Ref3_16Vdd,
  k5over16Vdd = LPCOMP_REFSEL_REFSEL_Ref5_16Vdd,
  k7over16Vdd = LPCOMP_REFSEL_REFSEL_Ref7_16Vdd,
  k9over16Vdd = LPCOMP_REFSEL_REFSEL_Ref9_16Vdd,
  k11over16Vdd = LPCOMP_REFSEL_REFSEL_Ref11_16Vdd,
  k13over16Vdd = LPCOMP_REFSEL_REFSEL_Ref13_16Vdd,
  k15over16Vdd = LPCOMP_REFSEL_REFSEL_Ref15_16Vdd,
};

enum class LpcompDetect : uint8_t {
  kCross = LPCOMP_ANADETECT_ANADETECT_Cross,
  kUp = LPCOMP_ANADETECT_ANADETECT_Up,
  kDown = LPCOMP_ANADETECT_ANADETECT_Down,
};

class Lpcomp {
 public:
  explicit Lpcomp(uint32_t base = nrf54l15::LPCOMP_BASE);

  bool begin(const Pin& inputPin,
             LpcompReference reference = LpcompReference::k4over8Vdd,
             bool hysteresis = false,
             LpcompDetect detect = LpcompDetect::kCross,
             const Pin& externalReferencePin = kPinDisconnected,
             uint32_t spinLimit = 200000UL);
  bool beginThreshold(const Pin& inputPin,
                      uint16_t thresholdPermille,
                      bool hysteresis = false,
                      LpcompDetect detect = LpcompDetect::kCross,
                      const Pin& externalReferencePin = kPinDisconnected,
                      uint32_t spinLimit = 200000UL);
  // Convenience overload: specify threshold in millivolts instead of permille.
  // vddMv  — supply voltage in mV (e.g. 3300 for 3.3 V, 1800 for 1.8 V).
  // thresholdMv — desired wake threshold in mV.
  // The LPCOMP hardware snaps to the nearest of its 16 fixed reference levels;
  // use Serial.print after begin to confirm the effective level if needed.
  bool beginThresholdMv(const Pin& inputPin,
                        uint16_t vddMv,
                        uint16_t thresholdMv,
                        bool hysteresis = false,
                        LpcompDetect detect = LpcompDetect::kCross,
                        uint32_t spinLimit = 200000UL);
  void configureAnalogDetect(LpcompDetect detect);
  bool sample(uint32_t spinLimit = 200000UL) const;
  bool resultAbove() const;
  bool pollReady(bool clearEvent = true);
  bool pollUp(bool clearEvent = true);
  bool pollDown(bool clearEvent = true);
  bool pollCross(bool clearEvent = true);
  void clearEvents();
  void end();

 private:
  NRF_LPCOMP_Type* lpcomp_;
  bool active_;
};

enum class QdecSamplePeriod : uint8_t {
  k128us = QDEC_SAMPLEPER_SAMPLEPER_128us,
  k256us = QDEC_SAMPLEPER_SAMPLEPER_256us,
  k512us = QDEC_SAMPLEPER_SAMPLEPER_512us,
  k1024us = QDEC_SAMPLEPER_SAMPLEPER_1024us,
  k2048us = QDEC_SAMPLEPER_SAMPLEPER_2048us,
  k4096us = QDEC_SAMPLEPER_SAMPLEPER_4096us,
  k8192us = QDEC_SAMPLEPER_SAMPLEPER_8192us,
  k16384us = QDEC_SAMPLEPER_SAMPLEPER_16384us,
  k32ms = QDEC_SAMPLEPER_SAMPLEPER_32ms,
  k65ms = QDEC_SAMPLEPER_SAMPLEPER_65ms,
  k131ms = QDEC_SAMPLEPER_SAMPLEPER_131ms,
};

enum class QdecReportPeriod : uint8_t {
  k10Samples = QDEC_REPORTPER_REPORTPER_10Smpl,
  k40Samples = QDEC_REPORTPER_REPORTPER_40Smpl,
  k80Samples = QDEC_REPORTPER_REPORTPER_80Smpl,
  k120Samples = QDEC_REPORTPER_REPORTPER_120Smpl,
  k160Samples = QDEC_REPORTPER_REPORTPER_160Smpl,
  k200Samples = QDEC_REPORTPER_REPORTPER_200Smpl,
  k240Samples = QDEC_REPORTPER_REPORTPER_240Smpl,
  k280Samples = QDEC_REPORTPER_REPORTPER_280Smpl,
  k1Sample = QDEC_REPORTPER_REPORTPER_1Smpl,
};

enum class QdecLedPolarity : uint8_t {
  kActiveLow = QDEC_LEDPOL_LEDPOL_ActiveLow,
  kActiveHigh = QDEC_LEDPOL_LEDPOL_ActiveHigh,
};

enum class QdecInputPull : uint8_t {
  kDisabled = GPIO_PIN_CNF_PULL_Disabled,
  kPullDown = GPIO_PIN_CNF_PULL_Pulldown,
  kPullUp = GPIO_PIN_CNF_PULL_Pullup,
};

class Qdec {
 public:
  explicit Qdec(uint32_t base = nrf54l15::QDEC20_BASE);

  bool begin(const Pin& pinA,
             const Pin& pinB,
             QdecSamplePeriod samplePeriod = QdecSamplePeriod::k1024us,
             QdecReportPeriod reportPeriod = QdecReportPeriod::k1Sample,
             bool debounce = true,
             QdecInputPull inputPull = QdecInputPull::kPullUp,
             const Pin& ledPin = kPinDisconnected,
             QdecLedPolarity ledPolarity = QdecLedPolarity::kActiveLow,
             uint16_t ledPreUs = 16U);
  void end();
  void start();
  void stop();

  int32_t sampleValue() const;
  int32_t accumulator() const;
  int32_t readAndClearAccumulator();
  uint32_t doubleTransitions() const;
  uint32_t readAndClearDoubleTransitions();

  bool pollSampleReady(bool clearEvent = true);
  bool pollReportReady(bool clearEvent = true);
  bool pollOverflow(bool clearEvent = true);
  bool pollDoubleReady(bool clearEvent = true);
  bool pollStopped(bool clearEvent = true);

 private:
  NRF_QDEC_Type* qdec_;
  bool configured_;
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

enum class AdcInternalInput : uint8_t {
  kAvdd = SAADC_CH_PSELP_INTERNAL_Avdd,
  kDvdd = SAADC_CH_PSELP_INTERNAL_Dvdd,
  kVdd = SAADC_CH_PSELP_INTERNAL_Vdd,
};

enum class AdcOversample : uint8_t {
  kBypass = nrf54l15::saadc::OVERSAMPLE_BYPASS,
  k2x = nrf54l15::saadc::OVERSAMPLE_2X,
  k4x = nrf54l15::saadc::OVERSAMPLE_4X,
  k8x = nrf54l15::saadc::OVERSAMPLE_8X,
  k16x = nrf54l15::saadc::OVERSAMPLE_16X,
  k32x = nrf54l15::saadc::OVERSAMPLE_32X,
  k64x = nrf54l15::saadc::OVERSAMPLE_64X,
  k128x = nrf54l15::saadc::OVERSAMPLE_128X,
  k256x = nrf54l15::saadc::OVERSAMPLE_256X,
};

class Saadc {
 public:
  explicit Saadc(uint32_t base = nrf54l15::SAADC_BASE);

  bool begin(AdcResolution resolution = AdcResolution::k12bit,
             uint32_t spinLimit = 2000000UL);
  bool begin(AdcResolution resolution, AdcOversample oversample,
             uint32_t spinLimit = 2000000UL);
  bool calibrate(uint32_t spinLimit = 2000000UL);
  void end();

  // Configures one active single-ended channel and disables the others.
  bool configureSingleEnded(uint8_t channel, const Pin& pin,
                            AdcGain gain = AdcGain::k2over8,
                            uint16_t tacq = 159,
                            uint8_t tconv = 4,
                            bool burst = false);
  bool configureSingleEnded(uint8_t channel, AdcInternalInput input,
                            AdcGain gain = AdcGain::k2over8,
                            uint16_t tacq = 159,
                            uint8_t tconv = 4,
                            bool burst = false);
  bool configureDifferential(uint8_t channel, const Pin& positivePin,
                             const Pin& negativePin,
                             AdcGain gain = AdcGain::k1,
                             uint16_t tacq = 159,
                             uint8_t tconv = 4,
                             bool burst = false);

  bool sampleRaw(int16_t* outRaw, uint32_t spinLimit = 2000000UL) const;
  bool sampleMilliVolts(int32_t* outMilliVolts,
                        uint32_t spinLimit = 2000000UL) const;
  bool sampleMilliVoltsSigned(int32_t* outMilliVolts,
                              uint32_t spinLimit = 2000000UL) const;

 private:
  uint32_t base_;
  AdcResolution resolution_;
  AdcGain gain_;
  AdcOversample oversample_;
  bool differential_;
  bool configured_;
};

enum class BoardAntennaPath : uint8_t {
  kCeramic = 0,
  kExternal = 1,
  kControlHighImpedance = 2,
};

class BoardControl {
 public:
  // Control the desired active RF switch path on P2.05:
  // - ceramic/external actively drive RF_SW_CTL
  // - control-high-impedance releases control pin (no active drive)
  static bool setAntennaPath(BoardAntennaPath path);

  // Returns the observed current RF switch route on the board without
  // changing the remembered desired active path.
  static BoardAntennaPath antennaPath();

  // Controls the shared IMU + microphone power/enable rail on XIAO nRF54L15.
  static bool setImuMicEnabled(bool enable);
  static bool imuMicEnabled();

  // Enables/disables the battery-divider path used for VBAT sense.
  static bool setBatterySenseEnabled(bool enable);

  // Powers the RF switch IC on P2.03. This is independent from the selected
  // RF path and can be disabled to remove the switch quiescent current.
  static bool setRfSwitchPowerEnabled(bool enable);
  static bool rfSwitchPowerEnabled();

  // Powers the RF switch and applies the requested route for an active RF
  // window such as a BLE TX/RX event.
  static bool enableRfPath(BoardAntennaPath path = BoardAntennaPath::kCeramic);

  // Releases RF_SW_CTL to the requested idle state and optionally removes the
  // RF switch supply to collapse board-side quiescent current while idle.
  static bool collapseRfPathIdle(
      BoardAntennaPath idlePath = BoardAntennaPath::kControlHighImpedance,
      bool disablePower = true);

  // Applies the board-level lowest-power state used before System OFF.
  static void enterLowestPowerState();

  // Reads VBAT in millivolts (divider-compensated, x2).
  // This method enables the divider path only for the sampling window.
  static bool sampleBatteryMilliVolts(int32_t* outMilliVolts,
                                      uint32_t settleDelayUs = 5000U,
                                      uint32_t spinLimit = 500000UL);

  // Reads the chip VDD rail through the SAADC internal VDD input.
  // This does not consume an external analog pin and is not raw VBAT.
  static bool sampleVddMilliVolts(int32_t* outMilliVolts,
                                  uint32_t spinLimit = 500000UL);

  // Approximate Li-ion state-of-charge from measured VBAT.
  // Defaults are intentionally conservative for 1-cell chemistry.
  static bool sampleBatteryPercent(uint8_t* outPercent,
                                   int32_t emptyMilliVolts = 3300,
                                   int32_t fullMilliVolts = 4200,
                                   uint32_t settleDelayUs = 5000U,
                                   uint32_t spinLimit = 500000UL);
};

enum class PowerLatencyMode : uint8_t {
  kLowPower = 0,
  kConstantLatency = 1,
};

enum class PowerFailThreshold : uint8_t {
  k1V7 = REGULATORS_POFCON_THRESHOLD_V17,
  k1V8 = REGULATORS_POFCON_THRESHOLD_V18,
  k1V9 = REGULATORS_POFCON_THRESHOLD_V19,
  k2V0 = REGULATORS_POFCON_THRESHOLD_V20,
  k2V1 = REGULATORS_POFCON_THRESHOLD_V21,
  k2V2 = REGULATORS_POFCON_THRESHOLD_V22,
  k2V3 = REGULATORS_POFCON_THRESHOLD_V23,
  k2V4 = REGULATORS_POFCON_THRESHOLD_V24,
  k2V5 = REGULATORS_POFCON_THRESHOLD_V25,
  k2V6 = REGULATORS_POFCON_THRESHOLD_V26,
  k2V7 = REGULATORS_POFCON_THRESHOLD_V27,
  k2V8 = REGULATORS_POFCON_THRESHOLD_V28,
  k2V9 = REGULATORS_POFCON_THRESHOLD_V29,
  k3V0 = REGULATORS_POFCON_THRESHOLD_V30,
  k3V1 = REGULATORS_POFCON_THRESHOLD_V31,
  k3V2 = REGULATORS_POFCON_THRESHOLD_V32,
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
  bool configurePowerFailComparator(
      PowerFailThreshold threshold = PowerFailThreshold::k2V8,
      bool enableWarningEvent = true);
  void disablePowerFailComparator();
  bool powerFailComparatorEnabled() const;
  PowerFailThreshold powerFailThreshold() const;
  bool powerBelowPowerFailThreshold() const;
  bool powerFailWarningEventEnabled() const;
  bool pollPowerFailWarning(bool clearEvent = true);
  void clearPowerFailWarning();

  // Default system-off paths preserve .noinit RAM retention.
  [[noreturn]] void systemOff();
  [[noreturn]] void systemOffTimedWakeMs(uint32_t delayMs);
  [[noreturn]] void systemOffTimedWakeUs(uint32_t delayUs);

  // Explicit low-power paths that clear RAM retention before SYSTEM OFF.
  [[noreturn]] void systemOffNoRetention();
  [[noreturn]] void systemOffTimedWakeMsNoRetention(uint32_t delayMs);
  [[noreturn]] void systemOffTimedWakeUsNoRetention(uint32_t delayUs);

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

class GrtcPwm {
 public:
  explicit GrtcPwm(uint32_t base = nrf54l15::GRTC_BASE);

  static constexpr uint32_t frequencyHz() { return 32768UL / 256UL; }

  static bool supportsPin(const Pin& outPin);
  static bool supportsArduinoPin(uint8_t arduinoPin);

  bool begin(const Pin& outPin, uint8_t duty8 = 128U,
             GrtcClockSource clockSource = GrtcClockSource::kSystemLfclk,
             bool startNow = true, uint32_t spinLimit = 600000UL);
  bool beginArduinoPin(uint8_t arduinoPin, uint8_t duty8 = 128U,
                       GrtcClockSource clockSource =
                           GrtcClockSource::kSystemLfclk,
                       bool startNow = true, uint32_t spinLimit = 600000UL);

  bool setDuty8(uint8_t duty8);
  bool setDutyPermille(uint16_t dutyPermille);
  uint8_t duty8() const;

  bool ready() const;
  void enablePeriodEndEvent(bool enable = true);
  bool start(uint32_t spinLimit = 600000UL);
  bool stop(uint32_t spinLimit = 600000UL);
  bool pollPeriodEnd(bool clearEvent = true);
  void end(uint32_t spinLimit = 600000UL);

 private:
  bool takePin(const Pin& outPin);
  void restorePin();
  bool waitReady(uint32_t spinLimit) const;

  NRF_GRTC_Type* grtc_;
  Pin outPin_;
  uint32_t savedPinCnf_;
  bool savedOutputHigh_;
  bool pinOwned_;
  bool running_;
  uint8_t duty8_;
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

struct I2sTxConfig {
  Pin mck = kPinDisconnected;
  Pin sck = kPinDisconnected;
  Pin lrck = kPinDisconnected;
  Pin sdin = kPinDisconnected;
  Pin sdout = kPinDisconnected;
  uint32_t mckFreq = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV8;
  uint32_t ratio = I2S_CONFIG_RATIO_RATIO_256X;
  uint32_t sampleWidth = I2S_CONFIG_SWIDTH_SWIDTH_16Bit;
  uint32_t align = I2S_CONFIG_ALIGN_ALIGN_Left;
  uint32_t format = I2S_CONFIG_FORMAT_FORMAT_I2S;
  uint32_t channels = I2S_CONFIG_CHANNELS_CHANNELS_Stereo;
  uint8_t irqPriority = 3U;
  bool enableMasterClock = true;
  bool autoRestart = true;
};

class I2sTx {
 public:
  using RefillCallback = void (*)(uint32_t* buffer, uint32_t wordCount,
                                  void* context);

  explicit I2sTx(uint32_t base = nrf54l15::I2S20_BASE);

  bool begin(const I2sTxConfig& config, uint32_t* buffer0, uint32_t* buffer1,
             uint32_t wordCount);
  void end();

  bool start();
  bool stop();
  void service();
  void onIrq();

  bool setBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount);
  void setRefillCallback(RefillCallback callback, void* context = nullptr);
  bool makeActive();
  static void irqHandler();

  bool configured() const;
  bool running() const;
  bool restartPending() const;
  uint32_t txPtrUpdCount() const;
  uint32_t stoppedCount() const;
  uint32_t restartCount() const;
  uint32_t manualStopCount() const;

 private:
  void clearEvents();
  void armBuffer(uint8_t bufferIndex);

  NRF_I2S_Type* i2s_;
  I2sTxConfig config_;
  uint32_t* buffers_[2];
  uint32_t wordCount_;
  RefillCallback refillCallback_;
  void* refillContext_;
  uint8_t nextBufferIndex_;
  bool configured_;
  bool running_;
  bool restartPending_;
  uint32_t txPtrUpdCount_;
  uint32_t stoppedCount_;
  uint32_t restartCount_;
  uint32_t manualStopCount_;
};

struct I2sRxConfig {
  Pin mck = kPinDisconnected;
  Pin sck = kPinDisconnected;
  Pin lrck = kPinDisconnected;
  Pin sdin = kPinDisconnected;
  Pin sdout = kPinDisconnected;
  uint32_t mckFreq = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV8;
  uint32_t ratio = I2S_CONFIG_RATIO_RATIO_256X;
  uint32_t sampleWidth = I2S_CONFIG_SWIDTH_SWIDTH_16Bit;
  uint32_t align = I2S_CONFIG_ALIGN_ALIGN_Left;
  uint32_t format = I2S_CONFIG_FORMAT_FORMAT_I2S;
  uint32_t channels = I2S_CONFIG_CHANNELS_CHANNELS_Stereo;
  uint8_t irqPriority = 3U;
  bool enableMasterClock = true;
  bool autoRestart = true;
};

class I2sRx {
 public:
  using ReceiveCallback = void (*)(uint32_t* buffer, uint32_t wordCount,
                                   void* context);

  explicit I2sRx(uint32_t base = nrf54l15::I2S20_BASE);

  bool begin(const I2sRxConfig& config, uint32_t* buffer0, uint32_t* buffer1,
             uint32_t wordCount);
  void end();

  bool start();
  bool stop();
  void service();
  void onIrq();

  bool setBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount);
  void setReceiveCallback(ReceiveCallback callback, void* context = nullptr);
  bool makeActive();
  static void irqHandler();

  bool configured() const;
  bool running() const;
  bool restartPending() const;
  uint32_t rxPtrUpdCount() const;
  uint32_t stoppedCount() const;
  uint32_t restartCount() const;
  uint32_t manualStopCount() const;

 private:
  void clearEvents();
  void armBuffer(uint8_t bufferIndex);

  NRF_I2S_Type* i2s_;
  I2sRxConfig config_;
  uint32_t* buffers_[2];
  uint32_t wordCount_;
  ReceiveCallback receiveCallback_;
  void* receiveContext_;
  uint8_t nextBufferIndex_;
  bool configured_;
  bool running_;
  bool restartPending_;
  uint32_t rxPtrUpdCount_;
  uint32_t stoppedCount_;
  uint32_t restartCount_;
  uint32_t manualStopCount_;
};

struct I2sDuplexConfig {
  Pin mck = kPinDisconnected;
  Pin sck = kPinDisconnected;
  Pin lrck = kPinDisconnected;
  Pin sdin = kPinDisconnected;
  Pin sdout = kPinDisconnected;
  uint32_t mckFreq = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV8;
  uint32_t ratio = I2S_CONFIG_RATIO_RATIO_256X;
  uint32_t sampleWidth = I2S_CONFIG_SWIDTH_SWIDTH_16Bit;
  uint32_t align = I2S_CONFIG_ALIGN_ALIGN_Left;
  uint32_t format = I2S_CONFIG_FORMAT_FORMAT_I2S;
  uint32_t channels = I2S_CONFIG_CHANNELS_CHANNELS_Stereo;
  uint8_t irqPriority = 3U;
  bool enableMasterClock = true;
  bool autoRestart = true;
};

class I2sDuplex {
 public:
  using TxRefillCallback = void (*)(uint32_t* buffer, uint32_t wordCount,
                                    void* context);
  using RxReceiveCallback = void (*)(uint32_t* buffer, uint32_t wordCount,
                                     void* context);

  explicit I2sDuplex(uint32_t base = nrf54l15::I2S20_BASE);

  bool begin(const I2sDuplexConfig& config, uint32_t* txBuffer0,
             uint32_t* txBuffer1, uint32_t* rxBuffer0, uint32_t* rxBuffer1,
             uint32_t wordCount);
  void end();

  bool start();
  bool stop();
  void service();
  void onIrq();

  bool setTxBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount);
  bool setRxBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount);
  void setTxRefillCallback(TxRefillCallback callback, void* context = nullptr);
  void setRxReceiveCallback(RxReceiveCallback callback, void* context = nullptr);
  bool makeActive();
  static void irqHandler();

  bool configured() const;
  bool running() const;
  bool restartPending() const;
  uint32_t txPtrUpdCount() const;
  uint32_t rxPtrUpdCount() const;
  uint32_t stoppedCount() const;
  uint32_t restartCount() const;
  uint32_t manualStopCount() const;

 private:
  void clearEvents();
  void armTxBuffer(uint8_t bufferIndex);
  void armRxBuffer(uint8_t bufferIndex);

  NRF_I2S_Type* i2s_;
  I2sDuplexConfig config_;
  uint32_t* txBuffers_[2];
  uint32_t* rxBuffers_[2];
  uint32_t wordCount_;
  TxRefillCallback txRefillCallback_;
  void* txRefillContext_;
  RxReceiveCallback rxReceiveCallback_;
  void* rxReceiveContext_;
  uint8_t nextTxBufferIndex_;
  uint8_t nextRxBufferIndex_;
  bool configured_;
  bool running_;
  bool restartPending_;
  uint32_t txPtrUpdCount_;
  uint32_t rxPtrUpdCount_;
  uint32_t stoppedCount_;
  uint32_t restartCount_;
  uint32_t manualStopCount_;
};

struct ZigbeeFrame {
  uint8_t channel;
  int8_t rssiDbm;
  uint8_t length;
  uint8_t psdu[127];
};

struct ZigbeeDataFrameView {
  bool valid;
  bool ackRequested;
  uint8_t sequence;
  uint16_t panId;
  uint16_t destinationShort;
  uint16_t sourceShort;
  const uint8_t* payload;
  uint8_t payloadLength;
};

struct ZigbeeMacCommandView {
  bool valid;
  bool ackRequested;
  uint8_t sequence;
  uint16_t panId;
  uint16_t destinationShort;
  uint16_t sourceShort;
  uint8_t commandId;
  const uint8_t* payload;
  uint8_t payloadLength;
};

struct ZigbeeMacAcknowledgementView {
  bool valid;
  bool framePending;
  uint8_t sequence;
};

struct ZigbeeTransmitDebug {
  bool endSeen = false;
  bool disabledSeen = false;
  bool ccaBusy = false;
  bool ackRequested = false;
  bool ackReceived = false;
  bool ackFramePending = false;
  bool rxAttempted = false;
  bool rxDone = false;
  bool crcOk = false;
  bool crcError = false;
  bool syncSeen = false;
  bool rxReadySeen = false;
  bool frameStartSeen = false;
  bool addressSeen = false;
  bool rxAddressSeen = false;
  bool endEventSeen = false;
  bool devMatchSeen = false;
  bool devMissSeen = false;
  bool mhrMatchSeen = false;
  uint8_t txLength = 0U;
  uint8_t ackSequence = 0U;
  uint8_t rxLength = 0U;
  uint8_t rxSequence = 0U;
  uint32_t txEndTimestampUs = 0U;
  uint32_t disabledTimestampUs = 0U;
  uint32_t rxDoneTimestampUs = 0U;
};

struct ZigbeeReceiveDebug {
  uint32_t rxDoneCount = 0U;
  uint32_t crcErrorCount = 0U;
  uint32_t invalidLengthCount = 0U;
  uint32_t filteredCount = 0U;
  uint8_t lastPhr = 0U;
  uint8_t lastLength = 0U;
};

class ZigbeeRadio {
 public:
  using MacDataRequestPendingCallback = bool (*)(const uint8_t* psdu,
                                                 uint8_t length,
                                                 void* context);
  using MacFrameReceiveFilterCallback = bool (*)(const uint8_t* psdu,
                                                 uint8_t length,
                                                 void* context);

  explicit ZigbeeRadio(uint32_t radioBase = nrf54l15::RADIO_BASE);

  bool begin(uint8_t channel = 15U, int8_t txPowerDbm = 0);
  void end();

  bool setChannel(uint8_t channel);
  uint8_t channel() const;
  bool setTxPowerDbm(int8_t dbm);

  bool transmit(const uint8_t* psdu, uint8_t length, bool performCca = false,
                uint32_t spinLimit = 1400000UL);
  bool transmitThenReceive(const uint8_t* psdu, uint8_t length,
                           ZigbeeFrame* frame,
                           uint32_t listenWindowUs = 7000U,
                           bool performCca = false,
                           uint32_t spinLimit = 1400000UL);
  bool beginReceive(uint32_t spinLimit = 1400000UL);
  bool pollReceive(ZigbeeFrame* frame, uint32_t spinLimit = 1400000UL);
  void cancelReceive(uint32_t spinLimit = 1400000UL);
  bool receiverArmed() const;
  bool receive(ZigbeeFrame* frame, uint32_t listenWindowUs = 7000U,
               uint32_t spinLimit = 1400000UL);
  bool sampleEnergyDetect(uint8_t* outEdLevel, uint32_t spinLimit = 300000UL);
  ZigbeeTransmitDebug lastTransmitDebug() const;
  ZigbeeReceiveDebug lastReceiveDebug() const;
  void setMacDataRequestPendingCallback(MacDataRequestPendingCallback callback,
                                        void* context = nullptr);
  void setMacFrameReceiveFilterCallback(MacFrameReceiveFilterCallback callback,
                                        void* context = nullptr);

  static bool buildDataFrameShort(uint8_t sequence, uint16_t panId,
                                  uint16_t destinationShort,
                                  uint16_t sourceShort,
                                  const uint8_t* payload, uint8_t payloadLength,
                                  uint8_t* outPsdu, uint8_t* outLength,
                                  bool requestAck = false);
  static bool buildMacCommandFrameShort(uint8_t sequence, uint16_t panId,
                                        uint16_t destinationShort,
                                        uint16_t sourceShort,
                                        uint8_t commandId,
                                        const uint8_t* payload,
                                        uint8_t payloadLength, uint8_t* outPsdu,
                                        uint8_t* outLength,
                                        bool requestAck = false);
  static bool buildMacAcknowledgement(uint8_t sequence, uint8_t* outPsdu,
                                      uint8_t* outLength,
                                      bool framePending = false);
  static bool parseDataFrameShort(const uint8_t* psdu, uint8_t length,
                                  ZigbeeDataFrameView* outView);
  static bool parseMacCommandFrameShort(const uint8_t* psdu, uint8_t length,
                                        ZigbeeMacCommandView* outView);
  static bool parseMacAcknowledgement(const uint8_t* psdu, uint8_t length,
                                      ZigbeeMacAcknowledgementView* outView);

 private:
  bool configureIeee802154();
  bool performCcaCheck(uint32_t spinLimit);
  bool waitForMacAcknowledgement(uint8_t sequence,
                                 bool* outFramePending,
                                 uint32_t spinLimit);
  bool sendMacAcknowledgement(uint8_t sequence, bool framePending,
                              uint32_t spinLimit);
  bool shouldAcceptReceivedMacFrame(const uint8_t* psdu,
                                    uint8_t length) const;
  bool shouldSetMacAckFramePending(const uint8_t* psdu, uint8_t length) const;
  static bool frameRequestsMacAcknowledgement(const uint8_t* psdu,
                                              uint8_t length,
                                              uint8_t* outSequence);
  static bool enableMacAcknowledgementRequest(uint8_t* psdu, uint8_t length,
                                              uint8_t* outSequence);

  NRF_RADIO_Type* radio_;
  bool initialized_;
  bool rfPathOwnedByZigbee_;
  bool receiverArmed_;
  uint8_t channel_;
  alignas(4) uint8_t txPacket_[1 + 127];
  alignas(4) uint8_t rxPacket_[1 + 127];
  ZigbeeTransmitDebug lastTransmitDebug_{};
  ZigbeeReceiveDebug lastReceiveDebug_{};
  MacDataRequestPendingCallback macDataRequestPendingCallback_ = nullptr;
  void* macDataRequestPendingContext_ = nullptr;
  MacFrameReceiveFilterCallback macFrameReceiveFilterCallback_ = nullptr;
  void* macFrameReceiveFilterContext_ = nullptr;
};

struct RawRadioConfig {
  uint8_t frequencyOffsetMhz = 8U;
  uint32_t addressBase0 = 0xC2C2C2C2UL;
  uint8_t addressPrefix0 = 0xC2U;
  int8_t txPowerDbm = -8;
  uint8_t maxPayloadLength = 32U;
  uint32_t crcPolynomial = 0x11021UL;
  uint32_t crcInit = 0xFFFFUL;
  bool whiteningEnabled = false;
  bool bigEndian = true;
};

struct RawRadioPacket {
  uint8_t length;
  int8_t rssiDbm;
  uint8_t payload[255];
};

enum class RawRadioReceiveStatus : uint8_t {
  kIdle = 0,
  kPacket = 1,
  kCrcError = 2,
  kError = 3,
};

class RawRadioLink {
 public:
  explicit RawRadioLink(uint32_t radioBase = nrf54l15::RADIO_BASE);

  bool begin(const RawRadioConfig& config = RawRadioConfig());
  void end();

  bool setFrequencyOffsetMhz(uint8_t frequencyOffsetMhz);
  bool setPipe(uint32_t addressBase0, uint8_t addressPrefix0);
  bool setTxPowerDbm(int8_t dbm);

  bool transmit(const uint8_t* payload, uint8_t length,
                uint32_t spinLimit = 3000000UL);
  bool armReceive();
  RawRadioReceiveStatus pollReceive(RawRadioPacket* outPacket = nullptr,
                                    uint32_t spinLimit = 3000000UL);
  RawRadioReceiveStatus waitForReceive(RawRadioPacket* outPacket,
                                       uint32_t listenWindowUs,
                                       uint32_t spinLimit = 3000000UL);

  bool initialized() const;
  bool receiverArmed() const;
  uint8_t maxPayloadLength() const;
  const RawRadioConfig& config() const;

 private:
  bool configureProprietary1M();
  RawRadioReceiveStatus finishReceive(RawRadioPacket* outPacket,
                                      uint32_t spinLimit,
                                      bool timedOut);

  NRF_RADIO_Type* radio_;
  PowerManager power_;
  RawRadioConfig config_;
  bool initialized_;
  bool receiverArmed_;
  alignas(4) uint8_t txPacket_[1U + 255U];
  alignas(4) uint8_t rxPacket_[1U + 255U];
};

enum class BleAddressType : uint8_t {
  kPublic = 0,
  kRandomStatic = 1,
};

#if !defined(NRF54L15_CLEAN_BLE_DEFAULT_TX_DBM)
#define NRF54L15_CLEAN_BLE_DEFAULT_TX_DBM -8
#endif

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

static constexpr uint8_t kBleLegacyAddressLength = 6U;
static constexpr size_t kBleAddressStringLength = 18U;
static constexpr uint8_t kBleLegacyAdDataMaxLength = 31U;
static constexpr uint8_t kBleLegacyRawPayloadMaxLength =
    static_cast<uint8_t>(kBleLegacyAddressLength + kBleLegacyAdDataMaxLength);
static constexpr uint8_t kCustomGattUuid128Length = 16U;
// Current controller scope supports the documented 995-byte extended payload
// limit using one AUX_ADV_IND followed by three AUX_CHAIN_IND packets.
static constexpr uint16_t kBleExtendedAdvDataMaxLength = 995U;
static constexpr uint8_t kBleExtendedSecondaryPacketCountMax = 4U;

bool parseBleAddressString(const char* text,
                           uint8_t addressOut[kBleLegacyAddressLength]);
size_t formatBleAddressString(const uint8_t address[kBleLegacyAddressLength],
                              char* out, size_t outSize);

enum class BleConnectionRole : uint8_t {
  kNone = 0,
  kPeripheral = 1,
  kCentral = 2,
};

enum BlePhy : uint8_t {
  kBlePhyNone = 0x00U,
  kBlePhy1M = 0x01U,
  kBlePhy2M = 0x02U,
  kBlePhyCoded = 0x04U,
};

enum BleGattCharacteristicProperty : uint8_t {
  kBleGattPropRead = 0x02U,
  kBleGattPropWriteNoRsp = 0x04U,
  kBleGattPropWrite = 0x08U,
  kBleGattPropNotify = 0x10U,
  kBleGattPropIndicate = 0x20U,
};

struct BleScanPacket {
  BleAdvertisingChannel channel;
  int8_t rssiDbm;
  uint8_t pduHeader;
  uint8_t length;
  const uint8_t* payload;
};

struct BleActiveScanResult {
  BleAdvertisingChannel channel;
  int8_t advRssiDbm;
  uint8_t advHeader;
  // Raw legacy PDU payload copied from the air: [AdvA(6)] [AdvData(0..31)].
  uint8_t advPayloadLength;
  bool advertiserAddressRandom;
  uint8_t advertiserAddress[kBleLegacyAddressLength];
  uint8_t advPayload[kBleLegacyRawPayloadMaxLength];
  bool scanResponseReceived;
  int8_t scanRspRssiDbm;
  uint8_t scanRspHeader;
  // Raw legacy SCAN_RSP payload copied from the air: [AdvA(6)] [ScanRspData(0..31)].
  uint8_t scanRspPayloadLength;
  uint8_t scanRspPayload[kBleLegacyRawPayloadMaxLength];

  const uint8_t* advData() const {
    return (advPayloadLength > kBleLegacyAddressLength)
               ? &advPayload[kBleLegacyAddressLength]
               : nullptr;
  }

  uint8_t advDataLength() const {
    return (advPayloadLength > kBleLegacyAddressLength)
               ? static_cast<uint8_t>(advPayloadLength - kBleLegacyAddressLength)
               : 0U;
  }

  const uint8_t* scanRspData() const {
    return (scanRspPayloadLength > kBleLegacyAddressLength)
               ? &scanRspPayload[kBleLegacyAddressLength]
               : nullptr;
  }

  uint8_t scanRspDataLength() const {
    return (scanRspPayloadLength > kBleLegacyAddressLength)
               ? static_cast<uint8_t>(scanRspPayloadLength -
                                      kBleLegacyAddressLength)
               : 0U;
  }
};

struct BleExtendedScanResult {
  BleAdvertisingChannel primaryChannel;
  int8_t primaryRssiDbm;
  uint8_t primaryHeader;
  uint8_t primaryPayloadLength;
  uint8_t advMode;
  bool advertiserAddressRandom;
  uint8_t advertiserAddress[kBleLegacyAddressLength];
  uint16_t did;
  uint8_t sid;
  uint8_t auxChannel;
  uint8_t auxPhy;
  uint32_t auxOffsetUs;
  uint8_t secondaryPacketCount;
  uint16_t dataLength;
  uint8_t data[kBleExtendedAdvDataMaxLength];
  bool scanResponseReceived;
  int8_t scanRspRssiDbm;
  uint8_t scanRspSecondaryPacketCount;
  uint16_t scanRspDataLength;
  uint8_t scanRspData[kBleExtendedAdvDataMaxLength];
};

struct BleAdvInteraction {
  BleAdvertisingChannel channel;
  bool receivedScanRequest;
  bool scanResponseTransmitted;
  bool receivedConnectInd;
  bool connectIndChSel2;
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
  uint8_t txPhy;
  uint8_t rxPhy;
};

struct BleConnectionEvent {
  bool eventStarted;
  bool packetReceived;
  bool crcOk;
  bool emptyAckTransmitted;
  bool packetIsNew;
  bool peerAckedLastTx;
  bool freshTxAllowed;
  bool implicitEmptyAck;
  bool terminateInd;
  bool disconnectReasonValid;
  bool disconnectReasonRemote;
  bool llControlPacket;
  bool attPacket;
  bool txPacketSent;
  uint16_t eventCounter;
  uint8_t dataChannel;
  uint8_t txPhy;
  uint8_t rxPhy;
  int8_t rssiDbm;
  uint8_t llid;
  uint8_t rxNesn;
  uint8_t rxSn;
  uint8_t disconnectReason;
  uint8_t llControlOpcode;
  uint8_t attOpcode;
  uint8_t payloadLength;
  uint8_t txLlid;
  uint8_t txNesn;
  uint8_t txSn;
  uint8_t txPayloadLength;
  const uint8_t* payload;
  const uint8_t* txPayload;
};

enum class BleDisconnectReason : uint8_t {
  kNone = 0U,
  kApi = 1U,
  kSupervisionTimeout = 2U,
  kPeerTerminate = 3U,
  kMicFailure = 4U,
  kInternalTerminate = 5U,
};

struct BleDisconnectDebug {
  uint32_t sequence;
  uint32_t nextEventUs;
  uint16_t eventCounter;
  uint16_t missedEventCount;
  uint8_t valid;
  uint8_t reason;
  uint8_t role;
  uint8_t errorCode;
  uint8_t expectedRxSn;
  uint8_t txSn;
  uint8_t freshTxAllowed;
  uint8_t txHistoryValid;
  uint8_t pendingTxValid;
  uint8_t pendingTxLlid;
  uint8_t pendingTxLength;
  uint8_t lastTxLlid;
  uint8_t lastTxLength;
  uint8_t lastTxOpcode;
  uint8_t lastRxLlid;
  uint8_t lastRxLength;
  uint8_t lastRxOpcode;
  uint8_t lastRxNesn;
  uint8_t lastRxSn;
  uint8_t lastPacketIsNew;
  uint8_t lastPeerAckedLastTx;
};

// Debug counters for link-layer encryption (LL_ENC_REQ/RSP, LL_START_ENC_REQ/RSP).
// Useful when pairing/bonding fails due to tight T_IFS timing.
struct BleEncryptionDebugCounters {
  uint32_t followupArmed;
  uint32_t followupEndSeen;
  uint32_t followupCrcOk;
  uint32_t followupStartEncReqSeen;
  uint32_t followupStartEncRspTxOk;
  uint32_t followupRxLlid1;
  uint32_t followupRxLlid2;
  uint32_t followupRxLlid3;
  uint8_t lastFollowHdr;
  uint8_t lastFollowLlid;
  uint8_t lastFollowLen;
  uint8_t lastFollowByte0;
  uint8_t lastFollowWasNew;
  uint8_t lastFollowWasDecrypted;
  uint16_t reservedFollow0;
  uint32_t lastFollowCounterLo;
  // Main (first) RX/TX observations for start-encryption debugging.
  uint32_t mainEncReqSeen;
  uint32_t mainStartEncReqSeen;
  uint32_t mainStartEncReqSeenDecrypted;
  uint32_t mainEncRspTxOk;
  uint32_t mainStartEncRspTxOk;
  // While awaiting LL_START_ENC_REQ, track any LL control PDU observed so we can
  // see what the peer is sending (plain vs encrypted) without spamming Serial.
  uint32_t startPendingControlRxSeen;
  uint8_t startPendingLastHdr;
  uint8_t startPendingLastLenRaw;
  uint8_t startPendingLastByte0;
  uint8_t startPendingLastDecrypted;
  // TXEN scheduling lag (relative to the intended target) for LL_ENC_RSP.
  uint32_t encRspTxenLagLastUs;
  uint32_t encRspTxenLagMaxUs;
  // Low-overhead MIC failure diagnostics (kept compact for serial print).
  uint32_t encRxMicFailCount;
  uint32_t encRxShortPduCount;
  uint32_t encRxLastMicFailCounterLo;
  uint8_t encRxLastMicFailHdr;
  uint8_t encRxLastMicFailLenRaw;
  uint8_t encRxLastMicFailDir;
  uint8_t encRxLastMicFailState;
  uint8_t encRxLastMicFailData0;
  uint8_t encRxLastMicFailData1;
  uint8_t encRxLastMicFailData2;
  uint8_t encRxLastMicFailData3;
  uint8_t encRxLastMicFailData4;
  // Encryption clear/transition reason breadcrumbs.
  uint32_t encStartRspRxCount;
  uint8_t encStartRspLastRawLen;
  uint8_t encStartRspLastDecrypted;
  uint8_t encStartRspLastHdr;
  uint8_t reserved0;
  uint32_t encPauseReqAcceptedCount;
  uint32_t encPauseRspRxCount;
  uint32_t encClearCount;
  uint8_t encLastClearReason;
  // TXEN scheduling lag diagnostics (all packets and encrypted packets).
  uint32_t txenLagLastUs;
  uint32_t txenLagMaxUs;
  uint32_t encTxenLagLastUs;
  uint32_t encTxenLagMaxUs;
  uint32_t encTxPacketCount;
  uint32_t encRandomHardwareCount;
  uint32_t encRandomFallbackCount;
  uint32_t encRandomPrefetchUseCount;
  uint32_t encRandomPrefetchFillCount;
  uint32_t encLastTxCounterLo;
  uint32_t encLastRxCounterLo;
  uint8_t encLastTxHdr;
  uint8_t encLastTxPlainLen;
  uint8_t encLastTxAirLen;
  uint8_t encLastTxWasFresh;
  uint8_t encLastTxWasEncrypted;
  uint8_t encLastRxHdr;
  uint8_t encLastRxLenRaw;
  uint8_t encLastRxWasNew;
  uint8_t encLastRxWasDecrypted;
  uint8_t reserved1;
  // Last observed ENC_REQ/ENC_RSP key material snapshots.
  uint8_t encLastSkdm[8];
  uint8_t encLastIvm[4];
  uint8_t encLastSkds[8];
  uint8_t encLastIvs[4];
  uint8_t encLastStk[16];
  uint8_t encLastSessionKey[16];
  uint8_t encLastSessionAltKey[16];
  uint8_t encLastSessionKeyValid;
  uint8_t encLastSessionAltKeyValid;
  uint8_t encRxLastMicFailPacketLen;
  uint8_t encRxLastMicFailPacket[32];
  uint8_t encLastRxDir;
  uint8_t encLastTxDir;
  // Connection-level timing and termination breadcrumbs piggyback on the
  // existing debug export so callers can inspect missed-event and caller-late
  // behavior without introducing a new public API.
  uint32_t connLatePollCount;
  uint32_t connRxTimeoutCount;
  uint32_t connRxCrcErrorCount;
  uint32_t connTxTimeoutCount;
  uint32_t connFollowupRxTimeoutCount;
  uint32_t connFollowupRxCrcErrorCount;
  uint32_t connDisconnectLocalCount;
  uint32_t connDisconnectRemoteCount;
  uint32_t connMissedEventCountLast;
  uint32_t connMissedEventCountMax;
  uint32_t connLastRxListenBudgetUs;
  uint32_t connLastFollowupListenBudgetUs;
  uint32_t txenLateCount;
  uint32_t connDeferredEventOverwriteCount;
  uint32_t connDeferredCallbackDropCount;
  uint32_t connDeferredTraceDropCount;
  uint32_t connImplicitAttProgressAckCount;
  uint32_t connChannelMapPendingCount;
  uint32_t connChannelMapAppliedCount;
  uint32_t connChannelMapDuplicateCount;
  uint32_t connChannelMapTimeoutAfterApplyCount;
  uint32_t connLastChannelMapRxEventCounter;
  uint32_t connLastChannelMapInstant;
  uint32_t connLastChannelMapAppliedEventCounter;
  uint32_t connLastChannelMapAppliedDataChannel;
  uint32_t connLastChannelMapTimeoutEventCounter;
  uint32_t connBgServiceThreadCount;
  uint32_t connBgServiceIsrCount;
  uint32_t connBgDueCount;
  uint32_t connBgWakeLagLastUs;
  uint32_t connBgWakeLagMaxUs;
  uint32_t connBgHfxoPrewarmArmCount;
  uint32_t connBgHfxoStopCount;
  uint32_t connBgHfxoFallbackStartCount;
  uint32_t connBgRxHardwareArmCount;
  uint32_t connBgRxHardwareFallbackCount;
  uint32_t connFastAttReadTurnaroundCount;
  uint32_t connFastLlControlTurnaroundCount;
  uint32_t connFastSignalingTurnaroundCount;
  uint32_t connAarResolveAttemptCount;
  uint32_t connAarResolveSuccessCount;
  uint32_t connAarResolveFailureCount;
  uint32_t connAarLastErrorStatus;
  uint8_t connLastDisconnectReason;
  uint8_t connLastDisconnectRemote;
  uint8_t connLastDisconnectValid;
  uint8_t reserved2;
};

struct BleBackgroundAdvertisingDebugCounters {
  uint32_t irqCompareCount;
  uint32_t serviceRunCount;
  uint32_t idleFallbackCount;
  uint32_t randomHardwareCount;
  uint32_t randomFallbackCount;
  uint32_t eventArmCount;
  uint32_t eventCompleteCount;
  uint32_t stageAdvanceCount;
  uint32_t rfPathPrewarmRestoreCount;
  uint32_t rfPathIdleCollapseCount;
  uint32_t constlatServiceObservedCount;
  uint32_t lowPowerReleaseCount;
  uint32_t constlatPrewarmHardwareCount;
  uint32_t constlatPrewarmFallbackCount;
  uint32_t txReadyCount;
  uint32_t txStartKickCount;
  uint32_t txKickRetryCount;
  uint32_t txKickFallbackCount;
  uint32_t clockIrqCount;
  uint32_t clockXotunedCount;
  uint32_t clockXotuneErrorCount;
  uint32_t clockXotuneFailedCount;
  uint32_t txSettleTimeoutCount;
  uint32_t txPhyendCount;
  uint32_t txDisabledCount;
  uint32_t lastEventStartUs;
  uint32_t lastCompletedEventStartUs;
  uint32_t lastServiceUs;
  uint32_t lastRandomDelayUs;
  uint8_t lastChannel;
  uint8_t currentStage;
  uint8_t enabled;
  uint8_t threeChannel;
  uint8_t rfPathManaged;
  uint8_t rfPathActive;
  uint8_t latencyManaged;
  uint8_t constlatActive;
  uint8_t lastRadioState;
  uint8_t lastTxReadySeen;
  uint8_t lastTxPhyendSeen;
  uint8_t lastTxDisabledSeen;
};

struct BleBondRecord {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t peerIdentityAddress[6];
  uint8_t peerIdentityAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t ltk[16];
  uint8_t rand[8];
  uint8_t peerIrk[16];
  uint8_t peerIrkValid;
  uint16_t ediv;
  uint8_t keySize;
  uint8_t reserved[2];
};

using BleBondLoadCallback = bool (*)(BleBondRecord* outRecord, void* context);
using BleBondSaveCallback = bool (*)(const BleBondRecord* record, void* context);
using BleBondClearCallback = bool (*)(void* context);
using BleTraceCallback = void (*)(const char* message, void* context);
using BleGattWriteCallback = void (*)(uint16_t valueHandle, const uint8_t* value,
                                      uint8_t valueLength, bool withResponse,
                                      void* context);

// Minimal BLE LL radio block (legacy ADV + passive scan) implemented on RADIO.
// This class intentionally avoids a full host/controller stack.
class BleRadio {
 public:
  static constexpr uint8_t kCustomGattMaxServices = 8U;
  static constexpr uint8_t kCustomGattMaxCharacteristics = 16U;
  static constexpr uint8_t kCustomGattMaxValueLength = 244U;

  explicit BleRadio(uint32_t radioBase = nrf54l15::RADIO_BASE,
                    uint32_t ficrBase = nrf54l15::FICR_BASE);

  bool begin(int8_t txPowerDbm = NRF54L15_CLEAN_BLE_DEFAULT_TX_DBM);
  void end();

  bool setTxPowerDbm(int8_t dbm);
  bool selectExternalAntenna(bool external);
  bool loadAddressFromFicr(bool forceRandomStatic = true);
  bool setDeviceAddressString(const char* addressText,
                              BleAddressType type = BleAddressType::kRandomStatic);
  bool setDeviceAddress(const uint8_t address[6],
                        BleAddressType type = BleAddressType::kRandomStatic);
  bool getDeviceAddressString(char* out, size_t outSize,
                              BleAddressType* typeOut = nullptr) const;
  bool getDeviceAddress(uint8_t addressOut[6], BleAddressType* typeOut = nullptr) const;

  bool setAdvertisingPduType(BleAdvPduType type);
  bool setAdvertisingChannelSelectionAlgorithm2(bool enabled);
  bool setAdvertisingData(const uint8_t* data, size_t len);
  bool setAdvertisingName(const char* name, bool includeFlags = true);
  bool setExtendedAdvertisingSid(uint8_t sid);
  bool setExtendedAdvertisingAuxChannel(uint8_t dataChannel);
  bool setExtendedAdvertisingData(const uint8_t* data, size_t len);
  bool setExtendedAdvertisingName(const char* name, bool includeFlags = true);
  bool setExtendedScanResponseData(const uint8_t* data, size_t len);
  bool setExtendedScanResponseName(const char* name);
  bool buildAdvertisingPacket();
  bool setGattDeviceName(const char* name);
  bool setGattAppearance(uint16_t appearance);
  bool setGattBatteryLevel(uint8_t percent);
  bool clearCustomGatt();
  bool addCustomGattService(uint16_t uuid16, uint16_t* outServiceHandle = nullptr);
  bool addCustomGattService128(const uint8_t uuid128[16],
                               uint16_t* outServiceHandle = nullptr);
  // Characteristics can only be appended to the most recently added custom
  // service, matching Zephyr's static service-table ordering model.
  bool addCustomGattCharacteristic(uint16_t serviceHandle, uint16_t uuid16,
                                   uint8_t properties,
                                   const uint8_t* initialValue = nullptr,
                                   uint8_t initialValueLength = 0U,
                                   uint16_t* outValueHandle = nullptr,
                                   uint16_t* outCccdHandle = nullptr);
  bool addCustomGattCharacteristic128(uint16_t serviceHandle,
                                      const uint8_t uuid128[16],
                                      uint8_t properties,
                                      const uint8_t* initialValue = nullptr,
                                      uint8_t initialValueLength = 0U,
                                      uint16_t* outValueHandle = nullptr,
                                      uint16_t* outCccdHandle = nullptr);
  bool setCustomGattCharacteristicValue(uint16_t valueHandle,
                                        const uint8_t* value,
                                        uint8_t valueLength);
  bool getCustomGattCharacteristicValue(uint16_t valueHandle,
                                        uint8_t* outValue,
                                        uint8_t* inOutValueLength) const;
  bool notifyCustomGattCharacteristic(uint16_t valueHandle,
                                      bool indicate = false);
  bool isCustomGattCccdEnabled(uint16_t valueHandle,
                               bool indication = false) const;
  bool isCustomGattNotificationQueued(uint16_t valueHandle,
                                      bool indicate = false) const;
  uint16_t currentDataLength() const;
  uint16_t currentAttMtu() const;
  uint8_t currentTxPhy() const;
  uint8_t currentRxPhy() const;
  uint8_t getPHY() const;
  uint8_t maxNotificationValueLength() const;
  bool setCustomGattWriteHandler(uint16_t valueHandle,
                                 BleGattWriteCallback callback,
                                 void* context = nullptr);
  void setCustomGattWriteCallback(BleGattWriteCallback callback,
                                  void* context = nullptr);

  bool setScanResponseData(const uint8_t* data, size_t len);
  bool setScanResponseName(const char* name);
  bool buildScanResponsePacket();
  // Set advertising to Flags + a single 128-bit service UUID (canonical big-endian
  // input, reversed to BLE little-endian on air).  Automatically moves the GAP
  // device name to the scan response if no scan-response data has been set yet.
  bool setAdvertisingServiceUuid128(const uint8_t uuid128[kCustomGattUuid128Length]);

  // TX-only legacy advertising helpers. These do not listen for SCAN_REQ or
  // CONNECT_IND, so they require kAdvNonConnInd.
  bool advertiseOnce(BleAdvertisingChannel channel,
                     uint32_t spinLimit = 600000UL);
  bool advertiseEvent(uint32_t interChannelDelayUs = 350U,
                      uint32_t spinLimit = 600000UL);
  // First controller-offload milestone: one legacy non-connectable advertising
  // packet on one primary advertising channel with fixed interval scheduling.
  // Optional Bluetooth random advertising delay is applied inside the
  // background scheduler when requested. Software still owns interval
  // programming and packet state; GRTC/DPPI/PPIB own the exact HFXO/RADIO
  // launch path for each event.
  bool beginBackgroundAdvertising(
      uint32_t intervalMs,
      BleAdvertisingChannel channel = BleAdvertisingChannel::k37,
      uint32_t hfxoLeadUs = 1200U,
      bool addRandomDelay = false);
  bool beginBackgroundAdvertising3Channel(uint32_t intervalMs,
                                          uint32_t interChannelDelayUs = 350U,
                                          uint32_t hfxoLeadUs = 1200U,
                                          bool addRandomDelay = false);
  void stopBackgroundAdvertising();
  bool isBackgroundAdvertisingEnabled() const;
  uint8_t getBackgroundAdvertisingLastStopReason() const;
  void getBackgroundAdvertisingDebugCounters(
      BleBackgroundAdvertisingDebugCounters* out) const;
  void clearBackgroundAdvertisingDebugCounters();
  bool advertiseExtendedEvent(uint32_t auxOffsetUs = 3000U,
                              uint32_t interPrimaryDelayUs = 350U,
                              uint32_t spinLimit = 600000UL);
  bool advertiseExtendedScannableEvent(uint32_t auxOffsetUs = 3000U,
                                       uint32_t interPrimaryDelayUs = 350U,
                                       uint32_t requestListenSpinLimit = 250000UL,
                                       uint32_t spinLimit = 600000UL);

  // Advertise and listen for SCAN_REQ / CONNECT_IND on a single channel.
  // Use kAdvInd or kAdvScanInd. Directed legacy advertising is not supported
  // by this helper.
  bool advertiseInteractOnce(BleAdvertisingChannel channel,
                             BleAdvInteraction* interaction,
                             uint32_t requestListenSpinLimit = 250000UL,
                             uint32_t spinLimit = 700000UL);
  bool advertiseInteractEvent(BleAdvInteraction* interaction,
                              uint32_t interChannelDelayUs = 350U,
                              uint32_t requestListenSpinLimit = 250000UL,
                              uint32_t spinLimit = 700000UL);

  bool initiateConnection(const uint8_t peerAddress[6], bool peerAddressRandom,
                          uint16_t intervalUnits = 24U,
                          uint16_t supervisionTimeoutUnits = 200U,
                          uint8_t hopIncrement = 9U,
                          uint32_t perChannelScanSpinLimit = 300000UL);
  bool queueAttRequest(const uint8_t* attPayload, uint8_t attPayloadLength);
  bool queueAttReadRequest(uint16_t handle);
  bool queueAttWriteRequest(uint16_t handle, const uint8_t* value,
                            uint8_t valueLength, bool withResponse = true);
  bool queueAttCccdWrite(uint16_t cccdHandle, bool notify,
                         bool indicate = false, bool withResponse = true);
  void setPreferredConnectionParameters(uint16_t intervalMinUnits,
                                        uint16_t intervalMaxUnits,
                                        uint16_t latency,
                                        uint16_t timeoutUnits);
  bool setPreferredPhyOptions(uint8_t txPhys, uint8_t rxPhys);
  bool requestPHY(uint8_t phy);
  bool requestPreferredPhy(uint8_t txPhys, uint8_t rxPhys);
  bool requestDataLengthUpdate();
  bool requestAttMtuExchange(uint16_t mtu);
  bool isConnected() const;
  BleConnectionRole connectionRole() const;
  bool isConnectionEncrypted() const;
  // Send an SMP Security Request (peripheral → central) to prompt the central
  // to initiate LE legacy JustWorks pairing.  Call once after each connection
  // when no stored bond is expected.  Returns false if a bond is already primed
  // or encryption is already in progress.
  bool sendSmpSecurityRequest();
  bool getConnectionInfo(BleConnectionInfo* info) const;
  void getEncryptionDebugCounters(BleEncryptionDebugCounters* out) const;
  void clearEncryptionDebugCounters();
  bool getDisconnectDebug(BleDisconnectDebug* out) const;
  void clearDisconnectDebug();
  bool hasBondRecord() const;
  bool getBondRecord(BleBondRecord* outRecord) const;
  bool clearBondRecord(bool clearPersistentStorage = true);
  void setBondPersistenceCallbacks(BleBondLoadCallback loadCallback,
                                   BleBondSaveCallback saveCallback,
                                   BleBondClearCallback clearCallback = nullptr,
                                   void* context = nullptr);
  void setTraceCallback(BleTraceCallback callback, void* context = nullptr);
  bool getLastDisconnectReason(uint8_t* outReason,
                               bool* outRemote = nullptr) const;
  bool getLatestConnectionRssiDbm(int8_t* outRssiDbm) const;
  bool disconnect(uint32_t spinLimit = 300000UL);
  bool pollConnectionEvent(BleConnectionEvent* event = nullptr,
                           uint32_t spinLimit = 400000UL);
  void setBackgroundConnectionServiceEnabled(bool enabled);
  bool isBackgroundConnectionServiceEnabled() const;
  bool consumeDeferredConnectionEvent(BleConnectionEvent* event);

  bool scanOnce(BleAdvertisingChannel channel, BleScanPacket* packet,
                uint32_t spinLimit = 900000UL);
  bool scanCycle(BleScanPacket* packet, uint32_t perChannelSpinLimit = 300000UL);
  bool scanActiveOnce(BleAdvertisingChannel channel, BleActiveScanResult* result,
                      uint32_t advListenSpinLimit = 900000UL,
                      uint32_t scanRspListenSpinLimit = 300000UL,
                      uint32_t spinLimit = 900000UL);
  bool scanActiveCycle(BleActiveScanResult* result,
                       uint32_t perChannelAdvListenSpinLimit = 300000UL,
                       uint32_t scanRspListenSpinLimit = 300000UL);
  bool scanExtendedOnce(BleAdvertisingChannel channel,
                        BleExtendedScanResult* result,
                        uint32_t primaryListenSpinLimit = 900000UL,
                        uint32_t secondaryListenSpinLimit = 300000UL,
                        uint32_t spinLimit = 900000UL);
  bool scanExtendedCycle(BleExtendedScanResult* result,
                         uint32_t perChannelPrimaryListenSpinLimit = 300000UL,
                         uint32_t secondaryListenSpinLimit = 300000UL);
  bool scanExtendedActiveOnce(BleAdvertisingChannel channel,
                              BleExtendedScanResult* result,
                              uint32_t primaryListenSpinLimit = 900000UL,
                              uint32_t secondaryListenSpinLimit = 300000UL,
                              uint32_t scanRspListenSpinLimit = 300000UL,
                              uint32_t spinLimit = 900000UL);
  bool scanExtendedActiveCycle(BleExtendedScanResult* result,
                               uint32_t perChannelPrimaryListenSpinLimit = 300000UL,
                               uint32_t secondaryListenSpinLimit = 300000UL,
                               uint32_t scanRspListenSpinLimit = 300000UL);

 private:
  friend void ::nrf54l15_clean_ble_idle_service(void);
  friend void ::nrf54l15_clean_ble_yield_service(void);
  static constexpr uint16_t kCustomGattHandleStart = 0x0020U;
  static constexpr uint16_t kCustomGattHandleEnd = 0x00FFU;

  struct BleCustomUuidState {
    uint8_t length;
    uint8_t bytes[kCustomGattUuid128Length];
  };

  struct BleCustomServiceState {
    uint8_t uuidSize;
    uint8_t uuid[16];
    uint16_t serviceHandle;
    uint16_t endHandle;
  };

  struct BleCustomCharacteristicState {
    uint16_t serviceHandle;
    uint8_t uuidSize;
    uint8_t uuid[16];
    uint8_t properties;
    uint16_t declarationHandle;
    uint16_t valueHandle;
    uint16_t cccdHandle;
    uint16_t cccdValue;
    uint8_t valueLength;
    uint8_t value[kCustomGattMaxValueLength];
  };

  struct BleCustomWriteHandlerState {
    uint16_t valueHandle;
    BleGattWriteCallback callback;
    void* context;
  };

  struct BleDeferredGattWriteState {
    BleGattWriteCallback callback;
    void* context;
    uint16_t valueHandle;
    uint8_t valueLength;
    uint8_t withResponse;
    uint8_t value[kCustomGattMaxValueLength];
  };

  struct BleLegacyAdvertisingConfigSnapshot {
    BleAddressType addressType;
    BleAdvPduType pduType;
    bool useChSel2;
    bool scanRspDataAutoDefault;
    uint8_t address[6];
    uint8_t advData[kBleLegacyAdDataMaxLength];
    size_t advDataLen;
    uint8_t scanRspData[kBleLegacyAdDataMaxLength];
    size_t scanRspDataLen;
  };

  struct BleBackgroundAdvertisingRestartState {
    bool active;
    bool threeChannel;
    bool randomDelay;
    BleAdvertisingChannel channel;
    uint32_t intervalMs;
    uint32_t interChannelDelayUs;
    uint32_t hfxoLeadUs;
  };

  struct BleQueuedCustomNotificationState;

  bool configureBle1M();
  bool configureBlePhy(uint8_t phy);
  bool beginUnconnectedRadioActivity(uint32_t spinLimit = 1500000UL);
  void endUnconnectedRadioActivity();
  bool ensureRfPathActiveForBle();
  void releaseRfPathForBle();
  void captureLegacyAdvertisingConfigSnapshot(
      BleLegacyAdvertisingConfigSnapshot* out) const;
  void restoreLegacyAdvertisingConfigSnapshot(
      const BleLegacyAdvertisingConfigSnapshot& snapshot);
  void captureBackgroundAdvertisingRestartState(
      BleBackgroundAdvertisingRestartState* out) const;
  bool restartBackgroundAdvertisingFromState(
      const BleBackgroundAdvertisingRestartState& state);
  void snapshotBackgroundAdvertisingRfPathRestoreState();
  void restoreBackgroundAdvertisingRfPathRestoreState();
  bool buildExtendedAdvertisingPackets(uint32_t auxOffsetUs,
                                       uint32_t* actualAuxOffsetUs = nullptr);
  bool buildExtendedScannableAdvertisingPackets(
      uint32_t auxOffsetUs,
      uint32_t* actualAuxOffsetUs = nullptr,
      uint32_t* actualScanRspChainOffsetUs = nullptr);
  bool kickPreparedPacketOnCurrentChannel(const uint8_t* packet);
  bool transmitPreparedPacketOnCurrentChannel(const uint8_t* packet,
                                              uint32_t spinLimit,
                                              uint32_t* txReadyUs = nullptr);
  bool advertiseOncePrepared(BleAdvertisingChannel channel, uint32_t spinLimit);
  bool advertiseInteractOncePrepared(BleAdvertisingChannel channel,
                                     BleAdvInteraction* interaction,
                                     uint32_t requestListenSpinLimit,
                                     uint32_t spinLimit);
  bool receivePacketOnCurrentChannel(uint32_t listenSpinLimit,
                                     uint32_t spinLimit,
                                     uint8_t* outHeader,
                                     uint8_t* outPayloadLength,
                                     const uint8_t** outPayload,
                                     int8_t* outRssiDbm,
                                     uint32_t* outStartUs = nullptr,
                                     uint32_t* outEndUs = nullptr);
  bool handleExtendedScanRequestAndMaybeRespond(uint32_t requestListenSpinLimit,
                                                uint32_t scanRspChainOffsetUs,
                                                uint32_t spinLimit);
  bool waitDisabled(uint32_t spinLimit);
  bool waitForEnd(uint32_t spinLimit);
  bool setAdvertisingChannel(BleAdvertisingChannel channel);
  bool setDataChannel(uint8_t dataChannel);
  bool initiateConnectionOnce(BleAdvertisingChannel channel,
                              const uint8_t peerAddress[6],
                              bool peerAddressRandom, uint16_t intervalUnits,
                              uint16_t supervisionTimeoutUnits,
                              uint8_t hopIncrement,
                              uint32_t advListenSpinLimit,
                              uint32_t spinLimit);
  bool handleRequestAndMaybeRespond(BleAdvertisingChannel channel,
                                    BleAdvInteraction* interaction,
                                    uint32_t requestListenSpinLimit,
                                    uint32_t spinLimit);
  bool startCentralConnection(const uint8_t peerAddress[6],
                              bool peerAddressRandom,
                              uint32_t accessAddress, uint32_t crcInit,
                              uint16_t intervalUnits,
                              uint16_t supervisionTimeoutUnits,
                              uint8_t hopIncrement, bool useChSel2,
                              uint32_t connectIndEndUs);
  bool startConnectionFromConnectInd(const uint8_t* payload, uint8_t length,
                                     bool peerAddressRandom, bool useChSel2,
                                     uint32_t connectIndEndUs);
  bool addCustomGattServiceCommon(const uint8_t* uuid, uint8_t uuidSize,
                                  uint16_t* outServiceHandle);
  bool addCustomGattCharacteristicCommon(uint16_t serviceHandle,
                                         const uint8_t* uuid, uint8_t uuidSize,
                                         uint8_t properties,
                                         const uint8_t* initialValue,
                                         uint8_t initialValueLength,
                                         uint16_t* outValueHandle,
                                         uint16_t* outCccdHandle);
  bool pollCentralConnectionEvent(BleConnectionEvent* event,
                                  uint32_t spinLimit);
  bool buildLlControlResponse(const uint8_t* payload, uint8_t length,
                              uint16_t currentEventCounter,
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
  uint16_t currentTxDataPduPayloadLength() const;
  uint16_t currentRxDataPduPayloadLength() const;
  uint16_t currentMaxAttPayloadLength() const;
  uint16_t currentDataLengthMaxTimeUs() const;
  uint16_t localMaxDataPduPayloadLength() const;
  uint16_t localPreferredAttMtu() const;
  uint8_t supportedBlePhys(uint8_t phys) const;
  uint8_t symmetricBlePhys(uint8_t txPhys, uint8_t rxPhys) const;
  uint8_t preferredSymmetricBlePhys() const;
  uint8_t normalizedLocalBlePhys(uint8_t phys) const;
  uint8_t selectPreferredBlePhy(uint8_t phys) const;
  bool connectionUsesCodedPhy() const;
  uint16_t minimumDataLengthTimeUsForPhy(uint8_t phy) const;
  uint16_t maximumDataLengthTimeUsForPhy(uint8_t phy) const;
  uint16_t payloadOctetsForDataLengthTimeUs(uint16_t maxTimeUs,
                                            uint8_t phy) const;
  void updateConnectionDataLengthFromPeer(uint16_t peerMaxRxOctets,
                                          uint16_t peerMaxRxTimeUs,
                                          uint16_t peerMaxTxOctets,
                                          uint16_t peerMaxTxTimeUs);
  bool queuePhyUpdateRequest();
  bool connParamsAreValid(uint16_t intervalMin, uint16_t intervalMax,
                          uint16_t latency, uint16_t timeout) const;
  uint16_t chooseAcceptedConnIntervalUnits(uint16_t intervalMin,
                                           uint16_t intervalMax) const;
  bool queueCentralDataLengthRequest();
  bool queueCentralAttMtuRequest();
  bool queuePeripheralConnParamUpdateRequest();
  bool queueCentralConnParamUpdateInd();
  void maybeQueueCentralLinkSetupRequest();
  bool applyPendingConnectionPhyUpdateAtInstant(uint16_t currentEventCounter);
  void applyPendingConnectionUpdateAtInstant(uint16_t currentEventCounter,
                                             uint32_t currentEventAnchorUs);
  void clearPreparedWriteState();
  bool applyCccdState(uint16_t handle, uint16_t cccd);
  bool buildAttErrorResponse(uint8_t requestOpcode, uint16_t handle,
                             uint8_t errorCode, uint8_t* outAttResponse,
                             uint16_t* outAttResponseLength) const;
  uint8_t readAttributeValue(uint16_t handle, uint16_t offset, uint8_t* outValue,
                             uint8_t maxLen) const;
  void clearSmpPairingState();
  void clearEncryptionState();
  void clearConnectionSecurityState();
  void prefetchConnectionSecurityMaterial(uint32_t spinLimit);
  bool isBondRecordUsable(const BleBondRecord& record) const;
  bool loadBondRecordFromPersistence();
  bool persistBondRecord(const BleBondRecord& record);
  bool clearPersistentBondRecord();
  bool buildCurrentBondRecord(BleBondRecord* outRecord) const;
  bool primeBondForCurrentPeer();
  void clearCustomGattConnectionState();
  bool addCustomGattServiceUuid(const uint8_t* uuidBytes, uint8_t uuidLength,
                                uint16_t* outServiceHandle);
  bool addCustomGattCharacteristicUuid(uint16_t serviceHandle,
                                       const uint8_t* uuidBytes,
                                       uint8_t uuidLength,
                                       uint8_t properties,
                                       const uint8_t* initialValue,
                                       uint8_t initialValueLength,
                                       uint16_t* outValueHandle,
                                       uint16_t* outCccdHandle);
  BleCustomServiceState* findCustomServiceByHandle(uint16_t serviceHandle);
  const BleCustomServiceState* findCustomServiceByHandle(uint16_t serviceHandle) const;
  BleCustomCharacteristicState* findCustomCharacteristicByValueHandle(uint16_t valueHandle);
  const BleCustomCharacteristicState* findCustomCharacteristicByValueHandle(
      uint16_t valueHandle) const;
  BleCustomWriteHandlerState* findCustomGattWriteHandler(uint16_t valueHandle);
  const BleCustomWriteHandlerState* findCustomGattWriteHandler(
      uint16_t valueHandle) const;
  BleCustomCharacteristicState* findCustomCharacteristicByCccdHandle(uint16_t cccdHandle);
  const BleCustomCharacteristicState* findCustomCharacteristicByCccdHandle(
      uint16_t cccdHandle) const;
  bool writeCustomGattCharacteristic(uint16_t handle, const uint8_t* value,
                                     uint16_t valueLength, bool withResponse,
                                     uint8_t* outErrorCode);
  void emitBleTrace(const char* message) const;
  void rememberDisconnectReason(uint8_t reason, bool remote);
  bool prepareBackgroundAdvertisingEvent();
  bool configureBackgroundAdvertisingHardware(bool enable);
  bool armBackgroundAdvertising(uint32_t targetTxUs, bool prewarmHfxo,
                                bool stopHfxoAfterTx);
  bool rescheduleBackgroundAdvertisingTxCompare(uint8_t compareChannel);
  bool rescheduleBackgroundAdvertisingService(uint8_t compareChannel);
  bool reconfigureBackgroundAdvertisingTxKick(bool useIrqKick);
  bool handleBackgroundAdvertisingPrewarmEvent();
  bool settleBackgroundAdvertisingRadioBeforeService(uint32_t waitBudgetUs);
  void collapseBackgroundAdvertisingRfPathIfManaged();
  bool backgroundAdvertisingConsumePendingPrewarmFromFallback();
  bool backgroundAdvertisingNeedsIdleService() const;
  bool backgroundAdvertisingConsumePendingCompareFromFallback();
  bool serviceBackgroundAdvertising();
  bool backgroundServicesActive() const;
  bool configureBackgroundConnectionHardware(bool enable);
  bool previewBackgroundConnectionDataChannel(uint8_t* outDataChannel) const;
  bool prepareBackgroundConnectionReceiveWindow(uint32_t rxStartUs);
  bool armBackgroundConnectionService();
  void stopBackgroundConnectionService();
  void snapshotDeferredConnectionEvent(const BleConnectionEvent& event);
  bool serviceBackgroundConnectionEvent();
  bool enqueueDeferredGattWrite(BleGattWriteCallback callback, void* context,
                                uint16_t valueHandle, const uint8_t* value,
                                uint8_t valueLength, bool withResponse);
  void dispatchDeferredGattWrites();
  bool enqueueDeferredTrace(const char* message);
  void dispatchDeferredTraces();
  void serviceDeferredApplicationWork();
  static void resetConnectionEventStruct(BleConnectionEvent* event);
  bool disconnectWithReason(BleDisconnectReason reason, uint8_t errorCode,
                            uint32_t spinLimit);
  bool pollConnectionEventInternal(BleConnectionEvent* event,
                                   uint32_t spinLimit);
  void serviceBackgroundConnection(uint32_t spinLimit = 120000UL);
  void clearQueuedConnectionEvents();
  bool enqueueConnectionEvent(const BleConnectionEvent& event);
  bool dequeueConnectionEvent(BleConnectionEvent* event);
  static bool shouldQueueConnectionEvent(const BleConnectionEvent& event);
  void rememberLatestConnectionRssi(const BleConnectionEvent& event);
  bool enqueueCustomGattNotification(uint8_t characteristicIndex, bool indicate,
                                     const uint8_t* value,
                                     uint8_t valueLength);
  bool peekCustomGattNotification(
      BleQueuedCustomNotificationState* outNotification) const;
  void popCustomGattNotification();
  void filterCustomGattNotificationQueue(uint8_t characteristicIndex,
                                         bool notificationsEnabled,
                                         bool indicationsEnabled);
  void resetDisconnectDebugState();
  void rememberConnectionRxDebug(uint8_t llid, uint8_t rxLength,
                                 uint8_t protocolOpcode, uint8_t nesn,
                                 uint8_t sn, bool packetIsNew,
                                 bool peerAckedLastTx);
  void recordDisconnectDebug(BleDisconnectReason reason, uint8_t errorCode);
  void updateNextConnectionEventTime();
  void armConnectionResyncWindow(uint32_t extraPostAnchorUs, uint8_t attempts);
  uint8_t selectNextDataChannel(bool useCurrentEventCounter);
  void restoreAdvertisingLinkDefaults();
  static uint32_t txPowerRegFromDbm(int8_t dbm);
  friend void nrf54l15_ble_idle_service(void);
  friend void nrf54l15_ble_grtc_irq_service(void);
  friend void nrf54l15_ble_clock_irq_service(void);
  friend uint32_t nrf54l15_ble_grtc_reserved_cc_mask(void);

  static constexpr uint8_t kConnectionEventQueueDepth = 4U;
  static constexpr uint8_t kCustomGattNotificationQueueDepth = 8U;
  struct BleQueuedConnectionEvent {
    BleConnectionEvent event;
    uint8_t payload[255];
    uint8_t txPayload[255];
  };

  struct BleQueuedCustomNotificationState {
    bool valid;
    uint8_t characteristicIndex;
    bool indication;
    uint8_t valueLength;
    uint8_t value[kCustomGattMaxValueLength];
  };

  NRF_RADIO_Type* radio_;
  NRF_FICR_Type* ficr_;
  bool initialized_;
  BleAddressType addressType_;
  BleAdvPduType pduType_;
  bool useChSel2_;
  bool externalAntenna_;
  uint8_t address_[6];
  uint8_t advData_[kBleLegacyAdDataMaxLength];
  size_t advDataLen_;
  uint8_t extendedAdvData_[kBleExtendedAdvDataMaxLength];
  size_t extendedAdvDataLen_;
  uint8_t extendedScanRspData_[kBleExtendedAdvDataMaxLength];
  size_t extendedScanRspDataLen_;
  uint8_t extendedAdvSid_;
  uint16_t extendedAdvDid_;
  uint8_t extendedAdvAuxChannel_;
  uint8_t extendedSecondaryPacketCount_;
  uint8_t extendedScanRspPacketCount_;
  bool scanRspDataAutoDefault_;
  uint8_t scanRspData_[kBleLegacyAdDataMaxLength];
  size_t scanRspDataLen_;
  alignas(4) uint8_t txPacket_[2 + 251];
  alignas(4) uint8_t extendedPrimaryPacket_[2 + 255];
  alignas(4) uint8_t extendedAuxAdvPacket_[2 + 255];
  alignas(4) uint8_t extendedSecondaryPackets_[kBleExtendedSecondaryPacketCountMax][2 + 255];
  alignas(4) uint8_t extendedScanRspPackets_[kBleExtendedSecondaryPacketCountMax][2 + 255];
  alignas(4) uint8_t scanRspPacket_[2 + 6 + 31];
  alignas(4) uint8_t rxPacket_[2 + 255];
  alignas(4) uint8_t connectionTxPayload_[255];

  bool connected_;
  bool rfPathOwnedByBle_;
  bool backgroundAdvertisingRestoreRfPathValid_;
  bool backgroundAdvertisingRestoreRfPathPowerEnabled_;
  BoardAntennaPath backgroundAdvertisingRestoreRfPath_;
  BleConnectionRole connectionRole_;
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
  bool connectionUseChSel2_;
  uint16_t connectionChannelId_;
  uint8_t connectionSca_;
  uint8_t connectionChanUse_;
  uint8_t connectionExpectedRxSn_;
  uint8_t connectionTxSn_;
  uint8_t connectionLastTxSn_;
  bool connectionTxHistoryValid_;
  bool connectionFreshTxAllowed_;
  uint16_t connectionEventCounter_;
  uint16_t connectionMissedEventCount_;
  uint32_t connectionNextEventUs_;
  uint32_t connectionFirstEventListenUs_;
  uint8_t connectionSyncAttemptsRemaining_;
  uint8_t connectionCurrentTxPhy_;
  uint8_t connectionCurrentRxPhy_;
  uint8_t connectionPreferredTxPhyMask_;
  uint8_t connectionPreferredRxPhyMask_;
  bool connectionPhyRequestPending_;
  bool connectionPhyRequestInFlight_;
  bool connectionPhyUpdatePending_;
  uint16_t connectionPhyUpdateInstant_;
  uint8_t connectionPendingTxPhy_;
  uint8_t connectionPendingRxPhy_;
  uint16_t connectionMaxTxPayloadLength_;
  uint16_t connectionMaxRxPayloadLength_;
  bool connectionDataLengthUpdatePending_;
  bool connectionDataLengthUpdateInFlight_;
  bool connectionDataLengthUpdateComplete_;
  bool connectionAttMtuExchangePending_;
  bool connectionAttMtuExchangeInFlight_;
  bool connectionAttMtuExchangeComplete_;
  bool connectionConnParamUpdatePending_;
  bool connectionConnParamUpdateInFlight_;
  uint8_t connectionConnParamUpdateIdentifier_;
  bool connectionCentralConnParamIndPending_;
  uint16_t connectionCentralConnParamIntervalUnits_;
  uint16_t connectionCentralConnParamLatency_;
  uint16_t connectionCentralConnParamTimeoutUnits_;
  uint16_t connectionRequestedAttMtu_;
  uint16_t connectionAttMtu_;
  uint32_t connectionCentralLinkSetupNotBeforeMs_;
  uint8_t connectionLastTxLlid_;
  uint8_t connectionLastTxLength_;
  // Snapshot of the last transmitted plaintext payload (before any encryption),
  // so callers can safely inspect `BleConnectionEvent::txPayload` even though
  // `connectionTxPayload_` is reused later in the connection event.
  uint8_t connectionLastTxPlainLlid_;
  uint8_t connectionLastTxPlainLength_;
  uint8_t connectionLastTxPlainPayload_[255];
  uint8_t connectionPendingTxLlid_;
  uint8_t connectionPendingTxLength_;
  bool connectionPendingTxValid_;
  uint8_t connectionPendingTxPayload_[255];
  bool connectionUpdatePending_;
  uint16_t connectionUpdateInstant_;
  uint8_t connectionPendingWinSize_;
  uint16_t connectionPendingWinOffset_;
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
  static constexpr uint8_t kDeferredConnectionEventDepth = 8U;
  bool backgroundAdvertisingEnabled_;
  bool backgroundAdvertisingArmed_;
  bool backgroundAdvertisingInProgress_;
  bool backgroundAdvertisingDue_;
  bool backgroundAdvertisingThreeChannel_;
  bool backgroundAdvertisingRandomDelay_;
  bool backgroundAdvertisingManageRfPath_;
  bool backgroundAdvertisingRfPathCollapsed_;
  bool backgroundAdvertisingUseIrqTxKick_;
  bool backgroundAdvertisingManageLatency_;
  bool backgroundAdvertisingConstlatHardwareVerified_;
  bool backgroundAdvertisingAwaitingHfxoTuned_;
  BleAdvertisingChannel backgroundAdvertisingChannel_;
  uint32_t backgroundAdvertisingIntervalUs_;
  uint32_t backgroundAdvertisingInterChannelDelayUs_;
  uint32_t backgroundAdvertisingHfxoLeadUs_;
  uint32_t backgroundAdvertisingNextTxUs_;
  uint32_t backgroundAdvertisingServiceUs_;
  uint32_t backgroundAdvertisingEventStartTxUs_;
  uint32_t backgroundAdvertisingRandomState_;
  uint8_t backgroundAdvertisingPrimaryStage_;
  uint8_t backgroundAdvertisingLastStopReason_;
  uint8_t backgroundAdvertisingTxKickRetryCountCurrent_;
  uint8_t backgroundAdvertisingPendingTxCompareChannel_;
  uint8_t backgroundAdvertisingPendingServiceCompareChannel_;
  BleBackgroundAdvertisingDebugCounters backgroundAdvertisingDebug_;
  bool backgroundConnectionServiceEnabled_;
  bool backgroundConnectionServiceArmed_;
  bool backgroundConnectionServiceInProgress_;
  bool backgroundConnectionServiceDue_;
  bool backgroundConnectionRxPrepared_;
  uint8_t deferredConnectionEventHead_;
  uint8_t deferredConnectionEventTail_;
  uint8_t deferredConnectionEventCount_;
  uint8_t backgroundConnectionPreparedDataChannel_;
  uint32_t backgroundConnectionWakeUs_;
  uint8_t deferredConnectionPayload_[kDeferredConnectionEventDepth][255];
  uint8_t deferredConnectionTxPayload_[kDeferredConnectionEventDepth][255];
  uint8_t consumedDeferredConnectionPayload_[255];
  uint8_t consumedDeferredConnectionTxPayload_[255];
  BleConnectionEvent deferredConnectionEvents_[kDeferredConnectionEventDepth];
  BleDeferredGattWriteState deferredGattWrites_[4];
  const char* deferredTraces_[8];
  uint8_t deferredGattWriteHead_;
  uint8_t deferredGattWriteTail_;
  uint8_t deferredGattWriteCount_;
  uint8_t deferredTraceHead_;
  uint8_t deferredTraceTail_;
  uint8_t deferredTraceCount_;
  uint8_t lastDisconnectReason_;
  bool lastDisconnectReasonValid_;
  bool lastDisconnectReasonRemote_;
  uint8_t smpPairingState_;
  uint8_t smpPairingReq_[7];
  uint8_t smpPairingRsp_[7];
  uint8_t smpPeerConfirm_[16];
  uint8_t smpPeerRandom_[16];
  uint8_t smpLocalRandom_[16];
  bool smpPrefetchedLocalRandomValid_;
  uint8_t smpPrefetchedLocalRandom_[16];
  uint8_t smpLocalConfirm_[16];
  uint8_t smpStk_[16];
  bool smpStkValid_;
  bool connectionEncSessionValid_;
  bool connectionEncRxEnabled_;
  bool connectionEncTxEnabled_;
  bool connectionEncStartReqPending_;
  bool connectionEncStartReqTxPending_;
  bool connectionEncAwaitingStartRsp_;
  bool connectionEncEnableTxOnNextEvent_;
  uint64_t connectionEncRxCounter_;
  uint64_t connectionEncTxCounter_;
  bool connectionEncKeyDerivationPending_;
  uint8_t connectionEncSkd_[16];
  uint8_t connectionEncSessionKey_[16];
  uint8_t connectionEncSessionKeyAlt_[16];
  bool connectionEncAltKeyValid_;
  uint8_t connectionEncRxDirection_;
  uint8_t connectionEncTxDirection_;
  uint8_t connectionEncIv_[8];
  bool connectionPrefetchedEncMaterialValid_;
  uint8_t connectionPrefetchedSkds_[8];
  uint8_t connectionPrefetchedIvs_[4];
  bool connectionLastTxWasEncrypted_;
  uint8_t connectionLastTxEncryptedLength_;
  uint8_t connectionLastTxEncryptedPayload_[255];  // max DLE payload (251) + AES-CCM MIC (4)
  bool connectionEncPrecomputedEmptyValid_;
  uint64_t connectionEncPrecomputedCounter_;
  uint8_t connectionEncPrecomputedPayload_[4];
  bool connectionEncPrecomputedStartRspValid_;
  uint8_t connectionEncPrecomputedStartRsp_[1 + 4];
  bool connectionEncPrecomputedStartRspTxValid_;
  uint64_t connectionEncPrecomputedStartRspTxCounter_;
  uint8_t connectionEncPrecomputedStartRspTx_[1 + 4];
  BleBondLoadCallback bondLoadCallback_;
  BleBondSaveCallback bondSaveCallback_;
  BleBondClearCallback bondClearCallback_;
  void* bondCallbackContext_;
  BleBondRecord bondRecord_;
  bool bondRecordValid_;
  bool bondStorageLoaded_;
  bool bondKeyPrimedForConnection_;
  BleTraceCallback traceCallback_;
  void* traceCallbackContext_;
  bool smpBondingRequested_;
  bool smpExpectInitiatorEncKey_;
  bool smpExpectInitiatorIdKey_;
  bool smpPeerLtkValid_;
  bool smpPeerLtkAwaitMasterId_;
  bool smpPeerIrkValid_;
  bool smpPeerIdentityAddressValid_;
  bool smpPeerIdentityAddressRandom_;
  uint8_t smpPeerLtk_[16];
  uint8_t smpPeerIrk_[16];
  uint8_t smpPeerIdentityAddress_[6];
  uint8_t smpEncReqRand_[8];
  uint16_t smpEncReqEdiv_;
  uint8_t smpKeySize_;
  uint8_t advCycleStartIndex_;
  uint8_t scanCycleStartIndex_;
  uint8_t gapDeviceName_[31];
  uint8_t gapDeviceNameLen_;
  uint16_t gapAppearance_;
  uint16_t gapPpcpIntervalMin_;
  uint16_t gapPpcpIntervalMax_;
  uint16_t gapPpcpLatency_;
  uint16_t gapPpcpTimeout_;
  uint8_t gapBatteryLevel_;
  BleCustomServiceState customGattServices_[kCustomGattMaxServices];
  BleCustomCharacteristicState customGattCharacteristics_[kCustomGattMaxCharacteristics];
  BleCustomWriteHandlerState customGattWriteHandlers_[kCustomGattMaxCharacteristics];
  uint8_t customGattServiceCount_;
  uint8_t customGattCharacteristicCount_;
  uint16_t customGattNextHandle_;
  BleQueuedCustomNotificationState
      connectionCustomNotificationQueue_[kCustomGattNotificationQueueDepth];
  uint8_t connectionCustomNotificationQueueHead_;
  uint8_t connectionCustomNotificationQueueTail_;
  uint8_t connectionCustomNotificationQueueCount_;
  uint16_t connectionCustomIndicationAwaitingHandle_;
  bool connectionSmpSecurityRequestPending_;
  BleGattWriteCallback customGattWriteCallback_;
  void* customGattWriteContext_;
  BleEncryptionDebugCounters encDebug_;
  BleQueuedConnectionEvent queuedConnectionEvents_[kConnectionEventQueueDepth];
  uint8_t queuedConnectionEventPayloadScratch_[255];
  uint8_t queuedConnectionEventTxPayloadScratch_[255];
  uint8_t queuedConnectionEventHead_;
  uint8_t queuedConnectionEventTail_;
  uint8_t queuedConnectionEventCount_;
  int8_t latestConnectionRssiDbm_;
  bool latestConnectionRssiValid_;
  bool connectionServiceBusy_;
  BleDisconnectDebug disconnectDebug_;
  uint8_t lastObservedRxLlid_;
  uint8_t lastObservedRxLength_;
  uint8_t lastObservedRxOpcode_;
  uint8_t lastObservedRxNesn_;
  uint8_t lastObservedRxSn_;
  bool lastObservedPacketIsNew_;
  bool lastObservedPeerAckedLastTx_;
};

}  // namespace xiao_nrf54l15
