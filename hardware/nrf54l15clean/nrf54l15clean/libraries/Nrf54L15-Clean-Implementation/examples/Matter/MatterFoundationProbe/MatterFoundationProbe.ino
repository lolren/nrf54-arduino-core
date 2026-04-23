#include <matter_platform_nrf54l15.h>

using xiao_nrf54l15::MatterRuntimeOwnership;

namespace {

void printFlag(const char* label, bool value) {
  Serial.print(label);
  Serial.print('=');
  Serial.println(value ? 1 : 0);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.print("matter_foundation build=");
  Serial.println(xiao_nrf54l15::matterFoundationBuildMode());
  Serial.print("matter_foundation transport=");
  Serial.println(xiao_nrf54l15::matterFoundationTransportName());
  Serial.print("matter_foundation commissioning=");
  Serial.println(xiao_nrf54l15::matterFoundationCommissioningName());
  Serial.print("matter_foundation device=");
  Serial.println(MatterRuntimeOwnership::kFirstDeviceType);

  printFlag("cpuapp", MatterRuntimeOwnership::kCpuAppHostsMatter);
  printFlag("thread", MatterRuntimeOwnership::kUsesThreadTransport);
  printFlag("ble", MatterRuntimeOwnership::kUsesBleRendezvousFirst);
  printFlag("prefs", MatterRuntimeOwnership::kUsesPreferencesStorage);
  printFlag("entropy", MatterRuntimeOwnership::kUsesCracenEntropy);
  printFlag("crypto", MatterRuntimeOwnership::kUsesCracenCrypto);
  printFlag("clock", MatterRuntimeOwnership::kUsesHalTimebase);
  printFlag("loop", MatterRuntimeOwnership::kUsesCooperativeLoopPump);
  printFlag("vpr", MatterRuntimeOwnership::kUsesVprOffload);
  printFlag("import", MatterRuntimeOwnership::kConnectedHomeIpImportPathDefined);
  printFlag("matter_target", MatterRuntimeOwnership::kCompileOnlyMatterTargetClaimed);

  Serial.print("matter_foundation import=");
  Serial.println(MatterRuntimeOwnership::kMatterImportScriptPath);
  Serial.print("matter_foundation stage=");
  Serial.println(MatterRuntimeOwnership::kMatterStagingPath);
  Serial.print("matter_foundation docs=");
  Serial.println(MatterRuntimeOwnership::kOwnershipDocPath);
}

void loop() {
}
