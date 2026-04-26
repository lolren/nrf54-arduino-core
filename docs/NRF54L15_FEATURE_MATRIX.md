# nRF54L15 Feature Matrix

Status baseline:

- Repository release line: `0.6.45`
- Audit date: `2026-04-26`
- Silicon source: `Nordic_nRF54L15_Datasheet_v1.0.pdf`
- Core source: `hardware/nrf54l15clean/nrf54l15clean`
- Main library source: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation`

This document is the long-running checklist for what the clean Arduino core
actually supports, what is only partially implemented, and what still needs
work. A checked box means implemented and usable enough to claim. An unchecked
box means either missing, partial, experimental, or not validated enough.

## Status Rules

| Box | Status | Meaning |
|---|---|---|
| [x] | Done | Implemented, exposed, and has at least compile/example validation. |
| [ ] | Partial | Some code exists, but the feature is not complete enough to claim. |
| [ ] | Missing | Datasheet or ecosystem feature exists, but the core has no real wrapper/API. |
| [ ] | Not silicon | Requested feature is not provided by nRF54L15 hardware. |
| [ ] | Low priority | Real block exists, but current boards do not expose a useful path. |

## Highest-Risk Gaps

These are the items most likely to matter for users or product work.

| Box | Area | Current state | What remains |
|---|---|---|---|
| [ ] | Matter end-to-end | Compile-only/on-network bootstrap and on/off-light model exist. | Real commissioning, discovery, control, reboot recovery, and Home Assistant validation. |
| [ ] | BLE production controller parity | Legacy advertising, scanning, connect, GATT, MTU, PHY requests, extended advertising/scanning, and many Bluefruit-compatible flows exist. | Full controller conformance, robust pairing/bonding across hosts, multi-role stress, periodic advertising, ISO, and full interop matrix. |
| [ ] | Bluetooth Channel Sounding product support | Real two-board CS bring-up, DFE/RTT helpers, HCI-style parsing, and VPR CS transport scaffolding exist. | Production BLE controller runtime behind CS and Bluetooth CS interoperability. |
| [ ] | Thread production support | Experimental staged OpenThread path can form leader/child/router and pass UDP between two boards. | Reference-network attach, joiner/commissioner, border-router interop, reboot recovery, and non-experimental API claim. |
| [ ] | Zigbee production stack | IEEE 802.15.4 MAC-lite, role demos, HA-ish examples, ZCL codec/security pieces exist. | Certified/full Zigbee stack behavior: robust commissioning, NWK/APS/ZDO/ZCL/security profile completeness. |
| [ ] | VPR softperipheral/runtime | VPR boot/control, shared transport, lifecycle probes, ticker/offload demos, and CS service scaffolding exist. | General VPR runtime, reusable softperipheral framework, sQSPI, production controller-service ownership. |
| [ ] | Security product path | CRACEN RNG/AAR/ECB/CCM, KMU wrapper, TAMPC wrapper, and some proofs exist. | Reusable KMU-to-CRACEN key consumers, CRACEN PKE/ECDSA, secure/non-secure policy docs, external tamper reset characterization. |

## Datasheet Peripheral Matrix

This section maps the datasheet peripheral blocks against the core. It is
intentionally silicon-oriented, not just Arduino API oriented.

| Box | Datasheet block | Hardware instances seen | Core status | Notes / next tick |
|---|---|---|---|---|
| [x] | `AAR` | `AAR` | Done | Accelerated address resolver wrapper and example exist. |
| [x] | `CCM` | `CCM` | Done | BLE packet encryption/authentication helpers and examples exist. |
| [x] | `COMP` | `COMP` | Done | Single-ended/differential/window APIs and examples exist. |
| [x] | `ECB` | `ECB` | Done | AES block wrapper and known-vector example exist. |
| [x] | `EGU` | `EGU10`, `EGU20` | Done | Public wrapper exists; broader multi-instance validation can still be expanded. |
| [x] | `GPIO` | GPIO ports | Done | Arduino digital IO plus HAL GPIO wrapper exist. |
| [x] | `GPIOTE` | `GPIOTE20`, `GPIOTE30` | Mostly done | Interrupts and DPPI task/event paths exist; secondary-instance example coverage can be expanded. |
| [x] | `GRTC` | `GRTC` | Mostly done | `millis`, `micros`, low-power delays, compare channels, System OFF wake, and HAL wrapper exist. |
| [ ] | `GRTC PWM` | Single low-frequency PWM | Missing | Datasheet exposes one 8-bit non-inverted GRTC PWM that can run in System OFF; no Arduino/HAL wrapper yet. |
| [x] | `I2S` | `I2S20` | Done | TX/RX/duplex wrappers and examples exist. |
| [x] | `LPCOMP` | `LPCOMP` | Done | Threshold, millivolt threshold, System ON/OFF wake, and meter-pulse examples exist. |
| [ ] | `NFCT` | `NFCT` | Missing / low priority | Headers exist, but no NFC tag API. Current XIAO/HOLYIOT boards do not provide a useful antenna path. |
| [x] | `PDM` | `PDM20`, `PDM21` | Partial | Wrapper and microphone examples exist; validate/expose `PDM21` explicitly before marking full. |
| [x] | `PWM` | `PWM20`, `PWM21`, `PWM22` | Mostly done | `analogWrite`, per-pin frequency fallback, hardware PWM examples, and stress test exist. |
| [x] | `QDEC` | `QDEC20`, `QDEC21` | Partial | QDEC wrapper and example exist; validate `QDEC21` explicitly before marking full. |
| [x] | `RADIO BLE` | `RADIO` | Partial | Practical BLE paths exist, but not a complete Bluetooth controller. |
| [x] | `RADIO IEEE 802.15.4` | `RADIO` | Partial | Raw 802.15.4, Zigbee-lite, and Thread PAL paths exist; full Zigbee/Thread product support remains open. |
| [x] | `RADIO proprietary/raw` | `RADIO` | Done | Raw packet TX/RX/ACK examples exist. |
| [x] | `RADIO DFE / CSTONES` | `RADIO` | Partial | CS/DFE helpers and examples exist; production Bluetooth CS interop remains open. |
| [x] | `SAADC` | `SAADC` | Done | `analogRead`, differential probe, internal VDD/VBAT style paths, oversampling/gain wrapper exist. |
| [x] | `SPIM` | `SPIM00`, `SPIM20`, `SPIM21`, `SPIM22`, `SPIM30` | Partial | Arduino `SPI` and HAL `Spim` exist; broader instance/HS behavior needs more examples and validation. |
| [x] | `SPIS` | serial-fabric target instances | Partial | `Spis` wrapper and echo example exist; multi-instance/HS validation remains open. |
| [x] | `TEMP` | `TEMP` | Done | HAL wrapper exists. |
| [x] | `TIMER` | `TIMER20`-`TIMER24` plus system timers | Mostly done | Arduino timing and timer-backed PWM use timers; broader standalone examples for every instance remain open. |
| [x] | `TWIM` | `TWIM20`, `TWIM21`, `TWIM22`, `TWIM30` | Mostly done | `Wire`, `Wire1`, repeated-start, and HAL wrappers exist. |
| [x] | `TWIS` | `TWIS20`, `TWIS21`, `TWIS22`, `TWIS30` | Partial | I2C target callbacks exist; broader target-mode validation remains open. |
| [x] | `UARTE` | `UARTE00`, `UARTE20`, `UARTE21`, `UARTE22`, `UARTE30` | Mostly done | `Serial`, `Serial1`, routing options, and HAL wrapper exist; serial-fabric sharing remains a validation area. |
| [x] | `VPR` | `VPR00` | Partial | Boot/control/shared transport/lifecycle/offload examples exist; no general softperipheral runtime yet. |
| [x] | `WDT` | `WDT30`, `WDT31` | Mostly done | Watchdog wrapper and examples exist; multi-instance behavior can be documented further. |
| [x] | `DPPIC` | `DPPIC00`, `DPPIC10`, `DPPIC20`, `DPPIC30` | Mostly done | HAL wrapper and BLE/DPPI examples exist; domain-specific docs can be clearer. |
| [x] | `REGULATORS` | `REGULATORS` | Partial | System OFF, DC/DC, POF warning, and low-power paths exist; deeper regulator policy coverage remains open. |
| [ ] | `CACHE` / `ICACHE` | `CACHE`, cache RAM blocks | Partial internal only | Used internally for VPR image/lifecycle handling; no public cache management API or examples. |
| [ ] | `KMU` | `KMU` | Partial | Wrapper and metadata/task surface exist; full product key provisioning and reusable CRACEN consumers remain open. |
| [ ] | `CRACEN` PKE / advanced crypto | `CRACEN` | Partial | RNG and selected symmetric crypto paths exist; ECDSA/PKE is still not exposed. |
| [ ] | `TAMPC` / `GLITCHDET` | `TAMPC` | Partial | Wrapper exists for controls/status; external tamper/reset behavior needs hardware characterization. |
| [ ] | `sQSPI` softperipheral | VPR softperipheral | Missing | Datasheet mentions sQSPI as VPR softperipheral; no Arduino core path yet. |

## Arduino Core API Matrix

This section tracks user-facing Arduino behavior.

| Box | API area | Current state | What remains |
|---|---|---|---|
| [x] | `pinMode`, `digitalRead`, `digitalWrite` | Implemented. | Add more board-specific pin validation as new boards are added. |
| [x] | `attachInterrupt`, `detachInterrupt` | GPIOTE-backed rising/falling/change support. | More stress tests with BLE/Thread active. |
| [x] | `millis`, `micros`, `delay`, low-power delay | Implemented. | More long-duration drift/current characterization. |
| [x] | `analogRead`, `analogReadResolution` | SAADC-backed. | Add more public examples for internal channels and oversampling. |
| [x] | Internal VDD measurement | Implemented through SAADC helper/example. | Add board-specific calibration notes if measurements vary. |
| [x] | `analogWrite` | Implemented on `D0-D15` with hardware/timer/software paths. | Continue low-duty and multi-pin regression testing. |
| [x] | `analogWriteFrequency` | Implemented. | More docs around shared-frequency hardware limitations. |
| [x] | `analogWritePinFrequency` | Implemented for per-pin timer-backed `D0-D5`. | More examples for per-pin stress and scope validation. |
| [x] | `Serial`, `Serial1`, `Serial2` compatibility | Implemented. | Keep board routing docs current. |
| [x] | `SPI` | Implemented. | More examples for extra serial-fabric instances. |
| [x] | `Wire`, `Wire1` | Implemented. | More I2C target examples and multi-instance validation. |
| [x] | `EEPROM` / `Preferences` style persistence | Implemented enough for bond/Thread/Matter staging. | Capacity/fragmentation docs and stress tests. |
| [x] | Board Manager install/update | Implemented. | Keep release index checksum testing mandatory. |
| [x] | Upload tooling | Implemented with packaged tools. | Continue fresh Linux/Windows/macOS smoke tests. |
| [x] | Tools menu: BLE / VPR / Zigbee / power / CPU / RAM reclaim | Implemented. | Keep options visible for all supported boards after package updates. |
| [ ] | Public Tools menu: production Thread | Experimental only. | Remove experimental label only after validation matrix is green. |
| [ ] | Public Tools menu: production Matter | Experimental compile/bootstrap only. | Remove experimental label only after real commissioning/control works. |

## BLE Matrix

| Box | BLE capability | Current state | What remains |
|---|---|---|---|
| [x] | Legacy advertising | Implemented. | Continue phone interop regression. |
| [x] | Background legacy advertising | Implemented. | More long-duration current/latency characterization. |
| [x] | Legacy scan response | Implemented. | Keep active-scan examples maintained. |
| [x] | Passive/active scanning | Implemented. | More host/phone interop matrix. |
| [x] | Extended advertising/scanning | Implemented for practical examples. | Periodic advertising and conformance-level validation remain open. |
| [x] | Connectable peripheral | Implemented. | More host combinations and long-lived connection tests. |
| [x] | Central initiate/minimal client | Implemented baseline. | Multi-role and broader client discovery/write/notify stress. |
| [x] | Dynamic 16-bit custom GATT | Implemented. | 128-bit dynamic service editing/descriptors/multi-service mutation remain open. |
| [x] | Notifications/indications | Implemented. | More throughput and retransmission stress. |
| [x] | ATT MTU exchange | Implemented enough for Bluefruit compatibility paths. | More interop/edge-case testing. |
| [x] | PHY request/get API | Implemented for `1M`, `2M`, and coded PHY request paths. | More central/peripheral combinations and fallback behavior tests. |
| [ ] | Pairing/bonding production stability | Partial | Existing code and persistence exist, but repeated cross-host stability remains the top BLE security gap. |
| [ ] | Full Bluetooth controller conformance | Partial | This core is a clean register-level stack, not Zephyr/NCS controller parity. |
| [ ] | Periodic advertising | Missing | Not exposed. |
| [ ] | LE Audio / ISO | Missing | Not exposed. |
| [ ] | Mesh | Missing | Not exposed. |
| [ ] | Bluetooth Channel Sounding product mode | Partial | CS scaffolding exists; full controller-backed interoperability remains open. |

## Thread Matrix

| Box | Thread capability | Current state | What remains |
|---|---|---|---|
| [x] | OpenThread PAL skeleton | Implemented. | Keep compile coverage. |
| [x] | 802.15.4 TX/RX PAL | Implemented and two-board proven. | More stress and reference sniff validation. |
| [x] | Fixed dataset attach | Implemented in experimental stage mode. | Reference-network attach validation. |
| [x] | Leader/child role path | Implemented in experimental stage mode. | Reboot recovery and longer soak. |
| [x] | Router promotion | Implemented in experimental stage mode. | Broader router behavior validation. |
| [x] | UDP send/receive wrapper | Implemented in experimental stage mode. | More socket/multicast/fragmentation tests. |
| [x] | PSKc/passphrase dataset build | Implemented. | More compatibility vectors. |
| [ ] | Joiner | Missing / not claimed | Needed for normal commissioned Thread network bring-up. |
| [ ] | Commissioner | Missing / not claimed | Not a first-pass goal. |
| [ ] | Border router | Missing / non-goal | Should likely remain external. |
| [ ] | CSL / sleepy end device depth | Partial | OpenThread PAL returns not-implemented for CSL operations. |
| [ ] | Coexistence metrics/control | Missing | OpenThread radio coex APIs are not implemented. |
| [ ] | Link metrics / enhanced ACK probing | Missing | PAL returns not-implemented for enhanced ACK probing. |
| [ ] | Reference Thread network attach | Missing validation | Required before removing "experimental". |
| [ ] | Reboot recovery | Missing validation | Required before removing "experimental". |

## Matter Matrix

| Box | Matter capability | Current state | What remains |
|---|---|---|---|
| [x] | `connectedhomeip` scaffold | Staged. | Keep upstream intake controlled. |
| [x] | Platform ownership docs | Implemented. | Update as runtime grows. |
| [x] | On/off light state model | Implemented. | Bind to real Matter exchange path. |
| [x] | On-network bootstrap object | Implemented. | Wire into real CHIP runtime. |
| [x] | Manual/QR onboarding helper | Implemented. | Validate with real commissioner once runtime exists. |
| [x] | Thread dataset TLV export/import seam | Implemented. | Validate against real controller-provided dataset flow. |
| [ ] | BLE rendezvous commissioning | Not selected for first pass | Current plan is on-network Thread commissioning first. |
| [ ] | Real commissioning | Missing | Required for product claim. |
| [ ] | Discovery | Missing | Required for product claim. |
| [ ] | Control from commissioner | Missing | Required for product claim. |
| [ ] | Home Assistant validation | Missing | Required for user-facing claim. |
| [ ] | Reboot/reconnect recovery | Missing | Required for user-facing claim. |
| [ ] | More Matter device types | Missing / deferred | Do not add before first on/off light is actually commissioned. |

## Zigbee / IEEE 802.15.4 Matrix

| Box | Capability | Current state | What remains |
|---|---|---|---|
| [x] | Raw IEEE 802.15.4 TX/RX | Implemented. | More channel/power/regulatory docs. |
| [x] | MAC ACK/request helpers | Implemented. | More sniffer validation. |
| [x] | Coordinator/router/end-device demos | Implemented. | Soak and network-scale tests. |
| [x] | HA-ish ZCL examples | Implemented. | Certification-level behavior not claimed. |
| [x] | Zigbee security codec pieces | Implemented. | Product commissioning/key-management completeness remains open. |
| [ ] | Full Zigbee stack | Partial | NWK/APS/ZDO/ZCL/security profiles are not complete enough to claim certification-level support. |

## VPR Matrix

| Box | Capability | Current state | What remains |
|---|---|---|---|
| [x] | VPR boot/start/reset control | Implemented. | More lifecycle docs. |
| [x] | Shared-memory transport | Implemented. | More framing/error handling stress. |
| [x] | Controller-service host wrapper | Implemented. | Broader service command set. |
| [x] | Simple offload demos | FNV1a, CRC32, CRC32C, ticker examples exist. | Real user-facing offload API policy. |
| [x] | Hibernate/restart lifecycle probes | Implemented and validated enough for current docs. | Raw CPU-context resume remains investigation. |
| [x] | VPR BLE/CS service scaffolding | Implemented for current CS bring-up path. | Production BLE controller ownership on VPR. |
| [ ] | Generic VPR softperipheral runtime | Missing | Needed before claiming VPR as a reusable coprocessor platform. |
| [ ] | sQSPI softperipheral | Missing | Mentioned by datasheet; not implemented. |
| [ ] | VPR-host memory ownership hardening | Partial | Needs clearer API contracts and misuse tests. |

## Security / Crypto / Tamper Matrix

| Box | Capability | Current state | What remains |
|---|---|---|---|
| [x] | CRACEN RNG | Implemented. | Long-run entropy/health documentation. |
| [x] | AAR | Implemented. | More BLE privacy examples if needed. |
| [x] | ECB | Implemented. | None urgent. |
| [x] | CCM | Implemented. | More vector coverage and BLE encrypted-link regression. |
| [x] | OpenThread AES/HMAC/HKDF/PBKDF2 path | Implemented enough for staged Thread. | ECDSA remains not capable. |
| [ ] | CRACEN PKE / ECDSA | Missing | Required for broader product security and future Matter depth. |
| [ ] | KMU product key flow | Partial | Need safe provisioning examples, slot lifecycle docs, reusable CRACEN key consumers. |
| [ ] | TAMPC external tamper behavior | Partial | Need reset-cause and external input hardware characterization. |
| [ ] | Glitch/tamper product policy docs | Partial | Need secure/non-secure behavior notes and safe examples. |
| [ ] | Side-channel protection policy | Missing / not wrapped | Datasheet advertises side-channel protection; no user-facing policy/API. |

## Board Support Matrix

| Box | Board / target | Current state | What remains |
|---|---|---|---|
| [x] | Seeed XIAO nRF54L15 / Sense | Main target. | Keep examples and pin docs current. |
| [x] | HOLYIOT-25007 | Supported. | More board-specific runtime tests. |
| [x] | HOLYIOT-25008 | Supported. | More pin/power/antenna examples. |
| [x] | Nordic PCA10156 DK | Supported. | More board-specific pin/peripheral validation. |
| [x] | Generic 36-pad module | Supported. | More custom-board bring-up docs. |
| [ ] | macOS fresh install validation | Partial | Package has macOS assets; needs recurring real-host smoke testing. |
| [x] | Linux fresh install validation | Mostly done | Keep Mint/udev/upload paths tested after upload-tool changes. |
| [x] | Windows fresh install validation | Mostly done | Keep checksum/index and packaged-tool tests mandatory. |

## Validation Checklist To Keep Current

Use this section as the high-level release gate checklist. Tick only after a
fresh compile/install/hardware pass.

| Box | Validation item | Required evidence |
|---|---|---|
| [x] | Package index checksum verification | `scripts/verify_package_index.py` or public release verification output. |
| [x] | Clean Arduino CLI package install | Isolated CLI data/user directory install. |
| [ ] | Compile all board-menu examples | Saved compile logs for all supported boards. |
| [ ] | Compile all library examples | Saved compile logs for all supported boards or declared board subset. |
| [ ] | Upload smoke test on XIAO | Serial boot message or debugger readback. |
| [ ] | Upload smoke test on HOLYIOT | Serial/debugger confirmation. |
| [ ] | GPIO/interrupt smoke | `FeatureParitySelfTest` or equivalent. |
| [ ] | PWM scope/logic validation | `PwmDatasheetStress` plus low-duty regression. |
| [ ] | SAADC/VDD validation | Known voltage or internal VDD sanity output. |
| [ ] | I2C/SPI/UART smoke | Board-specific loopback/device examples. |
| [ ] | BLE advertise/scan/connect smoke | Host or phone evidence. |
| [ ] | BLE MTU/PHY smoke | Two-device or host evidence. |
| [ ] | BLE pair/bond smoke | Repeated cross-host evidence. |
| [ ] | 802.15.4 two-board smoke | Zigbee or Thread radio pair evidence. |
| [ ] | Thread staged UDP smoke | Two-board `ThreadExperimentalUdpHello` evidence. |
| [ ] | Matter staged compile smoke | Stage examples compile. |
| [ ] | Matter real commission smoke | Commissioner/Home Assistant evidence. |
| [ ] | Low-power current smoke | Measured current for selected board/profile. |
| [ ] | VPR lifecycle smoke | VPR restart/hibernate probe evidence. |

## Do Not Claim Yet

These should stay out of marketing/release claims until the corresponding boxes
above are checked.

- Full Matter support.
- Production Thread support.
- Full Zigbee stack / certification-level Zigbee.
- Full Bluetooth controller / full BLE 6 conformance.
- Production Bluetooth Channel Sounding interoperability.
- Inverter-safe complementary PWM with dead time.
- NFCT support.
- sQSPI support.
- CRACEN PKE / ECDSA support.

## Reference Documents

- `README.md`
- `docs/THREAD_MATTER_IMPLEMENTATION_PLAN.md`
- `docs/THREAD_RUNTIME_OWNERSHIP.md`
- `docs/MATTER_RUNTIME_OWNERSHIP.md`
- `docs/MATTER_FOUNDATION_MANIFEST.md`
- `docs/BLE_CS_COMPLETION_CHECKLIST.md`
- `docs/CHANNEL_SOUNDING_VPR_CONTINUATION.md`
- `docs/low-power-ble-patterns.md`
- `docs/board-reference.md`
