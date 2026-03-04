# Development Environment

## Linux (Ubuntu/Debian)

```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  libseccomp-dev \
  linux-libc-dev \
  libcap-dev \
  libelf-dev \
  gdb \
  strace \
  ltrace \
  valgrind \
  clang-format \
  clang-tidy \
  iproute2 \
  iptables \
  nftables \
  libmnl-dev \
  tcpdump \
  wireshark-common \
  tshark \
  socat \
  netcat-openbsd \
  dnsutils \
  libfuse3-dev \
  fuse3 \
  libbpf-dev \
  bpftool \
  linux-tools-common \
  linux-tools-generic \
  docker.io \
  docker-compose \
  criu \
  git \
  curl
```

### By category

| Category | Packages | Purpose |
|----------|----------|---------|
| **Compiler & build** | `build-essential`, `cmake`, `pkg-config` | gcc, make, libc-dev, build system |
| **Kernel & seccomp** | `libseccomp-dev`, `linux-libc-dev`, `libcap-dev`, `libelf-dev` | seccomp-bpf API, syscall headers, capabilities, ELF parsing |
| **Debugging** | `gdb`, `strace`, `ltrace`, `valgrind` | debugger, syscall tracing, library call tracing, memory leaks |
| **Code quality** | `clang-format`, `clang-tidy` | formatting, static analysis |
| **Network** | `iproute2`, `iptables`, `nftables`, `libmnl-dev`, `tcpdump`, `tshark`, `socat`, `netcat-openbsd`, `dnsutils` | tc/netem latency simulation, filtering, sniffing, testing |
| **FUSE** | `libfuse3-dev`, `fuse3` | virtual `/dev/urandom`, filesystem interception |
| **eBPF** | `libbpf-dev`, `bpftool`, `linux-tools-common`, `linux-tools-generic` | eBPF observability, syscall monitoring |
| **Containers** | `docker.io`, `docker-compose` | sandbox containers with LD_PRELOAD |
| **Snapshot/restore** | `criu` | LinBox snapshot/restore (CRIU-style) |
| **Utilities** | `git`, `curl` | basics |

### Which stories need what

| Packages | First needed in |
|----------|-----------------|
| `build-essential`, `cmake`, `pkg-config`, `clang-format`, `clang-tidy` | S01 (scaffold) |
| `libseccomp-dev`, `linux-libc-dev`, `libcap-dev`, `libelf-dev` | S01 (shim headers), S04 (seccomp) |
| `gdb`, `strace`, `ltrace`, `valgrind` | S01 (debugging from day one) |
| `iproute2`, `iptables`, `nftables`, `libmnl-dev` | S02 (network control) |
| `tcpdump`, `tshark`, `socat`, `netcat-openbsd`, `dnsutils` | S02 (network debugging) |
| `libfuse3-dev`, `fuse3` | S03 (virtual /dev/urandom) |
| `libbpf-dev`, `bpftool`, `linux-tools-common`, `linux-tools-generic` | S04 (eBPF observability) |
| `docker.io`, `docker-compose` | S06 (container integration) |
| `criu` | Future (snapshot/restore) |

## macOS

TBD — cross-compilation or dev container setup.

---

[← Back](README.md)
