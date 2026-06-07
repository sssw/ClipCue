#pragma once

#include "clipcue/History.h"

#include <windows.h>

#include <string>
#include <vector>

namespace clipcue {

struct JobRequest {
  OperationKind op = OperationKind::Unknown;
  std::wstring targetDir;
  std::vector<std::wstring> sources;
  std::wstring conflictPolicy = L"ask";       // ask / skip / keep_both / overwrite
  std::wstring overwritePolicy = L"ask";      // ask / recycle_then_overwrite / overwrite
  bool allowElevation = true;
};

struct JobResult {
  HRESULT hr = S_OK;
  bool completed = false;
  bool anyAborted = false;
  std::size_t successCount = 0;
  std::size_t failureCount = 0;
  std::wstring message;
};

bool SaveJobFile(const JobRequest& job, const std::wstring& path, std::wstring* error = nullptr);
bool LoadJobFile(const std::wstring& path, JobRequest* job, std::wstring* error = nullptr);
bool SaveSourcesFile(const std::vector<std::wstring>& sources, const std::wstring& path, std::wstring* error = nullptr);
bool LoadSourcesFile(const std::wstring& path, std::vector<std::wstring>* sources, std::wstring* error = nullptr);

bool SaveResultFile(const JobResult& result, const std::wstring& path, std::wstring* error = nullptr);
bool LoadResultFile(const std::wstring& path, JobResult* result, std::wstring* error = nullptr);

std::wstring CreateTempClipCueFile(const std::wstring& extension);

}  // namespace clipcue
