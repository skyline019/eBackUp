#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "ebbackup/eb_backup.h"
#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace {

TEST(EbBackupCapiTest, SetBackupHooksStored) {
  const std::string repo = ebbackup::test::TempDir("capi_hooks_repo");
  const std::string source = ebbackup::test::TempDir("capi_hooks_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "hooks");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  eb_backup_set_backup_hooks(eng, "echo pre_ok", "echo post_ok");
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, AbiVersionAndStats) {
  EXPECT_EQ(eb_backup_abi_version(), EB_BACKUP_ABI_VERSION);
  const std::string repo = ebbackup::test::TempDir("capi_repo");
  const std::string source = ebbackup::test::TempDir("capi_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "hello capi");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  EbBackupStats stats{};
  ASSERT_EQ(eb_backup_get_stats(eng, &stats), EB_OK);
  EXPECT_GE(stats.files_processed, 1u);
  EXPECT_GE(stats.chunks_written + stats.chunks_reused, 1u);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, OpenExReportsError) {
  const std::string repo = ebbackup::test::TempDir("capi_bad_open");
  ebbackup::ChunkRecordHeader hdr{};
  std::memset(&hdr, 0xFF, sizeof(hdr));
  const std::string bytes(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  ebbackup::test::WriteFile(repo + "/data/chunks", bytes);
  EbStatus err = EB_OK;
  EbBackupEngine* eng = eb_backup_open_ex(repo.c_str(), &err);
  EXPECT_EQ(eng, nullptr);
  EXPECT_NE(err, EB_OK);
}

TEST(EbBackupCapiTest, LoadFilterFileExcludesGlob) {
  const std::string repo = ebbackup::test::TempDir("capi_filter_repo");
  const std::string source = ebbackup::test::TempDir("capi_filter_source");
  const std::string filter_path = ebbackup::test::TempDir("capi_filter_cfg") + "/filter.conf";
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/keep.txt", "keep-me");
  ebbackup::test::WriteFile(source + "/drop.tmp", "drop-me");
  ebbackup::test::WriteFile(filter_path, "exclude_glob=*.tmp\ninclude_glob=keep.txt\n");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_load_filter_file(eng, filter_path.c_str()), EB_OK);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);

  const std::string dest = ebbackup::test::TempDir("capi_filter_dest");
  ASSERT_EQ(eb_backup_restore(eng, dest.c_str()), EB_OK);
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/drop.tmp"));
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, RestoreSkipContentVerifyFlag) {
  const std::string repo = ebbackup::test::TempDir("capi_skip_verify_repo");
  const std::string source = ebbackup::test::TempDir("capi_skip_verify_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/keep.txt", "keep-me");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  const std::string dest = ebbackup::test::TempDir("capi_skip_verify_dest");
  ASSERT_EQ(eb_backup_restore_ex(eng, dest.c_str(),
                                 EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY),
            EB_OK);
  EXPECT_TRUE(std::filesystem::exists(dest + "/keep.txt"));
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, V03InitCompactAndRepoStats) {
  const std::string repo = ebbackup::test::TempDir("capi_v03_repo");
  const std::string source = ebbackup::test::TempDir("capi_v03_source");
  const uint32_t init_flags = EB_BACKUP_INIT_LEGACY | EB_BACKUP_FLAG_COMPRESS_AUTO |
                              EB_BACKUP_FLAG_MANIFEST_BINARY;
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), init_flags), EB_OK);
  ebbackup::test::WriteFile(source + "/data.bin",
                            ebbackup::test::MakeSyntheticData(256 * 1024, 7));

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run_ex(eng, source.c_str(), EB_BACKUP_FLAG_COMPRESS_AUTO),
            EB_OK);

  EbRepoStats stats{};
  ASSERT_EQ(eb_backup_repo_stats(eng, &stats), EB_OK);
  EXPECT_GT(stats.physical_bytes, 0u);
  EXPECT_DOUBLE_EQ(stats.ampl_ratio, 1.0);

  const std::string orphan = ebbackup::test::MakeSyntheticData(16 * 1024, 99);
  ebbackup::ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(orphan.data()),
                      orphan.size(), hash)
                  .ok());

  EbRepoStats after_orphan{};
  ASSERT_EQ(eb_backup_repo_stats(eng, &after_orphan), EB_OK);
  EXPECT_GT(after_orphan.ampl_ratio, 1.0);

  EbCompactReport compact{};
  ASSERT_EQ(eb_backup_compact(eng, 0, &compact), EB_OK);
  EXPECT_LE(compact.ampl_ratio_after, 1.05);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, BalancedDurabilityFlag) {
  const std::string repo = ebbackup::test::TempDir("capi_balanced_repo");
  const std::string source = ebbackup::test::TempDir("capi_balanced_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "balanced-durability");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run_ex(eng, source.c_str(),
                             EB_BACKUP_FLAG_BALANCED_DURABILITY),
            EB_OK);
  ASSERT_EQ(eb_backup_verify(eng), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, SnapshotsListRestoreAt) {
  const std::string repo = ebbackup::test::TempDir("capi_snap_repo");
  const std::string source = ebbackup::test::TempDir("capi_snap_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "v1");
  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "v2");
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  EbSnapshotInfo* snaps = nullptr;
  size_t count = 0;
  ASSERT_EQ(eb_backup_list_snapshots(eng, &snaps, &count), EB_OK);
  ASSERT_GE(count, 2u);
  const uint64_t txn = snaps[0].txn_id;
  eb_backup_free_snapshots(snaps);

  const std::string dest = ebbackup::test::TempDir("capi_snap_dest");
  ASSERT_EQ(eb_backup_restore_at(eng, dest.c_str(), txn, 0), EB_OK);
  {
    std::ifstream in(dest + "/a.txt");
    std::string restored;
    std::getline(in, restored);
    EXPECT_EQ(restored, "v1");
  }

  ASSERT_EQ(eb_backup_verify_at(eng, txn), EB_OK);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, PruneSnapshots) {
  const std::string repo = ebbackup::test::TempDir("capi_prune_repo");
  const std::string source = ebbackup::test::TempDir("capi_prune_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  for (int i = 0; i < 5; ++i) {
    ebbackup::test::WriteFile(source + "/f.txt", "v" + std::to_string(i));
    ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  }
  EbPruneReport report{};
  ASSERT_EQ(eb_backup_prune_snapshots(eng, "1h:24,1d:7,7d:4,30d:6", 3, 0, &report),
            EB_OK);
  EXPECT_GE(report.kept_count, 3u);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, JobsApiV19) {
  EXPECT_EQ(eb_backup_abi_version(), EB_BACKUP_ABI_VERSION);
  const std::string repo = ebbackup::test::TempDir("capi_jobs_repo");
  const std::string source = ebbackup::test::TempDir("capi_jobs_source");
  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "job-data");

  ebbackup::job::BackupJob job{};
  job.id = "docs";
  job.name = "Docs";
  job.source_path = source;
  job.retention_tag = 3;
  const std::string wrapped = ebbackup::job::JobsToJson({job});
  const std::string job_json = wrapped.substr(1, wrapped.size() - 2);
  ASSERT_EQ(eb_backup_upsert_job_json(repo.c_str(), job_json.c_str()), EB_OK);

  char* listed = eb_backup_list_jobs_json(repo.c_str());
  ASSERT_NE(listed, nullptr);
  EXPECT_NE(std::string(listed).find("docs"), std::string::npos);
  eb_backup_free_string(listed);

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run_job(eng, "docs", 0, 0), EB_OK);

  char* reports = eb_backup_list_job_reports_json(repo.c_str(), "docs", 0, 10);
  ASSERT_NE(reports, nullptr);
  EXPECT_NE(std::string(reports).find("\"ok\":true"), std::string::npos);
  EXPECT_NE(std::string(reports).find("\"reuse_pct\""), std::string::npos);
  eb_backup_free_string(reports);

  char* preview = eb_backup_preview_in_place_json(eng, 0, source.c_str(), nullptr);
  ASSERT_NE(preview, nullptr);
  EXPECT_NE(std::string(preview).find("\"ok\":true"), std::string::npos);
  eb_backup_free_string(preview);

  ebbackup::test::WriteFile(source + "/a.txt", "changed");
  char* applied = eb_backup_apply_in_place_json(
      eng, 0, source.c_str(), "{\"conflict_policy\":\"skip\"}");
  ASSERT_NE(applied, nullptr);
  EXPECT_NE(std::string(applied).find("\"ok\":true"), std::string::npos);
  EXPECT_NE(std::string(applied).find("\"applied_count\""), std::string::npos);
  eb_backup_free_string(applied);

  eb_backup_close(eng);

  ASSERT_EQ(eb_backup_delete_job(repo.c_str(), "docs"), EB_OK);
}

