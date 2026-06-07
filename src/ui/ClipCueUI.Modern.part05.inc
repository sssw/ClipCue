  Refresh(s, L"Path clipboard queue selection saved.");
}

void OnQueueSelectionChanged(UiState* s) {
  auto selected = SelectedListIndices(s->fileQueue);
  if (selected.size() != 1 || selected[0] < 0 || static_cast<std::size_t>(selected[0]) >= s->fileEntriesData.size()) {
    SetWindowTextW(s->fileQueueDetails, L"Select one queue entry to inspect its action, repeat count, and full paths.");
    return;
  }
  const auto& entry = s->fileEntriesData[static_cast<std::size_t>(selected[0])];
  SetWindowTextW(s->fileQueueDetails, ClipboardEntryDetails(entry).c_str());
}

void UpdateSelectedQueueEntries(UiState* s, OperationKind op, bool active, bool stale, const std::wstring& message) {
  auto selected = SelectedListIndices(s->fileQueue);
  if (selected.empty()) {
    MessageBoxW(s->hwnd, L"Select one or more queue entries first.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }

  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  bool changed = false;
  for (int index : selected) {
    if (index < 0 || static_cast<std::size_t>(index) >= s->fileEntriesData.size()) continue;
    ClipboardHistoryEntry entry = s->fileEntriesData[static_cast<std::size_t>(index)];
    entry.selected = active;
    entry.stale = stale;
    if (op != OperationKind::Unknown) entry.op = op;
    if (!db.UpdateClipboardEntry(entry, &err)) {
      MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
      return;
    }
    changed = true;
  }

  if (changed) {
    db.Load(nullptr);
    db.WriteMenuCache(L"", nullptr);
    Refresh(s, message);
  }
}

void OnQueueCopy(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Copy, true, false, L"Selected queue entries set to Copy.");
}

void OnQueueMove(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Move, true, false, L"Selected queue entries set to Move.");
}

void OnQueueSkip(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Unknown, false, false, L"Selected queue entries set to Skip.");
}

void OnQueueActivate(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Unknown, true, false, L"Selected queue entries activated.");
}

void OnClearFileQueue(UiState* s) {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  if (!db.MarkSelectedFileClipboardEntriesStale(&err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  db.Load(nullptr);
  db.WriteMenuCache(L"", nullptr);
  Refresh(s, L"Active path clipboard queue moved to history.");
}

void OnPickQueueTarget(UiState* s) {
  std::wstring folder = PickFolder(s->hwnd, L"Choose a target folder for the active queue", QueueTargetText(s));
  if (!folder.empty() && s->queueTarget) SetWindowTextW(s->queueTarget, folder.c_str());
}

void OnOpenControlPanel(UiState* s) {
  std::wstring ui = GetProgramPath();
  ShellExecuteW(s ? s->hwnd : nullptr, L"open", ui.c_str(), nullptr, ModuleDir().c_str(), SW_SHOWNORMAL);
}

void OnApplyQueue(UiState* s, const std::wstring& op) {
  std::wstring target = QueueTargetText(s);
  if (target.empty() || !DirectoryExists(target)) {
    MessageBoxW(s->hwnd, L"Choose an existing target folder first.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }

  std::wstring params = L"--paste-queue --target " + QuoteArg(target);
  if (!op.empty()) params += L" --op " + op;
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  std::wstring agent = AgentPath();
  sei.lpFile = agent.c_str();
  sei.lpParameters = params.c_str();
  sei.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&sei)) {
    std::wstring msg = L"Unable to open ClipCue.Agent.exe: " + GetLastErrorMessage();
    MessageBoxW(s->hwnd, msg.c_str(), L"ClipCue", MB_ICONERROR);
  }
}

bool SetClipboardUnicodeText(HWND owner, const std::wstring& text, std::wstring* error = nullptr) {
  if (!OpenClipboard(owner)) {
    if (error) *error = L"Unable to open clipboard: " + GetLastErrorMessage();
    return false;
  }
  EmptyClipboard();
  SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!memory) {
    CloseClipboard();
    if (error) *error = L"Unable to allocate clipboard memory.";
    return false;
  }
  void* raw = GlobalLock(memory);
  if (!raw) {
    GlobalFree(memory);
    CloseClipboard();
    if (error) *error = L"Unable to lock clipboard memory.";
    return false;
  }
  std::memcpy(raw, text.c_str(), bytes);
  GlobalUnlock(memory);
  if (!SetClipboardData(CF_UNICODETEXT, memory)) {
    GlobalFree(memory);
    CloseClipboard();
    if (error) *error = L"Unable to set clipboard text: " + GetLastErrorMessage();
    return false;
  }
  CloseClipboard();
  return true;
}

void SendCtrlV() {
  INPUT input[4]{};
  input[0].type = INPUT_KEYBOARD;
  input[0].ki.wVk = VK_CONTROL;
  input[1].type = INPUT_KEYBOARD;
  input[1].ki.wVk = 'V';
  input[2].type = INPUT_KEYBOARD;
  input[2].ki.wVk = 'V';
  input[2].ki.dwFlags = KEYEVENTF_KEYUP;
  input[3].type = INPUT_KEYBOARD;
  input[3].ki.wVk = VK_CONTROL;
  input[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, input, sizeof(INPUT));
}

void OnTextSelectionChanged(UiState* s) {
  std::wstring combined;
  for (int index : SelectedListIndices(s->textHistory)) {
    if (index < 0 || static_cast<std::size_t>(index) >= s->textEntriesData.size()) continue;
    if (!combined.empty()) combined += L"\r\n\r\n";
    combined += s->textEntriesData[static_cast<std::size_t>(index)].text;
  }
  if (!combined.empty()) SetWindowTextW(s->textEditor, combined.c_str());
  UpdateTextMeta(s);
}

void OnSelectAllText(UiState* s) {
  if (!s || !s->textHistory) return;
  SendMessageW(s->textHistory, LB_SETSEL, TRUE, static_cast<LPARAM>(-1));
  OnTextSelectionChanged(s);
}

std::wstring NormalizeTextForEditor(std::wstring text) {
  std::wstring out;
  bool previousSpace = false;
  for (wchar_t ch : text) {
    bool space = std::iswspace(ch) != 0;
    if (space) {
      if (!previousSpace) out.push_back(L' ');
      previousSpace = true;
    } else {
      out.push_back(ch);
      previousSpace = false;
    }
  }
  while (!out.empty() && out.front() == L' ') out.erase(out.begin());
  while (!out.empty() && out.back() == L' ') out.pop_back();
  return out;
}

void OnNormalizeEditedText(UiState* s) {
  if (!s || !s->textEditor) return;
  SetWindowTextW(s->textEditor, NormalizeTextForEditor(GetWindowTextString(s->textEditor)).c_str());
  UpdateTextMeta(s);
}

void OnClearEditedText(UiState* s) {
  if (s && s->textEditor) SetWindowTextW(s->textEditor, L"");
  UpdateTextMeta(s);
}

