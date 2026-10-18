#!/usr/bin/env bash
# ==============================================================================
# Run compiled RISC-V java binary using QEMU User Emulator inside container
# Usage: ./docker/qemu-java.sh [java options / args]
# Example: ./docker/qemu-java.sh -version
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export PATH="$HOME/.docker/bin:$PATH"
IMAGE_NAME="jdk8u-riscv-builder:latest"

JAVA_PATH_IN_CONTAINER="/workspace/build/linux-riscv64-normal-server-slowdebug/jdk/bin/java"

echo "==> Running QEMU RISC-V Java..."
docker run --rm \
  -v "${REPO_ROOT}:/workspace" \
  "${IMAGE_NAME}" \
  bash -c "
    if [ ! -f \"${JAVA_PATH_IN_CONTAINER}\" ]; then
      echo 'Error: ${JAVA_PATH_IN_CONTAINER} not found. Please build JDK first!'
      exit 1
    fi
    qemu-riscv64-static -cpu rv64,c=true,v=true -E LD_LIBRARY_PATH=/usr/riscv64-linux-gnu/lib \"${JAVA_PATH_IN_CONTAINER}\" \"\$@\"
  " -- "$@"
