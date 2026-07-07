cmake_minimum_required(VERSION 3.20)
project(ebbackup VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(EBBACKUP_BUILD_TESTS "Build backup kernel tests" ON)

if(EBBACKUP_BUILD_TESTS)
  set(GTEST_CAPI_DIR "${CMAKE_SOURCE_DIR}/gtest_capi" CACHE PATH "gtest_capi root")
  set(GTEST_CAPI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(GTEST_CAPI_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(GTEST_CAPI_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
  set(GTEST_CAPI_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${GTEST_CAPI_DIR}" "${CMAKE_BINARY_DIR}/gtest_capi_build")
endif()

add_subdirectory(engine_cpp)

if(EBBACKUP_BUILD_TESTS)
  enable_testing()
  add_test(NAME ebbackup_tests COMMAND ebbackup_tests)
  add_test(NAME eb_run_tests_capi COMMAND eb_run_tests_capi "EbBackupCapiTest.*")
  add_test(NAME ebbackup_bench_check COMMAND ebbackup_bench_check)
  set_tests_properties(ebbackup_bench_check PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endif()
