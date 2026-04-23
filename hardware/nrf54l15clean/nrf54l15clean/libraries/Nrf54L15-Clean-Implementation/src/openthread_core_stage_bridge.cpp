#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)

#if !defined(OPENTHREAD_CONFIG_CORE_USER_CONFIG_HEADER_ENABLE)
#error "Enable the staged OpenThread seam with build.thread_seam_flags so the bridge gets the upstream core include paths and config define."
#endif

#if !defined(OPENTHREAD_FTD)
#define OPENTHREAD_FTD 1
#endif

#if !defined(OPENTHREAD_MTD)
#define OPENTHREAD_MTD 0
#endif

#if !defined(OPENTHREAD_RADIO)
#define OPENTHREAD_RADIO 0
#endif

#if !defined(PACKAGE_NAME)
#define PACKAGE_NAME "OpenThread"
#endif

#if !defined(PACKAGE_VERSION)
#define PACKAGE_VERSION "staged-core"
#endif

#include "../third_party/openthread-core/src/core/common/error.cpp"
#include "../third_party/openthread-core/src/core/api/error_api.cpp"
#include "../third_party/openthread-core/src/core/instance/instance.cpp"
#include "../third_party/openthread-core/src/core/api/instance_api.cpp"

extern "C" size_t nrf54l15OpenThreadStageInstanceSize(void) {
  return sizeof(ot::Instance);
}

#endif
