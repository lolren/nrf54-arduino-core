/*
 * InterruptPwmApiProbe
 *
 * Verifies core-level attachInterrupt() and analogWrite() PWM behavior.
 *
 * Wiring used on XIAO nRF54L15:
 * - Button: built-in user button (active-low)
 * - PWM output: D6
 * - LED: built-in LED (active-low)
 */

static volatile uint32_t g_button_edges = 0;
static volatile uint8_t g_button_event = 0;

static uint16_t g_duty = 0;
static int16_t g_step = 64;
static bool g_pwm_enabled = true;

void onButtonFalling()
{
  ++g_button_edges;
  g_button_event = 1;
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Off (active-low LED)

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_D6, OUTPUT);

  analogWriteResolution(12);
  analogWrite(PIN_D6, 0);

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonFalling, FALLING);

  Serial.println("InterruptPwmApiProbe start");
  Serial.println("Press button to toggle PWM activity on D6");
}

void loop()
{
  static uint32_t last_update_ms = 0;
  const uint32_t now = millis();

  if (g_pwm_enabled && (now - last_update_ms) >= 4U) {
    last_update_ms = now;

    int32_t next = (int32_t)g_duty + g_step;
    if (next >= 4095) {
      next = 4095;
      g_step = -g_step;
    } else if (next <= 0) {
      next = 0;
      g_step = -g_step;
    }

    g_duty = (uint16_t)next;
    analogWrite(PIN_D6, g_duty);
  }

  if (g_button_event != 0U) {
    noInterrupts();
    g_button_event = 0U;
    const uint32_t edges = g_button_edges;
    interrupts();

    g_pwm_enabled = !g_pwm_enabled;
    if (!g_pwm_enabled) {
      analogWrite(PIN_D6, 0);
      digitalWrite(LED_BUILTIN, LOW);
      Serial.print("Button edge #");
      Serial.print(edges);
      Serial.println(" -> PWM disabled (static low)");
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      Serial.print("Button edge #");
      Serial.print(edges);
      Serial.println(" -> PWM enabled");
    }
  }

  delay(1);
}
