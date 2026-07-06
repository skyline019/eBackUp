#pragma once

#include <string>

namespace ebbackup {

enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kCorrupt,
  kIoError,
  kInternal,
  kConflict,
};

class Status {
 public:
  Status() = default;
  Status(StatusCode code, std::string message = {});

  static Status Ok() { return Status(); }
  static Status InvalidArgument(std::string msg) {
    return Status(StatusCode::kInvalidArgument, std::move(msg));
  }
  static Status NotFound(std::string msg) {
    return Status(StatusCode::kNotFound, std::move(msg));
  }
  static Status Corrupt(std::string msg) {
    return Status(StatusCode::kCorrupt, std::move(msg));
  }
  static Status IoError(std::string msg) {
    return Status(StatusCode::kIoError, std::move(msg));
  }
  static Status Internal(std::string msg) {
    return Status(StatusCode::kInternal, std::move(msg));
  }
  static Status Conflict(std::string msg) {
    return Status(StatusCode::kConflict, std::move(msg));
  }

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  StatusCode code_{StatusCode::kOk};
  std::string message_;
};

inline bool operator==(const Status& a, StatusCode b) { return a.code() == b; }
inline bool operator==(StatusCode b, const Status& a) { return a.code() == b; }

}  // namespace ebbackup
