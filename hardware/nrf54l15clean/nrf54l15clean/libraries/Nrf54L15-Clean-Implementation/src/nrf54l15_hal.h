#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nrf54l15_regs.h"
#include "xiao_nrf54l15_pins.h"

namespace xiao_nrf54l15 {

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
  bool pollCompare(uint8_t channel, bool clearEvent = true);
  volatile uint32_t* publishCompareConfigRegister(uint8_t channel) const;
  volatile uint32_t* subscribeStartConfigRegister() const;
  volatile uint32_t* subscribeStopConfigRegister() const;
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

class CracenRng {
 public:
  explicit CracenRng(uint32_t controlBase = nrf54l15::CRACEN_BASE,
                     uint32_t coreBase = nrf54l15::CRACENCORE_BASE);

  bool begin(uint32_t spinLimit = 400000UL);
  void end();
  bool fill(void* data, size_t length, uint32_t spinLimit = 400000UL);
  bool randomWord(uint32_t* outWord, uint32_t spinLimit = 400000UL);
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

enum class BoardAntennaPath : uint8_t {
  kCeramic = 0,
  kExternal = 1,
  kControlHighImpedance = 2,
};

class BoardControl {
 public:
  // Control RF switch path on P2.05:
  // - ceramic/external actively drive RF_SW_CTL
  // - control-high-impedance releases control pin (no active drive)
  static bool setAntennaPath(BoardAntennaPath path);
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

class ZigbeeRadio {
 public:
  explicit ZigbeeRadio(uint32_t radioBase = nrf54l15::RADIO_BASE);

  bool begin(uint8_t channel = 15U, int8_t txPowerDbm = 0);
  void end();

  bool setChannel(uint8_t channel);
  uint8_t channel() const;
  bool setTxPowerDbm(int8_t dbm);

  bool transmit(const uint8_t* psdu, uint8_t length, bool performCca = false,
                uint32_t spinLimit = 1400000UL);
  bool receive(ZigbeeFrame* frame, uint32_t listenWindowUs = 7000U,
               uint32_t spinLimit = 1400000UL);
  bool sampleEnergyDetect(uint8_t* outEdLevel, uint32_t spinLimit = 300000UL);

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
  static bool parseDataFrameShort(const uint8_t* psdu, uint8_t length,
                                  ZigbeeDataFrameView* outView);
  static bool parseMacCommandFrameShort(const uint8_t* psdu, uint8_t length,
                                        ZigbeeMacCommandView* outView);

 private:
  bool configureIeee802154();
  bool performCcaCheck(uint32_t spinLimit);

  NRF_RADIO_Type* radio_;
  bool initialized_;
  uint8_t channel_;
  alignas(4) uint8_t txPacket_[1 + 127];
  alignas(4) uint8_t rxPacket_[1 + 127];
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
  uint8_t advPayloadLength;
  bool advertiserAddressRandom;
  uint8_t advertiserAddress[6];
  uint8_t advPayload[31];
  bool scanResponseReceived;
  int8_t scanRspRssiDbm;
  uint8_t scanRspHeader;
  uint8_t scanRspPayloadLength;
  uint8_t scanRspPayload[31];
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
  bool llControlPacket;
  bool attPacket;
  bool txPacketSent;
  uint16_t eventCounter;
  uint8_t dataChannel;
  int8_t rssiDbm;
  uint8_t llid;
  uint8_t rxNesn;
  uint8_t rxSn;
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
  uint8_t encLastSessionKey[16];
  uint8_t encLastSessionAltKey[16];
  uint8_t encLastSessionKeyValid;
  uint8_t encLastSessionAltKeyValid;
  uint8_t encLastRxDir;
  uint8_t encLastTxDir;
};

struct BleBondRecord {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t ltk[16];
  uint8_t rand[8];
  uint16_t ediv;
  uint8_t keySize;
  uint8_t reserved[3];
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
  static constexpr uint8_t kCustomGattMaxServices = 4U;
  static constexpr uint8_t kCustomGattMaxCharacteristics = 8U;
  static constexpr uint8_t kCustomGattMaxValueLength = 20U;
  static constexpr uint8_t kCustomGattUuid128Length = 16U;

