#include "nrf54l15_vpr.h"

#include <string.h>

#include "vpr_cs_controller_stub_firmware.h"
#include "vpr_cs_transport_stub_firmware.h"

namespace xiao_nrf54l15 {
namespace {

constexpr uint32_t kCtrlApIntRxReady = (1UL << 0U);
constexpr uint32_t kCtrlApIntTxDone = (1UL << 1U);
constexpr uint32_t kCpuSystemCacheBase = 0xE0082000UL;
constexpr uint32_t kMemconfSecureBase = 0x500CF000UL;
constexpr uint32_t kMpc00SecureBase = 0x50041000UL;
constexpr uint32_t kSpu00SecureBase = 0x50040000UL;
constexpr uint32_t kVprSecureBase = 0x5004C000UL;
constexpr uint32_t kVprNonSecureBase = 0x4004C000UL;
constexpr uint8_t kVprMinTrigger = 16U;
constexpr uint8_t kVprMaxTrigger = 22U;
constexpr uint8_t kTransportTask = NRF54L15_VPR_TRANSPORT_HOST_TO_VPR_TASK;
constexpr uint8_t kTransportEvent = NRF54L15_VPR_TRANSPORT_VPR_TO_HOST_EVENT;
constexpr uint32_t kVprNordicCtrlOffset = VPRCSR_NORDIC_VPRNORDICCTRL;
constexpr uint32_t kVprNordicCacheCtrlOffset = VPRCSR_NORDIC_CACHE_CTRL;
constexpr uint32_t kVprContextRestorePowerIndex = 1U;
constexpr uint32_t kVprContextRestoreRetMask = MEMCONF_POWER_RET_MEM0_Msk;
constexpr uint32_t kVprContextRestoreRet2PowerIndex = 0U;
constexpr uint32_t kVprContextRestoreRet2Mask = MEMCONF_POWER_RET2_MEM7_Msk;
constexpr uint32_t kAddressSlavePos = 12U;
constexpr uint32_t kAddressSlaveMask = (0x3FUL << kAddressSlavePos);
constexpr uint32_t kMpcOverrideGranularity = 0x1000UL;
constexpr uint8_t kMpcTransportHostOverrideIndex = 0U;
constexpr uint8_t kMpcTransportVprOverrideIndex = 1U;

bool g_vprSecureAccessEnabled = false;

inline void dataSyncBarrier() {
  __DMB();
  __DSB();
}

inline void fullSyncBarrier() {
  __DMB();
  __DSB();
  __ISB();
}

inline volatile uint8_t* vprImageBase() {
  return reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(NRF54L15_VPR_IMAGE_BASE));
}

inline volatile uint8_t* vprSavedContextBase() {
  return reinterpret_cast<volatile uint8_t*>(
      static_cast<uintptr_t>(NRF54L15_VPR_CONTEXT_SAVE_BASE));
}

inline uint32_t currentVprBase() {
  return g_vprSecureAccessEnabled ? kVprSecureBase : kVprNonSecureBase;
}

inline bool isAlignedVprCsrOffset(uint32_t offset) {
  return (offset & 0x3U) == 0U;
}

inline void writeAlignedVprCsr(uint32_t offset, uint32_t value) {
  if (!isAlignedVprCsrOffset(offset)) {
    return;
  }
  nrf54l15::reg32(currentVprBase() + offset) = value;
}

inline uint32_t readAlignedVprCsr(uint32_t offset) {
  if (!isAlignedVprCsrOffset(offset)) {
    return 0U;
  }
  return nrf54l15::reg32(currentVprBase() + offset);
}

inline NRF_CACHE_Type* cpuSystemCache() {
  return reinterpret_cast<NRF_CACHE_Type*>(static_cast<uintptr_t>(kCpuSystemCacheBase));
}

inline NRF_MEMCONF_Type* memconf() {
  return reinterpret_cast<NRF_MEMCONF_Type*>(static_cast<uintptr_t>(kMemconfSecureBase));
}

inline void invalidateCpuSystemCache() {
  NRF_CACHE_Type* cache = cpuSystemCache();
  cache->TASKS_INVALIDATECACHE = CACHE_TASKS_INVALIDATECACHE_TASKS_INVALIDATECACHE_Trigger;
  dataSyncBarrier();
  uint32_t spinLimit = 100000U;
  while (((cache->STATUS & CACHE_STATUS_READY_Msk) == CACHE_STATUS_READY_Busy) &&
         (spinLimit-- > 0U)) {
    __NOP();
  }
  fullSyncBarrier();
}

inline NRF_SPU_Type* spu00() {
  return reinterpret_cast<NRF_SPU_Type*>(static_cast<uintptr_t>(kSpu00SecureBase));
}

inline NRF_MPC_Type* mpc00() {
  return reinterpret_cast<NRF_MPC_Type*>(static_cast<uintptr_t>(kMpc00SecureBase));
}

inline uint8_t vprSlaveIndex() {
  return static_cast<uint8_t>((kVprSecureBase & kAddressSlaveMask) >> kAddressSlavePos);
}

inline uint32_t vprPermValue() {
  return spu00()->PERIPH[vprSlaveIndex()].PERM;
}

inline bool configureSecureVprPeripheralAccess() {
  NRF_SPU_Type* periphSpu = spu00();
  const uint8_t index = vprSlaveIndex();
  uint32_t perm = periphSpu->PERIPH[index].PERM;
  const uint32_t secureMapping =
      (perm & SPU_PERIPH_PERM_SECUREMAPPING_Msk) >> SPU_PERIPH_PERM_SECUREMAPPING_Pos;
  if (secureMapping == SPU_PERIPH_PERM_SECUREMAPPING_NonSecure) {
    g_vprSecureAccessEnabled = false;
    return false;
  }

  if (secureMapping == SPU_PERIPH_PERM_SECUREMAPPING_UserSelectable ||
      secureMapping == SPU_PERIPH_PERM_SECUREMAPPING_Split) {
    perm = (perm & ~SPU_PERIPH_PERM_SECATTR_Msk) |
           (SPU_PERIPH_PERM_SECATTR_Secure << SPU_PERIPH_PERM_SECATTR_Pos);

    const uint32_t dmaCapability =
        (perm & SPU_PERIPH_PERM_DMA_Msk) >> SPU_PERIPH_PERM_DMA_Pos;
    if (dmaCapability == SPU_PERIPH_PERM_DMA_SeparateAttribute) {
      perm = (perm & ~SPU_PERIPH_PERM_DMASEC_Msk) |
             (SPU_PERIPH_PERM_DMASEC_Secure << SPU_PERIPH_PERM_DMASEC_Pos);
    }

    periphSpu->PERIPH[index].PERM = perm;
    dataSyncBarrier();
    perm = periphSpu->PERIPH[index].PERM;
  }

  g_vprSecureAccessEnabled =
      (((perm & SPU_PERIPH_PERM_SECATTR_Msk) >> SPU_PERIPH_PERM_SECATTR_Pos) ==
       SPU_PERIPH_PERM_SECATTR_Secure);
  fullSyncBarrier();
  return g_vprSecureAccessEnabled;
}

inline void clearMpcMemAccErrInternal() {
  NRF_MPC_Type* mpc = mpc00();
  mpc->EVENTS_MEMACCERR = 0U;
  dataSyncBarrier();
}

inline void configureMpcOverrideRegion(uint8_t index, uint32_t address) {
  if (index > 4U) {
    return;
  }
  NRF_MPC_Type* mpc = mpc00();
  volatile NRF_MPC_OVERRIDE_Type* region = &mpc->OVERRIDE[index];
  const uint32_t alignedAddress = address & ~(kMpcOverrideGranularity - 1U);
  region->CONFIG = 0U;
  region->STARTADDR = alignedAddress;
  region->ENDADDR = alignedAddress;
  region->PERM = (MPC_OVERRIDE_PERM_READ_Allowed << MPC_OVERRIDE_PERM_READ_Pos) |
                 (MPC_OVERRIDE_PERM_WRITE_Allowed << MPC_OVERRIDE_PERM_WRITE_Pos) |
                 (MPC_OVERRIDE_PERM_SECATTR_NonSecure << MPC_OVERRIDE_PERM_SECATTR_Pos);
  region->PERMMASK =
      (MPC_OVERRIDE_PERMMASK_READ_UnMasked << MPC_OVERRIDE_PERMMASK_READ_Pos) |
      (MPC_OVERRIDE_PERMMASK_WRITE_UnMasked << MPC_OVERRIDE_PERMMASK_WRITE_Pos) |
      (MPC_OVERRIDE_PERMMASK_SECATTR_UnMasked << MPC_OVERRIDE_PERMMASK_SECATTR_Pos);
  region->CONFIG = (MPC_OVERRIDE_CONFIG_ENABLE_Enabled << MPC_OVERRIDE_CONFIG_ENABLE_Pos);
}

inline void configureTransportMpcWindows() {
  clearMpcMemAccErrInternal();
  configureMpcOverrideRegion(kMpcTransportHostOverrideIndex,
                             NRF54L15_VPR_TRANSPORT_HOST_BASE);
  configureMpcOverrideRegion(kMpcTransportVprOverrideIndex,
                             NRF54L15_VPR_TRANSPORT_VPR_BASE);
  fullSyncBarrier();
}

inline void shortSpinDelay(uint32_t cycles) {
  while (cycles-- > 0U) {
    __NOP();
  }
}

inline bool waitForVprRunning(uint32_t spinLimit = 4096U) {
  NRF_VPR_Type* const vpr =
      reinterpret_cast<NRF_VPR_Type*>(static_cast<uintptr_t>(currentVprBase()));
  while (spinLimit-- > 0U) {
    if ((vpr->CPURUN & VPR_CPURUN_EN_Msk) != 0U) {
      return true;
    }
    shortSpinDelay(8U);
  }
  return false;
}

}  // namespace

NRF_CTRLAPPERI_Type* CtrlApMailbox::regs() {
  return reinterpret_cast<NRF_CTRLAPPERI_Type*>(static_cast<uintptr_t>(nrf54l15::CTRLAPPERI_BASE));
}

void CtrlApMailbox::clearEvents() {
  NRF_CTRLAPPERI_Type* periph = regs();
  periph->EVENTS_RXREADY = 0U;
  periph->EVENTS_TXDONE = 0U;
  dataSyncBarrier();
}

bool CtrlApMailbox::pollRxReady(bool clearEvent) {
  NRF_CTRLAPPERI_Type* periph = regs();
  if (periph->EVENTS_RXREADY == 0U) {
    return false;
  }
  if (clearEvent) {
    periph->EVENTS_RXREADY = 0U;
    dataSyncBarrier();
  }
  return true;
}

