HWND MakeStatic(UiState* s, const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr) {
  return MakeControl(s, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, -1, font);
}

void AddTab(HWND tab, int index, const wchar_t* text) {
  TCITEMW item{};
  item.mask = TCIF_TEXT;
  item.pszText = const_cast<LPWSTR>(text);
  TabCtrl_InsertItem(tab, index, &item);
}

void InvalidateChrome(UiState* s) {
  if (s && s->hwnd) InvalidateRect(s->hwnd, nullptr, TRUE);
}

void ShowPage(UiState* s, int page) {
  if (!s || page < 0 || page >= kPageCount) return;
  s->activePage = page;
  for (int i = 0; i < kPageCount; ++i) {
    for (HWND ctrl : s->pageControls[i]) ShowWindow(ctrl, i == page ? SW_SHOW : SW_HIDE);
  }
  InvalidateChrome(s);
}

std::wstring FormatTime(std::int64_t ts) {
  if (ts <= 0) return L"";
  __time64_t raw = static_cast<__time64_t>(ts);
  tm local{};
  if (_localtime64_s(&local, &raw) != 0) return L"";
  wchar_t buf[64]{};
  wcsftime(buf, _countof(buf), L"%Y-%m-%d %H:%M", &local);
  return buf;
}

std::wstring OperationDisplay(OperationKind op) {
  switch (op) {
    case OperationKind::Copy: return L"Copy";
    case OperationKind::Move: return L"Move";
    case OperationKind::Both: return L"Copy + Move";
    default: return L"Unknown";
  }
}

std::wstring CompactTextPreview(std::wstring text) {
  for (auto& ch : text) {
    if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
  }
  while (text.find(L"  ") != std::wstring::npos) text.erase(text.find(L"  "), 1);
  if (text.size() > 94) return text.substr(0, 91) + L"...";
  return text;
}

std::wstring RepeatSuffix(int repeatCount) {
  if (repeatCount <= 1) return L"";
  return L"  x" + std::to_wstring(repeatCount);
}

std::wstring ClipboardEntryLine(const ClipboardHistoryEntry& entry) {
  std::wstringstream ss;
  ss << FormatTime(entry.timestamp) << L"  ";
  if (entry.kind == ClipboardContentKind::Files) {
    std::wstring state = entry.selected && !entry.stale ? L"Active" : (entry.stale ? L"History" : L"Skipped");
    ss << L"[" << state << L"] [" << OperationDisplay(entry.op) << L"] "
       << entry.files.size() << L" path(s)" << RepeatSuffix(entry.repeatCount);
    if (!entry.files.empty()) ss << L"  " << FormatMenuLabel(entry.files.front());
  } else if (entry.kind == ClipboardContentKind::Text) {
    ss << L"[Text] " << entry.text.size() << L" char(s)" << RepeatSuffix(entry.repeatCount)
       << L"  " << CompactTextPreview(entry.text);
  }
  return ss.str();
}

std::wstring ClipboardEntryDetails(const ClipboardHistoryEntry& entry) {
  std::wstringstream ss;
  ss << L"State: " << (entry.selected && !entry.stale ? L"active" : (entry.stale ? L"history" : L"skipped")) << L"\r\n";
  ss << L"Action: " << OperationDisplay(entry.op) << L"\r\n";
  ss << L"Repeated: " << std::max(1, entry.repeatCount) << L"\r\n";
  ss << L"Last seen: " << FormatTime(entry.timestamp) << L"\r\n";
  if (!entry.sourceApp.empty()) ss << L"Source app: " << entry.sourceApp << L"\r\n";
  ss << L"\r\nPaths:\r\n";
  for (const auto& file : entry.files) ss << L"  " << file << L"\r\n";
  return ss.str();
}

std::wstring StatusText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  int selectedFileEntries = 0;
  int selectedFiles = 0;
  int staleFileEntries = 0;
  int fileEvents = 0;
  int textEntries = 0;
  int textEvents = 0;
  for (const auto& entry : db.clipboardEntries()) {
    int repeats = std::max(1, entry.repeatCount);
    if (entry.kind == ClipboardContentKind::Files) {
      fileEvents += repeats;
      if (entry.selected && !entry.stale) {
        ++selectedFileEntries;
        selectedFiles += static_cast<int>(entry.files.size());
      } else if (entry.stale) {
        ++staleFileEntries;
      }
    } else if (entry.kind == ClipboardContentKind::Text) {
      ++textEntries;
      textEvents += repeats;
    }
  }

  std::wstringstream ss;
  ss << L"Store: " << EncryptedRecordStore::DefaultStorePath() << L"\r\n";
  ss << L"Menu cache: " << EncryptedRecordStore::DefaultMenuCachePath() << L"\r\n";
  ss << L"Pinned targets: " << db.pinnedTargets().size() << L"    Operation records: " << db.operations().size() << L"\r\n";
  ss << L"Active path queue: " << selectedFileEntries << L" entr" << (selectedFileEntries == 1 ? L"y" : L"ies")
     << L", " << selectedFiles << L" path(s)    File clipboard events: " << fileEvents << L"\r\n";
  ss << L"Path queue history: " << staleFileEntries << L" entr" << (staleFileEntries == 1 ? L"y" : L"ies") << L"\r\n";
  ss << L"Text entries: " << textEntries << L"    Text clipboard events: " << textEvents << L"\r\n";
  ss << L"Protection: DPAPI CurrentUser encrypted record store; Explorer menu cache stores only quick-target and queue-count metadata.";
  if (!extra.empty()) ss << L"\r\n\r\nLatest action: " << extra;
  return ss.str();
}

std::wstring QueueSummaryText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  int activeEntries = 0;
  int activeFiles = 0;
  int copyFiles = 0;
  int moveFiles = 0;
  int historyEntries = 0;
  int skippedEntries = 0;
  int repeatedEvents = 0;
  for (const auto& entry : db.clipboardEntries()) {
    if (entry.kind != ClipboardContentKind::Files) continue;
    repeatedEvents += std::max(1, entry.repeatCount);
    if (entry.selected && !entry.stale) {
      ++activeEntries;
      activeFiles += static_cast<int>(entry.files.size());
      if (entry.op == OperationKind::Move) moveFiles += static_cast<int>(entry.files.size());
      else copyFiles += static_cast<int>(entry.files.size());
    } else if (entry.stale) {
      ++historyEntries;
    } else {
      ++skippedEntries;
    }
  }

  std::wstringstream ss;
  ss << L"Active queue: " << activeEntries << L" entr" << (activeEntries == 1 ? L"y" : L"ies")
     << L" / " << activeFiles << L" path(s)\r\n";
  ss << L"Ready to copy: " << copyFiles << L"    Ready to move: " << moveFiles << L"\r\n";
  ss << L"History: " << historyEntries << L"    Skipped: " << skippedEntries << L"    Clipboard events: " << repeatedEvents;
  if (!extra.empty()) ss << L"\r\n" << extra;
  return ss.str();
}

std::wstring DashboardText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  int activeEntries = 0;
  int activeFiles = 0;
  int textEntries = 0;
  for (const auto& entry : db.clipboardEntries()) {
    if (entry.kind == ClipboardContentKind::Files && entry.selected && !entry.stale) {
      ++activeEntries;
      activeFiles += static_cast<int>(entry.files.size());
