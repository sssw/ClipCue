    } else if (entry.kind == ClipboardContentKind::Text) {
      ++textEntries;
    }
  }
  std::wstringstream ss;
  ss << L"Pinned " << db.pinnedTargets().size()
     << L"  |  Active queue " << activeEntries << L" / " << activeFiles << L" paths"
     << L"  |  Text clips " << textEntries
     << L"  |  Records " << db.operations().size();
  if (!extra.empty()) ss << L"\r\n" << extra;
  else ss << L"\r\nSelect targets, prepare the clipboard queue, then apply copy/move operations from one workspace.";
  return ss.str();
}

void AddCandidateLines(HWND list, UiState* s, const HistoryDatabase& db, OperationKind op, const std::wstring& title) {
  auto candidates = db.GetGlobalCandidates(op, 8);
  if (candidates.empty()) return;
  SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
  s->suggestionTargetsData.push_back(L"");
  for (const auto& c : candidates) {
    std::wstring origin = c.pinned ? L"Pinned" : L"Detected";
    std::wstring label = c.label.empty() ? FormatMenuLabel(c.destParent) : c.label;
    std::wstring line = L"  [" + origin + L"]  " + label + L"  ->  " + c.destParent;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    s->suggestionTargetsData.push_back(c.destParent);
  }
}

std::wstring GetWindowTextString(HWND hwnd) {
  int len = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
  if (len > 0) GetWindowTextW(hwnd, text.data(), len + 1);
  text.resize(static_cast<std::size_t>(len));
  return text;
}

void UpdateTextMeta(UiState* s) {
  if (!s || !s->textMeta || !s->textEditor) return;
  std::wstring text = GetWindowTextString(s->textEditor);
  std::size_t lines = text.empty() ? 0 : 1 + static_cast<std::size_t>(std::count(text.begin(), text.end(), L'\n'));
  std::wstringstream ss;
  ss << text.size() << L" characters";
  if (!text.empty()) ss << L" across " << lines << L" line" << (lines == 1 ? L"" : L"s");
  ss << L". Select multiple clips to merge them, then copy or paste the edited result.";
  SetWindowTextW(s->textMeta, ss.str().c_str());
}

void Refresh(UiState* s, const std::wstring& extra = L"") {
  if (!s) return;
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  std::wstring message = !err.empty() && extra.empty() ? err : extra;
  if (s->dashboard) SetWindowTextW(s->dashboard, DashboardText(db, message).c_str());
  if (s->status) SetWindowTextW(s->status, StatusText(db, message).c_str());
  if (s->queueSummary) SetWindowTextW(s->queueSummary, QueueSummaryText(db, message).c_str());

  s->pinsData = db.pinnedTargets();
  if (s->pins) {
    SendMessageW(s->pins, LB_RESETCONTENT, 0, 0);
    for (const auto& pin : s->pinsData) {
      std::wstring line = L"[" + OperationDisplay(pin.op) + L"]  " +
                          (pin.label.empty() ? FormatMenuLabel(pin.destParent) : pin.label) +
                          L"  ->  " + pin.destParent;
      SendMessageW(s->pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (s->pinsData.empty()) SendMessageW(s->pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No pinned quick targets yet. Click Add target to pin a folder."));
  }

  if (s->suggestions) {
    s->suggestionTargetsData.clear();
    SendMessageW(s->suggestions, LB_RESETCONTENT, 0, 0);
    AddCandidateLines(s->suggestions, s, db, OperationKind::Move, L"Move suggestions");
    AddCandidateLines(s->suggestions, s, db, OperationKind::Copy, L"Copy suggestions");
    if (SendMessageW(s->suggestions, LB_GETCOUNT, 0, 0) == 0) {
      SendMessageW(s->suggestions, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No detected quick targets yet. Use ClipCue from Explorer to teach suggestions."));
      s->suggestionTargetsData.push_back(L"");
    }
  }

  if (s->history) {
    SendMessageW(s->history, LB_RESETCONTENT, 0, 0);
    const auto& ops = db.operations();
    int shown = 0;
    for (auto it = ops.rbegin(); it != ops.rend() && shown < 120; ++it, ++shown) {
      std::wstring line = FormatTime(it->timestamp) + L"  [" + OperationDisplay(it->op) + L"]  " +
                          FormatMenuLabel(it->sourceParent) + L"  ->  " + FormatMenuLabel(it->destParent) +
                          L"  (" + it->result + L")";
      SendMessageW(s->history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (shown == 0) SendMessageW(s->history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No file operations recorded yet."));
  }

  s->fileEntriesData.clear();
  s->textEntriesData.clear();
  if (s->fileQueue) SendMessageW(s->fileQueue, LB_RESETCONTENT, 0, 0);
  if (s->textHistory) SendMessageW(s->textHistory, LB_RESETCONTENT, 0, 0);
  for (auto it = db.clipboardEntries().rbegin(); it != db.clipboardEntries().rend(); ++it) {
    if (it->kind == ClipboardContentKind::Files) {
      int index = static_cast<int>(s->fileEntriesData.size());
      s->fileEntriesData.push_back(*it);
      if (s->fileQueue) {
        std::wstring line = ClipboardEntryLine(*it);
        SendMessageW(s->fileQueue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        SendMessageW(s->fileQueue, LB_SETSEL, (it->selected && !it->stale) ? TRUE : FALSE, index);
      }
    } else if (it->kind == ClipboardContentKind::Text) {
      if (s->textEntriesData.size() >= 300) continue;
      s->textEntriesData.push_back(*it);
      if (s->textHistory) {
        std::wstring line = ClipboardEntryLine(*it);
        SendMessageW(s->textHistory, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
      }
    }
  }
  if (s->fileQueue && s->fileEntriesData.empty()) SendMessageW(s->fileQueue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No path clipboard queue yet. Copy or cut files in Explorer, then return here."));
  if (s->textHistory && s->textEntriesData.empty()) SendMessageW(s->textHistory, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No text clipboard history yet. Copy text while ClipCue Monitor is running."));
  UpdateTextMeta(s);
}

std::wstring PickFolder(HWND owner, const std::wstring& title, const std::wstring& initial = L"") {
  CoInitializeScope co;
  if (FAILED(co.hr())) return L"";
  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) return L"";
  DWORD opts = 0;
  dialog->GetOptions(&opts);
  dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(title.c_str());
