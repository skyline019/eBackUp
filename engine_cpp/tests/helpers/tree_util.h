#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace test {

Status BuildNestedTree(const std::string& root, int depth, int breadth,
                     size_t file_size);

Status BuildUnicodePathTree(const std::string& root);

Status CompareDirectoryTrees(const std::string& expected_root,
                             const std::string& actual_root);

size_t CountRegularFiles(const std::string& root);

}  // namespace test
}  // namespace ebbackup
