/*
 * BleChannelSoundingVprLinkedInitiator
 *
 * Starts a nominal VPR-owned BLE connected-link snapshot, imports that link
 * into the dedicated VPR CS image, and runs one controller-backed CS workflow
 * against BleChannelSoundingReflector on a second board.
 *
 * This example is intentionally simpler than the VPR proof probes:
 *   - no SWD summary block
 *   - no probe harness
 *   - no proof-only event packing
 *
 * It is still a bring-up example. The printed `nominal_dist_m` value comes
 * from the current nominal synthetic CS regression path and is not a physical
 * tape-measure claim.
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"
#include "nrf54l15_vpr.h"

#ifdef ledOn
#undef ledOn
#endif
#ifdef ledOff
#undef ledOff
#endif

using namespace xiao_nrf54l15;

namespace {

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
static constexpr uint16_t kConnHandle = 0x0041U;
static constexpr uint8_t kRolePeripheral = 1U;
static constexpr uint16_t kIntervalUnits = 24U;
static constexpr uint16_t kLatency = 0U;
static constexpr uint16_t kSupervisionTimeout = 400U;
static constexpr uint8_t kPhy1M = 1U;
static constexpr uint32_t kEventTimeoutMs = 2500U;
static constexpr uint32_t kRunIntervalMs = 4000U;

VprSharedTransportStream gSourceTransport;
VprControllerServiceHost gSourceService(&gSourceTransport);
BleCsControllerVprHost gCsHost;
BleCsControllerVprHostConfig gHostConfig{};
uint32_t gLastRunMs = 0U;
uint32_t gRunCount = 0U;

void ledOn() {
  (void)Gpio::write(kPinUserLed, false);
}

void ledOff() {
  (void)Gpio::write(kPinUserLed, true);
}

void pulse(uint8_t count, uint16_t onMs = 25U, uint16_t offMs = 45U) {
  for (uint8_t i = 0U; i < count; ++i) {
    ledOn();
    delay(onMs);
    ledOff();
    if ((i + 1U) < count) {
      delay(offMs);
    }
  }
}

[[noreturn]] void failStage(uint8_t stage) {
  while (true) {
    pulse(stage, 90U, 120U);
    delay(900U);
  }
}

void configureBoard() {
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput,
                        GpioPull::kDisabled);
  ledOff();

  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (static_cast<uint32_t>(millis() - start) < 1500U)) {
  }

  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  if (!BoardControl::enableRfPath(kAntennaPath)) {
    failStage(1);
  }
}

bool runMeasurement() {
  VprControllerServiceCapabilities caps{};
  VprBleConnectionState sourceState{};
  VprBleConnectionSharedState sourceShared{};
  VprBleConnectionEvent connectEvent{};
  VprBleConnectionSharedState importedState{};
  BleCsControllerVprWorkflowStartStatus workflowStatus{};
  bool bootLinked = false;
  uint16_t bootHandle = 0U;
  uint8_t pumpCount = 0U;
  uint8_t pollCount = 0U;
  uint32_t sourceBeginMs = 0U;
  uint32_t sourceConnectMs = 0U;
  uint32_t handoffBootMs = 0U;
  uint32_t workflowStartMs = 0U;
  uint32_t completeMs = 0U;
  const uint32_t totalStartMs = millis();

  gCsHost.reset();

  uint32_t stepStartMs = millis();
  bool ok = gSourceService.bootDefaultService(true) &&
            gSourceTransport.isRunning() &&
            gSourceService.readCapabilities(&caps);
  sourceBeginMs = millis() - stepStartMs;

  stepStartMs = millis();
  ok = ok && gSourceService.configureBleConnection(
                 kConnHandle, kRolePeripheral, true, kIntervalUnits, kLatency,
                 kSupervisionTimeout, kPhy1M, kPhy1M, &sourceState) &&
       gSourceService.waitBleConnectionEvent(&connectEvent, kEventTimeoutMs) &&
       gSourceService.waitBleConnectionSharedState(
           true, 1U, &sourceShared, kEventTimeoutMs);
  sourceConnectMs = millis() - stepStartMs;

  stepStartMs = millis();
  ok = ok && gCsHost.beginFreshHostFromBleConnection(
                 gSourceService, gHostConfig, 16U, &pumpCount, &importedState,
                 kEventTimeoutMs);
  handoffBootMs = millis() - stepStartMs;
  bootLinked = gCsHost.vprState().linkSessionOpen;
  bootHandle = gCsHost.vprState().linkConnHandle;
  ok = ok && bootLinked && bootHandle == kConnHandle;

  stepStartMs = millis();
  ok = ok && gCsHost.directStartConfiguredWorkflow(true, &workflowStatus);
  workflowStartMs = millis() - stepStartMs;

  stepStartMs = millis();
  ok = ok &&
       gCsHost.pollUntilCompletedProcedureResult(1U, 1U, 1U, 24U, &pollCount);
  completeMs = millis() - stepStartMs;

  ok = ok &&
       (caps.opMask &
        VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
       sourceState.connected && sourceState.connHandle == kConnHandle &&
       connectEvent.flags == 0x01U && connectEvent.connHandle == kConnHandle &&
       sourceShared.connected && sourceShared.connHandle == kConnHandle &&
       importedState.connected && importedState.connHandle == kConnHandle &&
       workflowStatus.readRemoteSupportedCapabilities == 0U &&
       workflowStatus.setDefaultSettings == 0U &&
       workflowStatus.createConfig == 0U &&
       workflowStatus.securityEnable == 0U &&
       workflowStatus.setProcedureParameters == 0U &&
       workflowStatus.procedureEnable == 0U &&
       gCsHost.ready() && !gCsHost.failed() && gCsHost.estimateValid() &&
       gCsHost.vprState().linkSessionOpen &&
       gCsHost.vprState().linkConnHandle == kConnHandle &&
       gCsHost.sessionState().completedProcedureCounter >= 1U &&
       gCsHost.hostState().localSubeventResults >= 1U &&
       gCsHost.hostState().peerSubeventResults >= 1U;

  ++gRunCount;
  Serial.print(F("run="));
  Serial.print(gRunCount);
  Serial.print(F(" ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" svc="));
  Serial.print(caps.serviceVersionMajor);
  Serial.print('.');
  Serial.print(caps.serviceVersionMinor);
  Serial.print(F(" src="));
  Serial.print(sourceShared.connected ? 1 : 0);
  Serial.print(F("@0x"));
  Serial.print(sourceShared.connHandle, HEX);
  Serial.print(F("#"));
  Serial.print(sourceShared.eventCount);
  Serial.print(F(" boot="));
  Serial.print(bootLinked ? 1 : 0);
  Serial.print(F("@0x"));
  Serial.print(bootHandle, HEX);
  Serial.print(F(" import="));
  Serial.print(importedState.connected ? 1 : 0);
  Serial.print(F("@0x"));
  Serial.print(importedState.connHandle, HEX);
  Serial.print(F("#"));
  Serial.print(importedState.eventCount);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" status="));
  Serial.print(workflowStatus.readRemoteSupportedCapabilities, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.setDefaultSettings, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.createConfig, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.securityEnable, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.setProcedureParameters, HEX);
  Serial.print('/');
  Serial.print(workflowStatus.procedureEnable, HEX);
  Serial.print(F(" proc="));
  Serial.print(gCsHost.sessionState().completedProcedureCounter);
  Serial.print(F(" local_evt="));
  Serial.print(gCsHost.hostState().localSubeventResults);
  Serial.print(F(" peer_evt="));
  Serial.print(gCsHost.hostState().peerSubeventResults);
  Serial.print(F(" lat_ms="));
  Serial.print(sourceBeginMs);
  Serial.print('/');
  Serial.print(sourceConnectMs);
  Serial.print('/');
  Serial.print(handoffBootMs);
  Serial.print('/');
  Serial.print(workflowStartMs);
  Serial.print('/');
  Serial.print(completeMs);
  Serial.print('/');
  Serial.print(millis() - totalStartMs);
  Serial.print(F(" nominal_dist_m="));
  if (gCsHost.estimateValid()) {
    Serial.println(gCsHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }

  if (ok) {
    pulse(1U, 25U, 0U);
  } else {
    pulse(3U, 25U, 35U);
  }
  return ok;
}

}  // namespace

void setup() {
  configureBoard();
  BleCsControllerVprHost::fillDemoConfig(&gHostConfig);
  gHostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;

  Serial.println(F("BleChannelSoundingVprLinkedInitiator start"));
  Serial.println(F("pair_with=CoreBleChannelSoundingReflector"));
  Serial.println(F("link_source=generic_vpr_ble_connection_state"));
  Serial.println(F("note=nominal_synthetic_regression_output_only"));
  Serial.println(F("latency_fields_ms=source_boot/source_connect/handoff_boot/start/complete/total"));

  runMeasurement();
  gLastRunMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if ((now - gLastRunMs) >= kRunIntervalMs) {
    gLastRunMs = now;
    runMeasurement();
  }
}
