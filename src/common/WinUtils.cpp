#include "clipcue/WinUtils.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>

namespace clipcue {

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (len <= 0) return L"";
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len);
  return out;
}

std::string WideToUtf8(const std::wstring& input) {
  if (input.empty()) return "";
  int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (len <= 0) return "";
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len, nullptr, nullptr);
  return out;
}

std::wstring FormatHResult(HRESULT hr) {
  LPWSTR buffer = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::wstringstream ss;
  ss << L"0x" << std::hex << static_cast<unsigned long>(hr);
  if (len && buffer) {
    ss << L" " << buffer;
    LocalFree(buffer);
  }
  return ss.str();
}

std::wstring GetLastErrorMessage(DWORD error) {
  return FormatHResult(HRESULT_FROM_WIN32(error));
}

bool EnsureDirectory(const std::wstring& path) {
  if (path.empty()) return false;
  if (DirectoryExists(path)) return true;
  std::wstring parent = path;
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  size_t pos = parent.find_last_of(L"\\/");
  if (pos != std::wstring::npos && pos > 2) {
    EnsureDirectory(parent.substr(0, pos));
  }
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  DWORD err = GetLastError();
  return err == ERROR_ALREADY_EXISTS;
}

bool FileExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetKnownFolderLocalAppData() {
  PWSTR raw = nullptr;
  HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw);
  if (SUCCEEDED(hr) && raw) {
    std::wstring out(raw);
    CoTaskMemFree(raw);
    return out;
  }
  wchar_t buffer[MAX_PATH]{};
  DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  if (len > 0 && len < MAX_PATH) return buffer;
  return L".";
}

std::wstring GetProgramPath() {
  std::wstring buffer(32768, L'\0');
  DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  buffer.resize(len);
  return buffer;
}

std::wstring GetModuleDirectory(HMODULE module) {
  std::wstring buffer(32768, L'\0');
  DWORD len = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  buffer.resize(len);
  size_t pos = buffer.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L".";
  return buffer.substr(0, pos);
}

std::wstring PathCombineSimple(const std::wstring& left, const std::wstring& right) {
  if (left.empty()) return right;
  if (right.empty()) return left;
  if (left.back() == L'\\' || left.back() == L'/') return left + right;
  return left + L"\\" + right;
}

std::wstring QuoteArg(const std::wstring& value) {
  std::wstring out = L"\"";
  for (wchar_t ch : value) {
    if (ch == L'\"') out += L"\\\"";
    else out += ch;
  }
  out += L"\"";
  return out;
}

std::vector<std::wstring> SplitCommandLineArgs() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> args;
  if (argv) {
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);
  }
  return args;
}

CoInitializeScope::CoInitializeScope() {
  hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
}

CoInitializeScope::~CoInitializeScope() {
  if (SUCCEEDED(hr_)) CoUninitialize();
}

Handle::~Handle() { reset(); }
Handle::Handle(Handle&& other) noexcept : h_(other.release()) {}
Handle& Handle::operator=(Handle&& other) noexcept {
  if (this != &other) reset(other.release());
  return *this;
}
HANDLE Handle::release() {
  HANDLE tmp = h_;
  h_ = nullptr;
  return tmp;
}
void Handle::reset(HANDLE h) {
  if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
  h_ = h;
}

}  // namespace clipcue
