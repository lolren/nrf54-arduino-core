# Zephyr Channel Sounding Validation

This repo's clean Arduino core now includes a working two-board **phase-based** channel-sounding path using `RADIO.CSTONES` and DFE tone capture.

This document is for a different purpose:

- validating that **controller-backed Bluetooth Channel Sounding** also runs on the XIAO nRF54L15 hardware
- using the official Zephyr `connected_cs` sample as the reference implementation
- comparing the clean-core phase path against an NCS/Zephyr controller-managed CS stack

It does **not** make the Arduino core itself controller-backed or fully spec-complete.

Validated on this machine on March 9, 2026 with two attached XIAO nRF54L15 boards on `/dev/ttyACM0` and `/dev/ttyACM1`.

## What Was Verified

Using the bundled NCS tree available on this machine, the official Zephyr sample:

- `zephyr/samples/bluetooth/channel_sounding/connected_cs/initiator`
- `zephyr/samples/bluetooth/channel_sounding/connected_cs/reflector`

builds successfully for:

- `xiao_nrf54l15/nrf54l15/cpuapp`

and runs on the two attached XIAO nRF54L15 boards when flashed through the CMSIS-DAP probes.

Observed controller-backed output on the initiator included both RTT and phase estimates, for example:

```text
Estimated distance to reflector:
- Round-Trip Timing method: 6.835268 meters (derived from 5 samples)
- Phase-Based Ranging method: -0.011079 meters (derived from 36 samples)
```

That proves the official controller-backed CS procedure is active on this hardware, even though the desk setup is noisy and not calibrated.

Observed reflector-side output also reached the expected controller flow, including:

```text
CS security enabled.
CS procedures enabled.
```

Treat the reported meter values as a controller-backed bring-up proof, not as calibrated ground-truth distance.

## Helper Script

Use:

- [`scripts/zephyr_channel_sounding_validation.py`](/home/lolren/Desktop/Nrf54L15/repo/scripts/zephyr_channel_sounding_validation.py)

It can:

- build the official Zephyr `connected_cs` initiator/reflector roles
- flash either role to a chosen XIAO board by `--port` or `--uid`
- reset the board to retrigger console logs
- run a two-board `pair-demo` flow

The script expects a tools directory containing:

- `ncs/`
- `pydeps/`
- `zephyr-sdk/`

You can point it explicitly with:

```bash
export XIAO_NRF54L15_TOOLS_DIR=/path/to/hardware/.../tools
```

If unset, it tries a local bundled install first and then a known legacy local fallback.

## Typical Flow

Build both roles:

```bash
python3 scripts/zephyr_channel_sounding_validation.py build
```

Flash and reset both attached boards:

```bash
python3 scripts/zephyr_channel_sounding_validation.py pair-demo \
  --initiator-port /dev/ttyACM0 \
  --reflector-port /dev/ttyACM1
```

Open serial first if you want boot logs:

```bash
stty -F /dev/ttyACM0 115200 raw -echo && stdbuf -oL cat /dev/ttyACM0
stty -F /dev/ttyACM1 115200 raw -echo && stdbuf -oL cat /dev/ttyACM1
```

Then retrigger boot output:

```bash
python3 scripts/zephyr_channel_sounding_validation.py reset --port /dev/ttyACM0
python3 scripts/zephyr_channel_sounding_validation.py reset --port /dev/ttyACM1
```

## Expected Console

Reflector:

```text
Starting Channel Sounding Demo
Connected to ...
MTU exchange success (247)
CS capability exchange completed.
CS config creation complete. ID: 0
CS security enabled.
CS procedures enabled.
```

Initiator:

```text
Starting Channel Sounding Demo
Found device with name CS Sample, connecting...
Connected to ...
MTU exchange success (247)
CS capability exchange completed.
CS config creation complete. ID: 0
CS security enabled.
CS procedures enabled.
Estimated distance to reflector:
- Round-Trip Timing method: ...
- Phase-Based Ranging method: ...
```

## Boundary

Current clean-core status:

- Arduino core: working custom phase-based sounding using raw radio + `CSTONES`
- Zephyr/NCS validation path: working controller-backed BLE CS sample on the same hardware
- still not the same thing as a fully standards-complete controller-backed Arduino core

If the next step is full BLE CS inside the Arduino core itself, the practical path is to port toward a controller-backed architecture rather than extending the current bare-RADIO RTT/AUXDATA decode guesswork.
