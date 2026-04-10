#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

Tampc g_tampc;

void printStatus() {
  Serial.print("tamper=");
  Serial.print(g_tampc.tamperDetected() ? 1 : 0);
  Serial.print(" write_err=");
  Serial.print(g_tampc.writeErrorDetected() ? 1 : 0);
  Serial.print(" shield_en=");
  Serial.print(g_tampc.activeShieldMonitorEnabled() ? 1 : 0);
  Serial.print(" shield_mask=0x");
  Serial.print(g_tampc.activeShieldChannelMask(), HEX);
  Serial.print(" cracen_mon=");
  Serial.print(g_tampc.cracenTamperMonitorEnabled() ? 1 : 0);
  Serial.print(" glitch_slow=");
  Serial.print(g_tampc.glitchSlowMonitorEnabled() ? 1 : 0);
  Serial.print(" glitch_fast=");
  Serial.print(g_tampc.glitchFastMonitorEnabled() ? 1 : 0);
  Serial.print(" int_reset=");
  Serial.print(g_tampc.internalResetOnTamperEnabled() ? 1 : 0);
  Serial.print(" ext_reset=");
  Serial.print(g_tampc.externalResetOnTamperEnabled() ? 1 : 0);
  Serial.print(" erase=");
  Serial.print(g_tampc.eraseProtectEnabled() ? 1 : 0);
  Serial.print(" dbgen=");
  Serial.print(g_tampc.domainDbgenEnabled() ? 1 : 0);
  Serial.print(" niden=");
  Serial.print(g_tampc.domainNidenEnabled() ? 1 : 0);
  Serial.print(" spiden=");
  Serial.print(g_tampc.domainSpidenEnabled() ? 1 : 0);
  Serial.print(" spniden=");
  Serial.print(g_tampc.domainSpnidenEnabled() ? 1 : 0);
  Serial.print(" ap_dbgen=");
  Serial.print(g_tampc.apDbgenEnabled() ? 1 : 0);
  Serial.print(" status=0x");
  Serial.println(g_tampc.status(), HEX);
}

void printErrors() {
  Serial.print("err shield=");
  Serial.print(g_tampc.activeShieldStatusError() ? 1 : 0);
  Serial.print(" cracen=");
  Serial.print(g_tampc.cracenTamperStatusError() ? 1 : 0);
  Serial.print(" slow=");
  Serial.print(g_tampc.glitchSlowStatusError() ? 1 : 0);
  Serial.print(" fast=");
  Serial.print(g_tampc.glitchFastStatusError() ? 1 : 0);
  Serial.print(" int_reset=");
  Serial.print(g_tampc.intResetStatusError() ? 1 : 0);
  Serial.print(" ext_reset=");
  Serial.print(g_tampc.extResetStatusError() ? 1 : 0);
  Serial.print(" erase=");
  Serial.print(g_tampc.eraseProtectStatusError() ? 1 : 0);
  Serial.print(" dbgen=");
  Serial.print(g_tampc.domainDbgenStatusError() ? 1 : 0);
  Serial.print(" niden=");
  Serial.print(g_tampc.domainNidenStatusError() ? 1 : 0);
  Serial.print(" spiden=");
  Serial.print(g_tampc.domainSpidenStatusError() ? 1 : 0);
  Serial.print(" spniden=");
  Serial.print(g_tampc.domainSpnidenStatusError() ? 1 : 0);
  Serial.print(" ap_dbgen=");
  Serial.println(g_tampc.apDbgenStatusError() ? 1 : 0);
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  s status");
  Serial.println("  e error/status bits");
  Serial.println("  m cycle active-shield channel mask");
  Serial.println("  a toggle active-shield monitor");
  Serial.println("  c toggle CRACEN tamper monitor");
  Serial.println("  g toggle glitch slow+fast monitors");
  Serial.println("  d toggle DOMAIN0.DBGEN");
  Serial.println("  n toggle DOMAIN0.NIDEN");
  Serial.println("  p toggle DOMAIN0.SPIDEN");
  Serial.println("  q toggle DOMAIN0.SPNIDEN");
  Serial.println("  b toggle AP0.DBGEN");
  Serial.println("  i toggle internal tamper reset");
  Serial.println("  o toggle external tamper reset");
  Serial.println("  x clear TAMPC events");
}

