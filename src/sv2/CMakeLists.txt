find_program(SV2_CARGO cargo REQUIRED)
find_program(SV2_RUSTC rustc REQUIRED)
execute_process(COMMAND "${SV2_RUSTC}" --version WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE sv2_rust_version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT sv2_rust_version MATCHES "^rustc 1\\.96\\.0 ")
  message(FATAL_ERROR "Embedded SV2 requires Rust 1.96.0 (src/sv2/rust-toolchain.toml).")
endif()

set(SV2_RUST_TARGET "" CACHE STRING "Rust target triple for SV2 cross compilation.")
if(CMAKE_CROSSCOMPILING AND NOT SV2_RUST_TARGET)
  message(FATAL_ERROR "Set SV2_RUST_TARGET when cross-compiling embedded SV2.")
endif()
set(sv2_target_dir "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(sv2_profile_dir "${sv2_target_dir}")
set(sv2_target_args "")
if(SV2_RUST_TARGET)
  list(APPEND sv2_target_args --target "${SV2_RUST_TARGET}")
  string(APPEND sv2_profile_dir "/${SV2_RUST_TARGET}")
endif()
string(APPEND sv2_profile_dir "/release")
if(MSVC)
  set(sv2_archive "${sv2_profile_dir}/purity_sv2.lib")
else()
  set(sv2_archive "${sv2_profile_dir}/libpurity_sv2.a")
endif()
file(GLOB_RECURSE sv2_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.rs")
set(sv2_cargo_env "CARGO_TARGET_DIR=${sv2_target_dir}")
set(sv2_test_cargo_env "CARGO_TARGET_DIR=${sv2_target_dir}-test-pool")
string(REPLACE "${sv2_target_dir}" "${sv2_target_dir}-test-pool" sv2_test_profile_dir "${sv2_profile_dir}")
set(sv2_rustflags "$ENV{RUSTFLAGS}")
if(MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
  string(APPEND sv2_rustflags " -C target-feature=+crt-static")
  list(APPEND sv2_test_cargo_env "RUSTFLAGS=$ENV{RUSTFLAGS} -C target-feature=+crt-static")
endif()
list(APPEND sv2_cargo_env "RUSTC=${SV2_RUSTC}")
if(SV2_RUST_TARGET)
  # C code in build scripts (secp256k1-sys) must use the same cross compiler.
  string(REPLACE "-" "_" sv2_target_env "${SV2_RUST_TARGET}")
  set(sv2_target_cflags "${CMAKE_C_COMPILER_ARG1} ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
  if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_TARGET)
    string(PREPEND sv2_target_cflags "--target=${CMAKE_C_COMPILER_TARGET} ")
  endif()
  if(CMAKE_OSX_SYSROOT)
    string(APPEND sv2_target_cflags " -isysroot ${CMAKE_OSX_SYSROOT}")
  endif()
  string(STRIP "${sv2_target_cflags}" sv2_target_cflags)
  list(APPEND sv2_cargo_env
    "CC_${sv2_target_env}=${CMAKE_C_COMPILER}"
    "CFLAGS_${sv2_target_env}=${sv2_target_cflags}"
    "AR_${sv2_target_env}=${CMAKE_AR}")
endif()

set(sv2_cargo_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(sv2_cargo_mode --locked)
set(sv2_cargo_prepare "")
set(sv2_cargo_verify "")
if(SV2_VENDOR_DIR)
  # Offline build from the depends crate sources. Cargo rewrites the stratum
  # entries of Cargo.lock as path sources, so build from a copy of the crate and
  # check afterwards that every other locked version is unchanged.
  set(sv2_cargo_dir "${CMAKE_CURRENT_BINARY_DIR}/crate")
  set(sv2_cargo_home "${CMAKE_CURRENT_BINARY_DIR}/cargo-home")
  file(MAKE_DIRECTORY "${sv2_cargo_dir}")
  file(WRITE "${sv2_cargo_home}/config.toml"
    "[source.crates-io]\n"
    "replace-with = \"purity-vendored\"\n\n"
    "[source.purity-vendored]\n"
    "directory = \"${SV2_VENDOR_DIR}/vendor\"\n\n"
    "[patch.\"https://github.com/stratum-mining/stratum\"]\n"
    "stratum-core = { path = \"${SV2_VENDOR_DIR}/stratum/stratum-core\" }\n\n"
    "[net]\n"
    "offline = true\n")
  if(SV2_BUILD_LINKER AND SV2_RUST_BUILD_TARGET)
    file(APPEND "${sv2_cargo_home}/config.toml"
      "\n[target.${SV2_RUST_BUILD_TARGET}]\n"
      "linker = \"${SV2_BUILD_LINKER}\"\n")
  endif()
  list(APPEND sv2_cargo_env "CARGO_HOME=${sv2_cargo_home}")
  # Keep build paths out of the archive so release builds are reproducible.
  string(APPEND sv2_rustflags " --remap-path-prefix=${SV2_VENDOR_DIR}=/purity-sv2-vendor --remap-path-prefix=${sv2_cargo_dir}=/purity-sv2")
  set(sv2_cargo_mode --offline)
  set(sv2_cargo_prepare
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${sv2_cargo_dir}/src"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${sv2_cargo_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/src" "${sv2_cargo_dir}/src"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml" "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock" "${CMAKE_CURRENT_SOURCE_DIR}/rust-toolchain.toml" "${sv2_cargo_dir}/")
  set(sv2_cargo_verify
    COMMAND "${CMAKE_COMMAND}" "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock" "-DACTUAL=${sv2_cargo_dir}/Cargo.lock" -P "${CMAKE_CURRENT_SOURCE_DIR}/check-vendored-lock.cmake")
endif()

string(STRIP "${sv2_rustflags}" sv2_rustflags)
if(sv2_rustflags)
  list(APPEND sv2_cargo_env "RUSTFLAGS=${sv2_rustflags}")
endif()

add_custom_command(OUTPUT "${sv2_archive}"
  ${sv2_cargo_prepare}
  COMMAND "${CMAKE_COMMAND}" -E env ${sv2_cargo_env} "${SV2_CARGO}" build ${sv2_cargo_mode} --release --lib ${sv2_target_args}
  ${sv2_cargo_verify}
  WORKING_DIRECTORY "${sv2_cargo_dir}"
  DEPENDS ${sv2_sources} Cargo.toml Cargo.lock rust-toolchain.toml
  COMMENT "Building pinned embedded SV2 Rust library"
  VERBATIM)
add_custom_target(sv2_rust_build DEPENDS "${sv2_archive}")
add_library(sv2_rust STATIC IMPORTED GLOBAL)
set_target_properties(sv2_rust PROPERTIES IMPORTED_LOCATION "${sv2_archive}")
add_dependencies(sv2_rust sv2_rust_build)
if(WIN32)
  set_property(TARGET sv2_rust APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 userenv bcrypt ntdll advapi32)
elseif(APPLE)
  # Link the SDK framework by name: depends toolchains confine find_library()
  # to the depends prefix, where no frameworks exist.
  set_property(TARGET sv2_rust APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-framework Security" resolv)
else()
  set_property(TARGET sv2_rust APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS} Threads::Threads m)
endif()

if(BUILD_TESTS)
  add_custom_target(sv2_test_pool
    COMMAND "${CMAKE_COMMAND}" -E env ${sv2_test_cargo_env} "${SV2_CARGO}" build --locked --release --features test-pool --bin purity-sv2-test-pool ${sv2_target_args}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sv2_test_profile_dir}/purity-sv2-test-pool${CMAKE_EXECUTABLE_SUFFIX}" "${PROJECT_BINARY_DIR}/bin/purity-sv2-test-pool${CMAKE_EXECUTABLE_SUFFIX}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
  add_dependencies(sv2_test_pool sv2_rust_build)
endif()
