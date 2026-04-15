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

#include "ble_channel_sounding.h"
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

struct ParsedCompletedResultSummary {
  bool valid = false;
  BleCsSubeventResult result{};
  uint16_t stepCount = 0U;
  uint8_t mode1Count = 0U;
  uint8_t mode2Count = 0U;
  uint32_t hash32 = 0U;
};

uint32_t fnv1a32Bytes(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  if (data == nullptr) {
    return hash;
  }
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint32_t>(data[i]);
    hash *= 16777619UL;
  }
  return hash;
}

bool countStepCallback(const BleCsSubeventStep* step, void* userData) {
  if (step == nullptr || userData == nullptr) {
    return false;
  }
  ParsedCompletedResultSummary* summary =
      static_cast<ParsedCompletedResultSummary*>(userData);
  ++summary->stepCount;
  if (step->mode == kBleCsMainMode1) {
    ++summary->mode1Count;
  } else if (step->mode == kBleCsMainMode2) {
    ++summary->mode2Count;
  }
  return true;
}

bool parseCompletedResult(const VprBleCsCompletedResultPayload& raw,
                          ParsedCompletedResultSummary* summary) {
  if (summary == nullptr || !raw.valid || raw.payloadLen == 0U) {
    return false;
  }
  *summary = ParsedCompletedResultSummary{};
  if (!BleChannelSoundingRadio::parseHciSubeventResultEvent(raw.payload, raw.payloadLen,
                                                            &summary->result)) {
    return false;
  }
  BleChannelSoundingRadio::parseSubeventStepData(summary->result.stepData,
                                                 summary->result.stepDataLen,
                                                 countStepCallback, summary);
  summary->hash32 = fnv1a32Bytes(raw.payload, raw.payloadLen);
  summary->valid = true;
  return true;
}

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
  ParsedCompletedResultSummary localRaw{};
  ParsedCompletedResultSummary peerRaw{};
  BleCsEstimate rawEstimate{};
  const bool serviceOk =
      g_service.runFreshBleConnectedCsWorkflow(config, kDisconnectReason, &run,
                                               true, kEventTimeoutMs) &&
      g_service.readCapabilities(&caps);
  const bool rawLocalOk =
      serviceOk && parseCompletedResult(run.completedLocalResult, &localRaw);
  const bool rawPeerOk =
      serviceOk && parseCompletedResult(run.completedPeerResult, &peerRaw);
  const bool rawEstimateOk =
      rawLocalOk && rawPeerOk &&
      BleChannelSoundingRadio::estimateDistanceFromSubeventResults(localRaw.result,
                                                                   peerRaw.result, true,
                                                                   &rawEstimate) &&
      rawEstimate.valid;
  const bool ok =
      serviceOk &&
      (caps.opMask & VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsLinkConfigure) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowConfigure) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowReadState) != 0U &&
      (caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowReadCompletedResult) != 0U &&
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
      run.completedLocalResult.valid &&
      !run.completedLocalResult.peerSide &&
      run.completedPeerResult.valid &&
      run.completedPeerResult.peerSide &&
      run.completedLocalResult.configId == kConfigId &&
      run.completedPeerResult.configId == kConfigId &&
      run.completedLocalResult.procedureCounter == kMaxProcedureCount &&
      run.completedPeerResult.procedureCounter == kMaxProcedureCount &&
      rawLocalOk &&
      rawPeerOk &&
      localRaw.valid &&
      peerRaw.valid &&
      localRaw.result.header.configId == kConfigId &&
      peerRaw.result.header.configId == kConfigId &&
      localRaw.result.header.procedureCounter == kMaxProcedureCount &&
      peerRaw.result.header.procedureCounter == kMaxProcedureCount &&
      localRaw.stepCount == run.completedWorkflow.completedLocalStepCount &&
      peerRaw.stepCount == run.completedWorkflow.completedPeerStepCount &&
      localRaw.mode1Count == run.completedWorkflow.completedLocalMode1Count &&
      peerRaw.mode1Count == run.completedWorkflow.completedPeerMode1Count &&
      localRaw.mode2Count == run.completedWorkflow.completedLocalMode2Count &&
      peerRaw.mode2Count == run.completedWorkflow.completedPeerMode2Count &&
      localRaw.hash32 == run.completedLocalResult.hash32 &&
      peerRaw.hash32 == run.completedPeerResult.hash32 &&
      localRaw.hash32 == run.completedWorkflow.completedLocalHash32 &&
      peerRaw.hash32 == run.completedWorkflow.completedPeerHash32 &&
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
  Serial.print(F(" raw="));
  Serial.print(rawLocalOk ? 1 : 0);
  Serial.print('/');
  Serial.print(rawPeerOk ? 1 : 0);
  Serial.print(F(" raw_steps="));
  Serial.print(localRaw.stepCount);
  Serial.print('/');
  Serial.print(peerRaw.stepCount);
  Serial.print(F(" raw_hash=0x"));
  Serial.print(localRaw.hash32, HEX);
  Serial.print(F("/0x"));
  Serial.print(peerRaw.hash32, HEX);
  Serial.print(F(" raw_est_m="));
  if (rawEstimateOk) {
    Serial.print(rawEstimate.distanceMeters, 4);
  } else {
    Serial.print(F("nan"));
  }
  Serial.print(F(" lat_ms="));
  Serial.print(run.timing.beginTotalMs);
  Serial.print('/');
  Serial.print(run.timing.waitCompletedMs);
  Serial.print('/');
  Serial.print(run.timing.disconnectMs);
  Serial.print('/');
  Serial.print(run.timing.totalRunMs);
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
  Serial.println(F("latency_fields_ms=begin/complete/disconnect/total"));
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
