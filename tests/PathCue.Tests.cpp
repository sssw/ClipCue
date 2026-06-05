#include "pathcue/History.h"
#include "pathcue/Job.h"
#include "pathcue/PathUtils.h"
#include "pathcue/CryptoStore.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

  std::wstring store = CreateTempPathCueFile(L".db");
  std::wstring cache = CreateTempPathCueFile(L".tsv");
  DeleteFileW(store.c_str());
  DeleteFileW(cache.c_str());

  HistoryDatabase db(store);
  OperationRecord copyRecord;
  copyRecord.op = OperationKind::Copy;
  copyRecord.sourceParent = L"C:\\Users\\Test\\Inbox";
  copyRecord.destParent = L"C:\\Users\\Test\\Archive";
  copyRecord.result = L"inferred";
  copyRecord.observedBy = L"external_clipboard_shell_create";
  ok = db.AppendOperation(copyRecord, &error);
  assert(ok);

  OperationRecord moveRecord;
  moveRecord.op = OperationKind::Move;
  moveRecord.sourceParent = L"C:\\Users\\Test\\Downloads";
  moveRecord.destParent = L"C:\\Users\\Test\\Sorted";
  moveRecord.result = L"inferred";
  moveRecord.observedBy = L"external_shell_rename";
  ok = db.AppendOperation(moveRecord, &error);
  assert(ok);

  ok = db.WriteMenuCache(cache, &error);
  assert(ok);
  std::ifstream cacheFile(cache, std::ios::binary);
  std::string cacheText((std::istreambuf_iterator<char>(cacheFile)), std::istreambuf_iterator<char>());
  assert(cacheText.find("*\tcopy\t") != std::string::npos);
  assert(cacheText.find("*\tmove\t") != std::string::npos);
  assert(cacheText.find("C:\\Users\\Test\\Archive") != std::string::npos);
  assert(cacheText.find("C:\\Users\\Test\\Sorted") != std::string::npos);
  DeleteFileW(store.c_str());
  DeleteFileW(cache.c_str());

  std::wcout << L"PathCue smoke tests passed\n";
  return 0;
}