bool CtrlApMailbox::pollTxDone(bool clearEvent) {
  NRF_CTRLAPPERI_Type* periph = regs();
  if (periph->EVENTS_TXDONE == 0U) {
    return false;
  }
  if (clearEvent) {
    periph->EVENTS_TXDONE = 0U;
    dataSyncBarrier();
  }
  return true;
}

bool CtrlApMailbox::rxPending() {
  return (regs()->MAILBOX.RXSTATUS & CTRLAPPERI_MAILBOX_RXSTATUS_RXSTATUS_Msk) != 0U;
}

bool CtrlApMailbox::txPending() {
  return (regs()->MAILBOX.TXSTATUS & CTRLAPPERI_MAILBOX_TXSTATUS_TXSTATUS_Msk) != 0U;
}

bool CtrlApMailbox::read(uint32_t* value) {
  if (value == nullptr || !rxPending()) {
    return false;
  }
  *value = regs()->MAILBOX.RXDATA;
  dataSyncBarrier();
  return true;
}

bool CtrlApMailbox::write(uint32_t value) {
  if (txPending()) {
    return false;
  }
  regs()->MAILBOX.TXDATA = value;
  dataSyncBarrier();
  return true;
}

void CtrlApMailbox::enableInterrupts(bool rxReady, bool txDone) {
  NRF_CTRLAPPERI_Type* periph = regs();
  const uint32_t mask = (rxReady ? kCtrlApIntRxReady : 0U) | (txDone ? kCtrlApIntTxDone : 0U);
  periph->INTENCLR = kCtrlApIntRxReady | kCtrlApIntTxDone;
  if (mask != 0U) {
    periph->INTENSET = mask;
  }
  dataSyncBarrier();
}

NRF_VPR_Type* VprControl::regs() {
  return reinterpret_cast<NRF_VPR_Type*>(static_cast<uintptr_t>(currentVprBase()));
}

bool VprControl::validTriggerIndex(uint8_t index) {
  return index >= kVprMinTrigger && index <= kVprMaxTrigger;
}

bool VprControl::setInitPc(uint32_t address) {
  if (address < NRF54L15_VPR_IMAGE_BASE ||
      address >= (NRF54L15_VPR_IMAGE_BASE + NRF54L15_VPR_IMAGE_SIZE)) {
    return false;
  }
  regs()->INITPC = address;
  fullSyncBarrier();
  return true;
}

uint32_t VprControl::initPc() { return regs()->INITPC; }

void VprControl::prepareForLaunch() {
  (void)configureSecureVprPeripheralAccess();
  stop();
  fullSyncBarrier();
}

void VprControl::run() {
  regs()->CPURUN = VPR_CPURUN_EN_Running;
  fullSyncBarrier();
}

bool VprControl::start(uint32_t address) {
  if (!configureSecureVprPeripheralAccess()) {
    return false;
  }
  stop();
  fullSyncBarrier();
  clearDebugHaltState();
  if (!hartResetPulse()) {
    return false;
  }
  enableRtPeripherals();
  if (!setInitPc(address)) {
    return false;
  }
  run();
  return waitForVprRunning();
}

void VprControl::stop() {
  regs()->CPURUN = VPR_CPURUN_EN_Stopped;
  fullSyncBarrier();
}

bool VprControl::isRunning() {
  return (regs()->CPURUN & VPR_CPURUN_EN_Msk) != 0U;
}

bool VprControl::secureAccessEnabled() { return g_vprSecureAccessEnabled; }

uint32_t VprControl::spuPerm() { return vprPermValue(); }

void VprControl::enableRtPeripherals() {
  // Only the aligned control CSR is touched from CPUAPP. Unaligned FLPR CSR
  // numbers such as VPRNORDICSLEEPCTRL are not safe to access this way.
  const uint32_t value =
      (VPRCSR_NORDIC_VPRNORDICCTRL_NORDICKEY_Enabled
       << VPRCSR_NORDIC_VPRNORDICCTRL_NORDICKEY_Pos) |
      VPRCSR_NORDIC_VPRNORDICCTRL_ENABLERTPERIPH_Msk;
  writeAlignedVprCsr(kVprNordicCtrlOffset, value);
  fullSyncBarrier();
}

void VprControl::configureDataAccessNonCacheable() {
  fullSyncBarrier();
}

void VprControl::clearCache() {
  writeAlignedVprCsr(kVprNordicCacheCtrlOffset,
                     VPRCSR_NORDIC_CACHE_CTRL_ENABLE_Msk |
                         VPRCSR_NORDIC_CACHE_CTRL_CACHECLR_Msk);
  fullSyncBarrier();
  writeAlignedVprCsr(kVprNordicCacheCtrlOffset, VPRCSR_NORDIC_CACHE_CTRL_ENABLE_Msk);
  invalidateCpuSystemCache();
  fullSyncBarrier();
}

void VprControl::clearBufferedSignals() {
  for (uint8_t index = kVprMinTrigger; index <= kVprMaxTrigger; ++index) {
    regs()->EVENTS_TRIGGERED[index] = 0U;
  }
  dataSyncBarrier();
}

void VprControl::clearDebugHaltState() {
  regs()->DEBUGIF.DMCONTROL =
      (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos) |
      (VPR_DEBUGIF_DMCONTROL_ACKHAVERESET_Clear << VPR_DEBUGIF_DMCONTROL_ACKHAVERESET_Pos) |
      (VPR_DEBUGIF_DMCONTROL_CLRRESETHALTREQ_Clear
       << VPR_DEBUGIF_DMCONTROL_CLRRESETHALTREQ_Pos) |
      (VPR_DEBUGIF_DMCONTROL_RESUMEREQ_Resumed << VPR_DEBUGIF_DMCONTROL_RESUMEREQ_Pos);
  fullSyncBarrier();
  regs()->DEBUGIF.DMCONTROL =
      (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos);
  fullSyncBarrier();
}

uint32_t VprControl::debugStatus() { return regs()->DEBUGIF.DMSTATUS; }

uint32_t VprControl::haltSummary0() { return regs()->DEBUGIF.HALTSUM0; }

uint32_t VprControl::haltSummary1() { return regs()->DEBUGIF.HALTSUM1; }

uint32_t VprControl::rawNordicAxCache() { return 0U; }

uint32_t VprControl::rawNordicTasks() { return 0U; }

uint32_t VprControl::rawNordicEvents() { return 0U; }

uint32_t VprControl::rawNordicEventStatus() { return 0U; }

uint32_t VprControl::rawSleepControl() { return 0U; }

uint32_t VprControl::rawNordicCacheCtrl() { return readAlignedVprCsr(kVprNordicCacheCtrlOffset); }

uint32_t VprControl::rawMpcMemAccErrEvent() { return mpc00()->EVENTS_MEMACCERR; }

uint32_t VprControl::rawMpcMemAccErrAddress() { return mpc00()->MEMACCERR.ADDRESS; }

uint32_t VprControl::rawMpcMemAccErrInfo() { return mpc00()->MEMACCERR.INFO; }

void VprControl::clearMpcMemAccErr() { clearMpcMemAccErrInternal(); }

bool VprControl::configureSleepControl(VprSleepState state,
                                       bool returnToSleep,
                                       bool stackOnSleep) {
  (void)state;
  (void)returnToSleep;
  (void)stackOnSleep;
  return false;
}

VprSleepState VprControl::sleepState() {
  return VprSleepState::kReset;
}

bool VprControl::returnToSleepEnabled() { return false; }

bool VprControl::stackOnSleepEnabled() { return false; }

bool VprControl::enableContextRestore(bool enable) {
  NRF_MEMCONF_Type* cfg = memconf();
  if (enable) {
    cfg->POWER[kVprContextRestorePowerIndex].RET |= kVprContextRestoreRetMask;
    cfg->POWER[kVprContextRestoreRet2PowerIndex].RET2 |= kVprContextRestoreRet2Mask;
  } else {
    cfg->POWER[kVprContextRestorePowerIndex].RET &= ~kVprContextRestoreRetMask;
    cfg->POWER[kVprContextRestoreRet2PowerIndex].RET2 &= ~kVprContextRestoreRet2Mask;
  }
  fullSyncBarrier();
  return contextRestoreEnabled() == enable;
}

bool VprControl::contextRestoreEnabled() {
  return ((rawMemconfPower1Ret() & kVprContextRestoreRetMask) != 0U) &&
         ((rawMemconfPower0Ret2() & kVprContextRestoreRet2Mask) != 0U);
}

bool VprControl::hartResetPulse(uint32_t spinLimit) {
  if (!configureSecureVprPeripheralAccess()) {
    return false;
  }

  regs()->DEBUGIF.DMCONTROL =
      (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos) |
      (VPR_DEBUGIF_DMCONTROL_CLRRESETHALTREQ_Clear
       << VPR_DEBUGIF_DMCONTROL_CLRRESETHALTREQ_Pos) |
      (VPR_DEBUGIF_DMCONTROL_NDMRESET_Active << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos);
  fullSyncBarrier();
  shortSpinDelay((spinLimit > 8192U) ? 8192U : spinLimit);

  regs()->DEBUGIF.DMCONTROL =
      (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos) |
      (VPR_DEBUGIF_DMCONTROL_CLRRESETHALTREQ_Clear
       << VPR_DEBUGIF_DMCONTROL_CLRRESETHALTREQ_Pos) |
      (VPR_DEBUGIF_DMCONTROL_RESUMEREQ_Resumed << VPR_DEBUGIF_DMCONTROL_RESUMEREQ_Pos);
  fullSyncBarrier();
  shortSpinDelay((spinLimit > 16384U) ? 16384U : spinLimit);

  regs()->DEBUGIF.DMCONTROL =
      (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos) |
      (VPR_DEBUGIF_DMCONTROL_ACKHAVERESET_Clear << VPR_DEBUGIF_DMCONTROL_ACKHAVERESET_Pos);
  fullSyncBarrier();
  regs()->DEBUGIF.DMCONTROL =
      (VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos);
  fullSyncBarrier();
  shortSpinDelay((spinLimit > 4096U) ? 4096U : spinLimit);
  return true;
}

bool VprControl::restartAfterHibernateReset() {
  if (!contextRestoreEnabled() || !configureSecureVprPeripheralAccess()) {
    return false;
  }
  stop();
  fullSyncBarrier();
  clearDebugHaltState();
  if (!hartResetPulse()) {
    return false;
  }
  clearCache();
  invalidateCpuSystemCache();
  enableRtPeripherals();
  if (!setInitPc(NRF54L15_VPR_IMAGE_BASE)) {
    return false;
  }
  run();
  return waitForVprRunning(32768U);
}

