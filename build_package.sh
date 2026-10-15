#!/bin/bash
set -euo pipefail

# Set GIT_DESCRIBE_* variables that rattler-build recipe expects
export GIT_DESCRIBE_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "0.0.0")
export GIT_DESCRIBE_NUMBER=$(git describe --tags --long 2>/dev/null | sed 's/.*-\([0-9]*\)-.*/\1/' || echo "0")
export GIT_DESCRIBE_HASH=$(git describe --tags --long 2>/dev/null | sed 's/.*-g\(.*\)/\1/' || echo "unknown")

echo "Building version: ${GIT_DESCRIBE_TAG} (${GIT_DESCRIBE_NUMBER}_${GIT_DESCRIBE_HASH})"

OUTPUT_DIR="${1:-${HOME}/outdir}"
mkdir -p "${OUTPUT_DIR}"

# EXTRA_CHANNEL (e.g. ga-fdp/label/staging) lets the build and its tests
# resolve a sibling package that is staged but not yet blessed.
CHANNEL_FLAGS=""
if [ -n "${EXTRA_CHANNEL:-}" ]; then
    CHANNEL_FLAGS="--channel ${EXTRA_CHANNEL}"
fi

rattler-build build --recipe recipe/ \
  ${CHANNEL_FLAGS} \
  --channel ga-fdp \
  --channel conda-forge \
  --output-dir "${OUTPUT_DIR}"
