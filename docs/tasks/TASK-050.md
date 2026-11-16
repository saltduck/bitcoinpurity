# TASK-050: Restore optional IPC builds

Restore the optional build on the current Purity branch. Modify project sources
only. See [build guide](../../doc/multiprocess.md).

## Concrete failures and changes

1. CMake overwrote WITH_MULTIPROCESS=ON with OFF and generated no bitcoin-node
   target. Restored the default-OFF option and target summary.
2. libmultiprocess v5.0 FunctionTraits cannot wrap const member getters; its
   generated proxy cannot implement reference-return template getters.
   Adopted Core v32.1's non-const, owned-snapshot C++ getter shape and adjusted
   the template JSON renderer.
3. Generated Mining and BlockTemplate proxies remained abstract because the
   schema omitted createNewBlock2 and getTxCoinAgePriorities. Added new ordinals
   and all full assembler options, including size, script, fee and validation.
4. macOS exposed unnecessary miner/validation header dependencies in the codec
   and a missing Boost header dependency in bitcoin_ipc_test. Used lightweight
   consensus headers and explicit Boost::headers.
5. Actual GUI startup then exposed Node miner not found: BitcoinGuiInit omitted
   makeMining(). Added the same mining factory used by the single-process GUI.
   The new feature_ipc_gui test failed before the fix and passes after it.

All existing method/parameter ordinals remain unchanged. New fields have
defaults matching C++. Raw script bytes remain raw (no CompactSize prefix).
A leading Context is used for new createNewBlock2. Appending Context after
legacy options loses return propagation with v5; moving it to the front would
renumber the old field. Neither change was shipped: legacy @4 stays unchanged.

## Tests and acceptance

- Red: requesting IPC initially generated no bitcoin-node target; after enabling
  the option, the concrete proxy/traits/header compiler failures reproduced.
- Linux arm64/GCC 13 and macOS arm64/AppleClang 21: bitcoin-node, bitcoin-gui,
  test_bitcoin and Qt targets compile with IPC and SV2 enabled.
- Eleven IPC/mining unit cases pass on both platforms. New cases cover all
  options, old payload defaults, actual template getters/metadata and solved
  block submission over IPC, with local chain-tip verification.
- Linux/macOS Qt tests pass; macOS retains the existing minimal-platform
  IntroTests skip. The separate actual GUI regression uses offscreen and passes.
- Linux bitcoin-node passes rpc_misc (actual spawned IPC process), mining_basic,
  GBT longpoll, full external-PoW SV2 acceptance and DATUM regression.
- Native macOS bitcoin-node passes rpc_misc including spawned-process echoipc.
- Native macOS daemon/Qt build with IPC/SV2/DATUM disabled passes.
- Existing Cap'n Proto 1.2.0 and libmultiprocess v5.0 archive pins are unchanged.
  All 323 and 60 original source files respectively match the archives after
  Linux and macOS out-of-source builds. No dependency source changes.
- Dependency helper passes on Linux; updated workflow passes actionlint 1.7.12.
  Hosted CI and native Windows IPC are not claimed.

The new GUI functional test passes on Linux/macOS: real child-process echoipc,
PoW block generation, template RPC and clean shutdown. It explicitly uses the
offscreen backend (the macOS minimal backend cannot load application fonts).
It preconfigures the sync wizard in an isolated temporary data/settings path.
The functional runner and Unix CI now include this regression. The build helper
passes ShellCheck; no dependencies are patched.
