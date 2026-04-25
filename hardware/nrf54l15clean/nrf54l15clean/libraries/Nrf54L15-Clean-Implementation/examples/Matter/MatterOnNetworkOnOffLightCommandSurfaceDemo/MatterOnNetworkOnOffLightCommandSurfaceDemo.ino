#include <Arduino.h>
#include <matter_onnetwork_onoff_light.h>
#include <matter_platform_nrf54l15.h>

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child/Router + UDP) before building this example."
#endif

#if !defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) || \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE == 0)
#error "Enable Tools > Matter Foundation > Experimental Compile Target (On-Network On/Off Light) before building this example."
#endif

#include <stdlib.h>
#include <string.h>

namespace {

constexpr char kThreadPassPhrase[] = "THREAD54";
constexpr char kThreadNetworkName[] = "Nrf54Matter";
constexpr uint8_t kThreadExtPanId[OT_EXT_PAN_ID_SIZE] = {
    0x31, 0x54, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6,
};
constexpr size_t kLineBufferSize = 48U;

xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode g_node;
char g_lineBuffer[kLineBufferSize] = {0};
size_t g_lineLength = 0U;
uint32_t g_lastHeartbeatMs = 0U;

void applyIndicator() {
#if defined(LED_BUILTIN)
  if (g_node.light().identifying()) {
    if (((millis() / 150UL) & 0x1UL) == 0U) {
      ledOn(LED_BUILTIN);
    } else {
      ledOff(LED_BUILTIN);
    }
    return;
  }

  if (g_node.light().on()) {
    ledOn(LED_BUILTIN);
  } else {
    ledOff(LED_BUILTIN);
  }
#endif
}

void printValue(const char* label,
                const xiao_nrf54l15::MatterAttributeValue& value) {
  Serial.print("matter_cmd_demo ");
  Serial.print(label);
  Serial.print('=');
  switch (value.type) {
    case xiao_nrf54l15::MatterAttributeValueType::kBool:
      Serial.println(value.boolValue ? 1 : 0);
      break;
    case xiao_nrf54l15::MatterAttributeValueType::kUint16:
      Serial.println(value.uint16Value);
      break;
    default:
      Serial.println("n/a");
      break;
  }
}

void printCodes() {
  char manualCode[xiao_nrf54l15::kMatterManualPairingLongCodeLength + 1U] = {
      0};
  char qrCode[xiao_nrf54l15::kMatterQrCodeTextLength + 1U] = {0};
  const bool manualOk = g_node.manualPairingCode(manualCode, sizeof(manualCode));
  const bool qrOk = g_node.qrCode(qrCode, sizeof(qrCode));
  Serial.print("matter_cmd_demo manual=");
  Serial.println(manualOk ? manualCode : "error");
  Serial.print("matter_cmd_demo qr=");
  Serial.println(qrOk ? qrCode : "error");
}

void printBundle() {
  xiao_nrf54l15::MatterOnNetworkCommissioningBundle bundle;
  if (!g_node.buildCommissioningBundle(&bundle)) {
    Serial.println("matter_cmd_demo bundle=error");
    return;
  }

  Serial.print("matter_cmd_demo bundle_ready=");
  Serial.println(bundle.ready ? 1 : 0);
  Serial.print("matter_cmd_demo bundle_window=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode::
                     commissioningWindowStateName(bundle.commissioningWindowState));
  Serial.print("matter_cmd_demo bundle_window_seconds=");
  Serial.println(bundle.commissioningWindowSecondsRemaining);
  Serial.print("matter_cmd_demo bundle_dataset_source=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode::
                     datasetSourceName(bundle.datasetSource));
  Serial.print("matter_cmd_demo bundle_manual=");
  Serial.println(bundle.manualCodeReady ? bundle.manualCode : "error");
  Serial.print("matter_cmd_demo bundle_qr=");
  Serial.println(bundle.qrCodeReady ? bundle.qrCode : "error");
  Serial.print("matter_cmd_demo bundle_ot_tlv_hex=");
  Serial.println(bundle.openThreadDatasetReady ? bundle.openThreadDatasetHex
                                               : "error");
#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  Serial.print("matter_cmd_demo bundle_chip_tlv_hex=");
  Serial.println(bundle.matterThreadDatasetReady ? bundle.matterThreadDatasetHex
                                                 : "error");
#endif
}

void printState(const char* reason) {
  xiao_nrf54l15::MatterOnNetworkOnOffLightStatus status;
  xiao_nrf54l15::MatterAttributeValue value;
  xiao_nrf54l15::MatterInteractionStatus interactionStatus =
      xiao_nrf54l15::MatterInteractionStatus::kInvalidState;

  if (!g_node.snapshot(&status)) {
    return;
  }

  Serial.print("matter_cmd_demo reason=");
  Serial.println(reason);
  Serial.print("matter_cmd_demo thread_role=");
  Serial.println(g_node.thread().roleName());
  Serial.print("matter_cmd_demo ready=");
  Serial.println(status.readyForOnNetworkCommissioning ? 1 : 0);
  Serial.print("matter_cmd_demo dataset_source=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode::
                     datasetSourceName(status.datasetSource));
  Serial.print("matter_cmd_demo window=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode::
                     commissioningWindowStateName(
                         status.commissioningWindowState));
  Serial.print("matter_cmd_demo window_seconds=");
  Serial.println(status.commissioningWindowSecondsRemaining);
  Serial.print("matter_cmd_demo build=");
  Serial.println(xiao_nrf54l15::matterFoundationBuildMode());

  xiao_nrf54l15::MatterAttributePath path;
  path.clusterId = xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOnOffClusterId;
  path.attributeId =
      xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOnOffAttributeId;
  if (g_node.endpoint().readAttribute(path, &value, &interactionStatus)) {
    printValue("onoff", value);
  } else {
    Serial.print("matter_cmd_demo onoff_error=");
    Serial.println(
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::statusName(
            interactionStatus));
  }

  path.attributeId =
      xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::
          kGlobalSceneControlAttributeId;
  if (g_node.endpoint().readAttribute(path, &value, &interactionStatus)) {
    printValue("global_scene", value);
  } else {
    Serial.print("matter_cmd_demo global_scene_error=");
    Serial.println(
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::statusName(
            interactionStatus));
  }

  path.clusterId =
      xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kIdentifyClusterId;
  path.attributeId =
      xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kIdentifyTimeAttributeId;
  if (g_node.endpoint().readAttribute(path, &value, &interactionStatus)) {
    printValue("identify_time", value);
  } else {
    Serial.print("matter_cmd_demo identify_time_error=");
    Serial.println(
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::statusName(
            interactionStatus));
  }
}

void printHelp() {
  Serial.println("matter_cmd_demo commands:");
  Serial.println("matter_cmd_demo   state");
  Serial.println("matter_cmd_demo   on");
  Serial.println("matter_cmd_demo   off");
  Serial.println("matter_cmd_demo   toggle");
  Serial.println("matter_cmd_demo   identify <seconds>");
  Serial.println("matter_cmd_demo   stop-identify");
  Serial.println("matter_cmd_demo   open-window <seconds>");
  Serial.println("matter_cmd_demo   close-window");
  Serial.println("matter_cmd_demo   bundle");
  Serial.println("matter_cmd_demo   manual");
  Serial.println("matter_cmd_demo   qr");
  Serial.println("matter_cmd_demo   help");
}

void runCommand(uint32_t clusterId, uint32_t commandId,
                bool hasUint16Value = false, uint16_t uint16Value = 0U) {
  xiao_nrf54l15::MatterCommandRequest request;
  request.path.clusterId = clusterId;
  request.path.commandId = commandId;
  request.hasUint16Value = hasUint16Value;
  request.uint16Value = uint16Value;

  xiao_nrf54l15::MatterCommandResult result;
  const bool ok = g_node.endpoint().invokeCommand(request, &result);
  Serial.print("matter_cmd_demo command=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::commandName(
      clusterId, commandId));
  Serial.print("matter_cmd_demo status=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::statusName(
      result.status));
  Serial.print("matter_cmd_demo accepted=");
  Serial.println(ok ? 1 : 0);
  Serial.print("matter_cmd_demo state_changed=");
  Serial.println(result.stateChanged ? 1 : 0);
  printState("command");
}

