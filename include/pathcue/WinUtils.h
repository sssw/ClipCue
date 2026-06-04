#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace pathcue {

std::wstring Utf8ToWide(const std::string& input);
std::string WideToUtf8(const std::wstring& input);

std::wstring FormatHResult(HRESULT hr);
std::wstring GetLastErrorMessage(DWORD error = GetLastError());

bool EnsureDirectory(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);

std::wstring GetKnownFolderLocalAppData();
std::wstring GetProgramPath();
std::wstring GetModuleDirectory(HMODULE module);
std::wstring PathCombineSimple(const std::wstring& left, const std::wstring& right);
std::wstring QuoteArg(const std::wstring& value);

std::vector<std::wstring> SplitCommandLineArgs();

class CoInitializeScope {
 public:
  CoInitializeScope();
  ~CoInitializeScope();
  CoInitializeScope(const CoInitializeScope&) = delete;
  CoInitializeScope& operator=(const CoInitializeScope&) = delete;
  HRESULT hr() const { return hr_; }
 private:
  HRESULT hr_{};
};

class Handle {
 public:
  Handle() = default;
  explicit Handle(HANDLE h) : h_(h) {}
  ~Handle();
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept;
  Handle& operator=(Handle&& other) noexcept;
  HANDLE get() const { return h_; }
  HANDLE release();
  void reset(HANDLE h = nullptr);
  explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }
 private:
  HANDLE h_ = nullptr;
};

}  // namespace pathcue
