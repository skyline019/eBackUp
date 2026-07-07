# Use vendored zstd-dev from the repo root (no network fetch).
set(ZSTD_LOCAL_DIR "${CMAKE_SOURCE_DIR}/zstd-dev")
if(NOT EXISTS "${ZSTD_LOCAL_DIR}/lib/zstd.h")
  set(ZSTD_LOCAL_DIR "${CMAKE_SOURCE_DIR}/../zstd-dev")
endif()
if(NOT EXISTS "${ZSTD_LOCAL_DIR}/lib/zstd.h")
  message(FATAL_ERROR "zstd-dev not found at ${ZSTD_LOCAL_DIR}")
endif()

set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

add_subdirectory(
  "${ZSTD_LOCAL_DIR}/build/cmake"
  "${CMAKE_BINARY_DIR}/zstd_build"
)
