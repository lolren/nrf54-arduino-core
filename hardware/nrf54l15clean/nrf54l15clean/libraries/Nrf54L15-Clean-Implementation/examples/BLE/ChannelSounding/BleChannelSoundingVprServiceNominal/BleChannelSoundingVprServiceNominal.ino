/*
 * BleChannelSoundingVprServiceNominal
 *
 * Runs the generic VPR BLE controller service in-place, configures one nominal
 * encrypted BLE link, binds the generic-service CS workflow to that link, and
 * waits for the built-in nominal CS procedure summary.
 *
 * This is a single-board generic-service example:
 *   - no dedicated CS image boot
 *   - no imported connected-handle handoff
 *   - no reflector board required
 *
 * The printed `nominal_dist_m` value is the current synthetic regression
 * summary produced by the generic VPR service. It is not a physical distance
 * claim.
 */

#include <Arduino.h>

#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kRolePeripheral = 1U;
constexpr uint16_t kIntervalUnits = 24U;
constexpr uint16_t kLatency = 0U;
constexpr uint16_t kSupervisionTimeout = 400U;
constexpr uint8_t kPhy1M = 1U;
constexpr uint8_t kConfigId = 1U;
constexpr uint8_t kMaxProcedureCount = 3U;
constexpr uint8_t kDisconnectReason = 0x13U;
constexpr uint32_t kEventTimeoutMs = 2500U;
constexpr uint32_t kRunIntervalMs = 4000U;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
uint32_t g_lastRunMs = 0U;
uint32_t g_runCount = 0U;

VprBleConnectedCsWorkflowConfig makeWorkflowConfig() {
  VprBleConnectedCsWorkflowConfig config{};
  config.connHandle = kConnHandle;
  config.role = kRolePeripheral;
  config.encrypted = true;
  config.intervalUnits = kIntervalUnits;
  config.latency = kLatency;
  config.supervisionTimeout = kSupervisionTimeout;
  config.txPhy = kPhy1M;
  config.rxPhy = kPhy1M;
  config.configId = kConfigId;
  config.defaultsApplied = true;
  config.createConfig = true;
  config.securityEnabled = true;
  config.procedureParamsApplied = true;
  config.procedureEnabled = true;
  config.maxProcedureCount = kMaxProcedureCount;
  return config;
}