bool VprControl::resumeRetainedContext() { return restartAfterHibernateReset(); }

uint32_t VprControl::rawMemconfPower0Ret2() {
  return memconf()->POWER[kVprContextRestoreRet2PowerIndex].RET2;
}

uint32_t VprControl::rawMemconfPower1Ret() {
  return memconf()->POWER[kVprContextRestorePowerIndex].RET;
}

uint32_t VprControl::savedContextAddress() { return NRF54L15_VPR_CONTEXT_SAVE_BASE; }

uint32_t VprControl::savedContextSize() { return NRF54L15_VPR_CONTEXT_SAVE_SIZE; }

bool VprControl::clearSavedContext() {
  volatile uint8_t* base = vprSavedContextBase();
  for (size_t i = 0U; i < NRF54L15_VPR_CONTEXT_SAVE_SIZE; ++i) {
    base[i] = 0U;
  }
  fullSyncBarrier();
  invalidateCpuSystemCache();
  return true;
}

bool VprControl::readSavedContext(void* buffer, size_t len, size_t offset) {
  if (buffer == nullptr || len == 0U ||
      offset >= NRF54L15_VPR_CONTEXT_SAVE_SIZE ||
      len > (NRF54L15_VPR_CONTEXT_SAVE_SIZE - offset)) {
    return false;
  }
  invalidateCpuSystemCache();
  volatile uint8_t* base = vprSavedContextBase() + offset;
  uint8_t* out = reinterpret_cast<uint8_t*>(buffer);
  for (size_t i = 0U; i < len; ++i) {
    out[i] = base[i];
  }
  return true;
}

bool VprControl::triggerTask(uint8_t index) {
  if (!validTriggerIndex(index)) {
    return false;
  }
  regs()->TASKS_TRIGGER[index] = VPR_TASKS_TRIGGER_TASKS_TRIGGER_Trigger;
  dataSyncBarrier();
  return true;
}

bool VprControl::pollEvent(uint8_t index, bool clearEvent) {
  if (!validTriggerIndex(index) || regs()->EVENTS_TRIGGERED[index] == 0U) {
    return false;
  }
  if (clearEvent) {
    regs()->EVENTS_TRIGGERED[index] = 0U;
    dataSyncBarrier();
  }
  return true;
}

bool VprControl::clearEvent(uint8_t index) {
  if (!validTriggerIndex(index)) {
    return false;
  }
  regs()->EVENTS_TRIGGERED[index] = 0U;
  dataSyncBarrier();
  return true;
}

bool VprControl::enableEventInterrupt(uint8_t index, bool enable) {
  if (!validTriggerIndex(index)) {
    return false;
  }
  const uint32_t mask = (1UL << index);
  if (enable) {
    regs()->INTENSET = mask;
  } else {
    regs()->INTENCLR = mask;
  }
  dataSyncBarrier();
  return true;
}

VprSharedTransportStream::VprSharedTransportStream() : rxBuffer_{0}, rxLen_(0U), rxIndex_(0U) {}

volatile Nrf54l15VprTransportHostShared* VprSharedTransportStream::hostShared() const {
  return nrf54l15_vpr_transport_host_shared();
}

volatile Nrf54l15VprTransportVprShared* VprSharedTransportStream::vprShared() const {
  return nrf54l15_vpr_transport_vpr_shared();
}

bool VprSharedTransportStream::resetSharedState(bool clearScripts) {
  volatile Nrf54l15VprTransportHostShared* host = hostShared();
  volatile Nrf54l15VprTransportVprShared* vpr = vprShared();
  const size_t hostPrefixLen = offsetof(Nrf54l15VprTransportHostShared, scripts);
  memset(reinterpret_cast<void*>(const_cast<Nrf54l15VprTransportHostShared*>(host)), 0,
         hostPrefixLen);
  if (clearScripts) {
    memset(reinterpret_cast<void*>(&const_cast<Nrf54l15VprTransportHostShared*>(host)->scripts[0]),
           0, sizeof(host->scripts));
  }
  memset(reinterpret_cast<void*>(const_cast<Nrf54l15VprTransportVprShared*>(vpr)), 0,
         sizeof(*vpr));
  host->magic = NRF54L15_VPR_TRANSPORT_MAGIC;
  host->version = NRF54L15_VPR_TRANSPORT_VERSION;
  vpr->magic = NRF54L15_VPR_TRANSPORT_MAGIC;
  vpr->version = NRF54L15_VPR_TRANSPORT_VERSION;
  vpr->status = NRF54L15_VPR_TRANSPORT_STATUS_STOPPED;
  dataSyncBarrier();
  rxLen_ = 0U;
  rxIndex_ = 0U;
  return true;
}

bool VprSharedTransportStream::clearScripts() {
  volatile Nrf54l15VprTransportHostShared* host = hostShared();
  memset(reinterpret_cast<void*>(&const_cast<Nrf54l15VprTransportHostShared*>(host)->scripts[0]), 0,
         sizeof(host->scripts));
  host->scriptCount = 0U;
  dataSyncBarrier();
  return true;
}

bool VprSharedTransportStream::addScriptResponse(uint16_t opcode,
                                                 const uint8_t* response,
                                                 size_t len) {
  if (response == nullptr || len == 0U || len > NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_RESPONSE) {
    return false;
  }
  volatile Nrf54l15VprTransportHostShared* host = hostShared();
  if (host->scriptCount >= NRF54L15_VPR_TRANSPORT_MAX_SCRIPT_COUNT) {
    return false;
  }
  Nrf54l15VprTransportScript* script =
      &const_cast<Nrf54l15VprTransportHostShared*>(host)->scripts[host->scriptCount];
  memset(script, 0, sizeof(*script));
  script->opcode = opcode;
  script->responseLen = static_cast<uint16_t>(len);
  memcpy(script->response, response, len);
  ++host->scriptCount;
  dataSyncBarrier();
  return true;
}

bool VprSharedTransportStream::loadFirmware(const uint8_t* image, size_t len) {
  if (image == nullptr || len == 0U || len > NRF54L15_VPR_IMAGE_SIZE) {
    return false;
  }
  VprControl::stop();
  memset(reinterpret_cast<void*>(const_cast<uint8_t*>(vprImageBase())), 0, NRF54L15_VPR_IMAGE_SIZE);
  memcpy(reinterpret_cast<void*>(const_cast<uint8_t*>(vprImageBase())), image, len);
  fullSyncBarrier();
  invalidateCpuSystemCache();
  return true;
}

bool VprSharedTransportStream::bootLoadedFirmware() {
  configureTransportMpcWindows();
  vprShared()->status = NRF54L15_VPR_TRANSPORT_STATUS_BOOTING;
  dataSyncBarrier();
  if (!VprControl::start(NRF54L15_VPR_IMAGE_BASE)) {
    return false;
  }
  return true;
}

bool VprSharedTransportStream::loadFirmwareAndStart(const uint8_t* image, size_t len) {
  if (!loadFirmware(image, len) || !bootLoadedFirmware()) {
    return false;
  }
  return waitReady();
}

bool VprSharedTransportStream::loadDefaultCsTransportStubImage() {
  return loadFirmware(kVprCsTransportStubFirmware, kVprCsTransportStubFirmwareSize);
}

bool VprSharedTransportStream::loadDefaultCsTransportStub() {
  return loadDefaultCsTransportStubImage() && bootLoadedFirmware() && waitReady();
}

bool VprSharedTransportStream::loadDefaultCsControllerStubImage() {
  return loadFirmware(kVprCsControllerStubFirmware, kVprCsControllerStubFirmwareSize);
}

bool VprSharedTransportStream::loadDefaultCsControllerStub() {
  return loadDefaultCsControllerStubImage() && bootLoadedFirmware() && waitReady();
}

bool VprSharedTransportStream::restartLoadedFirmware(bool clearScripts, uint32_t spinLimit) {
  stop();
  delay(10);
  VprControl::clearBufferedSignals();
  VprControl::clearDebugHaltState();
  VprControl::clearCache();
  invalidateCpuSystemCache();
  if (!resetSharedState(clearScripts) || !bootLoadedFirmware()) {
    return false;
  }
  return waitReady(spinLimit);
}

bool VprSharedTransportStream::resumeRetainedService(uint32_t spinLimit) {
  return restartAfterHibernateReset(spinLimit);
}

bool VprSharedTransportStream::retainedHibernateStatePending() const {
  return hostShared()->hibernateCookie == NRF54L15_VPR_TRANSPORT_HIBERNATE_COOKIE;
}

void VprSharedTransportStream::clearRetainedHibernateState() {
  volatile Nrf54l15VprTransportHostShared* host = hostShared();
  host->hibernateCookie = 0U;
  host->hibernateFlags = 0U;
  host->retainedTickerPeriodTicks = 0U;
  host->retainedTickerStep = 0U;
  host->retainedTickerAccum = 0U;
  host->retainedTickerCount = 0U;
  dataSyncBarrier();
}

