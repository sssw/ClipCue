#include "pathcue/History.h"
#include "pathcue/Job.h"
#include "pathcue/PathUtils.h"
#include "pathcue/CryptoStore.h"

#include <cassert>
#include <iostream>

using namespace pathcue;

int wmain() {
  std::wstring protectedText;
  std::wstring error;
  bool ok = ProtectTextCurrentUser(L"hello pathcue", &protectedText, &error);
  assert(ok && !protectedText.empty());
  std::wstring plain;
  ok = UnprotectTextCurrentUser(protectedText, &plain, &error);
  assert(ok && plain == L"hello pathcue");

  JobRequest job;
  job.op = OperationKind::Copy;
  job.targetDir = L"C:\\Temp";
  job.sources = {L"C:\\Users\\Test\\a.txt"};
  std::wstring temp = CreateTempPathCueFile(L".job");
  ok = SaveJobFile(job, temp, &error);
  assert(ok);
  JobRequest loaded;
  ok = LoadJobFile(temp, &loaded, &error);
  assert(ok);
  assert(loaded.op == OperationKind::Copy);
  assert(loaded.sources.size() == 1);
  DeleteFileW(temp.c_str());

  std::wstring parent = ParentPath(L"C:\\Users\\Test\\a.txt");
  assert(parent == L"C:\\Users\\Test");

  std::wcout << L"PathCue smoke tests passed\n";
  return 0;
}
