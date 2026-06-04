#include "pathcue/History.h"
#include "pathcue/CryptoStore.h"
#include "pathcue/WinUtils.h"

#include <iostream>

using namespace pathcue;

namespace {
void Usage() {
  std::wcout << L"PathCue.Settings " << PATHCUE_VERSION << L"\n"
             << L"Usage:\n"
             << L"  PathCue.Settings show\n"
             << L"  PathCue.Settings add-pin copy|move|both <target-folder> [label]\n"
             << L"  PathCue.Settings cleanup [days]\n"
             << L"  PathCue.Settings build-cache\n";
}
}

int wmain() {
  auto args = SplitCommandLineArgs();
  if (args.size() < 2) {
    Usage();
    return 0;
  }
  std::wstring cmd = args[1];
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);

  if (cmd == L"show") {
    std::wcout << L"Store: " << EncryptedRecordStore::DefaultStorePath() << L"\n";
    std::wcout << L"Operations: " << db.operations().size() << L"\n";
    std::wcout << L"Pinned targets: " << db.pinnedTargets().size() << L"\n";
    for (const auto& p : db.pinnedTargets()) {
      std::wcout << L"  [" << ToString(p.op) << L"] " << p.destParent << L"\n";
    }
    return 0;
  }

  if (cmd == L"add-pin") {
    if (args.size() < 4) {
      Usage();
      return 1;
    }
    PinnedTarget pin;
    pin.op = OperationFromString(args[2]);
    pin.destParent = args[3];
    if (args.size() >= 5) pin.label = args[4];
    if (!db.AddPinnedTarget(pin, &err)) {
      std::wcerr << L"Failed: " << err << L"\n";
      return 1;
    }
    db.WriteMenuCache(L"", nullptr);
    std::wcout << L"Pinned: " << pin.destParent << L"\n";
    return 0;
  }

  if (cmd == L"cleanup") {
    int days = args.size() >= 3 ? _wtoi(args[2].c_str()) : 90;
    if (!db.CleanupExpired(days, 365, &err)) {
      std::wcerr << L"Cleanup failed: " << err << L"\n";
      return 1;
    }
    db.Load(nullptr);
    db.WriteMenuCache(L"", nullptr);
    return 0;
  }

  if (cmd == L"build-cache") {
    if (!db.WriteMenuCache(L"", &err)) {
      std::wcerr << L"Cache failed: " << err << L"\n";
      return 1;
    }
    std::wcout << L"Cache: " << EncryptedRecordStore::DefaultMenuCachePath() << L"\n";
    return 0;
  }

  Usage();
  return 0;
}
