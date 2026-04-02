#ifndef NRF52_COMPAT_H_
#define NRF52_COMPAT_H_

#include <stdint.h>
#include "nrf54l15_types.h"

#ifndef NRF_FICR
#define NRF_FICR (reinterpret_cast<NRF_FICR_Type*>(0x00FFC000UL))
#endif

// nRF52 examples report flash size via INFO.FLASH. On nRF54L15 the same
// capacity is exposed as RRAM, so provide a field-name alias.
#ifndef FLASH
#define FLASH RRAM
#endif

struct Nrf52CompatUicr {
  volatile uint32_t NFCPINS;
};

struct Nrf52CompatNvmc {
  volatile uint32_t CONFIG;
  volatile uint32_t READY;
};

extern Nrf52CompatUicr g_nrf52_compat_uicr;
extern Nrf52CompatNvmc g_nrf52_compat_nvmc;

#ifndef NRF_UICR
#define NRF_UICR (&g_nrf52_compat_uicr)
#endif

#ifndef NRF_NVMC
#define NRF_NVMC (&g_nrf52_compat_nvmc)
#endif

#ifndef UICR_NFCPINS_PROTECT_Pos
#define UICR_NFCPINS_PROTECT_Pos 0UL
#endif

#ifndef UICR_NFCPINS_PROTECT_Msk
#define UICR_NFCPINS_PROTECT_Msk (0x1UL << UICR_NFCPINS_PROTECT_Pos)
#endif

#ifndef UICR_NFCPINS_PROTECT_NFC
#define UICR_NFCPINS_PROTECT_NFC 1UL
#endif

#ifndef NVMC_CONFIG_WEN_Pos
#define NVMC_CONFIG_WEN_Pos 0UL
#endif

#ifndef NVMC_CONFIG_WEN_Wen
#define NVMC_CONFIG_WEN_Wen 1UL
#endif

#ifndef NVMC_CONFIG_WEN_Ren
#define NVMC_CONFIG_WEN_Ren 0UL
#endif

#ifndef NVMC_READY_READY_Busy
#define NVMC_READY_READY_Busy 0UL
#endif

#ifndef NVMC_READY_READY_Ready
#define NVMC_READY_READY_Ready 1UL
#endif

static inline uint32_t SysTick_Config(uint32_t ticks) {
  (void)ticks;
  return 0UL;
}

void sd_power_system_off(void);
void NVIC_SystemReset(void);
void enterOTADfu(void);
void enterSerialDfu(void);
void dbgPrintVersion(void);
void dbgMemInfo(void);

class SchedulerClass {
 public:
  void startLoop(void (*fn)(void));
  void run(void);

 private:
  void (*loop_fn_)(void) = nullptr;
};

extern SchedulerClass Scheduler;

class HwPWMCompat {
 public:
  void addPin(uint8_t pin);
  void begin(void) {}
  void setResolution(uint8_t bits);
  void setClockDiv(uint32_t div) { (void)div; }
  void writePin(uint8_t pin, uint32_t value, bool invert = false);

 private:
  uint8_t resolution_bits_ = 8U;
};

extern HwPWMCompat HwPWM0;
extern HwPWMCompat HwPWM1;

#endif  // NRF52_COMPAT_H_
