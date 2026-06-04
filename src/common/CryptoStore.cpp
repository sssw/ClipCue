#include "pathcue/CryptoStore.h"
#include "pathcue/WinUtils.h"

#include <wincrypt.h>
#include <fstream>
#include <sstream>

namespace pathcue {
namespace {

const wchar_t kEntropyText[] = L"PathCue.DPAPI.RecordStore.v1";

DATA_BLOB MakeBlob(const std::vector<BYTE>& data) {
  DATA_BLOB blob{};
  blob.pbData = const_cast<BYTE*>(data.data());
  blob.cbData = static_cast<DWORD>(data.size());
  return blob;
}

DATA_BLOB MakeBlobFromWide(const std::wstring& text) {
  DATA_BLOB blob{};
  blob.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(text.data()));
  blob.cbData = static_cast<DWORD>(text.size() * sizeof(wchar_t));
  return blob;
}

std::wstring BytesToBase64(const BYTE* data, DWORD size) {
  DWORD chars = 0;
  if (!CryptBinaryToStringW(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars)) return L"";
  std::wstring out(chars, L'\0');
  if (!CryptBinaryToStringW(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &chars)) return L"";
  while (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

bool Base64ToBytes(const std::wstring& base64, std::vector<BYTE>* out) {
  DWORD bytes = 0;
  if (!CryptStringToBinaryW(base64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr)) return false;
  out->assign(bytes, 0);
  return CryptStringToBinaryW(base64.c_str(), 0, CRYPT_STRING_BASE64, out->data(), &bytes, nullptr, nullptr) != FALSE;
}

std::wstring StoreDirectory() {
  return PathCombineSimple(GetKnownFolderLocalAppData(), L"PathCue\\Data");
}

}  // namespace

EncryptedRecordStore::EncryptedRecordStore(std::wstring path) : path_(std::move(path)) {}

std::wstring EncryptedRecordStore::DefaultStorePath() {
  return PathCombineSimple(StoreDirectory(), L"pathcue.db");
}

std::wstring EncryptedRecordStore::DefaultCacheDirectory() {
  return PathCombineSimple(GetKnownFolderLocalAppData(), L"PathCue\\Cache");
}

std::wstring EncryptedRecordStore::DefaultMenuCachePath() {
  return PathCombineSimple(DefaultCacheDirectory(), L"menu_cache.tsv");
}

bool ProtectTextCurrentUser(const std::wstring& plaintext, std::wstring* protectedBase64, std::wstring* error) {
  std::string utf8 = WideToUtf8(plaintext);
  std::vector<BYTE> bytes(utf8.begin(), utf8.end());
  DATA_BLOB in = MakeBlob(bytes);
  DATA_BLOB entropy = MakeBlobFromWide(kEntropyText);
  DATA_BLOB out{};
  if (!CryptProtectData(&in, L"PathCue record", &entropy, nullptr, nullptr, 0, &out)) {
    if (error) *error = GetLastErrorMessage();
    return false;
  }
  *protectedBase64 = BytesToBase64(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return !protectedBase64->empty();
}

bool UnprotectTextCurrentUser(const std::wstring& protectedBase64, std::wstring* plaintext, std::wstring* error) {
  std::vector<BYTE> encrypted;
  if (!Base64ToBytes(protectedBase64, &encrypted)) {
    if (error) *error = L"Invalid base64 record";
    return false;
  }
  DATA_BLOB in = MakeBlob(encrypted);
  DATA_BLOB entropy = MakeBlobFromWide(kEntropyText);
  DATA_BLOB out{};
  if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, 0, &out)) {
    if (error) *error = GetLastErrorMessage();
    return false;
  }
  std::string utf8(reinterpret_cast<char*>(out.pbData), reinterpret_cast<char*>(out.pbData) + out.cbData);
  LocalFree(out.pbData);
  *plaintext = Utf8ToWide(utf8);
  return true;
}

bool EncryptedRecordStore::AppendRecord(const std::wstring& record, std::wstring* error) {
  EnsureDirectory(StoreDirectory());
  std::wstring protectedLine;
  if (!ProtectTextCurrentUser(record, &protectedLine, error)) return false;
  std::ofstream file(path_, std::ios::binary | std::ios::app);
  if (!file) {
    if (error) *error = L"Unable to open store for append: " + path_;
    return false;
  }
  std::string line = "D\t" + WideToUtf8(protectedLine) + "\n";
  file.write(line.data(), static_cast<std::streamsize>(line.size()));
  return true;
}

bool EncryptedRecordStore::LoadRecords(std::vector<std::wstring>* records, std::wstring* error) const {
  records->clear();
  std::ifstream file(path_, std::ios::binary);
  if (!file) return true;
  std::string line;
  int bad = 0;
  while (std::getline(file, line)) {
    if (line.rfind("D\t", 0) != 0) continue;
    std::wstring protectedLine = Utf8ToWide(line.substr(2));
    std::wstring plain;
    std::wstring localError;
    if (UnprotectTextCurrentUser(protectedLine, &plain, &localError)) {
      records->push_back(plain);
    } else {
      ++bad;
    }
  }
  if (bad && error) {
    std::wstringstream ss;
    ss << L"Skipped " << bad << L" unreadable encrypted records";
    *error = ss.str();
  }
  return true;
}

bool EncryptedRecordStore::RewriteRecords(const std::vector<std::wstring>& records, std::wstring* error) {
  EnsureDirectory(StoreDirectory());
  std::wstring temp = path_ + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
      if (error) *error = L"Unable to open temp store: " + temp;
      return false;
    }
    for (const auto& rec : records) {
      std::wstring protectedLine;
      if (!ProtectTextCurrentUser(rec, &protectedLine, error)) return false;
      std::string line = "D\t" + WideToUtf8(protectedLine) + "\n";
      file.write(line.data(), static_cast<std::streamsize>(line.size()));
    }
  }
  DeleteFileW(path_.c_str());
  if (!MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    if (error) *error = GetLastErrorMessage();
    return false;
  }
  return true;
}

}  // namespace pathcue
