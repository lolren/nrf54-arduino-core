# nPM1300 power management (XIAO nRF54LM20A)

The interrupt pins, watchdog, wake-button debounce and reset-cause APIs below
are available on `main` after `1.0.22`, not in the `1.0.22` release.

The XIAO nRF54LM20A has a Nordic nPM1300 PMIC. It provides the 3.3 V system rail, the battery charger, the IMU/mic rail, the on-board RGB LED driver and the SHPHLD button input, and it can put the board into very low power Ship or Hibernate modes.

This page describes how the core talks to it and how to use the button, interrupt, watchdog, sleep and reset-cause features. Register details come from the nPM1300 Product Specification v1.3.

## How the core talks to the PMIC

- I2C address `0x6B`, on TWIM23 with SCL on P1.17 and SDA on P1.18. These are internal pins, not the header `Wire` pins.
- Registers use a two-byte address, `{base, offset}`. The helpers take them as separate arguments:

```cpp
bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t *value);
bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value);
bool npm1300_update_reg(uint8_t base, uint8_t offset, uint8_t mask, uint8_t value);
// plus npm1300_read_burst / npm1300_write_burst
```

- Every helper returns `true` on success and initialises the bus on first use. The driver opens and closes the I2C bus around each transaction, so call it from `loop()` and not from an interrupt handler.
- Register bases are defined in `npm1300.h` (`NPM1300_BASE_MAIN`, `_SHIP`, `_GPIO`, `_TIMER`, `_ERRLOG` and so on).

## The SHPHLD button

SHPHLD is the nPM1300's button input. The pin is pulled up internally and the button connects it to ground, so a pressed button reads **low**.

`SHPHLDSTATUS` (SHIP base `0x0B`, offset `0x05`), bit 0, is the pin level:

```cpp
uint8_t s;
npm1300_read_reg(NPM1300_BASE_SHIP, 0x05, &s);
bool pressed = !(s & 0x01);
```

### Button interrupt

On this board two PMIC GPIOs are wired to the MCU: GPIO0 on P1.25 (`PIN_PMIC_GPIO0`) and GPIO1 on P1.26 (`PIN_PMIC_GPIO1`). The examples use GPIO0. Set that PMIC GPIO to interrupt-output mode, attach the MCU interrupt, and then enable the button events:

```cpp
npm1300_gpio_set_mode(0, NPM1300_GPIO_GPOIRQ);
npm1300_write_reg(NPM1300_BASE_MAIN, 0x13, 0xFF);   // clear stale events
pinMode(PIN_PMIC_GPIO0, INPUT);
attachInterrupt(digitalPinToInterrupt(PIN_PMIC_GPIO0), onNpmIrq, RISING);
npm1300_write_reg(NPM1300_BASE_MAIN, 0x14, 0x03);   // enable press + release
```

The interrupt handler should only set a flag. In `loop()`, read the pending events and clear what you saw:

| Register (MAIN base `0x00`) | Offset | Use |
|---|---|---|
| `EVENTSSHPHLDSET` | `0x12` | read: pending events |
| `EVENTSSHPHLDCLR` | `0x13` | write 1 to clear |
| `INTENEVENTSSHPHLDSET` | `0x14` | write 1 to enable |

Bit 0 is button press, bit 1 is release, bit 2 is "held long enough to exit Ship mode", and bit 3 is the watchdog warning. Clear exactly the bits you read, and read again until nothing is pending: an event that arrives while you are handling another keeps the line high, so there is no new rising edge. Also service a high interrupt line even if the ISR flag is clear, and retry failed I2C transactions. The complete example checks the setup and event transaction results.

The PMIC reports presses and releases only, so measure hold time yourself with `millis()`.

See `hardware/nrf54l15clean/nrf54l15clean/examples/Power/nPM1300_GPIO0_irq`.

## Ship and Hibernate modes

Both modes cut the battery off from the system and need **VBUS absent**. Hibernate is the same as Ship plus a running wake-up timer.

```cpp
npm1300_enter_ship_mode();                 // returns false if VBUS is present
npm1300_enter_timed_hibernate_ms(20000);   // does not return on success
```

The contributor measured about 0.6 µA in timed Hibernate on this board with a PPK.

