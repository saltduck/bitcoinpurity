#!/usr/bin/env bash
#
# Copyright (c) 2020-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export HOST=i686-pc-linux-gnu
export CONTAINER_NAME=ci_i686_multiprocess
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:24.04"
export CI_IMAGE_PLATFORM="linux/amd64"
export CI_CONTAINER_CAP="--security-opt seccomp=unconfined"
export PACKAGES="llvm clang g++-multilib"
export DEP_OPTS="DEBUG=1 MULTIPROCESS=1"
# GCC Debug builds can exhaust memory when they use every available CPU.
export MAKEJOBS="${MAKEJOBS:--j4}"
export GOAL="install"
export TEST_RUNNER_EXTRA="--v2transport"
# Use the depends compiler for the i686 C++ ABI shared by the node and libkj.
export BITCOIN_CONFIG="\
 -DCMAKE_BUILD_TYPE=Debug \
 -DAPPEND_CPPFLAGS='-DBOOST_MULTI_INDEX_ENABLE_SAFE_MODE' \
"