bool VprSharedTransportStream::restartAfterHibernateReset(uint32_t spinLimit) {
  configureTransportMpcWindows();
  volatile Nrf54l15VprTransportVprShared* state = vprShared();
  // Datasheet hibernate semantics are "restart by a reset". Use the same
  // conservative loaded-image restart path that already works cross-board, but
  // keep the retained host-side hibernate state intact so the VPR service can
  // rebuild its runtime state on boot.
  stop();
  delay(10);
  VprControl::clearBufferedSignals();
  VprControl::clearDebugHaltState();
  VprControl::clearCache();
  invalidateCpuSystemCache();
  // The service-level retained restart uses host-retained transport state,
  // not raw VPR CPU register restore. Disable hardware context restore before
  // restarting so the reboot path is deterministic across boards.
  (void)VprControl::enableContextRestore(false);
  state->status = NRF54L15_VPR_TRANSPORT_STATUS_BOOTING;
  state->vprFlags = 0U;
  state->vprLen = 0U;
  state->lastError = 0U;
  dataSyncBarrier();
  (void)VprControl::clearEvent(kTransportEvent);
  if (bootLoadedFirmware() && waitReady(spinLimit)) {
    return true;
  }

  for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
    const uint32_t previousHeartbeat = state->heartbeat;
    uint32_t notRunningSpins = 0U;
    state->status = NRF54L15_VPR_TRANSPORT_STATUS_BOOTING;
    state->vprFlags = 0U;
    state->vprLen = 0U;
    dataSyncBarrier();
    (void)VprControl::clearEvent(kTransportEvent);
    if (!VprControl::restartAfterHibernateReset()) {
      return false;
    }
    uint32_t waitBudget = spinLimit;
    uint32_t pollCount = 0U;
    invalidateCpuSystemCache();
    while (waitBudget-- > 0U) {
      (void)VprControl::pollEvent(kTransportEvent);
      if ((pollCount++ & 0x3FFFU) == 0U) {
        invalidateCpuSystemCache();
      }
      if (state->magic == NRF54L15_VPR_TRANSPORT_MAGIC &&
          state->version == NRF54L15_VPR_TRANSPORT_VERSION &&
          state->status == NRF54L15_VPR_TRANSPORT_STATUS_READY &&
          state->heartbeat != previousHeartbeat) {
        return true;
      }
      if (state->status == NRF54L15_VPR_TRANSPORT_STATUS_ERROR) {
        break;
      }
      if (!VprControl::isRunning()) {
        if (++notRunningSpins > 4096U) {
          break;
        }
      } else {
        notRunningSpins = 0U;
      }
    }
    delay(10);
  }

  const uint32_t previousHeartbeat = state->heartbeat;
  uint32_t notRunningSpins = 0U;
  stop();
  delay(10);
  VprControl::clearBufferedSignals();
  VprControl::clearDebugHaltState();
  VprControl::clearCache();
  invalidateCpuSystemCache();
  state->status = NRF54L15_VPR_TRANSPORT_STATUS_BOOTING;
  state->vprFlags = 0U;
  state->vprLen = 0U;
  state->lastError = 0U;
  dataSyncBarrier();
  (void)VprControl::clearEvent(kTransportEvent);
  if (!bootLoadedFirmware()) {
    return false;
  }
  uint32_t waitBudget = spinLimit;
  uint32_t pollCount = 0U;
  invalidateCpuSystemCache();
  while (waitBudget-- > 0U) {
    (void)VprControl::pollEvent(kTransportEvent);
    if ((pollCount++ & 0x3FFFU) == 0U) {
      invalidateCpuSystemCache();
    }
    if (state->magic == NRF54L15_VPR_TRANSPORT_MAGIC &&
        state->version == NRF54L15_VPR_TRANSPORT_VERSION &&
        state->status == NRF54L15_VPR_TRANSPORT_STATUS_READY &&
        state->heartbeat != previousHeartbeat) {
      return true;
    }
    if (state->status == NRF54L15_VPR_TRANSPORT_STATUS_ERROR) {
      return false;
    }
    if (!VprControl::isRunning()) {
      if (++notRunningSpins > 4096U) {
        return false;
      }
    } else {
      notRunningSpins = 0U;
    }
  }
  return false;
}

bool VprSharedTransportStream::recoverAfterHibernateFailure(bool clearScripts,
                                                            uint32_t spinLimit) {
  clearRetainedHibernateState();
  (void)VprControl::clearSavedContext();
  (void)VprControl::enableContextRestore(false);
  return restartLoadedFirmware(clearScripts, spinLimit);
}

void VprSharedTransportStream::stop() {
  VprControl::stop();
  vprShared()->status = NRF54L15_VPR_TRANSPORT_STATUS_STOPPED;
  dataSyncBarrier();
  rxLen_ = 0U;
  rxIndex_ = 0U;
}

bool VprSharedTransportStream::waitReady(uint32_t spinLimit) {
  volatile Nrf54l15VprTransportVprShared* state = vprShared();
  uint32_t pollCount = 0U;
  uint32_t notRunningSpins = 0U;
  invalidateCpuSystemCache();
  while (spinLimit-- > 0U) {
    (void)VprControl::pollEvent(kTransportEvent);
    if ((pollCount++ & 0x3FFFU) == 0U) {
      invalidateCpuSystemCache();
    }
    if (state->magic == NRF54L15_VPR_TRANSPORT_MAGIC &&
        state->version == NRF54L15_VPR_TRANSPORT_VERSION &&
        state->status == NRF54L15_VPR_TRANSPORT_STATUS_READY) {
      return true;
    }
    if (state->status == NRF54L15_VPR_TRANSPORT_STATUS_ERROR) {
      return false;
    }
    if (!VprControl::isRunning()) {
      if (++notRunningSpins > 4096U) {
        return false;
      }
    } else {
      notRunningSpins = 0U;
    }
  }
  return false;
}

bool VprSharedTransportStream::pullResponse() {
  invalidateCpuSystemCache();
  volatile Nrf54l15VprTransportVprShared* state = vprShared();
  if ((state->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) == 0U) {
    (void)VprControl::pollEvent(kTransportEvent);
    invalidateCpuSystemCache();
    if ((state->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) == 0U) {
      return false;
    }
  }
  const size_t copyLen = (state->vprLen <= sizeof(rxBuffer_)) ? state->vprLen : sizeof(rxBuffer_);
  memcpy(rxBuffer_, reinterpret_cast<const void*>(const_cast<const uint8_t*>(state->vprData)), copyLen);
  rxLen_ = copyLen;
  rxIndex_ = 0U;
  state->vprFlags = 0U;
  state->vprLen = 0U;
  dataSyncBarrier();
  return copyLen > 0U;
}

bool VprSharedTransportStream::poll() {
  if (rxIndex_ < rxLen_) {
    return true;
  }
  invalidateCpuSystemCache();
  return pullResponse();
}

uint32_t VprSharedTransportStream::heartbeat() const {
  invalidateCpuSystemCache();
  return vprShared()->heartbeat;
}

uint16_t VprSharedTransportStream::lastOpcode() const {
  invalidateCpuSystemCache();
  return static_cast<uint16_t>(vprShared()->lastOpcode & 0xFFFFU);
}

uint32_t VprSharedTransportStream::transportStatus() const {
  invalidateCpuSystemCache();
  return vprShared()->status;
}

uint32_t VprSharedTransportStream::lastError() const {
  invalidateCpuSystemCache();
  return vprShared()->lastError;
}

uint32_t VprSharedTransportStream::reservedState() const {
  invalidateCpuSystemCache();
  return vprShared()->reserved;
}

uint32_t VprSharedTransportStream::reservedAuxState() const {
  invalidateCpuSystemCache();
  return vprShared()->reservedAux;
}

uint32_t VprSharedTransportStream::reservedMetaState() const {
  invalidateCpuSystemCache();
  return vprShared()->reservedMeta;
}

uint32_t VprSharedTransportStream::reservedConfigState() const {
  invalidateCpuSystemCache();
  return vprShared()->reservedConfig;
}

uint32_t VprSharedTransportStream::initPc() const { return VprControl::initPc(); }

bool VprSharedTransportStream::isRunning() const { return VprControl::isRunning(); }

bool VprSharedTransportStream::secureAccessEnabled() const {
  return VprControl::secureAccessEnabled();
}

uint32_t VprSharedTransportStream::spuPerm() const { return VprControl::spuPerm(); }

uint32_t VprSharedTransportStream::debugStatus() const { return VprControl::debugStatus(); }

uint32_t VprSharedTransportStream::haltSummary0() const { return VprControl::haltSummary0(); }

uint32_t VprSharedTransportStream::haltSummary1() const { return VprControl::haltSummary1(); }

uint32_t VprSharedTransportStream::rawNordicTasks() const { return VprControl::rawNordicTasks(); }

uint32_t VprSharedTransportStream::rawNordicEvents() const { return VprControl::rawNordicEvents(); }

uint32_t VprSharedTransportStream::rawNordicEventStatus() const {
  return VprControl::rawNordicEventStatus();
}

uint32_t VprSharedTransportStream::rawSleepControl() const {
  return VprControl::rawSleepControl();
}

uint32_t VprSharedTransportStream::rawNordicCacheCtrl() const {
  return VprControl::rawNordicCacheCtrl();
}

uint32_t VprSharedTransportStream::rawMpcMemAccErrEvent() const {
  return VprControl::rawMpcMemAccErrEvent();
}

uint32_t VprSharedTransportStream::rawMpcMemAccErrAddress() const {
  return VprControl::rawMpcMemAccErrAddress();
}

uint32_t VprSharedTransportStream::rawMpcMemAccErrInfo() const {
  return VprControl::rawMpcMemAccErrInfo();
}

int VprSharedTransportStream::available() {
  (void)poll();
  return static_cast<int>((rxIndex_ < rxLen_) ? (rxLen_ - rxIndex_) : 0U);
}

int VprSharedTransportStream::read() {
  if (available() <= 0) {
    return -1;
  }
  return rxBuffer_[rxIndex_++];
}

int VprSharedTransportStream::peek() {
  if (available() <= 0) {
    return -1;
  }
  return rxBuffer_[rxIndex_];
}

void VprSharedTransportStream::flush() {}

size_t VprSharedTransportStream::write(uint8_t byte) { return write(&byte, 1U); }

size_t VprSharedTransportStream::write(const uint8_t* buffer, size_t len) {
  return writeInternal(buffer, len, false);
}

size_t VprSharedTransportStream::writeWakeRequest(const uint8_t* buffer, size_t len) {
  // True hibernate on nRF54L15 is restart-by-reset, not wake-by-command.
  // Keep the compatibility entry point, but treat hibernated transport as
  // non-writable and require the explicit reset-based restart path instead.
  return writeInternal(buffer, len, false);
}

