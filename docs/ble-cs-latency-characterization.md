# BLE / CS Latency Characterization

This note captures the current measured latency for the shipped BLE and Channel
Sounding bring-up paths on attached `XIAO nRF54L15 / Sense` hardware.

Scope:

- these are real hardware measurements
- they are taken from the current shipped sketch/helper boundaries
- timing resolution is `millis()`, so values are coarse by design
- the CS distance value that appears in the logs remains nominal synthetic
  regression output only, not a physical distance claim

## Hardware

- initiator / generic-service board: `XIAO nRF54L15 / Sense`
- reflector board for the two-board path: `XIAO nRF54L15 / Sense`
- two-board spacing during the linked CS check was roughly `0.7 m` to `1.0 m`
- upload path: `pyOCD`

## Measured Paths

### Generic In-Place VPR BLE -> CS Runtime

Example:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingVprServiceNominal/BleChannelSoundingVprServiceNominal.ino`

Printed latency fields:

- `lat_ms=begin/complete/disconnect/total`

Meaning:

- `begin`: boot generic VPR service, configure encrypted BLE link, bind CS,
  configure CS workflow, and reach the running state
- `complete`: wait from running state to completed nominal CS workflow summary
- `disconnect`: disconnect the BLE link and wait for cleared shared state
- `total`: full `begin + complete + disconnect` run

Measured repeated runs:

- `lat_ms=22/2/4/28`
- `lat_ms=22/2/4/28`
- `lat_ms=22/2/4/28`
- `lat_ms=22/2/4/28`

Current characterized result:

- begin: `~22 ms`
- complete: `~2 ms`
- disconnect and clear: `~4 ms`
- full single-board nominal BLE -> CS -> disconnect cycle: `~28 ms`

### Two-Board Linked BLE-Handoff -> Dedicated CS Runtime

Examples:

- initiator:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingVprLinkedInitiator/BleChannelSoundingVprLinkedInitiator.ino`
- reflector:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingReflector/BleChannelSoundingReflector.ino`

Printed latency fields:

- `lat_ms=source_boot/source_connect/handoff_boot/start/complete/total`

Meaning:

- `source_boot`: boot the generic VPR BLE service and read capabilities
- `source_connect`: configure the source BLE link and wait for connected shared
  state
- `handoff_boot`: boot the dedicated CS image and import the live BLE link
- `start`: direct CS workflow start sequence on the imported link
- `complete`: extra wait after `start` until the first completed result appears
- `total`: full source-boot -> imported-link CS result path

Measured repeated runs:

- `lat_ms=16/2/9/15/0/55`
- `lat_ms=16/2/9/15/0/55`
- `lat_ms=16/2/9/15/0/55`
- `lat_ms=16/2/9/15/0/55`

Current characterized result:

- source BLE service boot: `~16 ms`
- source BLE link configure/connect: `~2 ms`
- dedicated CS image boot + link import: `~9 ms`
- imported-link workflow start to first completed result availability:
  `~15 ms`
- extra post-start wait: `0 ms` in the observed runs because the first
  completed result was already staged when the explicit completion poll began
- full two-board imported-link BLE -> dedicated CS first-result path:
  `~55 ms`

## Interpretation

- the current generic in-place BLE -> CS nominal path is fast and stable on the
  attached board, but it is still the nominal synthetic CS runtime
- the two-board imported-link path has a higher total cost because it includes
  both the source generic BLE service and the dedicated CS image boot/import
  boundary
- the linked path numbers are useful for bring-up/runtime budgeting, not as a
  claim that the controller path is production-final

## Limits

- measurements are sketch/helper boundary timings, not hardware-internal radio
  event timestamps
- timing resolution is `1 ms`
- no power characterization is implied here
- no physical distance accuracy claim is implied here