void handleLine(char* line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (strcmp(line, "help") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "state") == 0) {
    printState("state");
    return;
  }
  if (strcmp(line, "manual") == 0) {
    printCodes();
    return;
  }
  if (strcmp(line, "qr") == 0) {
    printCodes();
    return;
  }
  if (strcmp(line, "bundle") == 0) {
    printBundle();
    return;
  }
  if (strcmp(line, "close-window") == 0) {
    g_node.closeCommissioningWindow();
    printState("close-window");
    return;
  }
  if (strcmp(line, "on") == 0) {
    runCommand(xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOnOffClusterId,
               xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOnCommandId);
    return;
  }
  if (strcmp(line, "off") == 0) {
    runCommand(xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOnOffClusterId,
               xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOffCommandId);
    return;
  }
  if (strcmp(line, "toggle") == 0) {
    runCommand(
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kOnOffClusterId,
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kToggleCommandId);
    return;
  }
  if (strcmp(line, "stop-identify") == 0) {
    runCommand(
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kIdentifyClusterId,
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kIdentifyCommandId, true,
        0U);
    return;
  }
  if (strncmp(line, "identify ", 9) == 0) {
    const long seconds = strtol(line + 9, nullptr, 10);
    if (seconds < 0 || seconds > 65535L) {
      Serial.println("matter_cmd_demo identify range is 0..65535");
      return;
    }
    runCommand(
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kIdentifyClusterId,
        xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint::kIdentifyCommandId, true,
        static_cast<uint16_t>(seconds));
    return;
  }
  if (strncmp(line, "open-window ", 12) == 0) {
    const long seconds = strtol(line + 12, nullptr, 10);
    if (seconds <= 0 || seconds > 65535L) {
      Serial.println("matter_cmd_demo open-window range is 1..65535");
      return;
    }
    Serial.print("matter_cmd_demo window_open=");
    Serial.println(
        g_node.openCommissioningWindow(static_cast<uint16_t>(seconds)) ? 1 : 0);
    printState("open-window");
    return;
  }

  Serial.print("matter_cmd_demo unknown=");
  Serial.println(line);
  printHelp();
}