size_t VprSharedTransportStream::writeInternal(const uint8_t* buffer,
                                               size_t len,
                                               bool allowDormantWake) {
  if (buffer == nullptr || len == 0U || len > NRF54L15_VPR_TRANSPORT_MAX_HOST_DATA) {
    return 0U;
  }
  invalidateCpuSystemCache();
  volatile Nrf54l15VprTransportHostShared* host = hostShared();
  volatile Nrf54l15VprTransportVprShared* vpr = vprShared();
  (void)allowDormantWake;
  if ((vpr->status != NRF54L15_VPR_TRANSPORT_STATUS_READY) ||
      (host->hostFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ||
      (vpr->vprFlags & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U ||
      rxIndex_ < rxLen_) {
    return 0U;
  }
  (void)VprControl::clearEvent(kTransportEvent);
  memcpy(reinterpret_cast<void*>(const_cast<uint8_t*>(host->hostData)), buffer, len);
  host->hostLen = static_cast<uint32_t>(len);
  ++host->hostSeq;
  host->hostFlags = NRF54L15_VPR_TRANSPORT_FLAG_PENDING;
  dataSyncBarrier();
  if (!VprControl::triggerTask(kTransportTask)) {
    host->hostFlags = 0U;
    host->hostLen = 0U;
    dataSyncBarrier();
    return 0U;
  }
  return len;
}

VprControllerServiceHost::VprControllerServiceHost(VprSharedTransportStream* transport)
    : transport_(transport),
      pendingH4Events_{},
      pendingH4EventLens_{},
      pendingH4EventHead_(0U),
      pendingH4EventTail_(0U),
      pendingH4EventCount_(0U),
      pendingH4EventDropped_(0U),
      pendingTickerEvents_{},
      pendingTickerEventHead_(0U),
      pendingTickerEventTail_(0U),
      pendingTickerEventCount_(0U),
      pendingTickerEventDropped_(0U),
      pendingBleLegacyAdvertisingEvents_{},
      pendingBleLegacyAdvertisingEventHead_(0U),
      pendingBleLegacyAdvertisingEventTail_(0U),
      pendingBleLegacyAdvertisingEventCount_(0U),
      pendingBleLegacyAdvertisingEventDropped_(0U),
      pendingBleConnectionEvents_{},
      pendingBleConnectionEventHead_(0U),
      pendingBleConnectionEventTail_(0U),
      pendingBleConnectionEventCount_(0U),
      pendingBleConnectionEventDropped_(0U) {}

void VprControllerServiceHost::attach(VprSharedTransportStream* transport) {
  transport_ = transport;
  clearPendingEvents();
}

void VprControllerServiceHost::clearPendingEvents() {
  memset(pendingH4Events_, 0, sizeof(pendingH4Events_));
  memset(pendingH4EventLens_, 0, sizeof(pendingH4EventLens_));
  pendingH4EventHead_ = 0U;
  pendingH4EventTail_ = 0U;
  pendingH4EventCount_ = 0U;
  pendingH4EventDropped_ = 0U;
  memset(pendingTickerEvents_, 0, sizeof(pendingTickerEvents_));
  pendingTickerEventHead_ = 0U;
  pendingTickerEventTail_ = 0U;
  pendingTickerEventCount_ = 0U;
  pendingTickerEventDropped_ = 0U;
  memset(pendingBleLegacyAdvertisingEvents_, 0,
         sizeof(pendingBleLegacyAdvertisingEvents_));
  pendingBleLegacyAdvertisingEventHead_ = 0U;
  pendingBleLegacyAdvertisingEventTail_ = 0U;
  pendingBleLegacyAdvertisingEventCount_ = 0U;
  pendingBleLegacyAdvertisingEventDropped_ = 0U;
  memset(pendingBleConnectionEvents_, 0, sizeof(pendingBleConnectionEvents_));
  pendingBleConnectionEventHead_ = 0U;
  pendingBleConnectionEventTail_ = 0U;
  pendingBleConnectionEventCount_ = 0U;
  pendingBleConnectionEventDropped_ = 0U;
}

bool VprControllerServiceHost::attached() const { return transport_ != nullptr; }

bool VprControllerServiceHost::bootDefaultService(bool rebootTransport) {
  if (!attached()) {
    return false;
  }
  clearPendingEvents();
  if (rebootTransport) {
    transport_->stop();
    delay(10);
    if (!transport_->resetSharedState() || !transport_->loadDefaultCsTransportStub()) {
      return false;
    }
  }
  return transport_->isRunning();
}

bool VprControllerServiceHost::restartLoadedService(bool clearScripts) {
  clearPendingEvents();
  return attached() && transport_->restartLoadedFirmware(clearScripts);
}

bool VprControllerServiceHost::restartAfterHibernateReset(uint32_t spinLimit) {
  clearPendingEvents();
  return attached() && transport_->restartAfterHibernateReset(spinLimit);
}

bool VprControllerServiceHost::recoverAfterHibernateFailure(bool clearScripts,
                                                            uint32_t spinLimit) {
  if (!attached()) {
    return false;
  }
  if (transport_->recoverAfterHibernateFailure(clearScripts, spinLimit)) {
    return true;
  }

  transport_->clearRetainedHibernateState();
  delay(10);
  return bootDefaultService(true);
}

uint32_t VprControllerServiceHost::readLe32(const uint8_t* data) {
  if (data == nullptr) {
    return 0U;
  }
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

bool VprControllerServiceHost::readH4Event(uint8_t* packet,
                                           size_t packetSize,
                                           size_t* packetLen,
                                           uint32_t timeoutMs) {
  if (!attached() || packet == nullptr || packetLen == nullptr || packetSize < 3U) {
    return false;
  }

  *packetLen = 0U;
  const uint32_t start = millis();
  size_t expectedLen = 0U;
  while ((millis() - start) < timeoutMs) {
    while (transport_->available() > 0) {
      const int incoming = transport_->read();
      if (incoming < 0) {
        break;
      }
      if (*packetLen >= packetSize) {
        return false;
      }
      packet[(*packetLen)++] = static_cast<uint8_t>(incoming);
      if (*packetLen == 3U) {
        expectedLen = 3U + packet[2];
        if (expectedLen > packetSize) {
          return false;
        }
      }
      if ((expectedLen != 0U) && (*packetLen >= expectedLen)) {
        return true;
      }
    }
    delay(2);
  }
  return false;
}

bool VprControllerServiceHost::parseCommandComplete(const uint8_t* packet,
                                                    size_t packetLen,
                                                    uint16_t expectedOpcode,
                                                    const uint8_t** payload,
                                                    size_t* payloadLen) {
  if (packet == nullptr || payload == nullptr || payloadLen == nullptr ||
      packetLen < 7U || packet[0] != 0x04U || packet[1] != 0x0EU) {
    return false;
  }
  const size_t paramLen = packet[2];
  if (packetLen != (3U + paramLen) || paramLen < 3U) {
    return false;
  }
  const uint16_t opcode =
      static_cast<uint16_t>(packet[4]) |
      (static_cast<uint16_t>(packet[5]) << 8U);
  if (opcode != expectedOpcode) {
    return false;
  }
  *payload = &packet[6];
  *payloadLen = paramLen - 3U;
  return true;
}

bool VprControllerServiceHost::parseCommandStatus(const uint8_t* packet,
                                                  size_t packetLen,
                                                  uint16_t expectedOpcode,
                                                  const uint8_t** payload,
                                                  size_t* payloadLen) {
  if (packet == nullptr || payload == nullptr || payloadLen == nullptr ||
      packetLen < 7U || packet[0] != 0x04U || packet[1] != 0x0FU) {
    return false;
  }
  const size_t paramLen = packet[2];
  if (packetLen != (3U + paramLen) || paramLen < 4U) {
    return false;
  }
  const uint16_t opcode =
      static_cast<uint16_t>(packet[5]) |
      (static_cast<uint16_t>(packet[6]) << 8U);
  if (opcode != expectedOpcode) {
    return false;
  }
  *payload = &packet[3];
  *payloadLen = paramLen;
  return true;
}

bool VprControllerServiceHost::parseVendorEvent(const uint8_t* packet,
                                                size_t packetLen,
                                                uint8_t expectedSubevent,
                                                const uint8_t** payload,
                                                size_t* payloadLen) {
  if (packet == nullptr || payload == nullptr || payloadLen == nullptr ||
      packetLen < 5U || packet[0] != 0x04U || packet[1] != kVendorEventCode) {
    return false;
  }
  const size_t paramLen = packet[2];
  if (packetLen != (3U + paramLen) || paramLen < 1U || packet[3] != expectedSubevent) {
    return false;
  }
  *payload = &packet[4];
  *payloadLen = paramLen - 1U;
  return true;
}

bool VprControllerServiceHost::pushPendingTickerEvent(const VprTickerEvent& event) {
  if (pendingTickerEventCount_ >= kPendingTickerEventQueueDepth) {
    pendingTickerEventDropped_ += 1U;
    return false;
  }
  pendingTickerEvents_[pendingTickerEventTail_] = event;
  pendingTickerEventTail_ = (pendingTickerEventTail_ + 1U) % kPendingTickerEventQueueDepth;
  pendingTickerEventCount_ += 1U;
  return true;
}

bool VprControllerServiceHost::pushPendingBleLegacyAdvertisingEvent(
    const VprBleLegacyAdvertisingEvent& event) {
  if (pendingBleLegacyAdvertisingEventCount_ >=
      kPendingBleLegacyAdvertisingEventQueueDepth) {
    pendingBleLegacyAdvertisingEventDropped_ += 1U;
    return false;
  }
  pendingBleLegacyAdvertisingEvents_[pendingBleLegacyAdvertisingEventTail_] =
      event;
  pendingBleLegacyAdvertisingEventTail_ =
      (pendingBleLegacyAdvertisingEventTail_ + 1U) %
      kPendingBleLegacyAdvertisingEventQueueDepth;
  pendingBleLegacyAdvertisingEventCount_ += 1U;
  return true;
}

bool VprControllerServiceHost::pushPendingBleConnectionEvent(
    const VprBleConnectionEvent& event) {
  if (pendingBleConnectionEventCount_ >= kPendingBleConnectionEventQueueDepth) {
    pendingBleConnectionEventDropped_ += 1U;
    return false;
  }
  pendingBleConnectionEvents_[pendingBleConnectionEventTail_] = event;
  pendingBleConnectionEventTail_ =
      (pendingBleConnectionEventTail_ + 1U) %
      kPendingBleConnectionEventQueueDepth;
  pendingBleConnectionEventCount_ += 1U;
  return true;
}

bool VprControllerServiceHost::pushPendingH4Event(const uint8_t* packet, size_t packetLen) {
  if (packet == nullptr || packetLen == 0U || packetLen > kPendingH4EventMaxBytes) {
    pendingH4EventDropped_ += 1U;
    return false;
  }
  if (pendingH4EventCount_ >= kPendingH4EventQueueDepth) {
    pendingH4EventDropped_ += 1U;
    return false;
  }
  memcpy(pendingH4Events_[pendingH4EventTail_], packet, packetLen);
  pendingH4EventLens_[pendingH4EventTail_] = packetLen;
  pendingH4EventTail_ = (pendingH4EventTail_ + 1U) % kPendingH4EventQueueDepth;
  pendingH4EventCount_ += 1U;
  return true;
}

bool VprControllerServiceHost::stashAsyncEvent(const uint8_t* packet, size_t packetLen) {
  bool handled = false;
  if (packet != nullptr && packetLen >= 3U && packet[0] == 0x04U) {
    handled = true;
    (void)pushPendingH4Event(packet, packetLen);
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (parseVendorEvent(packet, packetLen, kVendorEventTicker, &payload, &payloadLen) &&
      payloadLen >= 13U) {
    VprTickerEvent event{};
    event.flags = payload[0];
    event.count = readLe32(&payload[1]);
    event.step = readLe32(&payload[5]);
    event.heartbeat = readLe32(&payload[9]);
    event.sequence = (payloadLen >= 17U) ? readLe32(&payload[13]) : 0U;
    (void)pushPendingTickerEvent(event);
    handled = true;
  }
  if (parseVendorEvent(packet, packetLen, kVendorEventBleLegacyAdvertising,
                       &payload, &payloadLen) &&
      payloadLen >= 14U) {
    VprBleLegacyAdvertisingEvent advEvent{};
    advEvent.flags = payload[0];
    advEvent.channelMask = payload[1];
    advEvent.eventCount = readLe32(&payload[2]);
    advEvent.heartbeat = readLe32(&payload[6]);
    advEvent.randomDelayTicks = readLe32(&payload[10]);
    advEvent.sequence = (payloadLen >= 18U) ? readLe32(&payload[14]) : 0U;
    (void)pushPendingBleLegacyAdvertisingEvent(advEvent);
    handled = true;
  }
  if (parseVendorEvent(packet, packetLen, kVendorEventBleConnection,
                       &payload, &payloadLen) &&
      payloadLen >= 24U) {
    VprBleConnectionEvent connEvent{};
    connEvent.flags = payload[0];
    connEvent.connHandle = static_cast<uint16_t>(payload[1]) |
                           (static_cast<uint16_t>(payload[2]) << 8U);
    connEvent.reason = payload[3];
    connEvent.role = payload[4];
    connEvent.encrypted = payload[5] != 0U;
    connEvent.intervalUnits = static_cast<uint16_t>(payload[6]) |
                              (static_cast<uint16_t>(payload[7]) << 8U);
    connEvent.latency = static_cast<uint16_t>(payload[8]) |
                        (static_cast<uint16_t>(payload[9]) << 8U);
    connEvent.supervisionTimeout = static_cast<uint16_t>(payload[10]) |
                                   (static_cast<uint16_t>(payload[11]) << 8U);
    connEvent.txPhy = payload[12];
    connEvent.rxPhy = payload[13];
    connEvent.eventCount = readLe32(&payload[14]);
    connEvent.disconnectCount = readLe32(&payload[18]);
    connEvent.sequence = (payloadLen >= 26U) ? readLe32(&payload[22]) : 0U;
    (void)pushPendingBleConnectionEvent(connEvent);
    handled = true;
  }
  return handled;
}

bool VprControllerServiceHost::popPendingH4Event(uint8_t* packet,
                                                 size_t packetSize,
                                                 size_t* packetLen) {
  if (packetLen != nullptr) {
    *packetLen = 0U;
  }
  if (packet == nullptr || packetLen == nullptr || pendingH4EventCount_ == 0U) {
    return false;
  }

  const size_t copyLen = pendingH4EventLens_[pendingH4EventHead_];
  if (copyLen == 0U || copyLen > packetSize) {
    pendingH4EventDropped_ += 1U;
    memset(pendingH4Events_[pendingH4EventHead_], 0,
           sizeof(pendingH4Events_[pendingH4EventHead_]));
    pendingH4EventLens_[pendingH4EventHead_] = 0U;
    pendingH4EventHead_ = (pendingH4EventHead_ + 1U) % kPendingH4EventQueueDepth;
    pendingH4EventCount_ -= 1U;
    return false;
  }

  memcpy(packet, pendingH4Events_[pendingH4EventHead_], copyLen);
  *packetLen = copyLen;
  memset(pendingH4Events_[pendingH4EventHead_], 0,
         sizeof(pendingH4Events_[pendingH4EventHead_]));
  pendingH4EventLens_[pendingH4EventHead_] = 0U;
  pendingH4EventHead_ = (pendingH4EventHead_ + 1U) % kPendingH4EventQueueDepth;
  pendingH4EventCount_ -= 1U;
  return true;
}

bool VprControllerServiceHost::popPendingTickerEvent(VprTickerEvent* event) {
  if (event == nullptr || pendingTickerEventCount_ == 0U) {
    return false;
  }
  *event = pendingTickerEvents_[pendingTickerEventHead_];
  memset(&pendingTickerEvents_[pendingTickerEventHead_], 0,
         sizeof(pendingTickerEvents_[pendingTickerEventHead_]));
  pendingTickerEventHead_ = (pendingTickerEventHead_ + 1U) % kPendingTickerEventQueueDepth;
  pendingTickerEventCount_ -= 1U;
  return true;
}

bool VprControllerServiceHost::popPendingBleLegacyAdvertisingEvent(
    VprBleLegacyAdvertisingEvent* event) {
  if (event == nullptr || pendingBleLegacyAdvertisingEventCount_ == 0U) {
    return false;
  }
  *event = pendingBleLegacyAdvertisingEvents_[pendingBleLegacyAdvertisingEventHead_];
  memset(&pendingBleLegacyAdvertisingEvents_[pendingBleLegacyAdvertisingEventHead_], 0,
         sizeof(pendingBleLegacyAdvertisingEvents_[pendingBleLegacyAdvertisingEventHead_]));
  pendingBleLegacyAdvertisingEventHead_ =
      (pendingBleLegacyAdvertisingEventHead_ + 1U) %
      kPendingBleLegacyAdvertisingEventQueueDepth;
  pendingBleLegacyAdvertisingEventCount_ -= 1U;
  return true;
}

bool VprControllerServiceHost::popPendingBleConnectionEvent(
    VprBleConnectionEvent* event) {
  if (event == nullptr || pendingBleConnectionEventCount_ == 0U) {
    return false;
  }
  *event = pendingBleConnectionEvents_[pendingBleConnectionEventHead_];
  memset(&pendingBleConnectionEvents_[pendingBleConnectionEventHead_], 0,
         sizeof(pendingBleConnectionEvents_[pendingBleConnectionEventHead_]));
  pendingBleConnectionEventHead_ =
      (pendingBleConnectionEventHead_ + 1U) %
      kPendingBleConnectionEventQueueDepth;
  pendingBleConnectionEventCount_ -= 1U;
  return true;
}

bool VprControllerServiceHost::sendHciCommand(uint16_t opcode,
                                              const uint8_t* params,
                                              size_t paramsLen,
                                              uint8_t* response,
                                              size_t responseSize,
                                              size_t* responseLen) {
  if (!attached() || response == nullptr || responseLen == nullptr || paramsLen > 250U) {
    return false;
  }

  uint8_t command[260];
  command[0] = 0x01U;
  command[1] = static_cast<uint8_t>(opcode & 0xFFU);
  command[2] = static_cast<uint8_t>((opcode >> 8U) & 0xFFU);
  command[3] = static_cast<uint8_t>(paramsLen);
  if (paramsLen != 0U && params != nullptr) {
    memcpy(&command[4], params, paramsLen);
  }

  const size_t commandLen = 4U + paramsLen;
  uint8_t packet[kPendingH4EventMaxBytes] = {0};
  size_t packetLen = 0U;
  const uint32_t start = millis();
  bool commandSent = false;
  while ((millis() - start) < 5000UL) {
    const uint32_t elapsed = millis() - start;
    const uint32_t remaining = (elapsed < 5000UL) ? (5000UL - elapsed) : 0U;

    if (!commandSent) {
      const size_t written = transport_->write(command, commandLen);
      if (written == commandLen) {
        commandSent = true;
        continue;
      }
    }

    const uint32_t readBudget = (remaining > 50UL) ? 50UL : remaining;
    if (!readH4Event(packet, sizeof(packet), &packetLen, readBudget)) {
      if (!commandSent) {
        delay(2);
        continue;
      }
      delay(2);
      continue;
    }

    const uint8_t* payload = nullptr;
    size_t payloadLen = 0U;
    if (commandSent &&
        (parseCommandComplete(packet, packetLen, opcode, &payload, &payloadLen) ||
         parseCommandStatus(packet, packetLen, opcode, &payload, &payloadLen))) {
      if (packetLen > responseSize) {
        return false;
      }
      memcpy(response, packet, packetLen);
      *responseLen = packetLen;
      return true;
    }

    if (stashAsyncEvent(packet, packetLen)) {
      continue;
    }
  }
  return false;
}

bool VprControllerServiceHost::ping(uint32_t cookie,
                                    uint32_t* echoedCookie,
                                    uint32_t* heartbeat) {
  uint8_t response[64];
  size_t responseLen = 0U;
  uint8_t params[4] = {
      static_cast<uint8_t>(cookie & 0xFFU),
      static_cast<uint8_t>((cookie >> 8U) & 0xFFU),
      static_cast<uint8_t>((cookie >> 16U) & 0xFFU),
      static_cast<uint8_t>((cookie >> 24U) & 0xFFU),
  };
  if (!sendHciCommand(kVendorPingOpcode, params, sizeof(params), response,
                      sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorPingOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 9U || payload[0] != 0U) {
    return false;
  }

  if (echoedCookie != nullptr) {
    *echoedCookie = readLe32(&payload[1]);
  }
  if (heartbeat != nullptr) {
    *heartbeat = readLe32(&payload[5]);
  }
  return true;
}

bool VprControllerServiceHost::readTransportInfo(VprControllerServiceInfo* info) {
  if (info == nullptr) {
    return false;
  }
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorInfoOpcode, nullptr, 0U, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorInfoOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 12U || payload[0] != 0U) {
    return false;
  }

  info->transportStatus = payload[1];
  info->transportError = payload[2];
  info->transportFlags = payload[3];
  info->heartbeat = readLe32(&payload[4]);
  info->scriptCount = readLe32(&payload[8]);
  return true;
}

bool VprControllerServiceHost::readCapabilities(VprControllerServiceCapabilities* caps) {
  if (caps == nullptr) {
    return false;
  }

  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorCapabilitiesOpcode, nullptr, 0U, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorCapabilitiesOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 11U || payload[0] != 0U) {
    return false;
  }

  caps->serviceVersionMajor = payload[1];
  caps->serviceVersionMinor = payload[2];
  caps->opMask = readLe32(&payload[3]);
  caps->maxInputLen = readLe32(&payload[7]);
  return true;
}

bool VprControllerServiceHost::hashFnv1a32(const uint8_t* data,
                                           size_t len,
                                           uint32_t* hash,
                                           uint32_t* processedLen) {
  if (hash == nullptr || len > 124U || (len != 0U && data == nullptr)) {
    return false;
  }

  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorFnv1a32Opcode, data, len, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorFnv1a32Opcode, &payload,
                            &payloadLen) ||
      payloadLen < 9U || payload[0] != 0U) {
    return false;
  }

  *hash = readLe32(&payload[1]);
  if (processedLen != nullptr) {
    *processedLen = readLe32(&payload[5]);
  }
  return true;
}

bool VprControllerServiceHost::crc32(const uint8_t* data,
                                     size_t len,
                                     uint32_t* crc,
                                     uint32_t* processedLen) {
  if (crc == nullptr || len > 124U || (len != 0U && data == nullptr)) {
    return false;
  }

  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorCrc32Opcode, data, len, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorCrc32Opcode, &payload,
                            &payloadLen) ||
      payloadLen < 9U || payload[0] != 0U) {
    return false;
  }

  *crc = readLe32(&payload[1]);
  if (processedLen != nullptr) {
    *processedLen = readLe32(&payload[5]);
  }
  return true;
}

