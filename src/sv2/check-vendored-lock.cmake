# Copyright (c) 2026 The Bitcoin Purity developers
# Distributed under the MIT software license.
#
# An offline depends build replaces the stratum git source with a local path,
# so cargo drops the `source = "git+..."` lines from Cargo.lock. Fail if
# anything else changed, which would mean a crate version was not the one
# pinned by the committed lockfile.
file(STRINGS "${EXPECTED}" expected)
file(STRINGS "${ACTUAL}" actual)
list(FILTER expected EXCLUDE REGEX "^source = \"git\\+")
list(FILTER actual EXCLUDE REGEX "^source = \"git\\+")
if(NOT expected STREQUAL actual)
  message(FATAL_ERROR "Offline SV2 build resolved dependencies that differ from src/sv2/Cargo.lock")
endif()
