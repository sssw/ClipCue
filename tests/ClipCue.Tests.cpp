#include "clipcue/History.h"
#include "clipcue/Job.h"
#include "clipcue/PathUtils.h"
#include "clipcue/CryptoStore.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace clipcue;

int wmain() {
  std::wstring protectedText;
  std::wstring error;
  bool ok = ProtectTextCurrentUser(L"hello clipcue", &protectedText, &error);
  assert(ok && !protectedText.empty());
  std::wstring plain;
  ok = UnprotectTextCurrentUser(protectedText, &plain, &error);
  assert(ok && plain == L"hello clipcue");

  JobRequest job;
  job.op = OperationKind::Copy;
  job.targetDir = L"C:\\Temp";
  job.sources = {L"C:\\Users\\Test\\a.txt"};
  std::wstring temp = CreateTempClipCueFile(L".job");
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

  std::wstring store = CreateTempClipCueFile(L".db");
  std::wstring cache = CreateTempClipCueFile(L".tsv");
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

  ClipboardHistoryEntry fileClip;
  fileClip.sequence = 42;
  fileClip.kind = ClipboardContentKind::Files;
  fileClip.op = OperationKind::Copy;
  fileClip.selected = true;
  fileClip.files = {L"C:\\Users\\Test\\Inbox\\a.txt", L"C:\\Users\\Test\\Downloads\\b.txt"};
  ok = db.AppendClipboardEntry(fileClip, &error);
  assert(ok);
  fileClip.sequence = 44;
  ok = db.AppendClipboardEntry(fileClip, &error);
  assert(ok);

  ClipboardHistoryEntry textClip;
  textClip.sequence = 43;
  textClip.kind = ClipboardContentKind::Text;
  textClip.op = OperationKind::Copy;
  textClip.text = L"first\r\nsecond";
  ok = db.AppendClipboardEntry(textClip, &error);
  assert(ok);
  textClip.sequence = 45;
  ok = db.AppendClipboardEntry(textClip, &error);
  assert(ok);

  HistoryDatabase loadedDb(store);
  ok = loadedDb.Load(&error);
  assert(ok);
  assert(loadedDb.clipboardEntries().size() == 2);
  assert(loadedDb.clipboardEntries()[0].repeatCount == 2);
  assert(loadedDb.clipboardEntries()[1].repeatCount == 2);
  assert(loadedDb.GetSelectedFileClipboardEntries(OperationKind::Copy).size() == 1);
  assert(loadedDb.HasClipboardEntry(44, ClipboardContentKind::Files));

  ok = db.WriteMenuCache(cache, &error);
  assert(ok);
  std::ifstream cacheFile(cache, std::ios::binary);
  std::string cacheText((std::istreambuf_iterator<char>(cacheFile)), std::istreambuf_iterator<char>());
  assert(cacheText.find("*\tcopy\t") != std::string::npos);
  assert(cacheText.find("*\tmove\t") != std::string::npos);
  assert(cacheText.find("\tmove\tC:\\Users\\Test\\Archive\tC:\\Users\\Test\\Archive") != std::string::npos);
  assert(cacheText.find("\tcopy\tC:\\Users\\Test\\Sorted\tC:\\Users\\Test\\Sorted") != std::string::npos);
  assert(cacheText.find("QUEUE\tcopy\t2") != std::string::npos);
  assert(cacheText.find("QUEUE_HISTORY\t1") != std::string::npos);
  assert(cacheText.find("TEXT\t1") != std::string::npos);
  assert(cacheText.find("C:\\Users\\Test\\Archive") != std::string::npos);
  assert(cacheText.find("C:\\Users\\Test\\Sorted") != std::string::npos);

  ok = loadedDb.MarkSelectedFileClipboardEntriesStale(&error);
  assert(ok);
  ok = loadedDb.Load(&error);
  assert(ok);
  assert(loadedDb.GetSelectedFileClipboardEntries(OperationKind::Copy).empty());
  assert(loadedDb.clipboardEntries()[0].stale);
  ClipboardHistoryEntry edited = loadedDb.clipboardEntries()[0];
  edited.selected = true;
  edited.stale = false;
  edited.op = OperationKind::Move;
  ok = loadedDb.UpdateClipboardEntry(edited, &error);
  assert(ok);
  ok = loadedDb.Load(&error);
  assert(ok);
  assert(loadedDb.GetSelectedFileClipboardEntries(OperationKind::Move).size() == 1);
  assert(loadedDb.GetSelectedFileClipboardEntries(OperationKind::Copy).empty());
  DeleteFileW(store.c_str());
  DeleteFileW(cache.c_str());

  std::wcout << L"ClipCue smoke tests passed\n";
  return 0;
}
