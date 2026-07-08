#pragma once

#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/winmeta/win_meta.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/scan/backup_filter.h"

namespace ebbackup {

Status ParseBackupFilterJson(const std::string& json, BackupFilterOptions* out);
Status ParseRestoreRemapJson(const std::string& json, RestorePathRemap* out);
Status ParseSymlinkRemapJson(const std::string& json, SymlinkRemap* out);
Status ParseRestoreAclPolicyJson(const std::string& json,
                                 winmeta::AclRestorePolicy* out);
Status ParseRestoreReparsePolicyJson(const std::string& json,
                                     winmeta::ReparseRestorePolicy* out);
Status ReadJsonStringField(const std::string& json, const char* key,
                           std::string* out);
Status ReadJsonStringArrayField(const std::string& json, const char* key,
                                std::vector<std::string>* out);
Status ReadJsonU64ArrayField(const std::string& json, const char* key,
                             std::vector<uint64_t>* out);
Status ReadJsonU64Field(const std::string& json, const char* key, uint64_t* out);
Status ReadJsonIntField(const std::string& json, const char* key, int* out);
Status ReadJsonBoolField(const std::string& json, const char* key, bool* out);
bool TryReadJsonU64Field(const std::string& json, const char* key, uint64_t* out);
bool TryReadJsonIntField(const std::string& json, const char* key, int* out);

}  // namespace ebbackup
