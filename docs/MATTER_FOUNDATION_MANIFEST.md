# Matter Foundation Manifest

This document pins the first honest Matter foundation target for the Arduino
repo after the staged Thread bring-up.

## Upstream Intake Path

- source repo:
  `https://github.com/project-chip/connectedhomeip`
- reserved staged path:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip`
- intake script:
  `scripts/import_connectedhomeip_scaffold.sh`
- current minimal imported ref:
  `337f8f54b4f0813681664e5b179dc3e16fdd14a0`
- current imported files:
  - `src/lib/core/DataModelTypes.h`
  - `src/lib/core/CHIPVendorIdentifiers.hpp`
  - `src/lib/core/CHIPError.h`
  - `src/lib/core/CHIPError.cpp`
  - `src/lib/core/CHIPKeyIds.h`
  - `src/lib/core/CHIPKeyIds.cpp`
  - `src/lib/core/ErrorStr.h`
  - `src/lib/core/ErrorStr.cpp`
  - `src/lib/core/GroupId.h`
  - `src/lib/core/PasscodeId.h`
  - `src/lib/core/NodeId.h`
  - `src/lib/core/Unchecked.h`
  - `src/lib/support/Base64.h`
  - `src/lib/support/Base64.cpp`
  - `src/lib/support/Base85.h`
  - `src/lib/support/Base85.cpp`
  - `src/lib/support/BitFlags.h`
  - `src/lib/support/BytesToHex.h`
  - `src/lib/support/BytesToHex.cpp`
  - `src/lib/support/ThreadOperationalDataset.h`
  - `src/lib/support/ThreadOperationalDataset.cpp`
  - `src/lib/support/SafeInt.h`
  - `src/lib/support/Span.h`
  - `src/lib/support/TimeUtils.h`
  - `src/lib/support/TimeUtils.cpp`
  - `src/lib/support/DLLUtil.h`
  - `src/lib/support/TypeTraits.h`
  - repo-owned shim:
    `src/matter_core_stage/lib/core/CHIPConfig.h`
    `src/matter_core_stage/lib/core/CHIPCore.h`
    `src/matter_core_stage/lib/core/CHIPEncoding.h`
    `src/matter_core_stage/lib/support/CodeUtils.h`
    `src/matter_core_stage/lib/support/logging/CHIPLogging.h`
  - repo-owned onboarding-code helper:
    `src/matter_manual_pairing.h`
    `src/matter_manual_pairing.cpp`
  - repo-owned compile target:
    `src/matter_foundation_target.h`
    `src/matter_foundation_target.cpp`

The intake script is intentionally separate from build integration. It creates
the upstream staging area the compile-only Matter target now consumes, without
pretending that the Arduino build already ships a commissioned Matter runtime.

## First Foundation Scope

- transport:
  `Thread` only
- first commissioning target:
  `on-network-only`
- first device type:
  `on-off-light`
- upstream app seed to study later:
  `connectedhomeip/examples/lighting-app`

## First Compile Target Shape

The current compile-only target is intentionally repo-owned and bounded:

- staged upstream support/core/data-model files stay in
  `third_party/connectedhomeip`
- the compile-only first-device surface lives in
  `src/matter_foundation_target.h`
  `src/matter_foundation_target.cpp`
- the user-facing compile proof lives in
  `examples/Matter/MatterOnOffLightFoundationCompileTarget`

The future larger target should still be derived from the staged upstream
layout around:

- `connectedhomeip/BUILD.gn`
- `connectedhomeip/src/CMakeLists.txt`
- `connectedhomeip/build/chip`
- `connectedhomeip/config`
- `connectedhomeip/src`
- `connectedhomeip/zzz_generated/app-common`

## Platform Boundary For That Target

- keep repo-owned platform glue in:
  `src/matter_platform_nrf54l15.h`
  plus future `.cpp` bridge files
- keep staged upstream CHIP sources in:
  `third_party/connectedhomeip`
- do not flatten staged upstream sources into Arduino-facing library headers
- do not claim a user-facing Matter API until a compile-only target exists and
  its platform dependencies are explicit

## Current Validation Boundary

What this slice claims:

- the intake path is defined
- a minimal staged upstream CHIP header/support/data-model seed is imported
  in-tree
- the ownership split is frozen
- the first commissioning target is chosen
- the first device type is chosen
- the Arduino board package now exposes
  `Tools > Matter Foundation > Experimental Compile Target (On-Network On/Off Light)`
  while still defaulting to `Disabled`
- the repo-owned `MatterFoundationProbe` can now compile against real staged
  upstream CHIP headers when the staged Matter menu is enabled
- the hidden seam now also links one real staged upstream support `.cpp`
  (`src/lib/support/Base64.cpp`) and the probe exercises it at runtime
- the hidden seam now also links staged upstream core error formatting
  (`src/lib/core/CHIPError.cpp` / `src/lib/core/ErrorStr.cpp`) through a
  repo-owned minimal config shim, and the probe exercises that formatter at
  runtime
- the hidden seam now also links staged upstream key-id logic
  (`src/lib/core/CHIPKeyIds.cpp`) through a repo-owned minimal `CodeUtils`
  shim, and the probe exercises that logic at runtime
- the hidden seam now also links staged upstream `Base85` support and the
  probe proves encode/decode round-trip on hardware
- the hidden seam now also links staged upstream `TimeUtils` support
  (`src/lib/support/TimeUtils.cpp`) through a repo-owned minimal
  `CHIPCore.h` shim, and the probe exercises CHIP-epoch/unix/calendar
  conversion paths plus date adjustment on hardware
- the hidden seam now also links staged upstream `BytesToHex` support
  (`src/lib/support/BytesToHex.cpp`) through repo-owned minimal
  `CHIPEncoding.h` and `CHIPLogging.h` shims, and the probe exercises
  uppercase hex encode/decode plus integer round-trip paths on hardware
- the hidden seam now also links staged upstream
  `ThreadOperationalDataset` support
  (`src/lib/support/ThreadOperationalDataset.cpp`) through the existing
  minimal `CHIPCore.h`, `CHIPEncoding.h`, and `CodeUtils.h` shims, and the
  probe exercises dataset build, validation, commissioned-state checks,
  field readback, and copy round-trip on hardware
- the repo-owned Matter onboarding helper now generates short and long decimal
  manual pairing codes with Verhoeff check digits, and the probe checks
  deterministic vectors from the upstream Matter setup-payload tests
- the same helper now generates basic QR setup payload strings with Matter's
  Base38 packing, and the probe checks both the upstream default QR vector and
  a Thread/on-network QR vector for the future commissioning path
- the repo-owned `Nrf54MatterOnOffLightFoundation` target now defines:
  - a root-node endpoint
  - a first on/off-light endpoint
  - the first endpoint/cluster metadata needed for an honest compile-only
    device shape
  - an explicit Thread dependency contract for the features this Matter slice
    actually needs
- the same target now exports an `otOperationalDataset` into staged
  `chip::Thread::OperationalDataset` TLV form, which is the first in-tree
  mechanical bridge from the staged Arduino Thread wrapper into Matter-facing
  onboarding data
- the repo-owned
  `examples/Matter/MatterOnOffLightFoundationCompileTarget`
  now compiles with
  `clean_thread=stage`
  and
  `clean_matter=stage`
  and reports endpoint layout, dependency resolution, onboarding codes, and
  exported dataset TLV state
- this intentionally does not import the larger setup-payload parser, optional
  QR TLV data, or heap-backed QR utilities yet

What this slice does not claim:

- a full linked Matter device runtime
- a commissioned device
- a Home Assistant integration