bool VprControllerServiceHost::crc32c(const uint8_t* data,
                                      size_t len,
                                      uint32_t* crc,
                                      uint32_t* processedLen) {
  if (crc == nullptr || len > 124U || (len != 0U && data == nullptr)) {
    return false;
  }

  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorCrc32cOpcode, data, len, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorCrc32cOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 9U || payload[0] != 0U) {
    return false;
  }

  *crc = readLe32(&payload[1]);
  if (processedLen != nullptr) {
    *processedLen = readLe32(&payload[5]);
  }
  return true;
}

bool VprControllerServiceHost::configureTicker(bool enabled,
                                               uint32_t periodTicks,
                                               uint32_t step,
                                               VprTickerState* state) {
  uint8_t params[9] = {
      static_cast<uint8_t>(enabled ? 1U : 0U),
      static_cast<uint8_t>(periodTicks & 0xFFU),
      static_cast<uint8_t>((periodTicks >> 8U) & 0xFFU),
      static_cast<uint8_t>((periodTicks >> 16U) & 0xFFU),
      static_cast<uint8_t>((periodTicks >> 24U) & 0xFFU),
      static_cast<uint8_t>(step & 0xFFU),
      static_cast<uint8_t>((step >> 8U) & 0xFFU),
      static_cast<uint8_t>((step >> 16U) & 0xFFU),
      static_cast<uint8_t>((step >> 24U) & 0xFFU),
  };
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorTickerConfigureOpcode, params, sizeof(params), response,
                      sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorTickerConfigureOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 14U || payload[0] != 0U) {
    return false;
  }

  if (state != nullptr) {
    state->enabled = payload[1] != 0U;
    state->periodTicks = readLe32(&payload[2]);
    state->step = readLe32(&payload[6]);
    state->count = readLe32(&payload[10]);
  }
  return true;
}