void cycleShieldMask() {
  const uint8_t nextMask = static_cast<uint8_t>((g_tampc.activeShieldChannelMask() + 1U) & 0x0FU);
  const bool ok = g_tampc.setActiveShieldChannelMask(nextMask);
  Serial.print("shield mask set=");
  Serial.print(ok ? 1 : 0);
  Serial.print(" new=0x");
  Serial.println(g_tampc.activeShieldChannelMask(), HEX);
}

template <typename Setter, typename Getter>
void toggleSetting(const char* label, Setter setter, Getter getter) {
  const bool next = !getter();
  const bool ok = setter(next);
  Serial.print(label);
  Serial.print(" set=");
  Serial.print(ok ? 1 : 0);
  Serial.print(" new=");
  Serial.println(getter() ? 1 : 0);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("TAMPC advanced config probe");
  printHelp();
  printStatus();
  printErrors();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }

  const int incoming = Serial.read();
  if (incoming < 0) {
    return;
  }

  switch (static_cast<char>(incoming)) {
    case 's':
      printStatus();
      break;
    case 'e':
      printErrors();
      break;
    case 'm':
      cycleShieldMask();
      break;
    case 'a':
      toggleSetting("active-shield", [](bool enable) {
        return g_tampc.setActiveShieldMonitor(enable);
      }, []() { return g_tampc.activeShieldMonitorEnabled(); });
      break;
    case 'c':
      toggleSetting("cracen-monitor", [](bool enable) {
        return g_tampc.setCracenTamperMonitor(enable);
      }, []() { return g_tampc.cracenTamperMonitorEnabled(); });
      break;
    case 'g': {
      const bool next = !g_tampc.glitchSlowMonitorEnabled();
      const bool okSlow = g_tampc.setGlitchSlowMonitor(next);
      const bool okFast = g_tampc.setGlitchFastMonitor(next);
      Serial.print("glitch set=");
      Serial.print((okSlow && okFast) ? 1 : 0);
      Serial.print(" slow=");
      Serial.print(g_tampc.glitchSlowMonitorEnabled() ? 1 : 0);
      Serial.print(" fast=");
      Serial.println(g_tampc.glitchFastMonitorEnabled() ? 1 : 0);
      break;
    }
    case 'd':
      toggleSetting("domain0-dbgen", [](bool enable) {
        return g_tampc.setDomainDbgen(enable);
      }, []() { return g_tampc.domainDbgenEnabled(); });
      break;
    case 'n':
      toggleSetting("domain0-niden", [](bool enable) {
        return g_tampc.setDomainNiden(enable);
      }, []() { return g_tampc.domainNidenEnabled(); });
      break;
    case 'p':
      toggleSetting("domain0-spiden", [](bool enable) {
        return g_tampc.setDomainSpiden(enable);
      }, []() { return g_tampc.domainSpidenEnabled(); });
      break;
    case 'q':
      toggleSetting("domain0-spniden", [](bool enable) {
        return g_tampc.setDomainSpniden(enable);
      }, []() { return g_tampc.domainSpnidenEnabled(); });
      break;
    case 'b':
      toggleSetting("ap0-dbgen", [](bool enable) {
        return g_tampc.setApDbgen(enable);
      }, []() { return g_tampc.apDbgenEnabled(); });
      break;
    case 'i':
      toggleSetting("internal-reset", [](bool enable) {
        return g_tampc.setInternalResetOnTamper(enable);
      }, []() { return g_tampc.internalResetOnTamperEnabled(); });
      break;
    case 'o':
      toggleSetting("external-reset", [](bool enable) {
        return g_tampc.setExternalResetOnTamper(enable);
      }, []() { return g_tampc.externalResetOnTamperEnabled(); });
      break;
    case 'x':
      g_tampc.clearEvents();
      Serial.println("events cleared");
      break;
    default:
      printHelp();
      break;
  }
}
