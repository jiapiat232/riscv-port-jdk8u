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

if [ -f "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/hotspot/linux_riscv64_compiler2/debug/libjvm.so" ]; then
  if [ -d "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/jdk/lib/riscv64/server" ]; then
    cp -f "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/hotspot/linux_riscv64_compiler2/debug/libjvm.so" \
          "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/jdk/lib/riscv64/server/libjvm.so"
  fi
  if [ -d "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/images/j2sdk-image/jre/lib/riscv64/server" ]; then
    cp -f "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/hotspot/linux_riscv64_compiler2/debug/libjvm.so" \
          "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/images/j2sdk-image/jre/lib/riscv64/server/libjvm.so"
  fi
fi

if [ -f "${REPO_ROOT}/build/linux-riscv64-normal-server-slowdebug/images/j2sdk-image/bin/java" ]; then
  JAVA_PATH_IN_CONTAINER="/workspace/build/linux-riscv64-normal-server-slowdebug/images/j2sdk-image/bin/java"
else
  JAVA_PATH_IN_CONTAINER="/workspace/build/linux-riscv64-normal-server-slowdebug/jdk/bin/java"
fi

QEMU_BIN="qemu-riscv64-static"
if [ -x "${REPO_ROOT}/docker/bin/qemu-riscv64-static" ]; then
  QEMU_BIN="/workspace/docker/bin/qemu-riscv64-static"
fi

echo "==> Running QEMU RISC-V Java..."
docker run --rm \
  -v "${REPO_ROOT}:/workspace" \
  "${IMAGE_NAME}" \
  bash -c "
    if [ ! -f \"${JAVA_PATH_IN_CONTAINER}\" ]; then
      echo 'Error: ${JAVA_PATH_IN_CONTAINER} not found. Please build JDK first!'
      exit 1
    fi
    trap 'cp /tmp/hs_err*.log /workspace/ 2>/dev/null || true' EXIT
    \"${QEMU_BIN}\" -cpu rv64 -E LD_LIBRARY_PATH=/usr/riscv64-linux-gnu/lib \"${JAVA_PATH_IN_CONTAINER}\" \"\$@\"
  " -- "$@"
