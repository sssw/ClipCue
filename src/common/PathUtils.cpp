#include "pathcue/PathUtils.h"
#include "pathcue/WinUtils.h"

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <shlwapi.h>

namespace pathcue {

static std::wstring ToLower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return s;
}

std::wstring RemoveTrailingSlashes(const std::wstring& path) {
  if (path.size() <= 3) return path;
  std::wstring out = path;
  while (out.size() > 3 && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
  return out;
}

std::wstring NormalizePathForDisplay(const std::wstring& path) {
  wchar_t full[32768]{};
  DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(_countof(full)), full, nullptr);
  if (len > 0 && len < _countof(full)) return RemoveTrailingSlashes(full);
  return RemoveTrailingSlashes(path);
}

std::wstring ParentPath(const std::wstring& path) {
  std::wstring p = RemoveTrailingSlashes(path);
  DWORD attr = GetFileAttributesW(p.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    // For a selected directory, Explorer semantics treat its parent as source context.
  }
  size_t pos = p.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L"";
  if (pos == 2 && p.size() >= 3 && p[1] == L':') return p.substr(0, 3);
  if (pos == 0) return p.substr(0, 1);
  return p.substr(0, pos);
}

std::wstring FileNameFromPath(const std::wstring& path) {
  std::wstring p = RemoveTrailingSlashes(path);
  size_t pos = p.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return p;
  return p.substr(pos + 1);
}

std::wstring MakeDestinationPath(const std::wstring& targetDir, const std::wstring& sourcePath) {
  return PathCombineSimple(RemoveTrailingSlashes(targetDir), FileNameFromPath(sourcePath));
}

std::wstring MakeUniquePath(const std::wstring& path) {
  if (!FileExists(path) && !DirectoryExists(path)) return path;
  std::wstring parent = ParentPath(path);
  std::wstring name = FileNameFromPath(path);
  std::wstring stem = name;
  std::wstring ext;
  size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot != 0) {
    stem = name.substr(0, dot);
    ext = name.substr(dot);
  }
  for (int i = 1; i < 10000; ++i) {
    std::wstringstream ss;
    ss << stem << L" (" << i << L")" << ext;
    std::wstring candidate = PathCombineSimple(parent, ss.str());
    if (!FileExists(candidate) && !DirectoryExists(candidate)) return candidate;
  }
  return path;
}

std::wstring HashPathForCache(const std::wstring& path) {
  std::wstring lower = ToLower(NormalizePathForDisplay(path));
  unsigned long long h = 1469598103934665603ull;
  for (wchar_t c : lower) {
    h ^= static_cast<unsigned long long>(c);
    h *= 1099511628211ull;
  }
  std::wstringstream ss;
  ss << std::hex << h;
  return ss.str();
}

bool IsLikelyDirectoryPath(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsSamePathCaseInsensitive(const std::wstring& a, const std::wstring& b) {
  return ToLower(NormalizePathForDisplay(a)) == ToLower(NormalizePathForDisplay(b));
}

std::wstring JoinLines(const std::vector<std::wstring>& lines) {
  std::wstring out;
  for (const auto& line : lines) {
    out += line;
    out += L"\n";
  }
  return out;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
  std::vector<std::wstring> lines;
  std::wstring cur;
  for (wchar_t ch : text) {
    if (ch == L'\r') continue;
    if (ch == L'\n') {
      if (!cur.empty()) lines.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

}  // namespace pathcue
