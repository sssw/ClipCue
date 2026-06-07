#include "clipcue/Job.h"
#include "clipcue/PathUtils.h"
#include "clipcue/WinUtils.h"

#include <fstream>
#include <sstream>
#include <cstdlib>

namespace clipcue {
namespace {

std::wstring EscapeValue(const std::wstring& s) {
  std::wstring out;
  for (wchar_t ch : s) {
    if (ch == L'\\') out += L"\\\\";
    else if (ch == L'\n') out += L"\\n";
    else if (ch == L'\r') out += L"\\r";
    else out.push_back(ch);
  }
  return out;
}

std::wstring UnescapeValue(const std::wstring& s) {
  std::wstring out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == L'\\' && i + 1 < s.size()) {
      wchar_t n = s[++i];
      if (n == L'n') out += L'\n';
      else if (n == L'r') out += L'\r';
      else out += n;
    } else {
      out += s[i];
    }
  }
  return out;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text, std::wstring* error) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error) *error = L"Unable to write file: " + path;
    return false;
  }
  std::string utf8 = WideToUtf8(text);
  file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  return true;
}

bool ReadUtf8File(const std::wstring& path, std::wstring* text, std::wstring* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = L"Unable to read file: " + path;
    return false;
  }
  std::stringstream ss;
  ss << file.rdbuf();
  *text = Utf8ToWide(ss.str());
  return true;
}

}  // namespace

bool SaveJobFile(const JobRequest& job, const std::wstring& path, std::wstring* error) {
  std::wstringstream ss;
  ss << L"op=" << ToString(job.op) << L"\n";
  ss << L"target=" << EscapeValue(job.targetDir) << L"\n";
  ss << L"conflict=" << EscapeValue(job.conflictPolicy) << L"\n";
  ss << L"overwrite=" << EscapeValue(job.overwritePolicy) << L"\n";
  ss << L"allow_elevation=" << (job.allowElevation ? L"1" : L"0") << L"\n";
  for (const auto& source : job.sources) ss << L"source=" << EscapeValue(source) << L"\n";
  return WriteUtf8File(path, ss.str(), error);
}

bool LoadJobFile(const std::wstring& path, JobRequest* job, std::wstring* error) {
  std::wstring text;
  if (!ReadUtf8File(path, &text, error)) return false;
  *job = JobRequest{};
  for (const auto& line : SplitLines(text)) {
    size_t eq = line.find(L'=');
    if (eq == std::wstring::npos) continue;
    std::wstring key = line.substr(0, eq);
    std::wstring value = UnescapeValue(line.substr(eq + 1));
    if (key == L"op") job->op = OperationFromString(value);
    else if (key == L"target") job->targetDir = value;
    else if (key == L"conflict") job->conflictPolicy = value;
    else if (key == L"overwrite") job->overwritePolicy = value;
    else if (key == L"allow_elevation") job->allowElevation = (value == L"1" || value == L"true");
    else if (key == L"source") job->sources.push_back(value);
  }
  if (job->sources.empty() || job->targetDir.empty() || job->op == OperationKind::Unknown) {
    if (error) *error = L"Invalid job file: " + path;
    return false;
  }
  return true;
}

bool SaveSourcesFile(const std::vector<std::wstring>& sources, const std::wstring& path, std::wstring* error) {
  return WriteUtf8File(path, JoinLines(sources), error);
}

bool LoadSourcesFile(const std::wstring& path, std::vector<std::wstring>* sources, std::wstring* error) {
  std::wstring text;
  if (!ReadUtf8File(path, &text, error)) return false;
  *sources = SplitLines(text);
  return true;
}

bool SaveResultFile(const JobResult& result, const std::wstring& path, std::wstring* error) {
  std::wstringstream ss;
  ss << L"hr=" << result.hr << L"\n";
  ss << L"completed=" << (result.completed ? 1 : 0) << L"\n";
  ss << L"aborted=" << (result.anyAborted ? 1 : 0) << L"\n";
  ss << L"success=" << result.successCount << L"\n";
  ss << L"failure=" << result.failureCount << L"\n";
  ss << L"message=" << EscapeValue(result.message) << L"\n";
  return WriteUtf8File(path, ss.str(), error);
}

bool LoadResultFile(const std::wstring& path, JobResult* result, std::wstring* error) {
  std::wstring text;
  if (!ReadUtf8File(path, &text, error)) return false;
  *result = JobResult{};
  for (const auto& line : SplitLines(text)) {
    size_t eq = line.find(L'=');
    if (eq == std::wstring::npos) continue;
    std::wstring key = line.substr(0, eq);
    std::wstring value = UnescapeValue(line.substr(eq + 1));
    if (key == L"hr") result->hr = static_cast<HRESULT>(_wtol(value.c_str()));
    else if (key == L"completed") result->completed = value == L"1";
    else if (key == L"aborted") result->anyAborted = value == L"1";
    else if (key == L"success") result->successCount = static_cast<std::size_t>(_wtoi(value.c_str()));
    else if (key == L"failure") result->failureCount = static_cast<std::size_t>(_wtoi(value.c_str()));
    else if (key == L"message") result->message = value;
  }
  return true;
}

std::wstring CreateTempClipCueFile(const std::wstring& extension) {
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  wchar_t tempFile[MAX_PATH]{};
  GetTempFileNameW(tempDir, L"ccq", 0, tempFile);
  std::wstring out(tempFile);
  if (!extension.empty()) {
    std::wstring renamed = out + extension;
    MoveFileExW(out.c_str(), renamed.c_str(), MOVEFILE_REPLACE_EXISTING);
    out = renamed;
  }
  return out;
}

}  // namespace clipcue
