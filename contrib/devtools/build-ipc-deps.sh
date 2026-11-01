#!/usr/bin/env sh
# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
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
mp_number=$(echo "$mp_version" | cut -c2-)
mkdir -p "$ipc_work" "$ipc_prefix"
cd "$ipc_work"
if [ ! -f capnp.tar.gz ]; then
    curl -fL --retry 3 "https://capnproto.org/capnproto-c++-$capnp_version.tar.gz" -o capnp.tar.gz
fi
if [ ! -f multiprocess.tar.gz ]; then
    curl -fL --retry 3 "https://codeload.github.com/bitcoin-core/libmultiprocess/tar.gz/refs/tags/$mp_version" -o multiprocess.tar.gz
fi
printf '%s  %s\n' "$capnp_sha" capnp.tar.gz "$mp_sha" multiprocess.tar.gz | shasum -a 256 -c -
if [ ! -d "capnproto-c++-$capnp_version" ]; then tar xf capnp.tar.gz; fi
if [ ! -d "libmultiprocess-$mp_number" ]; then tar xf multiprocess.tar.gz; fi
cmake -S "capnproto-c++-$capnp_version" -B capnp-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ipc_prefix" \
    -DBUILD_TESTING=OFF -DWITH_OPENSSL=OFF -DWITH_ZLIB=OFF
cmake --build capnp-build -j "$ipc_jobs"
cmake --install capnp-build
cmake -S "libmultiprocess-$mp_number" -B mp-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ipc_prefix" \
    -DCMAKE_PREFIX_PATH="$ipc_prefix"
cmake --build mp-build -j "$ipc_jobs"
cmake --install mp-build
