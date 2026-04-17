#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_BASE="$REPO_ROOT/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src"
TMP_DIR="$(mktemp -d)"
OT_REF="${1:-main}"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

git clone --depth 1 --branch "$OT_REF" --filter=blob:none --sparse \
  https://github.com/openthread/openthread "$TMP_DIR/openthread"

(
  cd "$TMP_DIR/openthread"
  git sparse-checkout set include examples/platforms
)

rm -rf "$DEST_BASE/openthread"
cp -R "$TMP_DIR/openthread/include/openthread" "$DEST_BASE/"
cp "$TMP_DIR/openthread/examples/platforms/openthread-system.h" "$DEST_BASE/openthread-system.h"
cp "$TMP_DIR/openthread/LICENSE" "$DEST_BASE/openthread-LICENSE.txt"

echo "Imported OpenThread public headers from ref: $OT_REF"
