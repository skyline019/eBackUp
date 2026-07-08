#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/report/backup_report.h"
#include "test_util.h"

#ifdef EBBACKUP_HAVE_SQLITE3
#include "sqlite3.h"
#endif

namespace ebbackup {
namespace test {
namespace {

#ifdef EBBACKUP_HAVE_SQLITE3
Status CreateWalDatabase(const std::string& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK || !db) {
    if (db) sqlite3_close(db);
    return Status::Internal("sqlite open failed");
  }
  char* err = nullptr;
  if (sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err) != SQLITE_OK) {
    if (err) sqlite3_free(err);
    sqlite3_close(db);
    return Status::Internal("wal mode failed");
  }
  if (sqlite3_exec(db, "CREATE TABLE t(v INTEGER); INSERT INTO t VALUES(42);", nullptr,
                   nullptr, &err) != SQLITE_OK) {
    if (err) sqlite3_free(err);
    sqlite3_close(db);
    return Status::Internal("schema failed");
  }
  sqlite3_close(db);
  return Status::Ok();
}
#endif

TEST(SqliteCheckpointPluginTest, BackupRestoreRoundTrip) {
#ifndef EBBACKUP_HAVE_SQLITE3
  GTEST_SKIP() << "sqlite amalgamation not available";
#endif
  const std::string repo = TempDir("sqlite_plugin_repo");
  const std::string source = TempDir("sqlite_plugin_src");
  const std::string dest = TempDir("sqlite_plugin_dest");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());

  const std::string db_path = source + "/app.db";
  ASSERT_TRUE(CreateWalDatabase(db_path).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  BackupOptions opts{};
  opts.plugins = {"sqlite_checkpoint"};
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  ManifestDocument doc;
  ASSERT_TRUE(engine.LoadManifest(0, &doc).ok());
  report::BackupReport br{};
  ASSERT_TRUE(report::LoadBackupReport(repo, doc.txn_id, &br).ok());
  ASSERT_FALSE(br.plugins.empty());
  bool saw_sqlite = false;
  for (const auto& frag : br.plugins) {
    if (frag.find("sqlite_checkpoint") != std::string::npos) {
      saw_sqlite = true;
      EXPECT_NE(frag.find("\"checkpointed\":"), std::string::npos);
    }
  }
  EXPECT_TRUE(saw_sqlite);

  ASSERT_TRUE(engine.Restore(dest).ok());

  sqlite3* restored = nullptr;
  ASSERT_EQ(sqlite3_open((dest + "/app.db").c_str(), &restored), SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(restored, "SELECT v FROM t;", -1, &stmt, nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 42);
  sqlite3_finalize(stmt);
  sqlite3_close(restored);
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
