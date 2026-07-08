#include "ebbackup/store/maintenance_wizard.h"

#include "ebbackup/engine/restore_options_json.h"
#include <cstdlib>

namespace ebbackup {

namespace {

bool ParseBoolField(const std::string& s, const char* key, bool default_value) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t pos = s.find(needle);
  if (pos == std::string::npos) return default_value;
  const size_t true_pos = s.find("true", pos);
  const size_t false_pos = s.find("false", pos);
  if (true_pos != std::string::npos &&
      (false_pos == std::string::npos || true_pos < false_pos)) {
    return true;
  }
  if (false_pos != std::string::npos) return false;
  return default_value;
}

int ParseIntField(const std::string& s, const char* key, int default_value) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t pos = s.find(needle);
  if (pos == std::string::npos) return default_value;
  const size_t colon = s.find(':', pos);
  if (colon == std::string::npos) return default_value;
  size_t i = colon + 1;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return static_cast<int>(std::strtol(s.c_str() + i, nullptr, 10));
}

}  // namespace

Status ParseMaintenanceWizardJson(const std::string& json,
                                  MaintenanceWizardOptions* out) {
  if (!out) return Status::InvalidArgument("out is null");
  MaintenanceWizardOptions opts{};
  opts.run_prune = ParseBoolField(json, "run_prune", true);
  opts.run_gc = ParseBoolField(json, "run_gc", true);
  opts.run_compact = ParseBoolField(json, "run_compact", true);
  opts.dry_run_only = ParseBoolField(json, "dry_run_only", false);
  opts.verify_after = ParseBoolField(json, "verify_after", false);
  opts.retain_min = ParseIntField(json, "retain_min", 3);
  std::string tiers;
  const Status st = ReadJsonStringField(json, "retention_tiers", &tiers);
  if (!st.ok()) return st;
  opts.retention_tiers = tiers;
  *out = std::move(opts);
  return Status::Ok();
}

Status RunMaintenanceWizard(BackupEngine* engine,
                            const MaintenanceWizardOptions& options,
                            MaintenanceWizardReport* report) {
  if (!engine || !report) return Status::InvalidArgument("null argument");
  report->compact_skipped = false;
  report->verify_ran = false;
  report->verify_ok = false;

  const Status stats_before_st = engine->GetRepoStats(&report->stats_before);
  if (!stats_before_st.ok()) return stats_before_st;

  RetentionPolicy policy = DefaultRetentionPolicy();
  if (!options.retention_tiers.empty()) {
    const Status parse_st = ParseRetentionTiers(options.retention_tiers, &policy);
    if (!parse_st.ok()) return parse_st;
  }
  if (options.retain_min > 0) policy.retain_min = options.retain_min;

  if (options.run_prune) {
    const Status prune_st =
        engine->PruneSnapshots(policy, true, &report->prune);
    if (!prune_st.ok()) return prune_st;
    if (!options.dry_run_only && report->prune.pruned_count > 0) {
      const Status live_st =
          engine->PruneSnapshots(policy, false, &report->prune);
      if (!live_st.ok()) return live_st;
    }
  }

  if (options.run_gc) {
    const Status gc_dry_st = engine->GcOrphans(true, &report->gc);
    if (!gc_dry_st.ok()) return gc_dry_st;
    if (!options.dry_run_only && report->gc.orphan_count > 0) {
      const Status gc_live_st = engine->GcOrphans(false, &report->gc);
      if (!gc_live_st.ok()) return gc_live_st;
    }
  }

  if (options.run_compact) {
    const Status compact_dry_st = engine->Compact(true, &report->compact);
    if (!compact_dry_st.ok()) return compact_dry_st;
    const bool should_compact =
        report->compact.ampl_ratio_before > 1.02 ||
        report->stats_before.orphan_bytes > 0;
    if (!options.dry_run_only && should_compact) {
      const Status compact_live_st = engine->Compact(false, &report->compact);
      if (!compact_live_st.ok()) return compact_live_st;
    } else if (!options.dry_run_only) {
      report->compact_skipped = true;
    }
  }

  if (options.verify_after && !options.dry_run_only) {
    report->verify_ran = true;
    report->verify_ok = engine->Verify().ok();
  }

  return engine->GetRepoStats(&report->stats_after);
}

}  // namespace ebbackup
