# Use vendored lz4-dev from the repo root (no network fetch).
set(LZ4_LOCAL_DIR "${CMAKE_SOURCE_DIR}/lz4-dev")
if(NOT EXISTS "${LZ4_LOCAL_DIR}/lib/lz4.h")
  set(LZ4_LOCAL_DIR "${CMAKE_SOURCE_DIR}/../lz4-dev")
endif()
if(NOT EXISTS "${LZ4_LOCAL_DIR}/lib/lz4.h")
  message(FATAL_ERROR "lz4-dev not found at ${LZ4_LOCAL_DIR}")
endif()

set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(
  "${LZ4_LOCAL_DIR}/build/cmake"
  "${CMAKE_BINARY_DIR}/lz4_build"
)
