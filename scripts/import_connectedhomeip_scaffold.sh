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
    /LICENSE \
    /README.md \
    /src/lib/core/DataModelTypes.h \
    /src/lib/core/CHIPError.cpp \
    /src/lib/core/CHIPError.h \
    /src/lib/core/CHIPKeyIds.cpp \
    /src/lib/core/CHIPKeyIds.h \
    /src/lib/core/CHIPVendorIdentifiers.hpp \
    /src/lib/core/ErrorStr.cpp \
    /src/lib/core/ErrorStr.h \
    /src/lib/core/GroupId.h \
    /src/lib/core/NodeId.h \
    /src/lib/core/PasscodeId.h \
    /src/lib/core/Unchecked.h \
    /src/lib/support/Base64.cpp \
    /src/lib/support/Base64.h \
    /src/lib/support/Base85.cpp \
    /src/lib/support/Base85.h \
    /src/lib/support/BitFlags.h \
    /src/lib/support/BytesToHex.cpp \
    /src/lib/support/BytesToHex.h \
    /src/lib/support/DLLUtil.h \
    /src/lib/support/SafeInt.h \
    /src/lib/support/Span.h \
    /src/lib/support/ThreadOperationalDataset.cpp \
    /src/lib/support/ThreadOperationalDataset.h \
    /src/lib/support/TimeUtils.cpp \
    /src/lib/support/TimeUtils.h \
    /src/lib/support/TypeTraits.h
)

rm -rf "$DEST_BASE"
mkdir -p "$DEST_BASE"

cp "$TMP_DIR/connectedhomeip/LICENSE" "$DEST_BASE/connectedhomeip-LICENSE.txt"
cp "$TMP_DIR/connectedhomeip/README.md" "$DEST_BASE/connectedhomeip-README-upstream.md"

copy_file() {
  local rel="$1"
  mkdir -p "$(dirname "$DEST_BASE/$rel")"
  cp "$TMP_DIR/connectedhomeip/$rel" "$DEST_BASE/$rel"
}

copy_file "src/lib/core/CHIPError.cpp"
copy_file "src/lib/core/CHIPError.h"
copy_file "src/lib/core/CHIPKeyIds.cpp"
copy_file "src/lib/core/CHIPKeyIds.h"
copy_file "src/lib/core/CHIPVendorIdentifiers.hpp"
copy_file "src/lib/core/DataModelTypes.h"
copy_file "src/lib/core/ErrorStr.cpp"
copy_file "src/lib/core/ErrorStr.h"
copy_file "src/lib/core/GroupId.h"
copy_file "src/lib/core/NodeId.h"
copy_file "src/lib/core/PasscodeId.h"
copy_file "src/lib/core/Unchecked.h"
copy_file "src/lib/support/Base64.cpp"
copy_file "src/lib/support/Base64.h"
copy_file "src/lib/support/Base85.cpp"
copy_file "src/lib/support/Base85.h"
copy_file "src/lib/support/BitFlags.h"
copy_file "src/lib/support/BytesToHex.cpp"
copy_file "src/lib/support/BytesToHex.h"
copy_file "src/lib/support/DLLUtil.h"
copy_file "src/lib/support/SafeInt.h"
copy_file "src/lib/support/Span.h"
copy_file "src/lib/support/ThreadOperationalDataset.cpp"
copy_file "src/lib/support/ThreadOperationalDataset.h"
copy_file "src/lib/support/TimeUtils.cpp"
copy_file "src/lib/support/TimeUtils.h"
copy_file "src/lib/support/TypeTraits.h"

cat > "$DEST_BASE/README.intake.txt" <<EOF
This directory is the staged connectedhomeip path for future Matter work.

Current state:

- a minimal upstream header/support/error/key/time/hex/thread-dataset/data-model seed is
  imported from connectedhomeip commit $(cd "$TMP_DIR/connectedhomeip" && git rev-parse HEAD)
- that seed is only large enough for hidden-seam compile smoke against a few
  upstream core headers, staged upstream support implementation units, and
  staged upstream core error / key-id implementation units through repo-owned
  config / CodeUtils shims plus the repo-owned compile-only Matter foundation
  target
- it is not a full upstream scaffold or a commissioned Matter runtime

To refresh this minimal seed, use:

  scripts/import_connectedhomeip_scaffold.sh <connectedhomeip-ref>

The presence of this directory still does not mean a compileable Matter target
exists.
EOF

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
- this stages only the minimal upstream Matter header/support/error/key/time/
  hex/thread-dataset/data-model seed currently exercised by the Arduino
  compile-only foundation seam
- build integration is still a separate step
- current repo runtime still does not claim a commissioned Matter target
EOF
)

echo "Staged connectedhomeip scaffold from ref: $CHIP_REF"