TEST(EbBackupCapiTest, DeltaExportImportJsonV21) {
  const std::string repo = ebbackup::test::TempDir("capi_delta_repo");
  const std::string source = ebbackup::test::TempDir("capi_delta_source");
  const std::string imported = ebbackup::test::TempDir("capi_delta_imported");
  const std::string full_bundle = ebbackup::test::TempDir("capi_delta_base") + "/base.ebb";
  const std::string delta_bundle = ebbackup::test::TempDir("capi_delta_out") + "/delta.ebb";

  ASSERT_EQ(eb_backup_init_repo_ex(repo.c_str(), 0), EB_OK);
  ebbackup::test::WriteFile(source + "/a.txt", "v1");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  const uint64_t txn_a = 1;

  ebbackup::test::WriteFile(source + "/a.txt", "v2");
  ASSERT_EQ(eb_backup_run_incremental(eng, source.c_str()), EB_OK);
  eb_backup_close(eng);

  char* exported = eb_backup_export_delta_json(
      repo.c_str(), delta_bundle.c_str(), txn_a, 0, 0);
  ASSERT_NE(exported, nullptr);
  EXPECT_NE(std::string(exported).find("\"ok\":true"), std::string::npos);
  EXPECT_NE(std::string(exported).find("\"chunk_count\""), std::string::npos);
  eb_backup_free_string(exported);

  ASSERT_TRUE(ebbackup::ExportRepoToBundle(repo, full_bundle).ok());
  char* imported_json = eb_backup_import_delta_json(
      full_bundle.c_str(), delta_bundle.c_str(), imported.c_str());
  ASSERT_NE(imported_json, nullptr);
  EXPECT_NE(std::string(imported_json).find("\"ok\":true"), std::string::npos);
  eb_backup_free_string(imported_json);

  EbBackupEngine* imp = eb_backup_open(imported.c_str());
  ASSERT_NE(imp, nullptr);
  EXPECT_EQ(eb_backup_verify(imp), EB_OK);
  eb_backup_close(imp);
}

