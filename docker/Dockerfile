# ==============================================================================
# OpenJDK 8u RISC-V Port - Cross Compilation & Debug Environment
# Host: Linux arm64 (runs natively on Apple Silicon Mac via Docker/OrbStack)
# Target: Linux riscv64
# ==============================================================================

FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

ARG USE_MIRROR=false
ARG MIRROR_HOST=http://mirrors.ustc.edu.cn

# 1. Enable riscv64 multiarch (supporting official repos and optional mirror fallback)
RUN dpkg --add-architecture riscv64 && \
    HOST_ARCH=$(dpkg --print-architecture) && \
    if [ "$HOST_ARCH" = "amd64" ]; then \
        sed -i "s|deb http|deb [arch=amd64] http|g" /etc/apt/sources.list && \
        echo "deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports focal main restricted universe multiverse" > /etc/apt/sources.list.d/riscv64.list && \
        echo "deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports focal-updates main restricted universe multiverse" >> /etc/apt/sources.list.d/riscv64.list && \
        echo "deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports focal-security main restricted universe multiverse" >> /etc/apt/sources.list.d/riscv64.list; \
    fi && \
    if [ "$USE_MIRROR" = "true" ]; then \
        sed -i "s|http://archive.ubuntu.com/ubuntu|${MIRROR_HOST}/ubuntu|g" /etc/apt/sources.list; \
        sed -i "s|http://security.ubuntu.com/ubuntu|${MIRROR_HOST}/ubuntu|g" /etc/apt/sources.list; \
        sed -i "s|http://ports.ubuntu.com/ubuntu-ports|${MIRROR_HOST}/ubuntu-ports|g" /etc/apt/sources.list 2>/dev/null || true; \
        if [ -f /etc/apt/sources.list.d/riscv64.list ]; then \
            sed -i "s|http://ports.ubuntu.com/ubuntu-ports|${MIRROR_HOST}/ubuntu-ports|g" /etc/apt/sources.list.d/riscv64.list; \
        fi; \
    fi && \
    apt-get -o Acquire::Retries=5 -o Acquire::http::Pipeline-Depth=0 update && \
    apt-get -o Acquire::Retries=5 -o Acquire::http::Pipeline-Depth=0 install -y --no-install-recommends \
        build-essential \
        make \
        autoconf \
        file \
        zip \
        unzip \
        cpio \
        gawk \
        bison \
        flex \
        pkg-config \
        git \
        ca-certificates \
        curl \
        wget \
        vim \
        tree \
        # Host Boot JDK 8
        openjdk-8-jdk-headless \
        # Cross Toolchain for RISC-V 64
        gcc-riscv64-linux-gnu \
        g++-riscv64-linux-gnu \
        binutils-riscv64-linux-gnu \
        # QEMU User Emulator & Multiarch GDB
        qemu-user-static \
        gdb-multiarch \
        # Target riscv64 libraries needed by OpenJDK 8 client/headless build
        libc6-dev:riscv64 \
        linux-libc-dev:riscv64 \
        libx11-dev:riscv64 \
        libxext-dev:riscv64 \
        libxrender-dev:riscv64 \
        libxtst-dev:riscv64 \
        libxt-dev:riscv64 \
        libasound2-dev:riscv64 \
        libcups2-dev:riscv64 \
        libfontconfig1-dev:riscv64 \
        libfreetype6-dev:riscv64 \
        zlib1g-dev:riscv64 \
    && rm -rf /var/lib/apt/lists/*

# 2. Environment variables for cross-compilation
ENV CC=riscv64-linux-gnu-gcc
ENV CXX=riscv64-linux-gnu-g++
ENV AS=riscv64-linux-gnu-as
ENV AR=riscv64-linux-gnu-ar
ENV OBJCOPY=riscv64-linux-gnu-objcopy
ENV STRIP=riscv64-linux-gnu-strip
ENV QEMU_LD_PREFIX=/usr/riscv64-linux-gnu

WORKDIR /workspace

CMD ["/bin/bash"]
