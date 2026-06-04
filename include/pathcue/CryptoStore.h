#pragma once

#include <string>
#include <vector>

namespace pathcue {

// A small append-only per-user record store.  Each record is encrypted with
// DPAPI CurrentUser by default.  This is intentionally dependency-free so the
// repository builds on GitHub Actions without SQLCipher/vcpkg.
class EncryptedRecordStore {
 public:
  explicit EncryptedRecordStore(std::wstring path);

  const std::wstring& path() const { return path_; }

  bool AppendRecord(const std::wstring& record, std::wstring* error = nullptr);
  bool LoadRecords(std::vector<std::wstring>* records, std::wstring* error = nullptr) const;
  bool RewriteRecords(const std::vector<std::wstring>& records, std::wstring* error = nullptr);

  static std::wstring DefaultStorePath();
  static std::wstring DefaultCacheDirectory();
  static std::wstring DefaultMenuCachePath();

 private:
  std::wstring path_;
};

bool ProtectTextCurrentUser(const std::wstring& plaintext, std::wstring* protectedBase64, std::wstring* error = nullptr);
bool UnprotectTextCurrentUser(const std::wstring& protectedBase64, std::wstring* plaintext, std::wstring* error = nullptr);

}  // namespace pathcue
