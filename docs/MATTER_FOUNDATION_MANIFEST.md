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
  - `src/lib/support/Base64.h`
  - `src/lib/support/Base64.cpp`
  - `src/lib/support/Base85.h`
  - `src/lib/support/Base85.cpp`
  - `src/lib/support/DLLUtil.h`
  - `src/lib/support/TypeTraits.h`
  - repo-owned shim:
    `src/matter_core_stage/lib/core/CHIPConfig.h`
    `src/matter_core_stage/lib/support/CodeUtils.h`

The intake script is intentionally separate from build integration. It creates
the upstream staging area without pretending that the Arduino build already
links a real Matter target.

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

The future compile-only target should be built from staged upstream sources,
not copied into the Arduino library `src/` folder.

The first target should be derived from the staged upstream layout around:

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
- a minimal staged upstream CHIP header seed is imported in-tree
- the ownership split is frozen
- the first commissioning target is chosen
- the first device type is chosen
- the hidden Arduino build seam now has explicit Matter flags
- the repo-owned `MatterFoundationProbe` can now compile against real staged
  upstream CHIP headers when the hidden seam is enabled
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

What this slice does not claim:

- a linked Matter core
- a compile-only CHIP target
- a commissioned device
- a Home Assistant integration
