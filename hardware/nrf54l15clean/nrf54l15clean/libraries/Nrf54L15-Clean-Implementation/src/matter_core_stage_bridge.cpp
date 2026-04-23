#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)

#if !defined(NRF54L15_CLEAN_MATTER_SUPPORT_SEED_AVAILABLE)
#error "Enable the staged Matter seam with build.matter_seam_flags so the bridge gets the staged support seed include path."
#endif

#if !defined(NRF54L15_CLEAN_MATTER_CORE_ERROR_SEED_AVAILABLE)
#error "Enable the staged Matter seam with build.matter_seam_flags so the bridge gets the staged core-error seed include path."
#endif

#if !defined(NRF54L15_CLEAN_MATTER_CORE_KEY_SEED_AVAILABLE)
#error "Enable the staged Matter seam with build.matter_seam_flags so the bridge gets the staged core-key seed include path."
#endif

#if !defined(NRF54L15_CLEAN_MATTER_TIME_SEED_AVAILABLE)
#error "Enable the staged Matter seam with build.matter_seam_flags so the bridge gets the staged TimeUtils seed include path."
#endif

#include "../third_party/connectedhomeip/src/lib/support/Base64.cpp"
#include "../third_party/connectedhomeip/src/lib/support/Base85.cpp"
#include "../third_party/connectedhomeip/src/lib/support/TimeUtils.cpp"
#include "../third_party/connectedhomeip/src/lib/core/ErrorStr.cpp"
#include "../third_party/connectedhomeip/src/lib/core/CHIPError.cpp"
#include "../third_party/connectedhomeip/src/lib/core/CHIPKeyIds.cpp"

#endif