bool VprControllerServiceHost::readTickerState(VprTickerState* state) {
  if (state == nullptr) {
    return false;
  }
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorTickerReadStateOpcode, nullptr, 0U, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorTickerReadStateOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 14U || payload[0] != 0U) {
    return false;
  }

  state->enabled = payload[1] != 0U;
  state->periodTicks = readLe32(&payload[2]);
  state->step = readLe32(&payload[6]);
  state->count = readLe32(&payload[10]);
  return true;
}

bool VprControllerServiceHost::configureTickerEvents(bool enabled,
                                                     uint32_t emitEveryCount,
                                                     uint32_t* appliedEmitEveryCount,
                                                     uint32_t* droppedEvents) {
  uint8_t params[5] = {
      static_cast<uint8_t>(enabled ? 1U : 0U),
      static_cast<uint8_t>(emitEveryCount & 0xFFU),
      static_cast<uint8_t>((emitEveryCount >> 8U) & 0xFFU),
      static_cast<uint8_t>((emitEveryCount >> 16U) & 0xFFU),
      static_cast<uint8_t>((emitEveryCount >> 24U) & 0xFFU),
  };
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorTickerEventConfigureOpcode, params, sizeof(params), response,
                      sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorTickerEventConfigureOpcode,
                            &payload, &payloadLen) ||
      payloadLen < 14U || payload[0] != 0U) {
    return false;
  }

  if (appliedEmitEveryCount != nullptr) {
    *appliedEmitEveryCount = readLe32(&payload[2]);
  }
  if (droppedEvents != nullptr) {
    *droppedEvents = readLe32(&payload[10]);
  }
  return payload[1] == (enabled ? 1U : 0U);
}

bool VprControllerServiceHost::waitTickerEvent(VprTickerEvent* event, uint32_t timeoutMs) {
  if (event == nullptr || !attached()) {
    return false;
  }
  if (popPendingTickerEvent(event)) {
    return true;
  }

  uint8_t packet[64];
  size_t packetLen = 0U;
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    const uint32_t elapsed = millis() - start;
    const uint32_t remaining = (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0U;
    if (!readH4Event(packet, sizeof(packet), &packetLen, remaining)) {
      return false;
    }
    if (stashAsyncEvent(packet, packetLen) && popPendingTickerEvent(event)) {
      return true;
    }
  }
  return false;
}

bool VprControllerServiceHost::configureBleLegacyAdvertising(
    bool enabled,
    uint32_t intervalTicks,
    uint8_t channelMask,
    bool addRandomDelay,
    VprBleLegacyAdvertisingState* state) {
  uint8_t params[7] = {
      static_cast<uint8_t>(enabled ? 1U : 0U),
      static_cast<uint8_t>(intervalTicks & 0xFFU),
      static_cast<uint8_t>((intervalTicks >> 8U) & 0xFFU),
      static_cast<uint8_t>((intervalTicks >> 16U) & 0xFFU),
      static_cast<uint8_t>((intervalTicks >> 24U) & 0xFFU),
      static_cast<uint8_t>(channelMask & 0x07U),
      static_cast<uint8_t>(addRandomDelay ? 1U : 0U),
  };
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleLegacyAdvertisingConfigureOpcode, params,
                      sizeof(params), response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleLegacyAdvertisingConfigureOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 21U || payload[0] != 0U) {
    return false;
  }

  if (state != nullptr) {
    state->enabled = payload[1] != 0U;
    state->channelMask = payload[2];
    state->addRandomDelay = payload[3] != 0U;
    state->lastChannelMask = payload[4];
    state->intervalTicks = readLe32(&payload[5]);
    state->lastRandomDelayTicks = readLe32(&payload[9]);
    state->eventCount = readLe32(&payload[13]);
    state->droppedEvents = readLe32(&payload[17]);
  }
  return true;
}

bool VprControllerServiceHost::readBleLegacyAdvertisingState(
    VprBleLegacyAdvertisingState* state) {
  if (state == nullptr) {
    return false;
  }
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleLegacyAdvertisingReadStateOpcode, nullptr, 0U,
                      response, sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleLegacyAdvertisingReadStateOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 21U || payload[0] != 0U) {
    return false;
  }

  state->enabled = payload[1] != 0U;
  state->channelMask = payload[2];
  state->addRandomDelay = payload[3] != 0U;
  state->lastChannelMask = payload[4];
  state->intervalTicks = readLe32(&payload[5]);
  state->lastRandomDelayTicks = readLe32(&payload[9]);
  state->eventCount = readLe32(&payload[13]);
  state->droppedEvents = readLe32(&payload[17]);
  return true;
}

bool VprControllerServiceHost::writeBleLegacyAdvertisingData(
    const uint8_t* data,
    size_t len,
    VprBleLegacyAdvertisingData* applied) {
  if ((data == nullptr && len != 0U) || len > 31U) {
    return false;
  }

  uint8_t params[32];
  memset(params, 0, sizeof(params));
  params[0] = static_cast<uint8_t>(len);
  if (len != 0U) {
    memcpy(&params[1], data, len);
  }

  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleLegacyAdvertisingWriteDataOpcode, params,
                      len + 1U, response, sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleLegacyAdvertisingWriteDataOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 2U || payload[0] != 0U || payload[1] > 31U ||
      payloadLen < (size_t)(2U + payload[1])) {
    return false;
  }

  if (applied != nullptr) {
    memset(applied, 0, sizeof(*applied));
    applied->length = payload[1];
    if (applied->length != 0U) {
      memcpy(applied->bytes, &payload[2], applied->length);
    }
  }
  return true;
}

bool VprControllerServiceHost::readBleLegacyAdvertisingData(
    VprBleLegacyAdvertisingData* data) {
  if (data == nullptr) {
    return false;
  }

  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleLegacyAdvertisingReadDataOpcode, nullptr, 0U,
                      response, sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleLegacyAdvertisingReadDataOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 2U || payload[0] != 0U || payload[1] > 31U ||
      payloadLen < (size_t)(2U + payload[1])) {
    return false;
  }

  memset(data, 0, sizeof(*data));
  data->length = payload[1];
  if (data->length != 0U) {
    memcpy(data->bytes, &payload[2], data->length);
  }
  return true;
}

bool VprControllerServiceHost::waitBleLegacyAdvertisingEvent(
    VprBleLegacyAdvertisingEvent* event,
    uint32_t timeoutMs) {
  if (event == nullptr || !attached()) {
    return false;
  }
  if (popPendingBleLegacyAdvertisingEvent(event)) {
    return true;
  }

  uint8_t packet[64];
  size_t packetLen = 0U;
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    const uint32_t elapsed = millis() - start;
    const uint32_t remaining =
        (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0U;
    if (!readH4Event(packet, sizeof(packet), &packetLen, remaining)) {
      return false;
    }
    if (stashAsyncEvent(packet, packetLen) &&
        popPendingBleLegacyAdvertisingEvent(event)) {
      return true;
    }
  }
  return false;
}

bool VprControllerServiceHost::configureBleConnection(
    uint16_t connHandle,
    uint8_t role,
    bool encrypted,
    uint16_t intervalUnits,
    uint16_t latency,
    uint16_t supervisionTimeout,
    uint8_t txPhy,
    uint8_t rxPhy,
    VprBleConnectionState* state) {
  uint8_t params[12] = {
      static_cast<uint8_t>(connHandle & 0xFFU),
      static_cast<uint8_t>((connHandle >> 8U) & 0xFFU),
      role,
      static_cast<uint8_t>(encrypted ? 1U : 0U),
      static_cast<uint8_t>(intervalUnits & 0xFFU),
      static_cast<uint8_t>((intervalUnits >> 8U) & 0xFFU),
      static_cast<uint8_t>(latency & 0xFFU),
      static_cast<uint8_t>((latency >> 8U) & 0xFFU),
      static_cast<uint8_t>(supervisionTimeout & 0xFFU),
      static_cast<uint8_t>((supervisionTimeout >> 8U) & 0xFFU),
      txPhy,
      rxPhy,
  };
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleConnectionConfigureOpcode, params, sizeof(params),
                      response, sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleConnectionConfigureOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 22U || payload[0] != 0U) {
    return false;
  }

  if (state != nullptr) {
    state->connected = payload[1] != 0U;
    state->connHandle = static_cast<uint16_t>(payload[2]) |
                        (static_cast<uint16_t>(payload[3]) << 8U);
    state->role = payload[4];
    state->encrypted = payload[5] != 0U;
    state->intervalUnits = static_cast<uint16_t>(payload[6]) |
                           (static_cast<uint16_t>(payload[7]) << 8U);
    state->latency = static_cast<uint16_t>(payload[8]) |
                     (static_cast<uint16_t>(payload[9]) << 8U);
    state->supervisionTimeout = static_cast<uint16_t>(payload[10]) |
                                (static_cast<uint16_t>(payload[11]) << 8U);
    state->txPhy = payload[12];
    state->rxPhy = payload[13];
    state->eventCount = readLe32(&payload[14]);
    state->disconnectCount = readLe32(&payload[18]);
  }
  return true;
}

