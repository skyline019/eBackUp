cmake_minimum_required(VERSION 3.20)

set(EBSYNC_SOURCES
  src/sync_state.cc
  src/sync_outbox.cc
  src/ebb_reader.cc
  src/backoff.cc
  src/sync_agent.cc
  src/transport/local_dir_transport.cc
  src/transport/s3_crypto.cc
  src/transport/s3_transport.cc
  src/transport/pds_http.cc
  src/pds/pds_client.cc
  src/transport/pds_transport.cc
)

add_library(ebsync_core STATIC ${EBSYNC_SOURCES})
target_compile_features(ebsync_core PUBLIC cxx_std_17)
target_include_directories(ebsync_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(ebsync_core PUBLIC ebbackup_core)
if(WIN32)
  target_link_libraries(ebsync_core PUBLIC bcrypt winhttp)
  target_compile_options(ebsync_core PRIVATE /utf-8)
endif()

add_executable(eb-sync cli/eb_sync_main.cc)
target_link_libraries(eb-sync PRIVATE ebsync_core)
if(WIN32)
  target_compile_options(eb-sync PRIVATE /utf-8)
endif()

if(EBBACKUP_BUILD_TESTS)
  add_executable(ebsync_tests
    tests/sync_state_test.cc
    tests/ebb_reader_test.cc
    tests/transport_test.cc
    tests/backoff_test.cc
    tests/ferry_roundtrip_test.cc
    tests/local_push_roundtrip_test.cc
  )
  target_link_libraries(ebsync_tests PRIVATE ebsync_core ebbackup_testlib GTest::gtest GTest::gtest_main)
  target_include_directories(ebsync_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/engine_cpp/tests/helpers"
  )
  target_compile_definitions(ebsync_tests PRIVATE ${EBTEST_COMPILE_DEFS})
  add_test(NAME ebsync_tests COMMAND ebsync_tests)
endif()
