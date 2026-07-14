# Idempotent GoogleTest via gtest_capi (shared with repo root CMakeLists.txt).
if(TARGET GTest::gtest)
  return()
endif()

if(NOT GTEST_CAPI_DIR)
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../gtest_capi/CMakeLists.txt")
    set(GTEST_CAPI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../gtest_capi")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/gtest_capi/CMakeLists.txt")
    set(GTEST_CAPI_DIR "${CMAKE_SOURCE_DIR}/gtest_capi")
  else()
    message(FATAL_ERROR
      "gtest_capi not found. Build from repo root (cmake -S . -B build) "
      "or set -DGTEST_CAPI_DIR=/path/to/gtest_capi")
  endif()
endif()
set(GTEST_CAPI_DIR "${GTEST_CAPI_DIR}" CACHE PATH "gtest_capi root")

set(GTEST_CAPI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(GTEST_CAPI_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(GTEST_CAPI_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(GTEST_CAPI_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory("${GTEST_CAPI_DIR}" "${CMAKE_BINARY_DIR}/gtest_capi_build")

if(NOT TARGET GTest::gtest)
  message(FATAL_ERROR "SetupGTest: GTest::gtest target missing after gtest_capi")
endif()
