#include "clipcue/FileOps.h"
#include "clipcue/History.h"
#include "clipcue/Job.h"
#include "clipcue/PathUtils.h"
#include "clipcue/WinUtils.h"
#include "clipcue/CryptoStore.h"

#include <shobjidl.h>
#include <shellapi.h>
#include <iostream>
#include <map>
#include <sstream>

using namespace clipcue;

namespace {

void PrintUsage() {
  std::wcout << L"ClipCue.Agent " << CLIPCUE_VERSION << L"\n"
             << L"Usage:\n"
             << L"  ClipCue.Agent --pick-dest --op copy|move --sources-file <file>\n"
             << L"  ClipCue.Agent --shell-job <jobfile>\n"
             << L"  ClipCue.Agent --paste-queue --target <folder> [--op copy|move|both]\n"
             << L"  ClipCue.Agent --build-cache\n"
             << L"  ClipCue.Agent --cleanup\n";
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
  dialog->SetTitle(L"Select ClipCue target folder");
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
  return PathCombineSimple(dir, L"ClipCue.Elevated.exe");
}

JobResult RunElevated(const JobRequest& job) {
  JobResult result;
  std::wstring jobFile = CreateTempClipCueFile(L".job");
  std::wstring resultFile = CreateTempClipCueFile(L".result");
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
    MessageBoxW(nullptr, result.message.empty() ? L"ClipCue operation failed." : result.message.c_str(), L"ClipCue", MB_ICONERROR);
    return 2;
  }
  return 0;
}

std::wstring QueuePreviewText(const std::vector<ClipboardHistoryEntry>& entries, const std::wstring& target) {
  int copyFiles = 0;
  int moveFiles = 0;
  int copyGroups = 0;
  int moveGroups = 0;
  for (const auto& entry : entries) {
    if (entry.op == OperationKind::Copy) {
      ++copyGroups;
      copyFiles += static_cast<int>(entry.files.size());
    } else if (entry.op == OperationKind::Move) {
      ++moveGroups;
      moveFiles += static_cast<int>(entry.files.size());
    }
  }

  std::wstringstream ss;
  ss << L"ClipCue will apply the recorded clipboard queue to:\r\n" << target << L"\r\n\r\n";
  if (copyFiles > 0) ss << L"Copy: " << copyFiles << L" file(s) from " << copyGroups << L" clipboard action(s)\r\n";
  if (moveFiles > 0) ss << L"Move: " << moveFiles << L" file(s) from " << moveGroups << L" clipboard action(s)\r\n";
  ss << L"\r\nPreview:\r\n";

  int shown = 0;
  for (const auto& entry : entries) {
    for (const auto& file : entry.files) {
      if (shown >= 16) {
        ss << L"  ...\r\n";
        return ss.str();
      }
      ss << L"  [" << ToString(entry.op) << L"] " << file << L"\r\n";
      ++shown;
    }
  }
  return ss.str();
}

int ExecuteClipboardQueue(OperationKind requestedOp, const std::wstring& target) {
  if (target.empty() || !DirectoryExists(target)) {
    MessageBoxW(nullptr, L"Select an existing target folder for the ClipCue queue.", L"ClipCue", MB_ICONERROR);
    return 1;
  }

  HistoryDatabase db;
  std::wstring err;
  if (!db.Load(&err)) {
    MessageBoxW(nullptr, err.empty() ? L"Unable to load ClipCue clipboard history." : err.c_str(), L"ClipCue", MB_ICONERROR);
    return 1;
  }

  auto entries = db.GetSelectedFileClipboardEntries(requestedOp);
  if (entries.empty()) {
    MessageBoxW(nullptr, L"No selected file clipboard entries are queued. Copy or cut files first, or reselect entries in the ClipCue control panel.", L"ClipCue", MB_ICONINFORMATION);
    return 0;
  }

  std::wstring preview = QueuePreviewText(entries, NormalizePathForDisplay(target));
  if (MessageBoxW(nullptr, preview.c_str(), L"ClipCue Operation Preview", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;

  std::map<OperationKind, std::vector<std::wstring>> grouped;
  for (const auto& entry : entries) {
    if (entry.op != OperationKind::Copy && entry.op != OperationKind::Move) continue;
    grouped[entry.op].insert(grouped[entry.op].end(), entry.files.begin(), entry.files.end());
  }

  int rc = 0;
  bool attempted = false;
  for (OperationKind op : {OperationKind::Copy, OperationKind::Move}) {
    auto it = grouped.find(op);
    if (it == grouped.end() || it->second.empty()) continue;
    attempted = true;
    JobRequest job;
    job.op = op;
    job.targetDir = target;
    job.sources = it->second;
    job.conflictPolicy = L"ask";
    job.overwritePolicy = L"ask";
    job.allowElevation = true;
    int jobRc = ExecuteAndRecord(job);
    if (jobRc != 0) rc = jobRc;
  }

  if (attempted) {
    HistoryDatabase updated;
    updated.Load(nullptr);
    updated.WriteMenuCache(L"", nullptr);
  }
  return rc;
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
      std::wcerr << L"ClipCue cache failed: " << err << L"\n";
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
      MessageBoxW(nullptr, err.c_str(), L"ClipCue", MB_ICONERROR);
      return 1;
    }
    return ExecuteAndRecord(job);
  }

  if (HasArg(args, L"--paste-queue")) {
    std::wstring target = ArgValue(args, L"--target");
    OperationKind op = OperationFromString(ArgValue(args, L"--op"));
    if (op == OperationKind::Unknown) op = OperationKind::Both;
    return ExecuteClipboardQueue(op, target);
  }

  if (HasArg(args, L"--pick-dest")) {
    std::wstring opText = ArgValue(args, L"--op");
    std::wstring sourcesFile = ArgValue(args, L"--sources-file");
    std::vector<std::wstring> sources;
    std::wstring err;
    if (!LoadSourcesFile(sourcesFile, &sources, &err)) {
      MessageBoxW(nullptr, err.c_str(), L"ClipCue", MB_ICONERROR);
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
