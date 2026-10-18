#!/usr/bin/env bash
# ==============================================================================
# Enter interactive shell in JDK 8u RISC-V build container
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REF_JDK="/Users/wujiahua/code/jdk"

export PATH="$HOME/.docker/bin:$PATH"
IMAGE_NAME="jdk8u-riscv-builder:latest"

EXTRA_MOUNTS=()
if [ -d "${REF_JDK}" ]; then
  EXTRA_MOUNTS+=(-v "${REF_JDK}:/reference_jdk:ro")
fi

echo "==> Entering container shell..."
docker run --rm -it \
  -v "${REPO_ROOT}:/workspace" \
  "${EXTRA_MOUNTS[@]}" \
  -p 1234:1234 \
  --name "jdk8u-riscv-dev" \
  "${IMAGE_NAME}" \
  bash
