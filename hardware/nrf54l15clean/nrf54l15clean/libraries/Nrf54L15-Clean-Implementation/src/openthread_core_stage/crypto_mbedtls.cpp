#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
#include "../../third_party/openthread-core/src/core/crypto/mbedtls.hpp"

#include <openthread/platform/crypto.h>

namespace ot {
namespace Crypto {

MbedTls::MbedTls(void) {}

Error MbedTls::MapError(int aMbedTlsError) {
  return (aMbedTlsError == 0) ? kErrorNone : kErrorFailed;
}

#if OPENTHREAD_FTD || OPENTHREAD_MTD
int MbedTls::CryptoSecurePrng(void*, unsigned char* aBuffer, size_t aSize) {
  return (otPlatCryptoRandomGet(aBuffer, static_cast<uint16_t>(aSize)) ==
          OT_ERROR_NONE)
             ? 0
             : -1;
}
#endif

}  // namespace Crypto
}  // namespace ot
#endif