TEST(EbBackupCapiTest, JobQueueV22) {
  EXPECT_EQ(eb_backup_abi_version(), EB_BACKUP_ABI_VERSION);
  const std::string repo = ebbackup::test::TempDir("capi_queue_repo");
  const std::string source = ebbackup::test::TempDir("capi_queue_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/q.txt", "queue");

  ebbackup::job::BackupJob job{};
  job.id = "qjob";
  job.source_path = source;
  ASSERT_TRUE(ebbackup::job::UpsertJob(repo, job).ok());

  char* enq = eb_backup_enqueue_job_json(
      repo.c_str(), "{\"job_id\":\"qjob\",\"incremental\":false,\"flags\":0}");
  ASSERT_NE(enq, nullptr);
  EXPECT_NE(std::string(enq).find("\"ok\":true"), std::string::npos);
  eb_backup_free_string(enq);

  char* status = eb_backup_job_queue_status_json(repo.c_str());
  ASSERT_NE(status, nullptr);
  EXPECT_NE(std::string(status).find("\"pending_count\":1"), std::string::npos);
  eb_backup_free_string(status);

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  char* run = eb_backup_run_job_queue_json(eng, "{\"drain\":false}");
  ASSERT_NE(run, nullptr);
  EXPECT_NE(std::string(run).find("\"ok\":true"), std::string::npos);
  eb_backup_free_string(run);
  eb_backup_close(eng);

  status = eb_backup_job_queue_status_json(repo.c_str());
  ASSERT_NE(status, nullptr);
  EXPECT_NE(std::string(status).find("\"pending_count\":0"), std::string::npos);
  eb_backup_free_string(status);
}

