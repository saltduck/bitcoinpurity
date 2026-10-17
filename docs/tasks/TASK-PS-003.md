# TASK-PS-003: Signet ASERT numeric compatibility

Stage: S3. Status: calculator, local boundary tests, UBSan and cross-platform numeric comparison passed.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Evidence:

- Red: signet_asert_activation_uses_wide_limit aborted on the original 224-bit
  assertion (build/signet-asert-red.log).
- Green: 43 pow/chainparams/transaction cases passed, including 13 independent
  integer vectors, 63 mainnet-domain comparisons and 239-bit boundaries.
- GetNextWorkRequired selects the new calculator only for activated Signet.
- Original CalculateASERT/GetNextASERTWorkRequired text compared byte-for-byte
  with HEAD and is unchanged.

Mac arm64/Clang UBSan passed 82 related cases (including later S2/S4 regression
cases). A standalone harness compiled the actual current pow.cpp on macOS
arm64/Clang and Docker Linux x86_64/GCC 13.3.0 with UBSan: all 79 raw targets
matched byte-for-byte. It covers the 13 reference vectors, 63 original/Mainnet
comparisons and three 239-bit shift/multiply cases. The Linux original-wide
call aborted with status 134 at the unchanged 224-bit assertion. Functional
H/H+1 GBT/work cases passed. Real official anchor audit and full-node execution
on other release platforms remain release gates.

Historical numeric regression (not the launch height): H=318621, anchor=318616, bits=1d13c0c2, parent
time=1787261860 and previous time=1787266302 yield required bits=1d13fbb5.
Independent integer result and C++ wrapper agree. Linux signet_asert fuzz
checks the inherited/239-bit limits and equality with the original calculator
on the Mainnet domain; recorded in ../purity-signet-review.md.