bool VprControllerServiceHost::readBleConnectionState(
    VprBleConnectionState* state) {
  if (state == nullptr) {
    return false;
  }
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleConnectionReadStateOpcode, nullptr, 0U, response,
                      sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleConnectionReadStateOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 22U || payload[0] != 0U) {
    return false;
  }

  state->connected = payload[1] != 0U;
  state->connHandle = static_cast<uint16_t>(payload[2]) |
                      (static_cast<uint16_t>(payload[3]) << 8U);
  state->role = payload[4];
  state->encrypted = payload[5] != 0U;
  state->intervalUnits = static_cast<uint16_t>(payload[6]) |
                         (static_cast<uint16_t>(payload[7]) << 8U);
  state->latency = static_cast<uint16_t>(payload[8]) |
                   (static_cast<uint16_t>(payload[9]) << 8U);
  state->supervisionTimeout = static_cast<uint16_t>(payload[10]) |
                              (static_cast<uint16_t>(payload[11]) << 8U);
  state->txPhy = payload[12];
  state->rxPhy = payload[13];
  state->eventCount = readLe32(&payload[14]);
  state->disconnectCount = readLe32(&payload[18]);
  return true;
}

bool VprControllerServiceHost::disconnectBleConnection(
    uint16_t connHandle,
    uint8_t reason,
    VprBleConnectionState* state) {
  uint8_t params[3] = {
      static_cast<uint8_t>(connHandle & 0xFFU),
      static_cast<uint8_t>((connHandle >> 8U) & 0xFFU),
      reason,
  };
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleConnectionDisconnectOpcode, params, sizeof(params),
                      response, sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen,
                            kVendorBleConnectionDisconnectOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 22U || payload[0] != 0U) {
    return false;
  }

  if (state != nullptr) {
    state->connected = payload[1] != 0U;
    state->connHandle = static_cast<uint16_t>(payload[2]) |
                        (static_cast<uint16_t>(payload[3]) << 8U);
    state->role = payload[4];
    state->encrypted = payload[5] != 0U;
    state->intervalUnits = static_cast<uint16_t>(payload[6]) |
                           (static_cast<uint16_t>(payload[7]) << 8U);
    state->latency = static_cast<uint16_t>(payload[8]) |
                     (static_cast<uint16_t>(payload[9]) << 8U);
    state->supervisionTimeout = static_cast<uint16_t>(payload[10]) |
                                (static_cast<uint16_t>(payload[11]) << 8U);
    state->txPhy = payload[12];
    state->rxPhy = payload[13];
    state->eventCount = readLe32(&payload[14]);
    state->disconnectCount = readLe32(&payload[18]);
  }
  return true;
}

bool VprControllerServiceHost::readBleConnectionSharedState(
    VprBleConnectionSharedState* state) {
  if (state == nullptr || !attached()) {
    return false;
  }

  const uint32_t packed = transport_->reservedState();
  const uint32_t packedAux = transport_->reservedAuxState();
  const uint32_t packedMeta = transport_->reservedMetaState();
  const uint32_t packedConfig = transport_->reservedConfigState();

  memset(state, 0, sizeof(*state));
  state->hostRequestPending =
      (packed & NRF54L15_VPR_TRANSPORT_FLAG_PENDING) != 0U;
  state->restoredFromHibernate = (packed & 0x00000002UL) != 0U;
  state->connected = (packed & 0x00000004UL) != 0U;
  state->encrypted = (packed & 0x00000008UL) != 0U;
  state->csLinkBound = (packed & 0x00000010UL) != 0U;
  state->csLinkRunnable = (packed & 0x00000020UL) != 0U;
  state->role = static_cast<uint8_t>((packed >> 8U) & 0xFFU);
  state->connHandle = static_cast<uint16_t>((packed >> 16U) & 0xFFFFU);
  state->intervalUnits = static_cast<uint16_t>(packedAux & 0xFFFFU);
  state->latency = static_cast<uint16_t>((packedAux >> 16U) & 0xFFFFU);
  state->supervisionTimeout = static_cast<uint16_t>(packedMeta & 0xFFFFU);
  state->txPhy = static_cast<uint8_t>((packedMeta >> 16U) & 0xFFU);
  state->rxPhy = static_cast<uint8_t>((packedMeta >> 24U) & 0xFFU);
  state->lastEventFlags = static_cast<uint8_t>(packedConfig & 0xFFU);
  state->lastDisconnectReason = static_cast<uint8_t>((packedConfig >> 8U) & 0xFFU);
  state->eventCount = static_cast<uint16_t>((packedConfig >> 16U) & 0xFFFFU);
  return true;
}

bool VprControllerServiceHost::configureBleCsLink(bool bound,
                                                  uint16_t connHandle,
                                                  VprBleCsLinkState* state) {
  uint8_t params[3] = {
      static_cast<uint8_t>(bound ? 1U : 0U),
      static_cast<uint8_t>(connHandle & 0xFFU),
      static_cast<uint8_t>((connHandle >> 8U) & 0xFFU),
  };
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleCsLinkConfigureOpcode, params, sizeof(params),
                      response, sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorBleCsLinkConfigureOpcode,
                            &payload, &payloadLen) ||
      payloadLen < 12U || payload[0] != 0U) {
    return false;
  }

  if (state != nullptr) {
    memset(state, 0, sizeof(*state));
    state->bound = payload[1] != 0U;
    state->runnable = payload[2] != 0U;
    state->connected = payload[3] != 0U;
    state->encrypted = payload[4] != 0U;
    state->connHandle = static_cast<uint16_t>(payload[5]) |
                        (static_cast<uint16_t>(payload[6]) << 8U);
    state->role = payload[7];
    state->eventCount = readLe32(&payload[8]);
  }
  return true;
}

bool VprControllerServiceHost::readBleCsLinkState(VprBleCsLinkState* state) {
  if (state == nullptr) {
    return false;
  }
  uint8_t response[64];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorBleCsLinkReadStateOpcode, nullptr, 0U, response,
                      sizeof(response), &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorBleCsLinkReadStateOpcode,
                            &payload, &payloadLen) ||
      payloadLen < 12U || payload[0] != 0U) {
    return false;
  }

  memset(state, 0, sizeof(*state));
  state->bound = payload[1] != 0U;
  state->runnable = payload[2] != 0U;
  state->connected = payload[3] != 0U;
  state->encrypted = payload[4] != 0U;
  state->connHandle = static_cast<uint16_t>(payload[5]) |
                      (static_cast<uint16_t>(payload[6]) << 8U);
  state->role = payload[7];
  state->eventCount = readLe32(&payload[8]);
  return true;
}

bool VprControllerServiceHost::waitBleConnectionSharedState(
    bool connected,
    uint16_t minEventCount,
    VprBleConnectionSharedState* state,
    uint32_t timeoutMs) {
  if (!attached()) {
    return false;
  }

  VprBleConnectionSharedState snapshot{};
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    (void)transport_->poll();
    if (!readBleConnectionSharedState(&snapshot)) {
      return false;
    }
    if (snapshot.connected == connected && snapshot.eventCount >= minEventCount) {
      if (state != nullptr) {
        *state = snapshot;
      }
      return true;
    }
    delay(2);
  }
  return false;
}

bool VprControllerServiceHost::waitBleConnectionEvent(
    VprBleConnectionEvent* event,
    uint32_t timeoutMs) {
  if (event == nullptr || !attached()) {
    return false;
  }
  if (popPendingBleConnectionEvent(event)) {
    return true;
  }

  uint8_t packet[64];
  size_t packetLen = 0U;
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    const uint32_t elapsed = millis() - start;
    const uint32_t remaining =
        (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0U;
    if (!readH4Event(packet, sizeof(packet), &packetLen, remaining)) {
      return false;
    }
    if (stashAsyncEvent(packet, packetLen) && popPendingBleConnectionEvent(event)) {
      return true;
    }
  }
  return false;
}

uint32_t VprControllerServiceHost::pendingH4EventDropCount() const {
  return pendingH4EventDropped_;
}

uint32_t VprControllerServiceHost::pendingTickerEventDropCount() const {
  return pendingTickerEventDropped_;
}

uint32_t VprControllerServiceHost::pendingBleLegacyAdvertisingEventDropCount()
    const {
  return pendingBleLegacyAdvertisingEventDropped_;
}

uint32_t VprControllerServiceHost::pendingBleConnectionEventDropCount() const {
  return pendingBleConnectionEventDropped_;
}

bool VprControllerServiceHost::enterHibernate() {
  uint8_t response[32];
  size_t responseLen = 0U;
  if (!sendHciCommand(kVendorEnterHibernateOpcode, nullptr, 0U, response, sizeof(response),
                      &responseLen)) {
    return false;
  }

  const uint8_t* payload = nullptr;
  size_t payloadLen = 0U;
  if (!parseCommandComplete(response, responseLen, kVendorEnterHibernateOpcode, &payload,
                            &payloadLen) ||
      payloadLen < 1U || payload[0] != 0U) {
    return false;
  }
  return true;
}

bool VprControllerServiceHost::probe(uint32_t cookie,
                                     VprControllerServiceInfo* info,
                                     uint32_t* echoedCookie,
                                     uint32_t* heartbeat) {
  uint32_t echoed = 0U;
  uint32_t hb = 0U;
  if (!ping(cookie, &echoed, &hb) || echoed != cookie) {
    return false;
  }
  if (echoedCookie != nullptr) {
    *echoedCookie = echoed;
  }
  if (heartbeat != nullptr) {
    *heartbeat = hb;
  }
  return (info == nullptr) ? true : readTransportInfo(info);
}

}  // namespace xiao_nrf54l15
