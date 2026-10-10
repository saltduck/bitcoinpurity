# Official package manifest generation

`contrib/official-packages/generate-packages-manifest.py` is an offline operator
tool using only Python's standard library.
It reads ZIP central-directory entries and detects the common prefix around
`blocks/` and `chainstate/`, including archives with enclosing directories that
the Qt downloader already supports. macOS resource-fork entries are ignored
for root detection; inconsistent or multiple data roots are rejected. It reads
`bitcoinpurity-package.json` at that data root without extracting node data.
Metadata is combined with explicit CLI values;
conflicts, duplicate JSON keys, invalid field values, unsafe/duplicate entries,
unsupported compression, encryption and missing node-data paths are rejected.

The final ZIP is read in 1 MiB chunks to compute its SHA256. Download size is
the ZIP byte length; extracted size is the sum of uncompressed file sizes from
the central directory. This supports ZIP64 and does not require a node process
or LevelDB bindings. The operator supplies the actual mainnet snapshot metadata
when absent; the tool cannot determine or validate chainstate height/hash.

An unsigned single-package catalog containing only `packages` is written to
an exclusively created destination, preserving existing files. The generator
accepts no private keys and invokes no subprocesses or signing tools. No
network requests or client trust-policy changes are introduced. See
[CLI contract](../api/official-packages.md).
