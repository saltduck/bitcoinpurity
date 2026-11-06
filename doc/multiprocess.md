# Multiprocess Bitcoin

_This document describes usage of the multiprocess feature. For design information, see the [design/multiprocess.md](design/multiprocess.md) file._

## Build Option

On Unix systems, the `-DWITH_MULTIPROCESS=ON` build option can be passed to build the supplemental `bitcoin-node` and `bitcoin-gui` multiprocess executables.

## Debugging

The `-debug=ipc` command line option can be used to see requests and responses between processes.

## Installation

The multiprocess feature requires [Cap'n Proto](https://capnproto.org/) and [libmultiprocess](https://github.com/bitcoin-core/libmultiprocess) as dependencies. A simple way to get started using it without installing these dependencies manually is to use the [depends system](../depends) with the `MULTIPROCESS=1` [dependency option](../depends#dependency-options) passed to make:

```
cd <BITCOIN_SOURCE_DIRECTORY>
make -C depends NO_QT=1 MULTIPROCESS=1
# Set host platform to output of gcc -dumpmachine or clang -dumpmachine or check the depends/ directory for the generated subdirectory name
HOST_PLATFORM="x86_64-pc-linux-gnu"
cmake -B build --toolchain=depends/$HOST_PLATFORM/toolchain.cmake
cmake --build build
build/bin/bitcoin-node -regtest -printtoconsole -debug=ipc
BITCOIND=$(pwd)/build/bin/bitcoin-node build/test/functional/test_runner.py
```

The `cmake` build will pick up settings and library locations from the depends directory, so there is no need to pass `-DWITH_MULTIPROCESS=ON` as a separate flag when using the depends system (it's controlled by the `MULTIPROCESS=1` option).

Alternately, you can install [Cap'n Proto](https://capnproto.org/) and [libmultiprocess](https://github.com/bitcoin-core/libmultiprocess) packages on your system, and just run `cmake -B build -DWITH_MULTIPROCESS=ON` without using the depends system. The `cmake` build will be able to locate the installed packages via [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/). See [Installation](https://github.com/bitcoin-core/libmultiprocess/blob/master/doc/install.md) section of the libmultiprocess readme for install steps. See [build-unix.md](build-unix.md) and [build-osx.md](build-osx.md) for information about installing dependencies in general.

## Usage

`bitcoin-node` is a drop-in replacement for `bitcoind`, and `bitcoin-gui` is a drop-in replacement for `bitcoin-qt`, and there are no differences in use or external behavior between the new and old executables. But internally after [#10102](https://github.com/bitcoin/bitcoin/pull/10102), `bitcoin-gui` will spawn a `bitcoin-node` process to run P2P and RPC code, communicating with it across a socket pair, and `bitcoin-node` will spawn `bitcoin-wallet` to run wallet code, also communicating over a socket pair. This will let node, wallet, and GUI code run in separate address spaces for better isolation, and allow future improvements like being able to start and stop components independently on different machines and environments.
[#19460](https://github.com/bitcoin/bitcoin/pull/19460) also adds a new `bitcoin-node` `-ipcbind` option and a `bitcoind-wallet` `-ipcconnect` option to allow new wallet processes to connect to an existing node process.
And [#19461](https://github.com/bitcoin/bitcoin/pull/19461) adds a new `bitcoin-gui` `-ipcconnect` option to allow new GUI processes to connect to an existing node process.


## Purity integration

Purity restores WITH_MULTIPROCESS as an opt-in option (default OFF). Build the
pinned dependencies in separate build directories without source modifications:

~~~sh
sh contrib/devtools/build-ipc-deps.sh /absolute/ipc-work /absolute/ipc-prefix 3
cmake -S . -B build -DWITH_MULTIPROCESS=ON -DCMAKE_PREFIX_PATH=/absolute/ipc-prefix
cmake --build build --target bitcoin-node bitcoin-gui test_bitcoin
build/bin/test_bitcoin --run_test=ipc_tests,miner_tests
~~~

Include the Qt prefix in CMAKE_PREFIX_PATH when needed. The helper reads existing
versions/checksums from depends/packages and verifies downloaded archives.

The IPC wire contract follows Bitcoin Core v32.1, pinned to commit
`f0de87f040621468b956f01f526a43a010310501`. The dependency helper builds the
unmodified libmultiprocess snapshot from that Core archive and Cap'n Proto 1.2.0.
It does not patch dependency sources. Older Purity/Knots IPC bindings are
incompatible and must be regenerated; there is no parallel legacy endpoint.
The embedded SV2 gateway remains in-process in these executables.

See [Core v32.1 IPC migration](ipc-core32-migration.md) for method changes and
client migration requirements. Run the schema contract check with:

~~~sh
python3 test/util/check_ipc_schema.py
~~~
