  MakeLabel(s, kPagePaths, L"Review copied/cut paths, mark copy vs. move, and apply the prepared batch to a target folder.", 46, 362, 590, 20);
  s->fileQueue = MakePageControlEx(s, kPagePaths, WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                   46, 388, 386, 126, IDC_CLIPBOARD_FILES);
  s->fileQueueDetails = MakePageControlEx(s, kPagePaths, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                          444, 388, 198, 126, IDC_QUEUE_DETAILS, s->monoFont);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Set Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 46, 528, 84, 30, IDC_QUEUE_COPY);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Set Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 138, 528, 84, 30, IDC_QUEUE_MOVE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 230, 528, 70, 30, IDC_QUEUE_SKIP);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 308, 528, 84, 30, IDC_QUEUE_ACTIVATE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Save selection", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 404, 528, 116, 30, IDC_SAVE_FILE_QUEUE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Archive active", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 530, 528, 112, 30, IDC_CLEAR_FILE_QUEUE);

  MakeLabel(s, kPagePaths, L"Apply workflow", 696, 338, 220, 24, s->sectionFont);
  s->queueSummary = MakePageControl(s, kPagePaths, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 696, 366, 250, 74, IDC_QUEUE_SUMMARY);
  MakeLabel(s, kPagePaths, L"Target folder", 696, 448, 160, 20);
  s->queueTarget = MakePageControlEx(s, kPagePaths, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     696, 470, 200, 26, IDC_QUEUE_TARGET);
  MakePageControl(s, kPagePaths, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 904, 468, 42, 30, IDC_QUEUE_PICK_TARGET);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Apply all", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 696, 512, 78, 30, IDC_QUEUE_APPLY_ALL);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Copy only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 782, 512, 78, 30, IDC_QUEUE_APPLY_COPY);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Move only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 868, 512, 78, 30, IDC_QUEUE_APPLY_MOVE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Open target", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 696, 550, 98, 28, IDC_OPEN_SELECTED_TARGET);

  MakeLabel(s, kPageText, L"Clipboard text history", 46, kPageTop, 260, 24, s->sectionFont);
  MakeLabel(s, kPageText, L"Select one or more clips; ClipCue merges them into the editor.", 46, 152, 386, 20);
  s->textHistory = MakePageControlEx(s, kPageText, WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                     46, 180, 394, 330, IDC_CLIPBOARD_TEXTS);
  MakePageControl(s, kPageText, L"BUTTON", L"Select all", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 46, 528, 92, 30, IDC_SELECT_ALL_TEXT);
  MakePageControl(s, kPageText, L"BUTTON", L"Clear editor", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 148, 528, 102, 30, IDC_CLEAR_TEXT);

  MakeLabel(s, kPageText, L"Text editing lab", 492, kPageTop, 260, 24, s->sectionFont);
  MakeLabel(s, kPageText, L"Clean, combine, copy, or paste text back to the previous app.", 492, 152, 430, 20);
  s->textEditor = MakePageControlEx(s, kPageText, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL | ES_WANTRETURN,
                                    492, 180, 462, 330, IDC_TEXT_EDITOR, s->monoFont);
  s->textMeta = MakePageControl(s, kPageText, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 492, 518, 462, 32, IDC_TEXT_META);
  MakePageControl(s, kPageText, L"BUTTON", L"Normalize spaces", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 492, 558, 128, 30, IDC_NORMALIZE_TEXT);
  MakePageControl(s, kPageText, L"BUTTON", L"Copy edited text", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 630, 558, 132, 30, IDC_COPY_TEXT);
  MakePageControl(s, kPageText, L"BUTTON", L"Paste to previous app", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 772, 558, 152, 30, IDC_PASTE_TEXT);

  MakeLabel(s, kPageStatus, L"Monitor, store, and integration", 46, kPageTop, 360, 24, s->sectionFont);
  MakeLabel(s, kPageStatus, L"Use this page for diagnostics, Explorer menu registration, and maintenance.", 46, 152, 650, 20);
  s->status = MakePageControlEx(s, kPageStatus, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                46, 180, 908, 132, IDC_STATUS, s->monoFont);
  MakeLabel(s, kPageStatus, L"Recent ClipCue operations", 46, 362, 330, 24, s->sectionFont);
  s->history = MakePageControlEx(s, kPageStatus, WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                                 46, 392, 908, 162, IDC_HISTORY);
  MakePageControl(s, kPageStatus, L"BUTTON", L"Register Explorer menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 46, 562, 164, 30, IDC_REGISTER_MENU);
  MakePageControl(s, kPageStatus, L"BUTTON", L"Unregister menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 220, 562, 138, 30, IDC_UNREGISTER_MENU);
  MakePageControl(s, kPageStatus, L"BUTTON", L"Run cleanup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 368, 562, 112, 30, IDC_CLEANUP);

  MakeControl(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 706, 92, 30, IDC_REFRESH);
  MakeControl(s, L"BUTTON", L"Open data folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 126, 706, 134, 30, IDC_OPEN_DATA);
  MakeControl(s, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 270, 706, 118, 30, IDC_BUILD_CACHE);
  MakeControl(s, L"BUTTON", L"Open installer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 398, 706, 120, 30, IDC_OPEN_INSTALLER);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 916, 706, 94, 30, IDC_CLOSE);

  Refresh(s);
  SetWindowTextW(s->fileQueueDetails, L"Select one queue entry to inspect its action, repeat count, and full paths.");
  ShowPage(s, kPagePaths);
}

void OnCreateQueue(UiState* s) {
  s->background = CreateSolidBrush(kWindowBg);
  s->editBrush = CreateSolidBrush(RGB(255, 255, 255));
  s->font = MakeFont(9);
  s->titleFont = MakeFont(21, FW_SEMIBOLD);
