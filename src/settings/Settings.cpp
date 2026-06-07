#include "clipcue/History.h"
#include "clipcue/CryptoStore.h"
#include "clipcue/WinUtils.h"

#include <iostream>

using namespace clipcue;

namespace {
void Usage() {
  std::wcout << L"ClipCue.Settings " << CLIPCUE_VERSION << L"\n"
             << L"Usage:\n"
             << L"  ClipCue.Settings show\n"
             << L"  ClipCue.Settings add-pin copy|move|both <target-folder> [label]\n"
             << L"  ClipCue.Settings clipboard\n"
             << L"  ClipCue.Settings queue-clear\n"
             << L"  ClipCue.Settings cleanup [days]\n"
             << L"  ClipCue.Settings build-cache\n";
}

void PrintCandidates(const HistoryDatabase& db, OperationKind op, const std::wstring& title) {
  auto candidates = db.GetGlobalCandidates(op, 8);
  std::wcout << title << L":\n";
  if (candidates.empty()) {
    std::wcout << L"  (none)\n";
    return;
  }
  for (const auto& c : candidates) {
    std::wcout << L"  [" << (c.pinned ? L"pinned" : L"detected") << L"] "
               << (c.label.empty() ? FormatMenuLabel(c.destParent) : c.label)
               << L" -> " << c.destParent << L"\n";
  }
}

void PrintClipboardEntries(const HistoryDatabase& db) {
  int selectedFiles = 0;
  int fileEntries = 0;
  int textEntries = 0;
  std::wcout << L"Clipboard history:\n";
  for (auto it = db.clipboardEntries().rbegin(); it != db.clipboardEntries().rend(); ++it) {
    if (it->kind == ClipboardContentKind::Files) {
      ++fileEntries;
      if (it->selected && !it->stale) selectedFiles += static_cast<int>(it->files.size());
      const wchar_t* state = it->selected && !it->stale ? L"active" : (it->stale ? L"history" : L"off");
      std::wcout << L"  [" << state << L"] [" << ToString(it->op) << L"] "
                 << it->files.size() << L" file(s)";
      if (it->repeatCount > 1) std::wcout << L" x" << it->repeatCount;
      std::wcout << L", id=" << it->id << L"\n";
      if (!it->files.empty()) std::wcout << L"    " << it->files.front() << L"\n";
    } else if (it->kind == ClipboardContentKind::Text) {
      ++textEntries;
      std::wstring preview = it->text;
      for (auto& ch : preview) if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
      if (preview.size() > 96) preview = preview.substr(0, 93) + L"...";
      std::wcout << L"  [text] " << it->text.size() << L" char(s)";
      if (it->repeatCount > 1) std::wcout << L" x" << it->repeatCount;
      std::wcout << L", id=" << it->id << L"\n"
                 << L"    " << preview << L"\n";
    }
  }
  std::wcout << L"Summary: " << fileEntries << L" file entr" << (fileEntries == 1 ? L"y" : L"ies")
             << L", " << selectedFiles << L" selected file(s), " << textEntries << L" text entr"
             << (textEntries == 1 ? L"y" : L"ies") << L"\n";
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
    std::wcout << L"Clipboard entries: " << db.clipboardEntries().size() << L"\n";
    for (const auto& p : db.pinnedTargets()) {
      std::wcout << L"  [" << ToString(p.op) << L"] " << p.destParent << L"\n";
    }
    PrintCandidates(db, OperationKind::Move, L"ClipCue Move to suggestions");
    PrintCandidates(db, OperationKind::Copy, L"ClipCue Copy to suggestions");
    return 0;
  }

  if (cmd == L"clipboard") {
    PrintClipboardEntries(db);
    return 0;
  }

  if (cmd == L"queue-clear") {
    if (!db.ClearSelectedFileClipboardEntries(&err)) {
      std::wcerr << L"Queue clear failed: " << err << L"\n";
      return 1;
    }
    db.Load(nullptr);
    db.WriteMenuCache(L"", nullptr);
    std::wcout << L"Selected file clipboard queue cleared.\n";
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