bool runMeasurement() {
  const VprBleConnectedCsWorkflowConfig config = makeWorkflowConfig();
  VprControllerServiceCapabilities caps{};
  VprBleConnectedCsWorkflowRunState run{};
  const bool ok =
      g_service.runFreshBleConnectedCsWorkflow(config, kDisconnectReason, &run,
                                               true, kEventTimeoutMs) &&
      g_service.readCapabilities(&caps) &&
      (caps.opMask & VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsLinkConfigure) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowConfigure) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowReadState) != 0U &&
      run.configuredConnection.connected &&
      run.configuredConnection.connHandle == kConnHandle &&
      run.connectEvent.flags == 0x01U &&
      run.connectEvent.connHandle == kConnHandle &&
      run.connectedShared.connected &&
      run.connectedShared.connHandle == kConnHandle &&
      run.connectedShared.csLinkBound &&
      run.connectedShared.csLinkRunnable &&
      run.connectedShared.csWorkflowConfigured &&
      run.connectedShared.csWorkflowEnabled &&
      run.linkState.bound &&
      run.linkState.runnable &&
      run.linkState.connHandle == kConnHandle &&
      run.startedWorkflow.linkBound &&
      run.startedWorkflow.linkRunnable &&
      run.startedWorkflow.configured &&
      run.startedWorkflow.enabled &&
      run.startedWorkflow.running &&
      !run.startedWorkflow.completed &&
      run.startedWorkflow.connHandle == kConnHandle &&
      run.startedWorkflow.configId == kConfigId &&
      run.startedWorkflow.maxProcedureCount == kMaxProcedureCount &&
      run.completedWorkflow.completed &&
      !run.completedWorkflow.running &&
      run.completedWorkflow.completedProcedureCount == kMaxProcedureCount &&
      run.completedWorkflow.completedConfigId == kConfigId &&
      run.completedWorkflow.nominalDistanceQ4 > 0U &&
      run.completedWorkflow.workflowEventCount == kMaxProcedureCount &&
      run.completedWorkflow.completedLocalSubeventCount == 2U &&
      run.completedWorkflow.completedPeerSubeventCount == 3U &&
      run.completedWorkflow.completedLocalStepCount == 5U &&
      run.completedWorkflow.completedPeerStepCount == 5U &&
      run.completedWorkflow.completedLocalMode1Count == 1U &&
      run.completedWorkflow.completedPeerMode1Count == 1U &&
      run.completedWorkflow.completedLocalMode2Count == 4U &&
      run.completedWorkflow.completedPeerMode2Count == 4U &&
      run.completedWorkflow.completedDemoChannelsPacked != 0U &&
      run.completedWorkflow.completedLocalHash32 != 0U &&
      run.completedWorkflow.completedPeerHash32 != 0U &&
      run.completedWorkflow.completedLocalHash32 !=
          run.completedWorkflow.completedPeerHash32 &&
      !run.finalShared.connected &&
      !run.finalShared.csLinkBound &&
      !run.finalShared.csLinkRunnable &&
      !run.finalShared.csWorkflowConfigured &&
      !run.finalShared.csWorkflowEnabled &&
      run.finalShared.lastDisconnectReason == kDisconnectReason &&
      !run.finalWorkflow.connected &&
      !run.finalWorkflow.linkBound &&
      !run.finalWorkflow.linkRunnable &&
      !run.finalWorkflow.configured &&
      !run.finalWorkflow.enabled &&
      !run.finalWorkflow.running &&
      !run.finalWorkflow.completed &&
      run.finalWorkflow.connHandle == 0U;

  ++g_runCount;
  Serial.print(F("run="));
  Serial.print(g_runCount);
  Serial.print(F(" ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" svc="));
  Serial.print(caps.serviceVersionMajor);
  Serial.print('.');
  Serial.print(caps.serviceVersionMinor);
  Serial.print(F(" conn="));
  Serial.print(run.connectedShared.connected ? 1 : 0);
  Serial.print(F("@0x"));
  Serial.print(run.connectedShared.connHandle, HEX);
  Serial.print(F("#"));
  Serial.print(run.connectedShared.eventCount);
  Serial.print(F(" start="));
  Serial.print(run.startedWorkflow.linkBound ? 1 : 0);
  Serial.print('/');
  Serial.print(run.startedWorkflow.linkRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(run.startedWorkflow.configured ? 1 : 0);
  Serial.print('/');
  Serial.print(run.startedWorkflow.enabled ? 1 : 0);
  Serial.print('/');
  Serial.print(run.startedWorkflow.running ? 1 : 0);
  Serial.print(F(" done="));
  Serial.print(run.completedWorkflow.completed ? 1 : 0);
  Serial.print('/');
  Serial.print(run.completedWorkflow.completedProcedureCount);
  Serial.print(F("@"));
  Serial.print(run.completedWorkflow.nominalDistanceQ4 / 10000.0f, 4);
  Serial.print(F(" summary="));
  Serial.print(run.completedWorkflow.completedLocalSubeventCount);
  Serial.print('/');
  Serial.print(run.completedWorkflow.completedPeerSubeventCount);
  Serial.print(F(" steps="));
  Serial.print(run.completedWorkflow.completedLocalStepCount);
  Serial.print('/');
  Serial.print(run.completedWorkflow.completedPeerStepCount);
  Serial.print(F(" modes="));
  Serial.print(run.completedWorkflow.completedLocalMode1Count);
  Serial.print('+');
  Serial.print(run.completedWorkflow.completedLocalMode2Count);
  Serial.print('/');
  Serial.print(run.completedWorkflow.completedPeerMode1Count);
  Serial.print('+');
  Serial.print(run.completedWorkflow.completedPeerMode2Count);
  Serial.print(F(" ch=0x"));
  Serial.print(run.completedWorkflow.completedDemoChannelsPacked, HEX);
  Serial.print(F(" hash=0x"));
  Serial.print(run.completedWorkflow.completedLocalHash32, HEX);
  Serial.print(F("/0x"));
  Serial.print(run.completedWorkflow.completedPeerHash32, HEX);
  Serial.print(F(" final="));
  Serial.print(run.finalShared.connected ? 1 : 0);
  Serial.print('/');
  Serial.print(run.finalShared.csLinkBound ? 1 : 0);
  Serial.print('/');
  Serial.print(run.finalShared.csLinkRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(run.finalShared.csWorkflowConfigured ? 1 : 0);
  Serial.print('/');
  Serial.print(run.finalShared.csWorkflowEnabled ? 1 : 0);
  Serial.print(F("#"));
  Serial.print(run.finalShared.lastDisconnectReason, HEX);
  Serial.print(F(" nominal_dist_m="));
  Serial.println(run.completedWorkflow.nominalDistanceQ4 / 10000.0f, 4);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < 1500U) {
  }

  Serial.println(F("BleChannelSoundingVprServiceNominal start"));
  Serial.println(F("mode=generic_vpr_service_in_place"));
  Serial.println(F("reflector=not_required"));
  Serial.println(F("note=nominal_synthetic_regression_output_only"));
  runMeasurement();
  g_lastRunMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if ((now - g_lastRunMs) >= kRunIntervalMs) {
    g_lastRunMs = now;
    runMeasurement();
  }
}
