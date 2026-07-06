cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchLZ4.cmake")

enable_language(C)

set(EBTEST_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test_output"
    CACHE PATH "Local directory for ebbackup test temp files")
file(MAKE_DIRECTORY "${EBTEST_OUTPUT_DIR}")

set(EBTEST_COMPILE_DEFS "EBTEST_OUTPUT_DIR=\"${EBTEST_OUTPUT_DIR}\"")

set(EBBACKUP_CORE_SOURCES
  src/common/status.cc
  src/common/digest.cc
  src/common/fsync.cc
  src/chunk/fast_cdc.cc
  src/chunk/rolling_checksum.cc
  src/chunk/rabin_anchor.cc
  src/chunk/eb_hcrbo.cc
  src/codec/lz4_codec.cc
  src/io/mmap_reader.cc
  src/io/file_meta.cc
  src/scan/scan_entry.cc
  src/scan/backup_filter.cc
  src/scan/filter_loader.cc
  src/crypto/aes_block.cc
  src/crypto/gcm_portable.cc
  src/crypto/aes_gcm.cc
  src/io/fs_watch.cc
  src/archive/eb_bundle.cc
  src/daemon/backup_daemon.cc
  src/audit/merkle.cc
  src/audit/rar_chain.cc
  src/audit/rar_sign.cc
  src/audit/carl_anchor.cc
  src/store/chunk_store.cc
  src/store/orphan_gc.cc
  src/state/superblock.cc
  src/state/sync_executor.cc
  src/engine/manifest.cc
  src/pipeline/backup_pipeline.cc
  src/engine/backup_engine.cc
  src/engine/restore_engine.cc
  src/engine/eb_backup_capi.cc
)

add_library(ebbackup_core STATIC ${EBBACKUP_CORE_SOURCES})
target_compile_features(ebbackup_core PUBLIC cxx_std_17)

target_include_directories(ebbackup_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(ebbackup_core PRIVATE lz4_static)
if(WIN32)
  target_link_libraries(ebbackup_core PRIVATE bcrypt)
endif()

if(MSVC)
  target_compile_options(ebbackup_core PRIVATE /W4 /utf-8)
  target_compile_options(ebbackup_core PRIVATE
    $<$<CONFIG:Release>:/O2 /Ob2 /Oi /GL /arch:AVX2 /fp:fast>)
else()
  target_compile_options(ebbackup_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(eb cli/eb_main.cc)
target_link_libraries(eb PRIVATE ebbackup_core)
if(MSVC)
  target_link_options(eb PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

add_executable(ebbackup_bench tests/bench/chunk_bench.cc)
target_link_libraries(ebbackup_bench PRIVATE ebbackup_core)
if(MSVC)
  target_link_options(ebbackup_bench PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

add_executable(ebbackup_pipeline_bench tests/bench/backup_pipeline_bench.cc)
target_link_libraries(ebbackup_pipeline_bench PRIVATE ebbackup_core)
target_include_directories(ebbackup_pipeline_bench PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/helpers"
)
target_compile_definitions(ebbackup_pipeline_bench PRIVATE ${EBTEST_COMPILE_DEFS})
if(MSVC)
  target_link_options(ebbackup_pipeline_bench PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

if(EBBACKUP_BUILD_TESTS)
  set(EBBACKUP_TEST_SOURCES
    tests/chunk/fast_cdc_test.cc
    tests/chunk/fast_cdc_bounds_test.cc
    tests/codec/lz4_codec_test.cc
    tests/store/chunk_store_test.cc
    tests/store/chunk_store_lz4_test.cc
    tests/state/superblock_test.cc
    tests/state/sync_executor_test.cc
    tests/state/state_machine_test.cc
    tests/engine/manifest_test.cc
    tests/engine/backup_engine_test.cc
    tests/engine/recovery_test.cc
    tests/engine/eb_hcrbo_test.cc
    tests/engine/progress_test.cc
    tests/engine/eb_backup_capi_test.cc
    tests/engine/path_normalize_test.cc
    tests/engine/restore_test.cc
    tests/engine/restore_meta_test.cc
    tests/engine/restore_encrypt_test.cc
    tests/engine/selective_restore_test.cc
    tests/scan/special_files_test.cc
    tests/scan/backup_filter_test.cc
    tests/scan/filter_loader_test.cc
    tests/store/chunk_store_encrypt_test.cc
    tests/archive/eb_bundle_test.cc
    tests/daemon/schedule_smoke_test.cc
    tests/daemon/schedule_json_test.cc
    tests/daemon/watch_smoke_test.cc
    tests/crypto/aes_gcm_portable_test.cc
    tests/crypto/aes_gcm_nist_test.cc
    tests/crypto/pbkdf2_test.cc
    tests/pipeline/pipeline_encrypt_filter_test.cc
    tests/audit/merkle_test.cc
    tests/audit/rar_chain_test.cc
    tests/audit/carl_anchor_test.cc
    tests/store/orphan_gc_test.cc
    tests/pipeline/pipeline_parity_test.cc
    tests/failure/powerfail_test.cc
    tests/cli/restore_cli_test.cc
    tests/helpers/test_util.cc
  )

  add_library(ebbackup_testlib OBJECT ${EBBACKUP_TEST_SOURCES})
  target_link_libraries(ebbackup_testlib PUBLIC ebbackup_core GTest::gtest)
  target_include_directories(ebbackup_testlib PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/helpers"
  )
  target_compile_definitions(ebbackup_testlib PRIVATE
    EBTEST_EB_EXE="$<TARGET_FILE:eb>"
    ${EBTEST_COMPILE_DEFS}
  )

  add_executable(ebbackup_tests $<TARGET_OBJECTS:ebbackup_testlib>)
  target_link_libraries(ebbackup_tests PRIVATE ebbackup_core GTest::gtest GTest::gtest_main)
  target_include_directories(ebbackup_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/helpers"
  )
  target_compile_definitions(ebbackup_tests PRIVATE ${EBTEST_COMPILE_DEFS})
  if(MSVC)
    target_link_options(ebbackup_tests PRIVATE $<$<CONFIG:Release>:/LTCG>)
  endif()

  add_executable(eb_run_tests_capi
    tools/run_tests_capi.c
    $<TARGET_OBJECTS:ebbackup_testlib>
    "${GTEST_CAPI_DIR}/src/gtest_capi.cpp"
  )
  target_include_directories(eb_run_tests_capi PRIVATE
    "${GTEST_CAPI_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/helpers"
  )
  target_link_libraries(eb_run_tests_capi PRIVATE ebbackup_core GTest::gtest)
  target_compile_definitions(eb_run_tests_capi PRIVATE ${EBTEST_COMPILE_DEFS})
  if(MSVC)
    target_link_options(eb_run_tests_capi PRIVATE $<$<CONFIG:Release>:/LTCG>)
  endif()
endif()
