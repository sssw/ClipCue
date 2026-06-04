#include "pathcue/FileOps.h"
#include "pathcue/PathUtils.h"
#include "pathcue/WinUtils.h"

#include <shobjidl.h>
#include <shellapi.h>
#include <sstream>
#include <vector>

#ifndef FOFX_SHOWELEVATIONPROMPT
#define FOFX_SHOWELEVATIONPROMPT 0x00040000
#endif

namespace pathcue {
namespace {

class FileOperationSink : public IFileOperationProgressSink {
 public:
  FileOperationSink() = default;

  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG v = InterlockedDecrement(&ref_);
    if (!v) delete this;
    return v;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
      *ppv = static_cast<IFileOperationProgressSink*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT hrResult) override {
    finalHr_ = hrResult;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT hr, IShellItem*) override { Count(hr); return S_OK; }
  HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT hr, IShellItem*) override { Count(hr); return S_OK; }
  HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT hr, IShellItem*) override { Count(hr); return S_OK; }
  HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT hr, IShellItem*) override { Count(hr); return S_OK; }
  HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT hr, IShellItem*) override { Count(hr); return S_OK; }
  HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

  std::size_t success() const { return success_; }
  std::size_t failure() const { return failure_; }
  HRESULT finalHr() const { return finalHr_; }

 private:
  void Count(HRESULT hr) {
    if (SUCCEEDED(hr)) ++success_;
    else {
      ++failure_;
      if (SUCCEEDED(firstFailure_)) firstFailure_ = hr;
    }
  }
  volatile LONG ref_ = 1;
  std::size_t success_ = 0;
  std::size_t failure_ = 0;
  HRESULT finalHr_ = S_OK;
  HRESULT firstFailure_ = S_OK;
};

HRESULT ShellItemFromPath(const std::wstring& path, IShellItem** item) {
  return SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item));
}

bool IsAccessDenied(HRESULT hr) {
  return hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) || hr == E_ACCESSDENIED;
}

}  // namespace

bool RecyclePath(const std::wstring& path, HWND owner, std::wstring* error) {
  std::wstring multi = path;
  multi.push_back(L'\0');
  multi.push_back(L'\0');
  SHFILEOPSTRUCTW op{};
  op.hwnd = owner;
  op.wFunc = FO_DELETE;
  op.pFrom = multi.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI;
  int rc = SHFileOperationW(&op);
  if (rc != 0 || op.fAnyOperationsAborted) {
    if (error) {
      std::wstringstream ss;
      ss << L"Recycle failed: " << rc;
      *error = ss.str();
    }
    return false;
  }
  return true;
}

JobResult ExecuteFileOperationJob(const JobRequest& job, HWND owner) {
  JobResult result;
  if (job.sources.empty() || job.targetDir.empty() || job.op == OperationKind::Unknown) {
    result.hr = E_INVALIDARG;
    result.message = L"Invalid job request";
    return result;
  }

  CoInitializeScope co;
  if (FAILED(co.hr())) {
    result.hr = co.hr();
    result.message = L"COM initialization failed";
    return result;
  }

  IShellItem* target = nullptr;
  HRESULT hr = ShellItemFromPath(job.targetDir, &target);
  if (FAILED(hr)) {
    result.hr = hr;
    result.message = L"Unable to bind target folder: " + job.targetDir;
    return result;
  }

  IFileOperation* fo = nullptr;
  hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fo));
  if (FAILED(hr)) {
    target->Release();
    result.hr = hr;
    result.message = L"Unable to create IFileOperation";
    return result;
  }

  fo->SetOwnerWindow(owner);
  FILEOPERATION_FLAGS flags = static_cast<FILEOPERATION_FLAGS>(FOF_NOCONFIRMMKDIR | FOFX_SHOWELEVATIONPROMPT);
  fo->SetOperationFlags(flags);

  FileOperationSink* sink = new FileOperationSink();
  DWORD cookie = 0;
  fo->Advise(sink, &cookie);

  std::vector<IShellItem*> sourceItems;
  for (const auto& source : job.sources) {
    std::wstring finalName;
    std::wstring destPath = MakeDestinationPath(job.targetDir, source);
    bool destExists = FileExists(destPath) || DirectoryExists(destPath);

    if (destExists && job.overwritePolicy == L"recycle_then_overwrite") {
      std::wstring recycleError;
      if (!RecyclePath(destPath, owner, &recycleError)) {
        ++result.failureCount;
        result.message += recycleError + L"\n";
        if (SUCCEEDED(result.hr)) result.hr = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
        continue;
      }
    } else if (destExists && job.conflictPolicy == L"skip") {
      continue;
    } else if (destExists && job.conflictPolicy == L"keep_both") {
      std::wstring unique = MakeUniquePath(destPath);
      finalName = FileNameFromPath(unique);
    }

    IShellItem* item = nullptr;
    hr = ShellItemFromPath(source, &item);
    if (FAILED(hr)) {
      ++result.failureCount;
      result.message += L"Unable to bind source: " + source + L"\n";
      if (SUCCEEDED(result.hr)) result.hr = hr;
      continue;
    }
    sourceItems.push_back(item);
    LPCWSTR newName = finalName.empty() ? nullptr : finalName.c_str();
    if (job.op == OperationKind::Move) {
      hr = fo->MoveItem(item, target, newName, nullptr);
    } else {
      hr = fo->CopyItem(item, target, newName, nullptr);
    }
    if (FAILED(hr)) {
      ++result.failureCount;
      if (SUCCEEDED(result.hr)) result.hr = hr;
    }
  }

  if (SUCCEEDED(result.hr)) {
    hr = fo->PerformOperations();
    if (FAILED(hr)) result.hr = hr;
  }

  BOOL aborted = FALSE;
  fo->GetAnyOperationsAborted(&aborted);
  result.anyAborted = aborted != FALSE;
  result.completed = SUCCEEDED(result.hr) && !result.anyAborted;
  result.successCount += sink->success();
  result.failureCount += sink->failure();

  if (FAILED(result.hr) && result.message.empty()) {
    result.message = FormatHResult(result.hr);
  }
  if (result.failureCount == 0 && result.successCount == 0 && result.completed) {
    result.successCount = job.sources.size();
  }

  if (cookie) fo->Unadvise(cookie);
  sink->Release();
  for (auto* item : sourceItems) item->Release();
  fo->Release();
  target->Release();
  return result;
}

}  // namespace pathcue
