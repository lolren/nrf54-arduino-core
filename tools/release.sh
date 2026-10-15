#!/usr/bin/env bash
# Canonical Arduino and PlatformIO release-artifact builder.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="$ROOT/hardware/nrf54l15clean/nrf54l15clean/platform.txt"
DIST="$ROOT/dist"

if [[ ${1:-} == --* ]]; then
    VERSION="$(sed -n 's/^version=//p' "$PLATFORM" | head -n 1)"
else
    VERSION="${1:-$(sed -n 's/^version=//p' "$PLATFORM" | head -n 1)}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
fi

if [[ ! $VERSION =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]]; then
    echo "ERROR: version must look like MAJOR.MINOR.PATCH[-PRERELEASE]" >&2
    exit 2
fi

ARGS=(
    --root "$ROOT"
    --version "$VERSION"
    --source-version nrf54l15clean
    --repo-url https://github.com/lolren/nrf54-arduino-core
    --release-base-url 'https://github.com/lolren/nrf54-arduino-core/releases/download/v{version}'
    --host-tools-release-base-url 'https://github.com/lolren/nrf54-arduino-core/releases/download/host-tools-v{host_tool_version}'
)
if [[ ${NRF54_REBUILD_HOSTTOOLS:-0} != 1 ]]; then
    ARGS+=(--reuse-existing-hosttools)
fi

rm -rf "$DIST"
mkdir -p "$DIST"

python3 "$ROOT/scripts/test_core_io_regressions.py"
python3 "$ROOT/scripts/test_saadc_oversampling.py"
python3 "$ROOT/scripts/test_bluefruit_client_contracts.py"
python3 "$ROOT/scripts/test_upload_helper.py"
python3 "$ROOT/scripts/test_release_versions.py"
python3 "$ROOT/scripts/sync_platformio_boards.py" --check
python3 "$ROOT/scripts/test_platformio_integration.py"
python3 "$ROOT/scripts/test_platformio_transport.py"
python3 "$ROOT/scripts/test_platformio_release.py"
python3 "$ROOT/scripts/test_lm20a_pdm_contract.py"
python3 "$ROOT/scripts/test_cracen_ikg_contract.py"
python3 "$ROOT/scripts/test_lm20a_cracen_rng_header.py"
python3 "$ROOT/scripts/build_release.py" "${ARGS[@]}" "$@"

MANIFEST="$ROOT/dist/release-manifest.json"
ARCHIVE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["platform"]["archivePath"])' "$MANIFEST")"
PLATFORMIO_ARCHIVE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["platformio"]["archivePath"])' "$MANIFEST")"
CHANNEL="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["channel"])' "$MANIFEST")"
ARTIFACT_INDEXES="$(
    python3 -c \
        'import json,sys; print(*json.load(open(sys.argv[1]))["indexes"]["artifact"], sep="\n")' \
        "$MANIFEST"
)"

while IFS= read -r INDEX; do
    python3 "$ROOT/scripts/verify_package_index.py" \
        --index "$INDEX" \
        --archive "$ARCHIVE" \
        --version "$VERSION"
done <<< "$ARTIFACT_INDEXES"

for NAME in \
    package_nrf54l15clean_index.json \
    package_nrf54l15clean_stable_index.json \
    package_nrf54l15clean_archive_index.json; do
    cp "$ROOT/dist/$NAME" "$ROOT/$NAME"
done

python3 "$ROOT/scripts/verify_release_archive.py" --archive "$ARCHIVE"
python3 "$ROOT/scripts/verify_platformio_package.py" \
    --archive "$PLATFORMIO_ARCHIVE" --manifest "$MANIFEST"

echo
echo "Release artifacts are ready in $ROOT/dist"
echo "Release channel: $CHANNEL"
echo "Platform archive: $ARCHIVE"
echo "PlatformIO archive: $PLATFORMIO_ARCHIVE"
echo "Review and commit the version/index changes before publishing v$VERSION."
