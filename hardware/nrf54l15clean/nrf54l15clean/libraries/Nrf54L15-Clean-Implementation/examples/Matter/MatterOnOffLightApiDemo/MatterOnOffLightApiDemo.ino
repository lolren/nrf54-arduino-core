#include <Arduino.h>
#include <matter_onoff_light.h>
#include <matter_platform_nrf54l15.h>

#if !defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) || \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE == 0)
#error "Enable Tools > Matter Foundation > Experimental Compile Target (On-Network On/Off Light) before building this example."
#endif

namespace {

xiao_nrf54l15::Nrf54MatterOnOffLightDevice g_light;
bool g_buttonDown = false;
bool g_buttonHoldHandled = false;
uint32_t g_buttonPressedMs = 0U;
uint32_t g_lastStatePrintMs = 0U;

void applyIndicator() {
#if defined(LED_BUILTIN)
  if (g_light.identifying()) {
    if (((millis() / 150UL) & 0x1UL) == 0U) {
      ledOn(LED_BUILTIN);
    } else {
      ledOff(LED_BUILTIN);
    }
    return;
  }

  if (g_light.on()) {
    ledOn(LED_BUILTIN);
  } else {
    ledOff(LED_BUILTIN);
  }
#endif
}

void printState(const char* reason) {
  xiao_nrf54l15::MatterOnOffLightDeviceState state;
  if (!g_light.snapshot(&state)) {
    return;
  }

  Serial.print("matter_onoff_demo reason=");
  Serial.println(reason);
  Serial.print("matter_onoff_demo build=");
  Serial.println(xiao_nrf54l15::matterFoundationBuildMode());
  Serial.print("matter_onoff_demo target=");
  Serial.println(xiao_nrf54l15::matterFoundationTargetName());
  Serial.print("matter_onoff_demo on=");
  Serial.println(state.on ? 1 : 0);
  Serial.print("matter_onoff_demo identifying=");
  Serial.println(state.identifying ? 1 : 0);
  Serial.print("matter_onoff_demo identify_seconds=");
  Serial.println(state.identifyTimeSeconds);
  Serial.print("matter_onoff_demo startup=");
  Serial.println(
      xiao_nrf54l15::Nrf54MatterOnOffLightDevice::startUpBehaviorName(
          state.startUpBehavior));
  Serial.print("matter_onoff_demo storage=");
  Serial.println(state.persistentStorageOpen ? 1 : 0);
}

void onLightChanged(void*, bool on) {
  Serial.print("matter_onoff_demo callback_on=");
  Serial.println(on ? 1 : 0);
}

void onIdentifyChanged(void*, bool active, uint16_t remainingSeconds) {
  Serial.print("matter_onoff_demo callback_identify=");
  Serial.print(active ? 1 : 0);
  Serial.print(" remaining=");
  Serial.println(remainingSeconds);
}

void cycleStartUpBehavior() {
  using xiao_nrf54l15::MatterOnOffLightStartUpBehavior;
  MatterOnOffLightStartUpBehavior nextBehavior =
      MatterOnOffLightStartUpBehavior::kRestorePrevious;
  switch (g_light.startUpBehavior()) {
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

  if (g_light.setStartUpBehavior(nextBehavior, true)) {
    printState("startup-cycle");
    g_light.setIdentifyTimeSeconds(1U);
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

  g_light.setOnChangeCallback(onLightChanged, nullptr);
  g_light.setIdentifyCallback(onIdentifyChanged, nullptr);
  const bool beginOk = g_light.begin();
  Serial.print("matter_onoff_demo begin=");
  Serial.println(beginOk ? 1 : 0);
  printState("boot");
  Serial.println(
      "matter_onoff_demo short-press button to toggle; hold 1.5s to cycle startup behavior.");
}

void loop() {
  g_light.process();
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
      (void)g_light.toggle(true);
      (void)g_light.setIdentifyTimeSeconds(3U);
      printState("button-toggle");
    }
  }
#endif

  if ((millis() - g_lastStatePrintMs) >= 4000UL) {
    g_lastStatePrintMs = millis();
    printState("heartbeat");
  }
}
