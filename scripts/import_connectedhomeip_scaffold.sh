#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $(basename "$0") <connectedhomeip-ref>" >&2
  exit 64
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_BASE="$REPO_ROOT/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip"
TMP_DIR="$(mktemp -d)"
CHIP_REF="$1"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

git clone --no-checkout --filter=blob:none --sparse \
  https://github.com/project-chip/connectedhomeip \
  "$TMP_DIR/connectedhomeip"

(
  cd "$TMP_DIR/connectedhomeip"
  git fetch --depth 1 origin "$CHIP_REF"
  git checkout --detach FETCH_HEAD
  git sparse-checkout set --no-cone \
    /BUILD.gn \
    /LICENSE \
    /README.md \
    /build \
    /build_overrides \
    /config \
    /credentials \
    /data_model \
    /docs \
    /examples \
    /scripts \
    /src \
    /third_party \
    /zzz_generated
)

rm -rf "$DEST_BASE"
mkdir -p "$DEST_BASE"

cp "$TMP_DIR/connectedhomeip/BUILD.gn" "$DEST_BASE/"
cp "$TMP_DIR/connectedhomeip/LICENSE" "$DEST_BASE/connectedhomeip-LICENSE.txt"
cp "$TMP_DIR/connectedhomeip/README.md" "$DEST_BASE/connectedhomeip-README-upstream.md"
cp -R "$TMP_DIR/connectedhomeip/build" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/build_overrides" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/config" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/credentials" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/data_model" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/docs" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/examples" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/scripts" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/src" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/third_party" "$DEST_BASE/"
cp -R "$TMP_DIR/connectedhomeip/zzz_generated" "$DEST_BASE/"

(
  cd "$TMP_DIR/connectedhomeip"
  cat > "$DEST_BASE/IMPORT_INFO.txt" <<EOF
connectedhomeip scaffold staged for future Arduino Matter integration.

Source repository:
- https://github.com/project-chip/connectedhomeip

Requested ref:
- $CHIP_REF

Resolved commit:
- $(git rev-parse HEAD)

Scope note:
- this stages upstream Matter sources and support directories under
  third_party/connectedhomeip
- build integration is still a separate step
- current repo runtime still does not claim a compileable Matter target
EOF
)

echo "Staged connectedhomeip scaffold from ref: $CHIP_REF"
