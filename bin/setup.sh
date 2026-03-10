#!/usr/bin/env bash
#
# LinBox development environment setup (Ubuntu/Debian)
# Idempotent — safe to run multiple times.
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
if [[ "$(uname)" != "Linux" ]]; then
  error "This script is for Linux only."
  exit 1
fi

if ! command -v apt-get &>/dev/null; then
  error "apt-get not found. This script supports Ubuntu/Debian only."
  exit 1
fi

if [[ $EUID -ne 0 ]]; then
  error "Please run as root: sudo $0"
  exit 1
fi

# ---------------------------------------------------------------------------
# Packages
# ---------------------------------------------------------------------------

# Compiler & build
BUILD=(
  build-essential
  cmake
  pkg-config
)

# Kernel & seccomp
KERNEL=(
  libseccomp-dev
  linux-libc-dev
  libcap-dev
  libelf-dev
)

# Debugging
DEBUG=(
  gdb
  strace
  ltrace
  valgrind
)

# Code quality
QUALITY=(
  clang-format
  clang-tidy
)

# Network
NETWORK=(
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
)

# FUSE (virtual filesystem interception)
FUSE=(
  libfuse3-dev
  fuse3
)

# eBPF observability
EBPF=(
  libbpf-dev
  bpftool
  linux-tools-common
  linux-tools-generic
)

# Containers
CONTAINERS=(
  docker.io
  docker-compose
)

# Snapshot/restore
SNAPSHOT=(
  criu
)

# Utilities
UTILS=(
  git
  curl
)

ALL_PACKAGES=(
  "${BUILD[@]}"
  "${KERNEL[@]}"
  "${DEBUG[@]}"
  "${QUALITY[@]}"
  "${NETWORK[@]}"
  "${FUSE[@]}"
  "${EBPF[@]}"
  "${CONTAINERS[@]}"
  "${SNAPSHOT[@]}"
  "${UTILS[@]}"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
info "Updating package index..."
apt-get update -qq

info "Installing ${#ALL_PACKAGES[@]} packages..."
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${ALL_PACKAGES[@]}"

# ---------------------------------------------------------------------------
# Docker post-install: let current (non-root) user run docker without sudo
# ---------------------------------------------------------------------------
SUDO_USER="${SUDO_USER:-}"
if [[ -n "$SUDO_USER" ]]; then
  if ! id -nG "$SUDO_USER" | grep -qw docker; then
    info "Adding $SUDO_USER to docker group..."
    usermod -aG docker "$SUDO_USER"
    warn "Log out and back in (or run 'newgrp docker') for group changes to take effect."
  fi
fi

# ---------------------------------------------------------------------------
# Verify key tools
# ---------------------------------------------------------------------------
info "Verifying installation..."
FAILED=0
for cmd in gcc cmake pkg-config gdb strace clang-format clang-tidy docker git curl; do
  if ! command -v "$cmd" &>/dev/null; then
    error "  $cmd — NOT FOUND"
    FAILED=1
  fi
done

if [[ $FAILED -eq 1 ]]; then
  error "Some tools failed to install. Check output above."
  exit 1
fi

info "All done. Development environment is ready."