  explicit BleRadio(uint32_t radioBase = nrf54l15::RADIO_BASE,
                    uint32_t ficrBase = nrf54l15::FICR_BASE);

  bool begin(int8_t txPowerDbm = NRF54L15_CLEAN_BLE_DEFAULT_TX_DBM);
  void end();

  bool setTxPowerDbm(int8_t dbm);
  bool selectExternalAntenna(bool external);
  bool loadAddressFromFicr(bool forceRandomStatic = true);
  bool setDeviceAddress(const uint8_t address[6],
                        BleAddressType type = BleAddressType::kRandomStatic);
  bool getDeviceAddress(uint8_t addressOut[6], BleAddressType* typeOut = nullptr) const;

  bool setAdvertisingPduType(BleAdvPduType type);
  bool setAdvertisingChannelSelectionAlgorithm2(bool enabled);
  bool setAdvertisingData(const uint8_t* data, size_t len);
  bool setAdvertisingName(const char* name, bool includeFlags = true);
  bool buildAdvertisingPacket();
  bool setGattDeviceName(const char* name);
  bool setGattBatteryLevel(uint8_t percent);
  bool clearCustomGatt();
  bool addCustomGattService(uint16_t uuid16, uint16_t* outServiceHandle = nullptr);
  // 128-bit UUID arrays are passed in canonical big-endian order
  // (`00112233-4455-6677-8899-aabbccddeeff` -> `{0x00, 0x11, ... 0xff}`).
  bool addCustomGattService128(const uint8_t uuid128[kCustomGattUuid128Length],
                               uint16_t* outServiceHandle = nullptr);
  // Characteristics can only be appended to the most recently added custom
  // service, matching Zephyr's static service-table ordering model.
  bool addCustomGattCharacteristic(uint16_t serviceHandle, uint16_t uuid16,
                                   uint8_t properties,
                                   const uint8_t* initialValue = nullptr,
                                   uint8_t initialValueLength = 0U,
                                   uint16_t* outValueHandle = nullptr,
                                   uint16_t* outCccdHandle = nullptr);
  bool addCustomGattCharacteristic128(
      uint16_t serviceHandle,
      const uint8_t uuid128[kCustomGattUuid128Length],
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
  void setCustomGattWriteCallback(BleGattWriteCallback callback,
                                  void* context = nullptr);

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
  bool isConnectionEncrypted() const;
  bool getConnectionInfo(BleConnectionInfo* info) const;
  void getEncryptionDebugCounters(BleEncryptionDebugCounters* out) const;
  void clearEncryptionDebugCounters();
  bool hasBondRecord() const;
  bool getBondRecord(BleBondRecord* outRecord) const;
  bool clearBondRecord(bool clearPersistentStorage = true);
  void setBondPersistenceCallbacks(BleBondLoadCallback loadCallback,
                                   BleBondSaveCallback saveCallback,
                                   BleBondClearCallback clearCallback = nullptr,
                                   void* context = nullptr);
  void setTraceCallback(BleTraceCallback callback, void* context = nullptr);
  bool disconnect(uint32_t spinLimit = 300000UL);
  bool pollConnectionEvent(BleConnectionEvent* event = nullptr,
                           uint32_t spinLimit = 400000UL);

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

 private:
  static constexpr uint16_t kCustomGattHandleStart = 0x0020U;
  static constexpr uint16_t kCustomGattHandleEnd = 0x00FFU;

  struct BleCustomUuidState {
    uint8_t length;
    uint8_t bytes[kCustomGattUuid128Length];
  };

  struct BleCustomServiceState {
    BleCustomUuidState uuid;
    uint16_t serviceHandle;
    uint16_t endHandle;
  };

  struct BleCustomCharacteristicState {
    uint16_t serviceHandle;
    BleCustomUuidState uuid;
    uint8_t properties;
    uint16_t declarationHandle;
    uint16_t valueHandle;
    uint16_t cccdHandle;
    uint16_t cccdValue;
    uint8_t valueLength;
    uint8_t value[kCustomGattMaxValueLength];
  };

  bool configureBle1M();
  bool beginUnconnectedRadioActivity(uint32_t spinLimit = 1500000UL);
  void endUnconnectedRadioActivity();
  bool advertiseOncePrepared(BleAdvertisingChannel channel, uint32_t spinLimit);
  bool advertiseInteractOncePrepared(BleAdvertisingChannel channel,
                                     BleAdvInteraction* interaction,
                                     uint32_t requestListenSpinLimit,
                                     uint32_t spinLimit);
  bool waitDisabled(uint32_t spinLimit);
  bool waitForEnd(uint32_t spinLimit);
  bool setAdvertisingChannel(BleAdvertisingChannel channel);
  bool setDataChannel(uint8_t dataChannel);
  bool handleRequestAndMaybeRespond(BleAdvertisingChannel channel,
                                    BleAdvInteraction* interaction,
                                    uint32_t requestListenSpinLimit,
                                    uint32_t spinLimit);
  bool startConnectionFromConnectInd(const uint8_t* payload, uint8_t length,
                                     bool peerAddressRandom, bool useChSel2,
                                     uint32_t connectIndEndUs);
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
  void clearSmpPairingState();
  void clearEncryptionState();
  void clearConnectionSecurityState();
  bool isBondRecordUsable(const BleBondRecord& record) const;
  bool loadBondRecordFromPersistence();
  bool persistBondRecord(const BleBondRecord& record);
  bool clearPersistentBondRecord();
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
  BleCustomCharacteristicState* findCustomCharacteristicByCccdHandle(uint16_t cccdHandle);
  const BleCustomCharacteristicState* findCustomCharacteristicByCccdHandle(
      uint16_t cccdHandle) const;
  bool writeCustomGattCharacteristic(uint16_t handle, const uint8_t* value,
                                     uint16_t valueLength, bool withResponse,
                                     uint8_t* outErrorCode);
  void emitBleTrace(const char* message) const;
  void updateNextConnectionEventTime();
  uint8_t selectNextDataChannel(bool useCurrentEventCounter);
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
  bool connectionUseChSel2_;
  uint16_t connectionChannelId_;
  uint8_t connectionSca_;
  uint8_t connectionChanUse_;
  uint8_t connectionExpectedRxSn_;
  uint8_t connectionTxSn_;
  uint8_t connectionLastTxSn_;
  bool connectionTxHistoryValid_;
  uint16_t connectionEventCounter_;
  uint16_t connectionMissedEventCount_;
  uint32_t connectionNextEventUs_;
  uint32_t connectionFirstEventListenUs_;
  uint8_t connectionSyncAttemptsRemaining_;
  uint16_t connectionAttMtu_;
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
  bool connectionLastTxWasEncrypted_;
  uint8_t connectionLastTxEncryptedLength_;
  uint8_t connectionLastTxEncryptedPayload_[31];
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
  bool smpPeerLtkValid_;
  bool smpPeerLtkAwaitMasterId_;
  uint8_t smpPeerLtk_[16];
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
  uint8_t customGattServiceCount_;
  uint8_t customGattCharacteristicCount_;
  uint16_t customGattNextHandle_;
  bool connectionCustomNotificationPending_;
  uint8_t connectionCustomPendingCharIndex_;
  bool connectionCustomPendingIndication_;
  uint16_t connectionCustomIndicationAwaitingHandle_;
  BleGattWriteCallback customGattWriteCallback_;
  void* customGattWriteContext_;
  BleEncryptionDebugCounters encDebug_;
};

}  // namespace xiao_nrf54l15
