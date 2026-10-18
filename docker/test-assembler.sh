#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export PATH="$HOME/.docker/bin:$PATH"
IMAGE_NAME="jdk8u-riscv-builder:latest"

echo "==> Running RISC-V Assembler Differential TDD Test inside container..."
docker run --rm \
  -v "${REPO_ROOT}:/workspace" \
  "${IMAGE_NAME}" \
  bash -c "
    set -euo pipefail
    cd /workspace
    riscv64-linux-gnu-g++ -O2 /workspace/docker/test_assembler.cpp -o /tmp/test_assembler
    qemu-riscv64-static -cpu rv64 /tmp/test_assembler
  "
