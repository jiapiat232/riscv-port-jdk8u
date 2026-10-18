#!/usr/bin/env bash
# ==============================================================================
# Run OpenJDK 8 configure inside container
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export PATH="$HOME/.docker/bin:$PATH"
IMAGE_NAME="jdk8u-riscv-builder:latest"

DEBUG_LEVEL="${1:-slowdebug}"

echo "==> Running configure inside container (debug level: ${DEBUG_LEVEL})..."
docker run --rm \
  -v "${REPO_ROOT}:/workspace" \
  "${IMAGE_NAME}" \
  bash -c "
    set -euo pipefail
    BOOT_JDK=$(ls -d /usr/lib/jvm/java-8-openjdk-* 2>/dev/null | head -n 1)
    bash ./configure \
      --openjdk-target=riscv64-linux-gnu \
      --with-boot-jdk="${BOOT_JDK}" \
      --with-debug-level=${DEBUG_LEVEL} \
      --disable-headful \
      --with-jvm-variants=server \
      --with-extra-cflags='-Wno-error' \
      --with-extra-cxxflags='-Wno-error' \
      CC=riscv64-linux-gnu-gcc \
      CXX=riscv64-linux-gnu-g++ \
      CPP=riscv64-linux-gnu-cpp
  "
