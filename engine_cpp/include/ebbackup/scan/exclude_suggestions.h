#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/scan/backup_filter.h"

namespace ebbackup {

struct ExcludeFilterSuggestion {
  std::string apply_as;  // "exclude_path" | "exclude_glob"
  std::string pattern;
  std::string kind;
  std::string reason;
  std::string example_path;
  uint32_t hit_count{1};
};

struct SuggestExcludeFiltersOptions {
  int max_depth{4};
  size_t max_dirs_visited{10000};
  size_t max_suggestions{32};
  bool include_ide_dirs{false};
  const BackupFilterOptions* existing{nullptr};
};

struct ExcludeFilterSuggestions {
  std::vector<ExcludeFilterSuggestion> items;
  BackupFilterOptions recommended;
};

Status SuggestExcludeFilters(const std::string& source_path,
                             const SuggestExcludeFiltersOptions& opts,
                             ExcludeFilterSuggestions* out);
std::string ExcludeFilterSuggestionsToJson(const ExcludeFilterSuggestions& s);

}  // namespace ebbackup
