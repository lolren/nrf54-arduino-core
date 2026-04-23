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
    /include \
    /src \
    /third_party \
    /examples/platforms \
    /etc/cmake \
    /LICENSE \
    /README.md
)

rm -rf "$DEST_BASE"
mkdir -p "$DEST_BASE"

cp -R "$TMP_DIR/openthread/include" "$DEST_BASE/"
cp -R "$TMP_DIR/openthread/src" "$DEST_BASE/"
cp -R "$TMP_DIR/openthread/third_party" "$DEST_BASE/"
mkdir -p "$DEST_BASE/examples"
cp -R "$TMP_DIR/openthread/examples/platforms" "$DEST_BASE/examples/"
mkdir -p "$DEST_BASE/etc"
cp -R "$TMP_DIR/openthread/etc/cmake" "$DEST_BASE/etc/"
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
- this stages upstream core sources and supporting directories under
  third_party/openthread-core
- build integration is still a separate step
- current repo runtime is still public-headers + PAL only until those sources
  are wired into the Arduino build
EOF
)

echo "Staged OpenThread core scaffold from ref: $OT_REF"
