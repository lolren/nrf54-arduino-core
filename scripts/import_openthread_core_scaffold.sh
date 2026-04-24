#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_BASE="$REPO_ROOT/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core"
TMP_DIR="$(mktemp -d)"
OT_REF="${1:-254043deece3b8b372659dc2b79b84fa923483b8}"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

git clone --no-checkout --filter=blob:none --sparse \
  https://github.com/openthread/openthread "$TMP_DIR/openthread"

(
  cd "$TMP_DIR/openthread"
  git fetch --depth 1 origin "$OT_REF"
  git checkout --detach FETCH_HEAD
  git sparse-checkout set --no-cone \
    /src/core \
    /src/include \
    /third_party/mbedtls \
    /LICENSE \
    /README.md
)

rm -rf "$DEST_BASE"
mkdir -p "$DEST_BASE"

mkdir -p "$DEST_BASE/src" "$DEST_BASE/third_party"
cp -R "$TMP_DIR/openthread/src/core" "$DEST_BASE/src/"
cp -R "$TMP_DIR/openthread/src/include" "$DEST_BASE/src/"
cp -R "$TMP_DIR/openthread/third_party/mbedtls" "$DEST_BASE/third_party/"
find "$DEST_BASE" -type f \( -name "BUILD.gn" -o -name "CMakeLists.txt" -o -name "*.cmake" \) -delete
cp "$TMP_DIR/openthread/LICENSE" "$DEST_BASE/openthread-LICENSE.txt"
cp "$TMP_DIR/openthread/README.md" "$DEST_BASE/openthread-README-upstream.md"

(
  cd "$TMP_DIR/openthread"
  cat > "$DEST_BASE/IMPORT_INFO.txt" <<EOF
OpenThread core scaffold staged for future Arduino integration.

Source repository:
- https://github.com/openthread/openthread

Requested ref:
- $OT_REF

Resolved commit:
- $(git rev-parse HEAD)

Scope note:
- this stages only the embedded OpenThread core sources needed for future
  in-process nRF54 Thread/Matter work
- POSIX, NCP/RCP, CLI, build-system metadata, simulation/platform examples,
  spinel/HDLC, J-Link RTT, and tcplp are intentionally omitted
- build integration is still a separate step
- public API headers live in src/openthread and remain the Arduino-facing
  include surface until the full core is wired into the build
EOF
)

echo "Staged OpenThread core scaffold from ref: $OT_REF"
