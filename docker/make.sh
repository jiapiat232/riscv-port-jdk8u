#!/usr/bin/env bash
# ==============================================================================
# Run make inside container
# Usage: ./docker/make.sh [target] (e.g. hotspot, images)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export PATH="$HOME/.docker/bin:$PATH"
IMAGE_NAME="jdk8u-riscv-builder:latest"

TARGET="${1:-images}"

echo "==> Running make ${TARGET} inside container..."
docker run --rm \
  -v "${REPO_ROOT}:/workspace" \
  "${IMAGE_NAME}" \
  bash -c "
    set -euo pipefail
    cd /workspace
    make ${TARGET} LOG=info
  "
