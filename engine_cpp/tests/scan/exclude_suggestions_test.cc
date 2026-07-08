#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/scan/exclude_suggestions.h"
#include "ebbackup/scan/scan_entry.h"
#include "test_util.h"

namespace ebbackup {
namespace test {
namespace {

void Touch(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  WriteFile(path, "x");
}

TEST(ExcludeSuggestionsTest, DetectsGitAndNodeModules) {
  const std::string src = TempDir("suggest_src");
  Touch(src + "/.git/HEAD");
  Touch(src + "/apps/web/node_modules/pkg/index.js");
  Touch(src + "/apps/web/src/main.ts");

  SuggestExcludeFiltersOptions opts{};
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  bool saw_git = false;
  bool saw_nm = false;
  for (const auto& item : out.items) {
    if (item.pattern.find(".git") != std::string::npos) saw_git = true;
    if (item.pattern.find("node_modules") != std::string::npos) saw_nm = true;
  }
  EXPECT_TRUE(saw_git);
  EXPECT_TRUE(saw_nm);
}

TEST(ExcludeSuggestionsTest, DetectsThumbsDb) {
  const std::string src = TempDir("suggest_thumbs");
  Touch(src + "/Thumbs.db");
  SuggestExcludeFiltersOptions opts{};
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  bool saw = false;
  for (const auto& item : out.items) {
    if (item.apply_as == "exclude_glob" && item.pattern == "Thumbs.db") saw = true;
  }
  EXPECT_TRUE(saw);
}

TEST(ExcludeSuggestionsTest, RespectsMaxDepth) {
  const std::string src = TempDir("suggest_depth");
  std::string path = src;
  for (int i = 0; i < 6; ++i) {
    path += "/a";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  Touch(path + "/.git/HEAD");

  SuggestExcludeFiltersOptions opts{};
  opts.max_depth = 4;
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  bool saw_git = false;
  for (const auto& item : out.items) {
    if (item.pattern.find(".git") != std::string::npos) saw_git = true;
  }
  EXPECT_FALSE(saw_git);
}

TEST(ExcludeSuggestionsTest, SuppressesExistingExcludePath) {
  const std::string src = TempDir("suggest_existing");
  Touch(src + "/node_modules/pkg/index.js");

  BackupFilterOptions existing{};
  existing.exclude_paths = {"node_modules"};
  SuggestExcludeFiltersOptions opts{};
  opts.existing = &existing;
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  EXPECT_TRUE(out.items.empty());
}

TEST(ExcludeSuggestionsTest, RecommendedFilterExcludesNodeModules) {
  const std::string src = TempDir("suggest_filter");
  Touch(src + "/node_modules/pkg/index.js");
  Touch(src + "/keep.txt");

  SuggestExcludeFiltersOptions opts{};
  ExcludeFilterSuggestions suggestions{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &suggestions).ok());

  ScanResult scan{};
  ASSERT_TRUE(ScanDirectory(src, &scan).ok());
  ASSERT_TRUE(ApplyBackupFilter(suggestions.recommended, &scan.entries).ok());

  for (const auto& e : scan.entries) {
    EXPECT_EQ(e.relative_path.find("node_modules"), std::string::npos)
        << e.relative_path;
  }
  bool saw_keep = false;
  for (const auto& e : scan.entries) {
    if (e.relative_path == "keep.txt") saw_keep = true;
  }
  EXPECT_TRUE(saw_keep);
}

TEST(ExcludeSuggestionsTest, DetectsNextAndTurbo) {
  const std::string src = TempDir("suggest_next");
  Touch(src + "/web/.next/cache/foo");
  Touch(src + "/web/.turbo/cache/foo");

  SuggestExcludeFiltersOptions opts{};
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  bool saw_next = false;
  bool saw_turbo = false;
  for (const auto& item : out.items) {
    if (item.pattern.find(".next") != std::string::npos) saw_next = true;
    if (item.pattern.find(".turbo") != std::string::npos) saw_turbo = true;
  }
  EXPECT_TRUE(saw_next);
  EXPECT_TRUE(saw_turbo);
}

TEST(ExcludeSuggestionsTest, IncludeIdeDirs) {
  const std::string src = TempDir("suggest_ide");
  Touch(src + "/.idea/workspace.xml");

  SuggestExcludeFiltersOptions opts{};
  opts.include_ide_dirs = true;
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  bool saw_idea = false;
  for (const auto& item : out.items) {
    if (item.pattern.find(".idea") != std::string::npos) saw_idea = true;
  }
  EXPECT_TRUE(saw_idea);

  ExcludeFilterSuggestions out2{};
  ASSERT_TRUE(SuggestExcludeFilters(src, {}, &out2).ok());
  for (const auto& item : out2.items) {
    EXPECT_EQ(item.pattern.find(".idea"), std::string::npos);
  }
}

TEST(ExcludeSuggestionsTest, DetectsLogGlob) {
  const std::string src = TempDir("suggest_log");
  Touch(src + "/app/debug.log");

  SuggestExcludeFiltersOptions opts{};
  ExcludeFilterSuggestions out{};
  ASSERT_TRUE(SuggestExcludeFilters(src, opts, &out).ok());
  bool saw_log = false;
  for (const auto& item : out.items) {
    if (item.apply_as == "exclude_glob" && item.pattern == "*.log") saw_log = true;
  }
  EXPECT_TRUE(saw_log);
}

TEST(ExcludeSuggestionsTest, JsonRoundTripShape) {
  ExcludeFilterSuggestions s{};
  s.items.push_back(
      {"exclude_path", "node_modules", "package_cache", "cache", "a/node_modules",
       1});
  s.recommended.exclude_paths = {"node_modules"};
  const std::string json = ExcludeFilterSuggestionsToJson(s);
  EXPECT_NE(json.find("\"items\""), std::string::npos);
  EXPECT_NE(json.find("\"recommended\""), std::string::npos);
  EXPECT_NE(json.find("node_modules"), std::string::npos);
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
