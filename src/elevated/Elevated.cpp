#include "clipcue/FileOps.h"
#include "clipcue/Job.h"
#include "clipcue/WinUtils.h"

#include <iostream>

using namespace clipcue;

int wmain() {
  auto args = SplitCommandLineArgs();
  std::wstring jobFile;
  std::wstring resultFile;
  for (size_t i = 1; i + 1 < args.size(); ++i) {
    if (args[i] == L"--job") jobFile = args[i + 1];
    if (args[i] == L"--result") resultFile = args[i + 1];
  }
  JobResult result;
  if (jobFile.empty()) {
    result.hr = E_INVALIDARG;
    result.message = L"Usage: ClipCue.Elevated --job <jobfile> --result <resultfile>";
  } else {
    JobRequest job;
    std::wstring err;
    if (!LoadJobFile(jobFile, &job, &err)) {
      result.hr = E_FAIL;
      result.message = err;
    } else {
      result = ExecuteFileOperationJob(job, nullptr);
    }
  }
  if (!resultFile.empty()) SaveResultFile(result, resultFile, nullptr);
  return SUCCEEDED(result.hr) && !result.anyAborted ? 0 : 2;
}
