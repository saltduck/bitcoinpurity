#!/bin/sh
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
export LC_ALL=C
set -eu
if [ "$#" -ne 3 ]; then
    echo "Usage: sh $0 ABSOLUTE_WORK_DIR ABSOLUTE_PREFIX JOBS" >&2
    exit 1
fi
ipc_root=$(cd "$(dirname "$0")/../.." && pwd)
ipc_work=$1
ipc_prefix=$2
ipc_jobs=$3
capnp_version=$(awk -F= '/_version=/ {print $2; exit}' "$ipc_root/depends/packages/native_capnp.mk")
capnp_sha=$(awk -F= '/_sha256_hash=/ {print $2; exit}' "$ipc_root/depends/packages/native_capnp.mk")
mp_version=$(awk -F= '/_version=/ {print $2; exit}' "$ipc_root/depends/packages/native_libmultiprocess.mk")
mp_sha=$(awk -F= '/_sha256_hash=/ {print $2; exit}' "$ipc_root/depends/packages/native_libmultiprocess.mk")
mp_source="bitcoin-$mp_version/src/ipc/libmultiprocess"
mkdir -p "$ipc_work" "$ipc_prefix"
cd "$ipc_work"
if [ ! -f capnp.tar.gz ]; then
    curl -fL --retry 3 "https://capnproto.org/capnproto-c++-$capnp_version.tar.gz" -o capnp.tar.gz
fi
if [ ! -f "multiprocess-$mp_version.tar.gz" ]; then
    curl -fL --retry 3 "https://codeload.github.com/bitcoin/bitcoin/tar.gz/$mp_version" -o "multiprocess-$mp_version.tar.gz"
fi
printf '%s  %s\n' "$capnp_sha" capnp.tar.gz "$mp_sha" "multiprocess-$mp_version.tar.gz" | shasum -a 256 -c -
# Apply the same Cap'n Proto patches as depends/packages/capnp.mk. The marker
# file keeps a tree that was extracted but not fully patched from being reused.
capnp_source="capnproto-c++-$capnp_version"
if [ ! -f "$capnp_source/.ipc-deps-patched" ]; then
    rm -rf "$capnp_source"
    tar xf capnp.tar.gz
    awk -F= '/^[^ ]*_patches[ +]*=/ {n = split($2, p, " "); for (i = 1; i <= n; i++) print p[i]}' \
        "$ipc_root/depends/packages/capnp.mk" | while read -r capnp_patch; do
        patch -d "$capnp_source" -p2 < "$ipc_root/depends/patches/capnp/$capnp_patch"
    done
    touch "$capnp_source/.ipc-deps-patched"
fi
if [ ! -d "$mp_source" ]; then tar xf "multiprocess-$mp_version.tar.gz"; fi
cmake -S "capnproto-c++-$capnp_version" -B capnp-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ipc_prefix" \
    -DBUILD_TESTING=OFF -DWITH_OPENSSL=OFF -DWITH_ZLIB=OFF
cmake --build capnp-build -j "$ipc_jobs"
cmake --install capnp-build
cmake -S "$mp_source" -B "mp-build-$mp_version" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ipc_prefix" \
    -DCMAKE_PREFIX_PATH="$ipc_prefix"
cmake --build "mp-build-$mp_version" -j "$ipc_jobs"
cmake --install "mp-build-$mp_version"
