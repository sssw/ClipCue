#pragma once

#include "clipcue/Job.h"

namespace clipcue {

JobResult ExecuteFileOperationJob(const JobRequest& job, HWND owner = nullptr);
bool RecyclePath(const std::wstring& path, HWND owner = nullptr, std::wstring* error = nullptr);

}  // namespace clipcue
