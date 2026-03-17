#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script supports Linux only."
    exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "apt-get not found. Supported: Ubuntu/Debian."
    exit 1
fi

if [[ ${EUID} -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        exec sudo "$0" "$@"
    fi
    echo "Run as root (or install sudo)."
    exit 1
fi

PACKAGES=(
    build-essential
    cmake
    pkg-config
    libcriterion-dev
    libseccomp-dev
    linux-libc-dev
    libcap-dev
    libelf-dev
    gdb
    strace
    ltrace
    valgrind
    clang-format
    clang-tidy
    iproute2
    iptables
    nftables
    libmnl-dev
    tcpdump
    wireshark-common
    tshark
    socat
    netcat-openbsd
    dnsutils
    libfuse3-dev
    fuse3
    libbpf-dev
    bpftool
    linux-tools-common
    linux-tools-generic
    docker.io
    docker-compose
    criu
    git
    curl
)

echo "[linbox] apt update"
apt-get update -qq

echo "[linbox] installing packages (${#PACKAGES[@]})"
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${PACKAGES[@]}"

echo "[linbox] done"
