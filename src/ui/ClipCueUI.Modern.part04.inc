  if (!initial.empty()) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
      dialog->SetFolder(item);
      item->Release();
    }
  }
  std::wstring selected;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR raw = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
        selected = raw;
        CoTaskMemFree(raw);
      }
      item->Release();
    }
  }
  dialog->Release();
  return selected;
}

OperationKind SelectedOp(HWND combo) {
  int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (sel == 1) return OperationKind::Move;
  if (sel == 2) return OperationKind::Copy;
  return OperationKind::Both;
}

HRESULT CallShellRegistration(bool reg) {
  std::wstring dll = ShellDllPath();
  HMODULE h = LoadLibraryW(dll.c_str());
  if (!h) return HRESULT_FROM_WIN32(GetLastError());
  using Fn = HRESULT(__stdcall*)();
  Fn fn = reinterpret_cast<Fn>(GetProcAddress(h, reg ? "DllRegisterServer" : "DllUnregisterServer"));
  HRESULT hr = fn ? fn() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
  FreeLibrary(h);
  return hr;
}

void LaunchPath(const std::wstring& path) {
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring QueueTargetText(UiState* s) {
  return s && s->queueTarget ? GetWindowTextString(s->queueTarget) : L"";
}

std::wstring SelectedPinnedTarget(UiState* s) {
  if (!s || !s->pins) return L"";
  int sel = static_cast<int>(SendMessageW(s->pins, LB_GETCURSEL, 0, 0));
  if (sel >= 0 && static_cast<std::size_t>(sel) < s->pinsData.size()) return s->pinsData[static_cast<std::size_t>(sel)].destParent;
  return L"";
}

std::wstring SelectedSuggestedTarget(UiState* s) {
  if (!s || !s->suggestions) return L"";
  int sel = static_cast<int>(SendMessageW(s->suggestions, LB_GETCURSEL, 0, 0));
  if (sel >= 0 && static_cast<std::size_t>(sel) < s->suggestionTargetsData.size()) return s->suggestionTargetsData[static_cast<std::size_t>(sel)];
  return L"";
}

std::wstring CurrentTargetFromSelection(UiState* s) {
  if (!s) return L"";
  HWND focus = GetFocus();
  if (focus == s->suggestions) {
    std::wstring target = SelectedSuggestedTarget(s);
    if (!target.empty()) return target;
  }
  if (focus == s->pins) {
    std::wstring target = SelectedPinnedTarget(s);
    if (!target.empty()) return target;
  }
  std::wstring target = SelectedSuggestedTarget(s);
  if (!target.empty()) return target;
  target = SelectedPinnedTarget(s);
  if (!target.empty()) return target;
  return QueueTargetText(s);
}

void OnUseSelectedTarget(UiState* s) {
  if (!s || !s->queueTarget) return;
  std::wstring target = CurrentTargetFromSelection(s);
  if (target.empty()) {
    MessageBoxW(s->hwnd, L"Select a pinned or detected target first.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }
  SetWindowTextW(s->queueTarget, target.c_str());
}

void OnOpenSelectedTarget(UiState* s) {
  std::wstring target = CurrentTargetFromSelection(s);
  if (target.empty()) {
    MessageBoxW(s->hwnd, L"Choose or select a target folder first.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }
  LaunchPath(target);
}

void OnAddPin(UiState* s) {
  std::wstring folder = PickFolder(s->hwnd, L"Pin a ClipCue target folder", QueueTargetText(s));
  if (folder.empty()) return;
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  PinnedTarget pin;
  pin.op = SelectedOp(s->opCombo);
  pin.destParent = folder;
  pin.label = FormatMenuLabel(folder);
  if (!db.AddPinnedTarget(pin, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  db.WriteMenuCache(L"", &err);
  if (s->queueTarget) SetWindowTextW(s->queueTarget, folder.c_str());
  Refresh(s, L"Pinned target added, selected as the queue target, and menu cache rebuilt.");
}

void OnRemovePin(UiState* s) {
  int sel = static_cast<int>(SendMessageW(s->pins, LB_GETCURSEL, 0, 0));
  if (sel < 0 || static_cast<std::size_t>(sel) >= s->pinsData.size()) {
    MessageBoxW(s->hwnd, L"Select a pinned target first.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }
  if (MessageBoxW(s->hwnd, L"Remove the selected pinned target?", L"ClipCue", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  if (!db.RemovePinnedTarget(static_cast<std::size_t>(sel), &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  db.WriteMenuCache(L"", &err);
  Refresh(s, L"Pinned target removed and menu cache rebuilt.");
}

void OnBuildCache(UiState* s) {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  if (!db.WriteMenuCache(L"", &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  Refresh(s, L"Explorer menu cache rebuilt with the latest pins, suggestions, and queue counts.");
}

void OnCleanup(UiState* s) {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  if (!db.CleanupExpired(90, 365, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  db.Load(nullptr);
  db.WriteMenuCache(L"", nullptr);
  Refresh(s, L"Expired operation records cleaned. Pinned targets and protected user data were kept.");
}

std::vector<int> SelectedListIndices(HWND list) {
  std::vector<int> indices;
  int count = static_cast<int>(SendMessageW(list, LB_GETSELCOUNT, 0, 0));
  if (count <= 0) {
    int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel >= 0) indices.push_back(sel);
    return indices;
  }
  indices.resize(static_cast<std::size_t>(count));
  SendMessageW(list, LB_GETSELITEMS, static_cast<WPARAM>(count), reinterpret_cast<LPARAM>(indices.data()));
  return indices;
}

void OnSaveFileQueueSelection(UiState* s) {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);

  std::vector<std::wstring> allFileIds;
  for (const auto& entry : s->fileEntriesData) allFileIds.push_back(entry.id);
  db.SetClipboardEntriesSelected(allFileIds, false, &err);

  std::vector<std::wstring> selectedIds;
  for (int index : SelectedListIndices(s->fileQueue)) {
    if (index >= 0 && static_cast<std::size_t>(index) < s->fileEntriesData.size()) {
      selectedIds.push_back(s->fileEntriesData[static_cast<std::size_t>(index)].id);
    }
  }
  db.SetClipboardEntriesSelected(selectedIds, true, &err);
  db.Load(nullptr);
  db.WriteMenuCache(L"", nullptr);