void pollSerial() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      return;
    }

    const char c = static_cast<char>(raw);
    if (c == '\r' || c == '\n') {
      g_lineBuffer[g_lineLength] = '\0';
      handleLine(g_lineBuffer);
      g_lineLength = 0U;
      memset(g_lineBuffer, 0, sizeof(g_lineBuffer));
      continue;
    }

    if (g_lineLength + 1U < sizeof(g_lineBuffer)) {
      g_lineBuffer[g_lineLength++] = c;
      g_lineBuffer[g_lineLength] = '\0';
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
  ledOff(LED_BUILTIN);
#endif

  xiao_nrf54l15::MatterOnNetworkOnOffLightConfig config;
  config.threadPassPhrase = kThreadPassPhrase;
  config.threadNetworkName = kThreadNetworkName;
  config.threadExtPanId = kThreadExtPanId;
  config.restorePersistentState = true;
  config.wipeThreadSettings = false;
  config.autoStartThread = true;

  const bool beginOk = g_node.begin(&config);
  Serial.print("matter_cmd_demo begin=");
  Serial.println(beginOk ? 1 : 0);
  printCodes();
  printState("boot");
  printHelp();
}

void loop() {
  g_node.process();
  applyIndicator();
  pollSerial();

  if ((millis() - g_lastHeartbeatMs) >= 5000UL) {
    g_lastHeartbeatMs = millis();
    printState("heartbeat");
  }
}
