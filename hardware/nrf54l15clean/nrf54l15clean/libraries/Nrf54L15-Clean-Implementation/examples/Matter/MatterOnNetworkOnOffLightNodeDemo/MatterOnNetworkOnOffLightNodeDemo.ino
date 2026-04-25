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

namespace {

constexpr char kThreadPassPhrase[] = "THREAD54";
constexpr char kThreadNetworkName[] = "Nrf54Matter";
constexpr uint8_t kThreadExtPanId[OT_EXT_PAN_ID_SIZE] = {
    0x31, 0x54, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6,
};

xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode g_node;
bool g_buttonDown = false;
bool g_buttonHoldHandled = false;
uint32_t g_buttonPressedMs = 0U;
uint32_t g_lastStatusPrintMs = 0U;
bool g_lastReady = false;
xiao_nrf54l15::Nrf54ThreadExperimental::Role g_lastRole =
    xiao_nrf54l15::Nrf54ThreadExperimental::Role::kUnknown;

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

void printCodes() {
  char manualCode[xiao_nrf54l15::kMatterManualPairingLongCodeLength + 1U] = {
      0};
  char qrCode[xiao_nrf54l15::kMatterQrCodeTextLength + 1U] = {0};
  const bool manualOk = g_node.manualPairingCode(manualCode, sizeof(manualCode));
  const bool qrOk = g_node.qrCode(qrCode, sizeof(qrCode));

  Serial.print("matter_node_demo manual_ok=");
  Serial.println(manualOk ? 1 : 0);
  Serial.print("matter_node_demo manual=");
  Serial.println(manualOk ? manualCode : "error");
  Serial.print("matter_node_demo qr_ok=");
  Serial.println(qrOk ? 1 : 0);
  Serial.print("matter_node_demo qr=");
  Serial.println(qrOk ? qrCode : "error");
}

void printStatus(const char* reason) {
  xiao_nrf54l15::MatterOnNetworkOnOffLightStatus status;
  if (!g_node.snapshot(&status)) {
    return;
  }

  Serial.print("matter_node_demo reason=");
  Serial.println(reason);
  Serial.print("matter_node_demo build=");
  Serial.println(xiao_nrf54l15::matterFoundationBuildMode());
  Serial.print("matter_node_demo target=");
  Serial.println(xiao_nrf54l15::matterFoundationTargetName());
  Serial.print("matter_node_demo dataset_source=");
  Serial.println(xiao_nrf54l15::Nrf54MatterOnNetworkOnOffLightNode::
                     datasetSourceName(status.datasetSource));
  Serial.print("matter_node_demo thread_started=");
  Serial.println(status.threadStarted ? 1 : 0);
  Serial.print("matter_node_demo thread_attached=");
  Serial.println(status.threadAttached ? 1 : 0);
  Serial.print("matter_node_demo thread_role=");
  Serial.println(g_node.thread().roleName());
  Serial.print("matter_node_demo rloc16=0x");
  Serial.println(status.rloc16, HEX);
  Serial.print("matter_node_demo dataset_exportable=");
  Serial.println(status.threadDatasetExportable ? 1 : 0);
  Serial.print("matter_node_demo ready=");
  Serial.println(status.readyForOnNetworkCommissioning ? 1 : 0);
  Serial.print("matter_node_demo on=");
  Serial.println(status.light.on ? 1 : 0);
  Serial.print("matter_node_demo identifying=");
  Serial.println(status.light.identifying ? 1 : 0);
  Serial.print("matter_node_demo identify_seconds=");
  Serial.println(status.light.identifyTimeSeconds);
  Serial.print("matter_node_demo startup=");
  Serial.println(
      xiao_nrf54l15::Nrf54MatterOnOffLightDevice::startUpBehaviorName(
          status.light.startUpBehavior));
}

void cycleStartUpBehavior() {
  using xiao_nrf54l15::MatterOnOffLightStartUpBehavior;
  MatterOnOffLightStartUpBehavior nextBehavior =
      MatterOnOffLightStartUpBehavior::kRestorePrevious;
  switch (g_node.light().startUpBehavior()) {
    case MatterOnOffLightStartUpBehavior::kRestorePrevious:
      nextBehavior = MatterOnOffLightStartUpBehavior::kForceOff;
      break;
    case MatterOnOffLightStartUpBehavior::kForceOff:
      nextBehavior = MatterOnOffLightStartUpBehavior::kForceOn;
      break;
    case MatterOnOffLightStartUpBehavior::kForceOn:
      nextBehavior = MatterOnOffLightStartUpBehavior::kTogglePrevious;
      break;
    case MatterOnOffLightStartUpBehavior::kTogglePrevious:
      nextBehavior = MatterOnOffLightStartUpBehavior::kRestorePrevious;
      break;
    default:
      nextBehavior = MatterOnOffLightStartUpBehavior::kRestorePrevious;
      break;
  }

  if (g_node.light().setStartUpBehavior(nextBehavior, true)) {
    (void)g_node.light().setIdentifyTimeSeconds(1U);
    printStatus("startup-cycle");
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
#if defined(PIN_BUTTON)
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif

  xiao_nrf54l15::MatterOnNetworkOnOffLightConfig config;
  config.threadPassPhrase = kThreadPassPhrase;
  config.threadNetworkName = kThreadNetworkName;
  config.threadExtPanId = kThreadExtPanId;
  config.restorePersistentState = true;
  config.wipeThreadSettings = false;
  config.autoStartThread = true;

  const bool beginOk = g_node.begin(&config);
  Serial.print("matter_node_demo begin=");
  Serial.println(beginOk ? 1 : 0);
  printCodes();
  printStatus("boot");
  Serial.println(
      "matter_node_demo short-press button to toggle; hold 1.5s to cycle startup behavior.");
}

void loop() {
  g_node.process();
  applyIndicator();

#if defined(PIN_BUTTON)
  const bool buttonPressed = digitalRead(PIN_BUTTON) == LOW;
  if (buttonPressed && !g_buttonDown) {
    g_buttonDown = true;
    g_buttonHoldHandled = false;
    g_buttonPressedMs = millis();
  }

  if (buttonPressed && g_buttonDown && !g_buttonHoldHandled &&
      (millis() - g_buttonPressedMs) >= 1500UL) {
    g_buttonHoldHandled = true;
    cycleStartUpBehavior();
  }

  if (!buttonPressed && g_buttonDown) {
    g_buttonDown = false;
    if (!g_buttonHoldHandled) {
      (void)g_node.light().toggle(true);
      (void)g_node.light().setIdentifyTimeSeconds(3U);
      printStatus("button-toggle");
    }
  }
#endif

  const bool ready = g_node.readyForOnNetworkCommissioning();
  const xiao_nrf54l15::Nrf54ThreadExperimental::Role currentRole =
      g_node.thread().role();
  if (ready != g_lastReady || currentRole != g_lastRole) {
    g_lastReady = ready;
    g_lastRole = currentRole;
    printStatus("state-change");
  }

  if ((millis() - g_lastStatusPrintMs) >= 4000UL) {
    g_lastStatusPrintMs = millis();
    printStatus("heartbeat");
  }
}