The board wakes on any of these, and each wake is a cold boot into `setup()`:
- the wake-up timer (Hibernate only), in 16 ms steps
- holding SHPHLD low for the configured wake time
- VBUS being applied

### Wake hold time (debounce)

`SHPHLDCONFIG` (SHIP `0x04`) sets how long SHPHLD must be held low to wake from Ship or Hibernate:

| Code | Time |
|---|---|
| 0 | 16 ms |
| 1 | 32 ms |
| 2 | 64 ms |
| 3 | 96 ms (default) |
| 4 | 304 ms |
| 5 | 608 ms |
| 6 | 1008 ms |
| 7 | 3008 ms |

```cpp
npm1300_set_ship_debounce(0x07);   // 3 s hold to wake
npm1300_apply_ship_config();       // applies the value (strobe)
```

The value takes effect only after the strobe, and it must be applied **before every entry** into Ship or Hibernate. `npm1300_apply_ship_config()` does not write non-volatile memory. The times are nominal, so hold a little longer than the setting (for example 4 s for the 3 s setting).

### Things to know

- **Entering Hibernate while the button is held does not work.** The call returns and the PMIC stays on. Wait for release first: poll `SHPHLDSTATUS` until bit 0 reads high before calling the enter function.
- **Long-press reset.** While the PMIC is active, holding SHPHLD low for about 10 s power-cycles the whole board. It is enabled by default. If you want a hold-to-sleep gesture, keep it shorter than this.
- **Waking and still holding.** If the user keeps holding after a wake, the firmware starts with the button already down and there is no press event. Check `SHPHLDSTATUS` at boot and ignore the next release. On the board tested, the long-press reset timer counted from when the PMIC became active, so a user who held through the wake could see a reset about 10 s after boot.
- **Timed hibernate with USB attached.** With VBUS present the PMIC's own Hibernate is not allowed, so `npm1300_enter_timed_hibernate_ms()` falls back to putting only the MCU into System OFF with a timed wake. Its timing was not reliable with a serial monitor or USB connection open. Test sleep behaviour on battery.

## Watchdog

The nPM1300 has one timer, used as the watchdog or as the Hibernate wake-up timer, one at a time.

```cpp
npm1300_watchdog_start(10000);   // timeout in ms, rounded up to 16 ms steps
// in loop(), more often than the timeout, only while the firmware is healthy:
npm1300_watchdog_kick();
npm1300_watchdog_stop();
```

An expired watchdog power-cycles the board. The enter-Ship and enter-Hibernate functions stop the watchdog for you, as does the System OFF path of timed Hibernate. If you sleep some other way where you cannot kick (for example an MCU System OFF), call `npm1300_watchdog_stop()` first. Every kick is an I2C transaction, so a watchdog on a firmware that sleeps for long periods means waking regularly just to kick it. The nRF54's own watchdog may be a better fit for software hangs.

See `hardware/nrf54l15clean/nrf54l15clean/examples/Power/nPM1300_watchdog`.

## Reset cause

`RSTCAUSE` (ERRLOG base `0x0E`, offset `0x03`) latches the cause of the first PMIC reset after the error log was cleared or a power-on reset:

| Bit | Value | Meaning |
|---|---|---|
| 0 | `0x01` | Ship-mode exit |
| 1 | `0x02` | Boot monitor timeout |
| 2 | `0x04` | Watchdog |
| 3 | `0x08` | Long-press reset |

```cpp
uint8_t cause;
npm1300_reset_cause(&cause);
npm1300_clear_reset_cause();   // also clears the charger error log
```

Read it once at boot. A power-on reset clears the log to `0x00`. Until it is cleared, later resets do not replace the first latched cause.

## Examples

Both are under `hardware/nrf54l15clean/nrf54l15clean/examples/Power`:

- `nPM1300_GPIO0_irq`: press and release events with hold time, using the PMIC interrupt on `PIN_PMIC_GPIO0`.
- `nPM1300_watchdog`: start, kick, then let the watchdog expire and read the reset cause.

## Tested

The contributor reports hardware testing of press and release interrupts, the 3 s wake from Ship and from Hibernate, the watchdog expiring and not expiring, the reset-cause bits for Ship wake, watchdog and long-press reset, and timed Hibernate on battery. Disabling the long-press reset (`LPRESETCONFIG`) was not tested.
