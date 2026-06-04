#pragma once

#include "pathcue/Job.h"

namespace pathcue {

JobResult ExecuteFileOperationJob(const JobRequest& job, HWND owner = nullptr);
bool RecyclePath(const std::wstring& path, HWND owner = nullptr, std::wstring* error = nullptr);

}  // namespace pathcue
