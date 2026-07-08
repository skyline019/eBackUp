#include "ebbackup/winmeta/vss_session.h"

#ifndef _WIN32

namespace ebbackup {
namespace winmeta {

VssSession::VssSession() = default;
VssSession::~VssSession() { (void)End(); }

void VssSession::ResetState() {
  active_ = false;
  backup_finished_ = false;
  volume_maps_.clear();
  info_ = {};
}

Status VssSession::CheckPrerequisites() {
  return Status::InvalidArgument("VSS requires Windows");
}

Status VssSession::Begin(const std::vector<std::string>&,
                         const VssBeginOptions&) {
  return Status::InvalidArgument("VSS requires Windows");
}

Status VssSession::FinishBackup() {
  return Status::InvalidArgument("VSS requires Windows");
}

std::string VssSession::MapToShadow(const std::string& logical_utf8) const {
  return logical_utf8;
}

std::string VssSession::MapToLogicalForReport(const std::string& path_utf8) const {
  return path_utf8;
}

Status VssSession::End() {
  ResetState();
  return Status::Ok();
}

}  // namespace winmeta
}  // namespace ebbackup

#endif
