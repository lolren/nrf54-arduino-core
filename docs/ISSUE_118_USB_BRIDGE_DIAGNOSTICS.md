# XIAO nRF54LM20A Missing USB Serial Port

[Issue #118](https://github.com/lolren/nrf54-arduino-core/issues/118) reports a
successful upload followed by loss of the macOS `/dev/cu.usbmodem...` port.
The root cause has not been reproduced or established. The IMU example only
enables the sensor supply and reads the sensor; it does not request system
hibernate or turn off BUCK2.

The XIAO's SAMD11 provides USB CDC serial and CMSIS-DAP programming. The nRF54
application communicates with that bridge over UART. Checking USB, serial and
probe enumeration separately identifies which part of the path is missing.
The [Nordic board documentation](https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/boards/seeed/xiao_nrf54lm20a/doc/index.html#programming-and-debugging)
describes this arrangement.

## 1. Check USB Enumeration

With the affected board connected, run in macOS Terminal:

```bash
system_profiler SPUSBDataType
```

Keep the entry for the XIAO/Seeed/SAMD11 device, including its product name,
vendor/product IDs and serial number. If that entry is absent, the failure is
earlier than serial-port creation or the sketch's `Serial.begin()`.

## 2. Check Serial Interfaces

```bash
ioreg -r -c IOSerialBSDClient -l
```

Look for `IOCalloutDevice` and `IODialinDevice` associated with the board.
This checks macOS's serial registry independently of the Arduino Port menu.
An enumerated USB bridge without a corresponding serial interface is a
different result from a USB device that does not enumerate at all.

## 3. Check The Programming Interface

Use the pyOCD installation already used by the uploader:

```bash
pyocd list --probes
```

If the uploader populated the pinned Arduino host-tools environment, use:

```bash
python3 "$HOME/Library/Arduino15/packages/nrf54l15clean/tools/nrf54l15hosttools/1.1.5/pyocd_shim.py" list --probes
```

The second command uses the host-tools package's private Python dependencies.
If the installed host-tools version or Python executable differs, use the
matching path from the verbose upload log. Include an environment/import error
in the report rather than treating it as evidence that the board is absent.
Probe listing does not connect to or erase the nRF54 target.

## 4. Report The Results

Include the relevant USB entry, serial registry entry, probe-list output and
the original verbose upload log if available. State whether a battery or other
power source remained attached during the earlier USB disconnection tests.

| Result | What it establishes |
|---|---|
| No board in the USB tree | The bridge is not enumerating; changing the sketch's UART settings cannot restore a host USB interface that is absent. |
| USB device and CMSIS-DAP present, serial absent | The CMSIS-DAP interface enumerates; investigate the CDC interface and host binding separately. |
| Serial present in `ioreg`, absent in Arduino | Investigate IDE discovery; include the exact IDE version and serial path. |
| CMSIS-DAP present | The probe enumerates. This alone does not establish that the nRF54 target can be connected or programmed. |

Installing a new core on the computer does not replace the application already
stored on the board. These observations also do not establish permanent
hardware damage.

If power-rail shutdown is confirmed, see the
[BUCK2 recovery instructions](../README.md#xiao-nrf54lm20a-disappeared-after-buck2-control).
The [community rescue tool](https://forum.seeedstudio.com/t/how-to-unbrick-a-dead-xiao-nrf54l15-lm20a/295684)
is another recovery reference. Neither is presented as a confirmed fix for
#118; preserve the diagnostic evidence before performing erase/recovery steps.
