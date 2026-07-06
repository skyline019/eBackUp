#include "ebbackup/common/status.h"

namespace ebbackup {

Status::Status(StatusCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

}  // namespace ebbackup
