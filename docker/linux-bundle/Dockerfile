# syntax=docker/dockerfile:1
#
# Self-contained Beebium Linux server bundle.
#
# Builds the server executables with gRPC/protobuf (and abseil, re2, c-ares,
# openssl, zlib, upb) statically linked via vcpkg, installs the relocatable
# /opt-style tree, proves it runs in this build-floor image, and emits a
# distributable tarball.
#
# The build floor is debian:bookworm (glibc 2.36) on purpose: a bundle linked
# against an older glibc runs on any newer-glibc distro, so one artifact covers
# Ubuntu 22.04+, Raspberry Pi OS (Bookworm), and Arch (glibc 2.43). Building
# natively on the target distro would invert that and break portability.
#
# Per-arch build (run arm64 natively on Apple Silicon; amd64 is emulated):
#   docker buildx build --platform linux/arm64 \
#       -f docker/linux-bundle/Dockerfile \
#       --build-arg VCPKG_TRIPLET=arm64-linux-static \
#       --target artifact --output type=local,dest=./_artifacts .

ARG BASE=debian:bookworm
FROM ${BASE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
# Required for vcpkg on non-x64 Linux: vcpkg bundles prebuilt cmake/ninja only
# for x64, so on arm64 it must fall back to the system-provided tools.
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

# Toolchain + vcpkg host-tool prerequisites + libuuid (the b2-derived 6502
# shared lib links rt+uuid on Linux; rt is in glibc, uuid needs uuid-dev).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config \
        autoconf automake autoconf-archive libtool m4 \
        python3 perl \
        linux-libc-dev uuid-dev \
    && rm -rf /var/lib/apt/lists/*

# vcpkg pinned to the SAME commit the macOS CI uses (lukka/run-vcpkg
# vcpkgGitCommitId, == vcpkg.json builtin-baseline) so the Linux bundle links
# the identical gRPC/protobuf/abseil versions as the macOS build, and so the
# image is reproducible (a HEAD clone would drift to newer ports over time).
ENV VCPKG_ROOT=/opt/vcpkg
ARG VCPKG_COMMIT=77df67cfff9c12ccfdb52284e07c87c75092f723
RUN git clone https://github.com/microsoft/vcpkg "${VCPKG_ROOT}" \
    && git -C "${VCPKG_ROOT}" checkout "${VCPKG_COMMIT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
ARG VCPKG_TRIPLET=arm64-linux-static

# Install the vcpkg dependencies FIRST, from just the manifest + triplets, so
# this (expensive) layer is keyed only on vcpkg.json/triplets and is reused
# across source-only changes. The cache mount persists vcpkg's built binary
# archives across builds, so even a manifest change rebuilds quickly. Mirrors
# the CI split: install here, then configure with VCPKG_MANIFEST_INSTALL=OFF.
COPY vcpkg.json /src/vcpkg.json
COPY triplets /src/triplets
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    "${VCPKG_ROOT}/vcpkg" install \
        --triplet "${VCPKG_TRIPLET}" \
        --overlay-triplets=/src/triplets \
        --x-install-root=/src/vcpkg_installed

# Now bring in the full source and build only the servers + plugins (the
# beebium-servers aggregate skips the ~65 Catch2 test executables).
# BEEBIUM_BUILD_TESTS stays ON purely so the install_smoke CTest is registered.
COPY . /src
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_INSTALLED_DIR=/src/vcpkg_installed \
        -DVCPKG_OVERLAY_TRIPLETS=/src/triplets \
        -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
        -DVCPKG_MANIFEST_INSTALL=OFF \
        -DBEEBIUM_BUILD_TESTS=ON \
    && cmake --build build --target beebium-servers -j"$(nproc)"

# Prove the freshly built tree installs and boots in the build-floor image
# (the in-tree CTest), then install the relocatable bundle, validate the
# *installed* tree directly (self-containment + boot), and emit the tarball.
# The tarball is rooted at opt/beebium so it extracts cleanly to /opt.
RUN ctest --test-dir build -R install_smoke --output-on-failure \
    && cmake --install build --prefix /stage/opt/beebium \
    && bash scripts/smoke-installed-tree.sh /stage/opt/beebium \
    && tar -C /stage -czf /beebium-linux-bundle.tgz opt/beebium

# Minimal stage whose only content is the tarball, for `buildx --output`.
FROM scratch AS artifact
COPY --from=builder /beebium-linux-bundle.tgz /
