
void OnCopyEditedText(UiState* s, bool paste) {
  std::wstring text = GetWindowTextString(s->textEditor);
  if (text.empty()) {
    MessageBoxW(s->hwnd, L"Select or enter text first.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }
  std::wstring err;
  if (!SetClipboardUnicodeText(s->hwnd, text, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }

  if (paste && s->previousForeground && IsWindow(s->previousForeground) && s->previousForeground != s->hwnd) {
    SetForegroundWindow(s->previousForeground);
    Sleep(120);
    SendCtrlV();
  } else if (paste) {
    MessageBoxW(s->hwnd, L"Edited text copied. Switch to the target app and paste.", L"ClipCue", MB_ICONINFORMATION);
  } else {
    Refresh(s, L"Edited text copied to the clipboard.");
  }
}

void OnRegisterMenu(UiState* s, bool reg) {
  HRESULT hr = CallShellRegistration(reg);
  if (FAILED(hr)) {
    std::wstring msg = (reg ? L"Register failed: " : L"Unregister failed: ") + FormatHResult(hr) + L"\r\nDLL: " + ShellDllPath();
    MessageBoxW(s->hwnd, msg.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  Refresh(s, reg ? L"Classic Explorer context menu registered for the current user." : L"Classic Explorer context menu unregistered for the current user.");
}

void PaintCard(HDC hdc, int x, int y, int w, int h) {
  HBRUSH brush = CreateSolidBrush(kPanelBg);
  HPEN pen = CreatePen(PS_SOLID, 1, kPanelBorder);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  RoundRect(hdc, x, y, x + w, y + h, 14, 14);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
}

void PaintChrome(UiState* s, HDC hdc) {
  RECT rc{};
  GetClientRect(s->hwnd, &rc);
  HBRUSH bg = CreateSolidBrush(kWindowBg);
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);

  RECT header{0, 0, rc.right, 82};
  HBRUSH headerBrush = CreateSolidBrush(kHeaderBg);
  FillRect(hdc, &header, headerBrush);
  DeleteObject(headerBrush);

  RECT accent{0, 80, rc.right, 82};
  HBRUSH accentBrush = CreateSolidBrush(kAccent);
  FillRect(hdc, &accent, accentBrush);
  DeleteObject(accentBrush);

  if (s->queueMode) {
    PaintCard(hdc, 22, 106, 570, 438);
    PaintCard(hdc, 610, 106, 358, 438);
    PaintCard(hdc, 22, 562, 946, 116);
    return;
  }

  if (s->activePage == kPagePaths) {
    PaintCard(hdc, 30, 124, 462, 190);
    PaintCard(hdc, 510, 124, 462, 190);
    PaintCard(hdc, 30, 332, 630, 252);
    PaintCard(hdc, 678, 332, 294, 252);
  } else if (s->activePage == kPageText) {
    PaintCard(hdc, 30, 124, 428, 470);
    PaintCard(hdc, 476, 124, 496, 470);
  } else if (s->activePage == kPageStatus) {
    PaintCard(hdc, 30, 124, 942, 214);
    PaintCard(hdc, 30, 356, 942, 238);
  }
}

void CreateSharedChrome(UiState* s, const wchar_t* title, const wchar_t* subtitle) {
  HWND titleCtl = MakeControl(s, L"STATIC", title, WS_CHILD | WS_VISIBLE, 24, 16, 340, 32, -1, s->titleFont);
  SetControlFont(titleCtl, s->titleFont);
  MakeControl(s, L"STATIC", subtitle, WS_CHILD | WS_VISIBLE, 26, 50, 720, 22, -1);
}

void OnCreate(UiState* s) {
  s->background = CreateSolidBrush(kWindowBg);
  s->editBrush = CreateSolidBrush(RGB(255, 255, 255));
  s->font = MakeFont(9);
  s->titleFont = MakeFont(20, FW_SEMIBOLD);
  s->sectionFont = MakeFont(11, FW_SEMIBOLD);
  s->monoFont = MakeFont(9, FW_NORMAL, L"Consolas");

  CreateSharedChrome(s, L"ClipCue", L"Professional control center for Explorer copy, move, path queue, and clipboard text workflows");
  s->dashboard = MakeControl(s, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 590, 16, 410, 56, IDC_DASHBOARD);

  s->tab = MakeControl(s, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 22, 88, 988, 34, IDC_TAB);
  AddTab(s->tab, kPagePaths, L"Targets && path queue");
  AddTab(s->tab, kPageText, L"Text editor");
  AddTab(s->tab, kPageStatus, L"System status");
  TabCtrl_SetCurSel(s->tab, kPagePaths);

  MakeLabel(s, kPagePaths, L"Pinned quick targets", 46, kPageTop, 230, 24, s->sectionFont);
  MakeLabel(s, kPagePaths, L"Pin your safest destinations and decide whether they appear for Copy, Move, or both.", 46, 152, 420, 20);
  s->pins = MakePageControlEx(s, kPagePaths, WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                              46, 178, 428, 76, IDC_PINS);
  MakeLabel(s, kPagePaths, L"New target behavior", 46, 268, 132, 22);
  s->opCombo = MakePageControl(s, kPagePaths, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               184, 264, 134, 160, IDC_OP_COMBO);
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy + Move"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Move only"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy only"));
  SendMessageW(s->opCombo, CB_SETCURSEL, 0, 0);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Add target...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 328, 262, 126, 28, IDC_ADD_PIN);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 46, 282, 98, 26, IDC_REMOVE_PIN);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Use as target", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 154, 282, 112, 26, IDC_USE_SELECTED_TARGET);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 276, 282, 74, 26, IDC_OPEN_SELECTED_TARGET);

  MakeLabel(s, kPagePaths, L"Smart suggestions", 526, kPageTop, 230, 24, s->sectionFont);
  MakeLabel(s, kPagePaths, L"Detected from previous ClipCue and Explorer work; double-click to set target.", 526, 152, 410, 20);
  s->suggestions = MakePageControlEx(s, kPagePaths, WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                     526, 178, 428, 100, IDC_SUGGESTIONS);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Use suggestion", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 526, 286, 126, 26, IDC_USE_SELECTED_TARGET);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Open target", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 662, 286, 112, 26, IDC_OPEN_SELECTED_TARGET);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 784, 286, 118, 26, IDC_BUILD_CACHE);

  MakeLabel(s, kPagePaths, L"Path clipboard queue", 46, 338, 260, 24, s->sectionFont);
