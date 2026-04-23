#pragma once

// The staged OpenThread seam only needs the version macro for the current
// compile-only instance/API slice. The full mbedTLS checkout is not yet
// materialized in this repo import.
#define MBEDTLS_VERSION_NUMBER 0x03060000
