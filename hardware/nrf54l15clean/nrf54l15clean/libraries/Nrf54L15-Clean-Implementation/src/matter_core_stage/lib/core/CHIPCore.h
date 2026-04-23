#pragma once

// Minimal Matter core-stage shim for staged upstream support units that expect
// CHIP core umbrella headers, but do not actually need System/Inet/BLE yet.

#include <lib/core/CHIPConfig.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>

#define CHIP_CORE_IDENTITY "chip-core"
#define CHIP_CORE_PREFIX CHIP_CORE_IDENTITY ": "
