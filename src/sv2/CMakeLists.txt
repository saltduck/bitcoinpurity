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
if(MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
  list(APPEND sv2_cargo_env "RUSTFLAGS=$ENV{RUSTFLAGS} -C target-feature=+crt-static")
endif()
add_custom_command(OUTPUT "${sv2_archive}"
  COMMAND "${CMAKE_COMMAND}" -E env ${sv2_cargo_env} "${SV2_CARGO}" build --locked --release --lib ${sv2_target_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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
  find_library(SV2_SECURITY_FRAMEWORK Security REQUIRED)
  set_property(TARGET sv2_rust APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${SV2_SECURITY_FRAMEWORK}" resolv)
else()
  set_property(TARGET sv2_rust APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS} Threads::Threads m)
endif()

if(BUILD_TESTS)
  add_custom_target(sv2_test_pool
    COMMAND "${CMAKE_COMMAND}" -E env ${sv2_cargo_env} "${SV2_CARGO}" build --locked --release --features test-pool --bin purity-sv2-test-pool ${sv2_target_args}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sv2_profile_dir}/purity-sv2-test-pool${CMAKE_EXECUTABLE_SUFFIX}" "${PROJECT_BINARY_DIR}/bin/purity-sv2-test-pool${CMAKE_EXECUTABLE_SUFFIX}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
  add_dependencies(sv2_test_pool sv2_rust_build)
endif()
