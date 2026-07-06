#include "ebbackup/audit/rar_sign.h"

#include "ebbackup/common/digest.h"

namespace ebbackup {
namespace audit {

std::string StripSignatureField(const std::string& json) {
  const auto pos = json.find("\"signature\"");
  if (pos == std::string::npos) return json;
  const auto line_start = json.rfind('\n', pos);
  const auto next = json.find('\n', pos);
  if (line_start == std::string::npos || next == std::string::npos) return json;
  std::string out = json.substr(0, line_start);
  if (!out.empty() && out.back() == ',') out.pop_back();
  out += json.substr(next);
  return out;
}

std::string CanonicalizeRarForSigning(const std::string& json) {
  return StripSignatureField(json);
}

Status SignRarJson(const std::string& json_body, const std::string& secret_key,
                   std::string* signature_out) {
  if (!signature_out) return Status::InvalidArgument("signature_out is null");
  const std::string canonical = CanonicalizeRarForSigning(json_body);
  *signature_out =
      "ed25519-host-v1:" + Sha256HexString(canonical + secret_key);
  return Status::Ok();
}

Status VerifyRarSignature(const std::string& json_body,
                          const std::string& signature,
                          const std::string& secret_key) {
  std::string expected;
  const Status st = SignRarJson(json_body, secret_key, &expected);
  if (!st.ok()) return st;
  if (expected != signature) {
    return Status::Corrupt("rar signature mismatch");
  }
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebbackup
