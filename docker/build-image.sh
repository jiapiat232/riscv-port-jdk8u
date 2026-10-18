#!/usr/bin/env bash
# ==============================================================================
# Build Docker image for JDK 8u RISC-V development
#
# 3-Tier Intelligent Network Strategy:
#   Tier 1: Probe official repo (ports.ubuntu.com) directly. If OK -> use official repo directly.
#   Tier 2: If direct connection fails -> probe local/corporate proxy (or ask user). If OK -> use official repo + proxy.
#   Tier 3: If no proxy available or proxy fails -> automatically fallback to domestic mirror (USTC).
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$HOME/.docker/bin:$PATH"

IMAGE_NAME="jdk8u-riscv-builder:latest"
BUILD_ARGS=()

TEST_URL="http://ports.ubuntu.com"

probe_connection() {
  local target_url="$1"
  local proxy_url="${2:-}"
  if [ -n "${proxy_url}" ]; then
    curl -sI --connect-timeout 2 -x "${proxy_url}" "${target_url}" >/dev/null 2>&1
  else
    curl -sI --connect-timeout 2 "${target_url}" >/dev/null 2>&1
  fi
}

echo "==> 正在探测网络连接状态..."

# ------------------------------------------------------------------------------
# Tier 1: Probe direct connection
# ------------------------------------------------------------------------------
if probe_connection "${TEST_URL}"; then
  echo "==> [Tier 1 命中] 官方源直连畅通，使用官方源直接构建（无需代理，无需镜像）"
  BUILD_ARGS+=(--build-arg "USE_MIRROR=false")

else
  echo "==> [Tier 1 直连超时] 无法直连官方源，进入 Tier 2 代理探测..."

  # ----------------------------------------------------------------------------
  # Tier 2: Check for proxy
  # ----------------------------------------------------------------------------
  PROXY_CANDIDATE="${PROXY:-${http_proxy:-${HTTP_PROXY:-}}}"
  DOCKER_PROXY=""

  # Auto-detect on macOS (Clash Verge 7897, Clash 7890, etc.)
  if [ -z "${PROXY_CANDIDATE}" ] && [ "$(uname -s)" = "Darwin" ]; then
    for port in 7897 7890 1087 10808; do
      if nc -z 127.0.0.1 "${port}" 2>/dev/null; then
        PROXY_CANDIDATE="http://127.0.0.1:${port}"
        DOCKER_PROXY="http://host.docker.internal:${port}"
        echo "==> 自动嗅探到本地代理端口: ${port}"
        break
      fi
    done
  fi

  # If in an interactive terminal and no proxy found, ask the user
  if [ -z "${PROXY_CANDIDATE}" ] && [ -t 0 ]; then
    echo "------------------------------------------------------------------"
    echo "提示: 检测到无法直连 Ubuntu 官方源。"
    read -r -p "请问是否有本地/企业代理？(例如 http://127.0.0.1:7897，直接回车跳过使用国内镜像): " INPUT_PROXY
    echo "------------------------------------------------------------------"
    if [ -n "${INPUT_PROXY}" ]; then
      PROXY_CANDIDATE="${INPUT_PROXY}"
      if [ "$(uname -s)" = "Darwin" ]; then
        # Map loopback address to host.docker.internal for Mac container
        DOCKER_PROXY=$(echo "${INPUT_PROXY}" | sed -E 's/(127\.0\.0\.1|localhost)/host.docker.internal/')
      else
        DOCKER_PROXY="${INPUT_PROXY}"
      fi
    fi
  fi

  # If proxy candidate exists, test if it actually works
  PROXY_WORKS=false
  if [ -n "${PROXY_CANDIDATE}" ]; then
    if [ -z "${DOCKER_PROXY}" ]; then
      if [ "$(uname -s)" = "Darwin" ]; then
        DOCKER_PROXY=$(echo "${PROXY_CANDIDATE}" | sed -E 's/(127\.0\.0\.1|localhost)/host.docker.internal/')
      else
        DOCKER_PROXY="${PROXY_CANDIDATE}"
      fi
    fi

    echo "==> 正在验证代理连接: ${PROXY_CANDIDATE} ..."
    if probe_connection "${TEST_URL}" "${PROXY_CANDIDATE}"; then
      PROXY_WORKS=true
      echo "==> [Tier 2 命中] 代理验证通过！使用官方源 + 代理进行构建 (容器代理地址: ${DOCKER_PROXY})"
      BUILD_ARGS+=(
        --build-arg "USE_MIRROR=false"
        --build-arg "HTTP_PROXY=${DOCKER_PROXY}"
        --build-arg "HTTPS_PROXY=${DOCKER_PROXY}"
        --build-arg "http_proxy=${DOCKER_PROXY}"
        --build-arg "https_proxy=${DOCKER_PROXY}"
      )
    else
      echo "==> 警告: 提供的代理无法连通 ${TEST_URL}，将放弃代理。"
    fi
  fi

  # ----------------------------------------------------------------------------
  # Tier 3: Fallback to domestic mirror
  # ----------------------------------------------------------------------------
  if [ "${PROXY_WORKS}" = false ]; then
    echo "==> [Tier 3 命中] 未提供可用代理，自动启用国内高可用镜像源（USTC 中科大镜像）加速构建"
    BUILD_ARGS+=(--build-arg "USE_MIRROR=true")
  fi
fi

# ------------------------------------------------------------------------------
# Build Docker Image
# ------------------------------------------------------------------------------
echo "==> 正在构建 Docker 镜像: ${IMAGE_NAME} ..."
docker build "${BUILD_ARGS[@]}" -t "${IMAGE_NAME}" -f "${SCRIPT_DIR}/Dockerfile" "${SCRIPT_DIR}"
echo "==> Docker 镜像构建成功: ${IMAGE_NAME}"