TEST(EbBackupCapiTest, InPlaceThreeWayV23) {
  const std::string repo = ebbackup::test::TempDir("capi_inplace_v23_repo");
  const std::string source = ebbackup::test::TempDir("capi_inplace_v23_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/f.txt", "base");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);
  const uint64_t txn1 = 1;

  ebbackup::test::WriteFile(source + "/f.txt", "target");
  ASSERT_EQ(eb_backup_run_incremental(eng, source.c_str()), EB_OK);

  ebbackup::test::WriteFile(source + "/f.txt", "live");

  char* preview = eb_backup_preview_in_place_json(
      eng, 0, source.c_str(), "{\"base_txn_id\":1,\"use_three_way\":true}");
  ASSERT_NE(preview, nullptr);
  const std::string preview_s(preview);
  EXPECT_NE(preview_s.find("\"three_way\":true"), std::string::npos);
  EXPECT_NE(preview_s.find("\"both_changed\""), std::string::npos);
  eb_backup_free_string(preview);

  char* dry = eb_backup_apply_in_place_json(
      eng, 0, source.c_str(),
      "{\"conflict_policy\":\"overwrite\",\"dry_run\":true,\"base_txn_id\":1}");
  ASSERT_NE(dry, nullptr);
  EXPECT_NE(std::string(dry).find("\"dry_run\":true"), std::string::npos);
  eb_backup_free_string(dry);

  std::ifstream in(source + "/f.txt");
  std::string content;
  std::getline(in, content);
  EXPECT_EQ(content, "live");
  in.close();

  char* applied = eb_backup_apply_in_place_json(
      eng, 0, source.c_str(),
      "{\"conflict_policy\":\"overwrite\",\"base_txn_id\":1}");
  ASSERT_NE(applied, nullptr);
  const std::string applied_s(applied);
  ASSERT_NE(applied_s.find("\"ok\":true"), std::string::npos) << applied_s;
  EXPECT_NE(applied_s.find("\"overwritten_count\""), std::string::npos);
  eb_backup_free_string(applied);

  in.close();
  in.open(source + "/f.txt");
  std::getline(in, content);
  EXPECT_EQ(content, "target");
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, ReachabilityAndRpoV24) {
  EXPECT_EQ(eb_backup_abi_version(), EB_BACKUP_ABI_VERSION);
  const std::string repo = ebbackup::test::TempDir("capi_v24_repo");
  const std::string source = ebbackup::test::TempDir("capi_v24_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/rpo.txt", "v24");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  char* reach = eb_backup_snapshot_reachability_json(eng, 0);
  ASSERT_NE(reach, nullptr);
  EXPECT_NE(std::string(reach).find("\"reachable\":true"), std::string::npos);
  eb_backup_free_string(reach);

  char* rpo = eb_backup_rpo_summary_json(eng);
  ASSERT_NE(rpo, nullptr);
  const std::string rpo_s(rpo);
  EXPECT_NE(rpo_s.find("\"ok\":true"), std::string::npos);
  EXPECT_NE(rpo_s.find("\"snapshot_count\":"), std::string::npos);
  eb_backup_free_string(rpo);
  eb_backup_close(eng);
}

TEST(EbBackupCapiTest, OrphanExplainAndOpsAuditV25) {
  EXPECT_GE(eb_backup_abi_version(), 25u);
  const std::string repo = ebbackup::test::TempDir("capi_v25_repo");
  const std::string source = ebbackup::test::TempDir("capi_v25_source");
  ASSERT_EQ(eb_backup_init_repo(repo.c_str()), EB_OK);
  ebbackup::test::WriteFile(source + "/ops.txt", "v25");

  EbBackupEngine* eng = eb_backup_open(repo.c_str());
  ASSERT_NE(eng, nullptr);
  ASSERT_EQ(eb_backup_run(eng, source.c_str()), EB_OK);

  char* explain = eb_backup_orphan_explain_json(eng, 32);
  ASSERT_NE(explain, nullptr);
  const std::string explain_s(explain);
  EXPECT_NE(explain_s.find("\"ok\":true"), std::string::npos);
  EXPECT_NE(explain_s.find("\"total_orphans\":"), std::string::npos);
  eb_backup_free_string(explain);

  char* append = eb_backup_append_ops_audit_json(
      eng, R"({"op":"gc_orphans","dry_run":false,"orphan_count":0})");
  ASSERT_NE(append, nullptr);
  EXPECT_NE(std::string(append).find("\"ok\":true"), std::string::npos);
  eb_backup_free_string(append);

  char* list = eb_backup_list_ops_audit_json(eng);
  ASSERT_NE(list, nullptr);
  const std::string list_s(list);
  EXPECT_NE(list_s.find("\"ok\":true"), std::string::npos);
  EXPECT_NE(list_s.find("gc_orphans"), std::string::npos);
  eb_backup_free_string(list);
  eb_backup_close(eng);
}

}  // namespace
