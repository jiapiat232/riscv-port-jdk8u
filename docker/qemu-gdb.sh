#!/usr/bin/env bash
# ==============================================================================
# Debug compiled RISC-V java binary with QEMU + gdb-multiarch
# Usage: ./docker/qemu-gdb.sh [java args]
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export PATH="$HOME/.docker/bin:$PATH"
IMAGE_NAME="jdk8u-riscv-builder:latest"

JAVA_PATH_IN_CONTAINER="/workspace/build/linux-riscv64-normal-server-slowdebug/jdk/bin/java"

echo "==> Starting QEMU with GDB stub on port 1234..."
docker run --rm -it \
  -v "${REPO_ROOT}:/workspace" \
  -p 1234:1234 \
  "${IMAGE_NAME}" \
  bash -c "
    if [ ! -f \"${JAVA_PATH_IN_CONTAINER}\" ]; then
      echo 'Error: ${JAVA_PATH_IN_CONTAINER} not found. Please build JDK first!'
      exit 1
    fi
    # Start QEMU in background with gdb server
    qemu-riscv64-static -g 1234 -cpu rv64,c=true,v=true \"${JAVA_PATH_IN_CONTAINER}\" \"\$@\" &
    QEMU_PID=\$!
    echo 'QEMU started (PID: '\$QEMU_PID'). Attaching gdb-multiarch...'
    
    # Launch gdb-multiarch
    gdb-multiarch \
      -ex 'set architecture riscv:rv64' \
      -ex 'target remote localhost:1234' \
      -ex 'set sysroot /usr/riscv64-linux-gnu' \
      \"${JAVA_PATH_IN_CONTAINER}\"
    
    kill -9 \$QEMU_PID 2>/dev/null || true
  " -- "$@"
