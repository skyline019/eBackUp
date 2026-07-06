#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtest_capi.h>

int main(int argc, char** argv) {
  if (gtest_capi_init_from_argv(argc, (const char* const*)argv) != 0) {
    fprintf(stderr, "gtest_capi_init_from_argv failed\n");
    return 2;
  }
  if (argc >= 2 && argv[1] != NULL) {
    if (gtest_capi_set_filter(argv[1]) != 0) {
      fprintf(stderr, "gtest_capi_set_filter failed\n");
      return 2;
    }
  }
  const int rc = gtest_capi_run_all();
  if (rc != 0) return rc;
  if (gtest_capi_failed_test_count() != 0) return 1;
  return 0;
}
