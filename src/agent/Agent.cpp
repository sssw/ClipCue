#include "pathcue/FileOps.h"
#include "pathcue/History.h"
#include "pathcue/Job.h"
#include "pathcue/PathUtils.h"
#include "pathcue/WinUtils.h"
#include "pathcue/CryptoStore.h"

#include <shobjidl.h>
#include <shellapi.h>
#include <iostream>
#include <sstream>

using namespace pathcue;

namespace {

void PrintUsage() {
  std::wcout << L"PathCue.Agent " << PATHCUE_VERSION << L"\n"
             << L"Usage:\n"
             << L"  PathCue.Agent --pick-dest --op copy|move --sources-file <file>\n"
             << L"  PathCue.Agent --shell-job <jobfile>\n"
             << L"  PathCue.Agent --build-cache\n"
             << L"  PathCue.Agent --cleanup\n";
}

std::wstring ArgValue(const std::vector<std::wstring>& args, const std::wstring& key) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) return args[i + 1];
  }
  return L"";
}

bool HasArg(const std::vector<std::wstring>& args, const std::wstring& key) {
  for (const auto& a : args) if (a == key) return true;
  return false;
}

std::wstring PickFolder(HWND owner) {
  CoInitializeScope co;
  if (FAILED(co.hr())) return L"";
  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) return L"";
  DWORD opts = 0;
  dialog->GetOptions(&opts);
  dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(L"Select PathCue target folder");
  std::wstring selected;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR raw = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
        selected = raw;
        CoTaskMemFree(raw);
      }
      item->Release();
    }
  }
  dialog->Release();
  return selected;
}

std::wstring FindElevatedExe() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  std::wstring dir = pos == std::wstring::npos ? L"." : exe.substr(0, pos);
  return PathCombineSimple(dir, L"PathCue.Elevated.exe");
}

JobResult RunElevated(const JobRequest& job) {
  JobResult result;
  std::wstring jobFile = CreateTempPathCueFile(L".job");
  std::wstring resultFile = CreateTempPathCueFile(L".result");
  std::wstring err;
  if (!SaveJobFile(job, jobFile, &err)) {
    result.hr = E_FAIL;
    result.message = err;
    return result;
  }
  std::wstring params = L"--job " + QuoteArg(jobFile) + L" --result " + QuoteArg(resultFile);
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";
  std::wstring elevated = FindElevatedExe();
  sei.lpFile = elevated.c_str();
  sei.lpParameters = params.c_str();
  sei.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&sei)) {
    result.hr = HRESULT_FROM_WIN32(GetLastError());
    result.message = L"Unable to launch elevated helper: " + FormatHResult(result.hr);
    return result;
  }
  WaitForSingleObject(sei.hProcess, INFINITE);
  CloseHandle(sei.hProcess);
  if (!LoadResultFile(resultFile, &result, &err)) {
    result.hr = E_FAIL;
    result.message = L"Elevated helper did not return a result: " + err;
  }
  DeleteFileW(jobFile.c_str());
  DeleteFileW(resultFile.c_str());
  return result;
}

bool IsAccessDeniedHr(HRESULT hr) {
  return hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
}

int ExecuteAndRecord(const JobRequest& job) {
  JobResult result = ExecuteFileOperationJob(job, nullptr);
  if (FAILED(result.hr) && job.allowElevation && IsAccessDeniedHr(result.hr)) {
    result = RunElevated(job);
  }

  HistoryDatabase db;
  std::wstring error;
  db.Load(&error);
  const std::wstring destParent = NormalizePathForDisplay(job.targetDir);
  const std::wstring outcome = SUCCEEDED(result.hr) && !result.anyAborted ? L"success" : L"failed";
  for (const auto& source : job.sources) {
    OperationRecord r;
    r.timestamp = UnixNow();
    r.op = job.op;
    r.sourceParent = ParentPath(source);
    r.destParent = destParent;
    r.result = outcome;
    r.confidence = 1.0;
    r.observedBy = L"own_ifileoperation";
    r.conflictPolicy = job.conflictPolicy + L"/" + job.overwritePolicy;
    db.AppendOperation(r, &error);
  }
  db.WriteMenuCache(L"", &error);

  if (FAILED(result.hr) || result.anyAborted) {
    MessageBoxW(nullptr, result.message.empty() ? L"PathCue operation failed." : result.message.c_str(), L"PathCue", MB_ICONERROR);
    return 2;
  }
  return 0;
}

}  // namespace

int wmain() {
  auto args = SplitCommandLineArgs();
  if (args.size() <= 1 || HasArg(args, L"--help")) {
    PrintUsage();
    return 0;
  }

  if (HasArg(args, L"--build-cache")) {
    HistoryDatabase db;
    std::wstring err;
    if (!db.Load(&err) || !db.WriteMenuCache(L"", &err)) {
      std::wcerr << L"PathCue cache failed: " << err << L"\n";
      return 1;
    }
    std::wcout << L"Menu cache written to " << EncryptedRecordStore::DefaultMenuCachePath() << L"\n";
    return 0;
  }

  if (HasArg(args, L"--cleanup")) {
    HistoryDatabase db;
    std::wstring err;
    if (!db.Load(&err) || !db.CleanupExpired(90, 365, &err)) {
      std::wcerr << L"Cleanup failed: " << err << L"\n";
      return 1;
    }
    db.Load(nullptr);
    db.WriteMenuCache(L"", nullptr);
    return 0;
  }

  if (HasArg(args, L"--shell-job")) {
    std::wstring jobFile = ArgValue(args, L"--shell-job");
    JobRequest job;
    std::wstring err;
    if (!LoadJobFile(jobFile, &job, &err)) {
      MessageBoxW(nullptr, err.c_str(), L"PathCue", MB_ICONERROR);
      return 1;
    }
    return ExecuteAndRecord(job);
  }

  if (HasArg(args, L"--pick-dest")) {
    std::wstring opText = ArgValue(args, L"--op");
    std::wstring sourcesFile = ArgValue(args, L"--sources-file");
    std::vector<std::wstring> sources;
    std::wstring err;
    if (!LoadSourcesFile(sourcesFile, &sources, &err)) {
      MessageBoxW(nullptr, err.c_str(), L"PathCue", MB_ICONERROR);
      return 1;
    }
    std::wstring target = PickFolder(nullptr);
    if (target.empty()) return 0;
    JobRequest job;
    job.op = OperationFromString(opText);
    job.targetDir = target;
    job.sources = sources;
    job.conflictPolicy = L"ask";
    job.overwritePolicy = L"ask";
    job.allowElevation = true;
    return ExecuteAndRecord(job);
  }

  PrintUsage();
  return 0;
}
