set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
# Preserve compatibility of the pinned libevent port with CMake 4.
set(VCPKG_ENV_PASSTHROUGH CMAKE_POLICY_VERSION_MINIMUM)
