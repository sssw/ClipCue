#include "clipcue/CryptoStore.h"
#include "clipcue/History.h"
#include "clipcue/PathUtils.h"
#include "clipcue/WinUtils.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

using namespace clipcue;

namespace {

constexpr int IDC_STATUS = 1001;
constexpr int IDC_PINS = 1002;
constexpr int IDC_HISTORY = 1003;
constexpr int IDC_OP_COMBO = 1004;
constexpr int IDC_ADD_PIN = 1005;
constexpr int IDC_REMOVE_PIN = 1006;
constexpr int IDC_BUILD_CACHE = 1007;
constexpr int IDC_CLEANUP = 1008;
constexpr int IDC_REFRESH = 1009;
constexpr int IDC_OPEN_DATA = 1010;
constexpr int IDC_REGISTER_MENU = 1011;
constexpr int IDC_UNREGISTER_MENU = 1012;
constexpr int IDC_OPEN_INSTALLER = 1013;
constexpr int IDC_CLOSE = 1014;
constexpr int IDC_SUGGESTIONS = 1015;
constexpr int IDC_CLIPBOARD_FILES = 1016;
constexpr int IDC_CLIPBOARD_TEXTS = 1017;
constexpr int IDC_TEXT_EDITOR = 1018;
constexpr int IDC_SAVE_FILE_QUEUE = 1019;
constexpr int IDC_CLEAR_FILE_QUEUE = 1020;
constexpr int IDC_COPY_TEXT = 1021;
constexpr int IDC_PASTE_TEXT = 1022;
constexpr int IDC_TAB = 1023;
constexpr int IDC_QUEUE_COPY = 1024;
constexpr int IDC_QUEUE_MOVE = 1025;
constexpr int IDC_QUEUE_SKIP = 1026;
constexpr int IDC_QUEUE_ACTIVATE = 1027;
constexpr int IDC_QUEUE_DETAILS = 1028;
constexpr int IDC_QUEUE_SUMMARY = 1029;
constexpr int IDC_QUEUE_TARGET = 1030;
constexpr int IDC_QUEUE_PICK_TARGET = 1031;
constexpr int IDC_QUEUE_APPLY_ALL = 1032;
constexpr int IDC_QUEUE_APPLY_COPY = 1033;
constexpr int IDC_QUEUE_APPLY_MOVE = 1034;
constexpr int IDC_OPEN_PANEL = 1035;
constexpr int IDC_PIN_LABEL = 1036;
constexpr int IDC_PIN_TARGET = 1037;
constexpr int IDC_PICK_PIN_TARGET = 1038;
constexpr int IDC_TEXT_LOAD = 1039;
constexpr int IDC_TEXT_SAVE = 1040;
constexpr int IDC_TEXT_CLEAR = 1041;
constexpr int IDC_OPEN_QUEUE = 1042;
constexpr int IDC_DASHBOARD = 1044;
constexpr int IDC_TEXT_META = 1045;

constexpr COLORREF kPageBg = RGB(244, 247, 251);
constexpr COLORREF kPanelBg = RGB(255, 255, 255);
constexpr COLORREF kBodyText = RGB(51, 65, 85);

constexpr int kMainWidth = 1120;
constexpr int kMainHeight = 780;
constexpr int kQueueWidth = 1060;
constexpr int kQueueHeight = 740;

#ifdef ES_NOHIDESEL
constexpr DWORD kEditorStyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_BORDER | ES_NOHIDESEL;
#else
constexpr DWORD kEditorStyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_BORDER;
#endif

enum UiPage {
  kPageHome = 0,
  kPageTargets = 1,
  kPageQueue = 2,
  kPageText = 3,
  kPageSystem = 4,
  kPageCount = 5,
};

struct UiState {
  HWND hwnd = nullptr;
  HWND tab = nullptr;
  HWND dashboard = nullptr;
  HWND status = nullptr;
  HWND pins = nullptr;
  HWND suggestions = nullptr;
  HWND history = nullptr;
  HWND fileQueue = nullptr;
  HWND fileQueueDetails = nullptr;
  HWND textHistory = nullptr;
  HWND textEditor = nullptr;
  HWND textMeta = nullptr;
  HWND opCombo = nullptr;
  HWND pinLabel = nullptr;
  HWND pinTarget = nullptr;
  HWND queueSummary = nullptr;
  HWND queueTarget = nullptr;
  HFONT font = nullptr;
  HFONT smallFont = nullptr;
  HFONT titleFont = nullptr;
  HFONT heroFont = nullptr;
  HBRUSH background = nullptr;
  HBRUSH panelBrush = nullptr;
  HWND previousForeground = nullptr;
  int activePage = kPageHome;
  bool queueMode = false;
  bool textWrap = false;
  std::vector<HWND> pageControls[kPageCount];
  std::vector<PinnedTarget> pinsData;
  std::vector<ClipboardHistoryEntry> fileEntriesData;
  std::vector<ClipboardHistoryEntry> textEntriesData;
};

std::wstring ModuleDir() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : exe.substr(0, pos);
}

std::wstring ShellDllPath() { return PathCombineSimple(ModuleDir(), L"ClipCue.ShellClassic.dll"); }
std::wstring AgentPath() { return PathCombineSimple(ModuleDir(), L"ClipCue.Agent.exe"); }
std::wstring InstallerPath() { return PathCombineSimple(ModuleDir(), L"ClipCue.Installer.exe"); }

void SetControlFont(HWND h, HFONT font) {
  if (h && font) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND MakeControlEx(UiState* s, DWORD exStyle, const wchar_t* klass, const wchar_t* text, DWORD style,
                   int x, int y, int w, int h, int id, HFONT font = nullptr) {
  HWND ctrl = CreateWindowExW(exStyle, klass, text, style, x, y, w, h, s->hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SetControlFont(ctrl, font ? font : s->font);
  return ctrl;
}

HWND MakeControl(UiState* s, const wchar_t* klass, const wchar_t* text, DWORD style,
                 int x, int y, int w, int h, int id, HFONT font = nullptr) {
  return MakeControlEx(s, 0, klass, text, style, x, y, w, h, id, font);
}

HWND MakePageControl(UiState* s, int page, const wchar_t* klass, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id, HFONT font = nullptr, DWORD exStyle = 0) {
  HWND ctrl = MakeControlEx(s, exStyle, klass, text, style, x, y, w, h, id, font);
  if (ctrl && page >= 0 && page < kPageCount) s->pageControls[page].push_back(ctrl);
  return ctrl;
}

HWND MakeLabel(UiState* s, int page, const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr) {
  return MakePageControl(s, page, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, -1, font);
}

HWND MakeGroup(UiState* s, int page, const wchar_t* text, int x, int y, int w, int h) {
  return MakePageControl(s, page, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, w, h, -1, s->font);
}

void ShowPage(UiState* s, int page) {
  if (!s || page < 0 || page >= kPageCount) return;
  s->activePage = page;
  for (int i = 0; i < kPageCount; ++i) {
    for (HWND ctrl : s->pageControls[i]) ShowWindow(ctrl, i == page ? SW_SHOW : SW_HIDE);
  }
}

void AddTab(HWND tab, int index, const wchar_t* text) {
  TCITEMW item{};
  item.mask = TCIF_TEXT;
  item.pszText = const_cast<LPWSTR>(text);
  TabCtrl_InsertItem(tab, index, &item);
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
  if (text.size() > 110) return text.substr(0, 107) + L"...";
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
    std::wstring state = entry.selected && !entry.stale ? L"active" : (entry.stale ? L"history" : L"skipped");
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

struct DbSummary {
  int pinned = 0;
  int operations = 0;
  int activeQueueEntries = 0;
  int activeQueueFiles = 0;
  int queueHistoryEntries = 0;
  int skippedQueueEntries = 0;
  int fileClipboardEvents = 0;
  int textEntries = 0;
  int textEvents = 0;
};

DbSummary Summarize(const HistoryDatabase& db) {
  DbSummary summary;
  summary.pinned = static_cast<int>(db.pinnedTargets().size());
  summary.operations = static_cast<int>(db.operations().size());
  for (const auto& entry : db.clipboardEntries()) {
    int repeats = std::max(1, entry.repeatCount);
    if (entry.kind == ClipboardContentKind::Files) {
      summary.fileClipboardEvents += repeats;
      if (entry.selected && !entry.stale) {
        ++summary.activeQueueEntries;
        summary.activeQueueFiles += static_cast<int>(entry.files.size());
      } else if (entry.stale) {
        ++summary.queueHistoryEntries;
      } else {
        ++summary.skippedQueueEntries;
      }
    } else if (entry.kind == ClipboardContentKind::Text) {
      ++summary.textEntries;
      summary.textEvents += repeats;
    }
  }
  return summary;
}

std::wstring DashboardText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  DbSummary s = Summarize(db);
  std::wstringstream ss;
  ss << L"ClipCue workspace\r\n";
  ss << L"\r\n";
  ss << L"Pinned targets       " << s.pinned << L"\r\n";
  ss << L"Active queue         " << s.activeQueueEntries << L" entry/entries, " << s.activeQueueFiles << L" path(s)\r\n";
  ss << L"Detected operations  " << s.operations << L"\r\n";
  ss << L"Text snippets        " << s.textEntries << L" saved, " << s.textEvents << L" clipboard event(s)\r\n";
  ss << L"\r\n";
  ss << L"Recommended flow\r\n";
  ss << L"1. Pin folders you use every day on the Targets page.\r\n";
  ss << L"2. Use Queue to review copied/cut paths before applying them to a folder.\r\n";
  ss << L"3. Use Text Studio to merge, edit, save, copy, or paste clipboard text.\r\n";
  ss << L"4. Keep the monitor enabled so ClipCue can learn Explorer and clipboard activity.\r\n";
  if (!extra.empty()) ss << L"\r\nLast action: " << extra << L"\r\n";
  return ss.str();
}

std::wstring StatusText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  DbSummary s = Summarize(db);
  std::wstringstream ss;
  ss << L"Store: " << EncryptedRecordStore::DefaultStorePath() << L"\r\n";
  ss << L"Menu cache: " << EncryptedRecordStore::DefaultMenuCachePath() << L"\r\n";
  ss << L"Pinned targets: " << s.pinned << L"    Operation records: " << s.operations << L"\r\n";
  ss << L"Active path queue: " << s.activeQueueEntries << L" entry/entries, " << s.activeQueueFiles << L" path(s)    File clipboard events: " << s.fileClipboardEvents << L"\r\n";
  ss << L"Path queue history: " << s.queueHistoryEntries << L" entry/entries    Skipped: " << s.skippedQueueEntries << L"\r\n";
  ss << L"Text entries: " << s.textEntries << L"    Text clipboard events: " << s.textEvents << L"\r\n";
  ss << L"Protection: DPAPI CurrentUser encrypted record store; Explorer menu cache stores only quick-target and queue-count metadata.";
  if (!extra.empty()) ss << L"\r\n" << extra;
  return ss.str();
}

std::wstring QueueSummaryText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  DbSummary s = Summarize(db);
  int copyFiles = 0;
  int moveFiles = 0;
  for (const auto& entry : db.clipboardEntries()) {
    if (entry.kind != ClipboardContentKind::Files || !entry.selected || entry.stale) continue;
    if (entry.op == OperationKind::Move) moveFiles += static_cast<int>(entry.files.size());
    else copyFiles += static_cast<int>(entry.files.size());
  }

  std::wstringstream ss;
  ss << L"Active: " << s.activeQueueEntries << L" entry/entries / " << s.activeQueueFiles << L" path(s)";
  ss << L"    Copy: " << copyFiles << L"    Move: " << moveFiles;
  ss << L"    History: " << s.queueHistoryEntries << L"    Skipped: " << s.skippedQueueEntries;
  if (!extra.empty()) ss << L"\r\n" << extra;
  return ss.str();
}

std::wstring TextMetaText(UiState* state) {
  std::wstring text;
  if (state && state->textEditor) {
    int len = GetWindowTextLengthW(state->textEditor);
    text = L"Editor: " + std::to_wstring(len) + L" character(s)";
  } else {
    text = L"Editor ready";
  }
  if (state) text += L"    History entries: " + std::to_wstring(state->textEntriesData.size());
  return text;
}

void AddCandidateLines(HWND list, const HistoryDatabase& db, OperationKind op, const std::wstring& title) {
  auto candidates = db.GetGlobalCandidates(op, 10);
  if (candidates.empty()) return;
  SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
  for (const auto& c : candidates) {
    std::wstring origin = c.pinned ? L"pinned" : L"detected";
    std::wstring label = c.label.empty() ? FormatMenuLabel(c.destParent) : c.label;
    std::wstring line = L"  [" + origin + L"]  " + label + L"  ->  " + c.destParent;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
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
    if (s->pinsData.empty()) SendMessageW(s->pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No pinned quick targets yet."));
  }

  if (s->suggestions) {
    SendMessageW(s->suggestions, LB_RESETCONTENT, 0, 0);
    AddCandidateLines(s->suggestions, db, OperationKind::Move, L"ClipCue Move to...");
    AddCandidateLines(s->suggestions, db, OperationKind::Copy, L"ClipCue Copy to...");
    if (SendMessageW(s->suggestions, LB_GETCOUNT, 0, 0) == 0) {
      SendMessageW(s->suggestions, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No detected quick targets yet. Enable the monitor and use Explorer normally to teach ClipCue."));
    }
  }

  if (s->history) {
    SendMessageW(s->history, LB_RESETCONTENT, 0, 0);
    const auto& ops = db.operations();
    int shown = 0;
    for (auto it = ops.rbegin(); it != ops.rend() && shown < 160; ++it, ++shown) {
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
      if (s->textEntriesData.size() >= 400) continue;
      s->textEntriesData.push_back(*it);
      if (s->textHistory) {
        std::wstring line = ClipboardEntryLine(*it);
        SendMessageW(s->textHistory, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
      }
    }
  }
  if (s->fileQueue && s->fileEntriesData.empty()) SendMessageW(s->fileQueue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No path clipboard queue yet."));
  if (s->textHistory && s->textEntriesData.empty()) SendMessageW(s->textHistory, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No text clipboard history yet."));
  if (s->textMeta) SetWindowTextW(s->textMeta, TextMetaText(s).c_str());
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
  if (!initial.empty()) {
    IShellItem* folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
      dialog->SetFolder(folder);
      folder->Release();
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

std::wstring PickTextFile(HWND owner, bool save) {
  CoInitializeScope co;
  if (FAILED(co.hr())) return L"";
  IFileDialog* dialog = nullptr;
  HRESULT hr = save
      ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
      : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) return L"";
  COMDLG_FILTERSPEC filters[] = {{L"Text files", L"*.txt;*.md;*.log;*.csv"}, {L"All files", L"*.*"}};
  dialog->SetFileTypes(_countof(filters), filters);
  dialog->SetTitle(save ? L"Save edited ClipCue text" : L"Open a text file into ClipCue");
  if (save) dialog->SetFileName(L"ClipCue text.txt");
  DWORD opts = 0;
  dialog->GetOptions(&opts);
  dialog->SetOptions(opts | FOS_FORCEFILESYSTEM | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
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

std::wstring GetWindowTextString(HWND hwnd) {
  if (!hwnd) return L"";
  int len = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
  if (len > 0) GetWindowTextW(hwnd, text.data(), len + 1);
  text.resize(static_cast<std::size_t>(len));
  return text;
}

void SetText(HWND hwnd, const std::wstring& value) {
  if (hwnd) SetWindowTextW(hwnd, value.c_str());
}

void OnPickPinTarget(UiState* s) {
  std::wstring current = GetWindowTextString(s->pinTarget);
  std::wstring folder = PickFolder(s->hwnd, L"Choose a quick target folder", current);
  if (!folder.empty()) {
    SetText(s->pinTarget, folder);
    if (GetWindowTextString(s->pinLabel).empty()) SetText(s->pinLabel, FormatMenuLabel(folder));
  }
}

void OnAddPin(UiState* s) {
  std::wstring folder = GetWindowTextString(s->pinTarget);
  if (folder.empty()) folder = PickFolder(s->hwnd, L"Choose a ClipCue target folder");
  if (folder.empty()) return;
  if (!DirectoryExists(folder)) {
    MessageBoxW(s->hwnd, L"Choose an existing folder before adding a quick target.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  PinnedTarget pin;
  pin.op = SelectedOp(s->opCombo);
  pin.destParent = folder;
  pin.label = GetWindowTextString(s->pinLabel);
  if (pin.label.empty()) pin.label = FormatMenuLabel(folder);
  if (!db.AddPinnedTarget(pin, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  db.WriteMenuCache(L"", &err);
  SetText(s->pinTarget, L"");
  SetText(s->pinLabel, L"");
  Refresh(s, L"Pinned target added and menu cache rebuilt.");
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
  Refresh(s, L"Menu cache rebuilt.");
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
  Refresh(s, L"Expired operation records cleaned. Pinned targets were kept.");
}

std::vector<int> SelectedListIndices(HWND list) {
  std::vector<int> indices;
  if (!list) return indices;
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
  Refresh(s, L"Path clipboard queue selection saved.");
}

void OnQueueSelectionChanged(UiState* s) {
  auto selected = SelectedListIndices(s->fileQueue);
  if (selected.size() != 1 || selected[0] < 0 || static_cast<std::size_t>(selected[0]) >= s->fileEntriesData.size()) {
    SetText(s->fileQueueDetails, L"Select one queue entry to inspect paths and status.");
    return;
  }
  const auto& entry = s->fileEntriesData[static_cast<std::size_t>(selected[0])];
  SetText(s->fileQueueDetails, ClipboardEntryDetails(entry));
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

std::wstring QueueTargetText(UiState* s) {
  return s && s->queueTarget ? GetWindowTextString(s->queueTarget) : L"";
}

void OnPickQueueTarget(UiState* s) {
  std::wstring folder = PickFolder(s->hwnd, L"Choose a target folder", QueueTargetText(s));
  if (!folder.empty() && s->queueTarget) SetText(s->queueTarget, folder);
}

void OnOpenControlPanel(UiState* s) {
  std::wstring ui = GetProgramPath();
  ShellExecuteW(s ? s->hwnd : nullptr, L"open", ui.c_str(), nullptr, ModuleDir().c_str(), SW_SHOWNORMAL);
}

void OnOpenQueueWorkspace(UiState* s) {
  std::wstring ui = GetProgramPath();
  ShellExecuteW(s ? s->hwnd : nullptr, L"open", ui.c_str(), L"queue", ModuleDir().c_str(), SW_SHOWNORMAL);
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
    if (!combined.empty()) combined += L"\r\n\r\n---\r\n\r\n";
    combined += s->textEntriesData[static_cast<std::size_t>(index)].text;
  }
  if (!combined.empty()) SetText(s->textEditor, combined);
  if (s->textMeta) SetText(s->textMeta, TextMetaText(s));
}

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

bool ReadAllBytes(const std::wstring& path, std::vector<char>* bytes, std::wstring* error) {
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) {
    if (error) *error = L"Unable to open file: " + GetLastErrorMessage();
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024) {
    if (error) *error = L"Text file is too large or unreadable. Keep imports under 16 MB.";
    return false;
  }
  bytes->resize(static_cast<std::size_t>(size.QuadPart));
  DWORD read = 0;
  if (!bytes->empty() && !ReadFile(file.get(), bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr)) {
    if (error) *error = L"Unable to read file: " + GetLastErrorMessage();
    return false;
  }
  bytes->resize(read);
  return true;
}

std::wstring DecodeTextBytes(const std::vector<char>& bytes) {
  if (bytes.empty()) return L"";
  if (bytes.size() >= 2) {
    const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
    const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
    if (b0 == 0xFF && b1 == 0xFE) {
      std::wstring out;
      std::size_t chars = (bytes.size() - 2) / sizeof(wchar_t);
      out.resize(chars);
      if (chars) std::memcpy(out.data(), bytes.data() + 2, chars * sizeof(wchar_t));
      return out;
    }
  }
  std::size_t offset = 0;
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
      static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
    offset = 3;
  }
  return Utf8ToWide(std::string(bytes.data() + offset, bytes.data() + bytes.size()));
}

void OnLoadTextFile(UiState* s) {
  std::wstring path = PickTextFile(s->hwnd, false);
  if (path.empty()) return;
  std::vector<char> bytes;
  std::wstring err;
  if (!ReadAllBytes(path, &bytes, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  SetText(s->textEditor, DecodeTextBytes(bytes));
  if (s->textMeta) SetText(s->textMeta, (L"Loaded: " + path).c_str());
}

void OnSaveTextFile(UiState* s) {
  std::wstring text = GetWindowTextString(s->textEditor);
  if (text.empty()) {
    MessageBoxW(s->hwnd, L"Enter or select text before saving.", L"ClipCue", MB_ICONINFORMATION);
    return;
  }
  std::wstring path = PickTextFile(s->hwnd, true);
  if (path.empty()) return;
  std::string utf8 = WideToUtf8(text);
  Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) {
    MessageBoxW(s->hwnd, (L"Unable to create file: " + GetLastErrorMessage()).c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
  DWORD written = 0;
  WriteFile(file.get(), bom, sizeof(bom), &written, nullptr);
  if (!utf8.empty() && !WriteFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
    MessageBoxW(s->hwnd, (L"Unable to save file: " + GetLastErrorMessage()).c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  if (s->textMeta) SetText(s->textMeta, L"Saved: " + path);
}

void OnClearTextEditor(UiState* s) {
  SetText(s->textEditor, L"");
  if (s->textMeta) SetText(s->textMeta, TextMetaText(s));
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

void CreateFonts(UiState* s, int heroSize) {
  s->background = CreateSolidBrush(kPageBg);
  s->panelBrush = CreateSolidBrush(kPanelBg);
  s->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->smallFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->heroFont = CreateFontW(-heroSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void OnCreate(UiState* s) {
  CreateFonts(s, 30);

  HWND title = MakeControl(s, L"STATIC", L"ClipCue", WS_CHILD | WS_VISIBLE, 26, 18, 240, 36, -1, s->heroFont);
  SetControlFont(title, s->heroFont);
  MakeControl(s, L"STATIC", L"A focused workspace for Explorer routes, path queues, and clipboard text.", WS_CHILD | WS_VISIBLE,
              28, 56, 660, 22, -1, s->font);
  MakeControl(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 704, 24, 98, 32, IDC_REFRESH);
  MakeControl(s, L"BUTTON", L"Queue window", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 814, 24, 128, 32, IDC_OPEN_QUEUE);
  MakeControl(s, L"BUTTON", L"Open installer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 954, 24, 128, 32, IDC_OPEN_INSTALLER);

  s->tab = MakeControl(s, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 24, 92, 1058, 604, IDC_TAB);
  AddTab(s->tab, kPageHome, L"Overview");
  AddTab(s->tab, kPageTargets, L"Targets");
  AddTab(s->tab, kPageQueue, L"Path Queue");
  AddTab(s->tab, kPageText, L"Text Studio");
  AddTab(s->tab, kPageSystem, L"System");
  TabCtrl_SetCurSel(s->tab, kPageHome);

  MakeGroup(s, kPageHome, L"Workspace summary", 42, 132, 492, 482);
  s->dashboard = MakePageControl(s, kPageHome, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                 62, 162, 452, 360, IDC_DASHBOARD, s->font, WS_EX_CLIENTEDGE);
  MakePageControl(s, kPageHome, L"BUTTON", L"Open Path Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 62, 540, 146, 34, IDC_OPEN_QUEUE);
  MakePageControl(s, kPageHome, L"BUTTON", L"Open data folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 220, 540, 146, 34, IDC_OPEN_DATA);
  MakePageControl(s, kPageHome, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 378, 540, 136, 34, IDC_BUILD_CACHE);

  MakeGroup(s, kPageHome, L"Recent operations", 558, 132, 504, 482);
  s->history = MakePageControl(s, kPageHome, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
                               578, 162, 464, 412, IDC_HISTORY, s->smallFont, WS_EX_CLIENTEDGE);

  MakeGroup(s, kPageTargets, L"Pinned quick targets", 42, 132, 492, 482);
  s->pins = MakePageControl(s, kPageTargets, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                            62, 162, 452, 212, IDC_PINS, s->smallFont, WS_EX_CLIENTEDGE);
  MakeLabel(s, kPageTargets, L"Label", 62, 394, 76, 22);
  s->pinLabel = MakePageControl(s, kPageTargets, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                142, 390, 372, 28, IDC_PIN_LABEL, s->font, WS_EX_CLIENTEDGE);
  MakeLabel(s, kPageTargets, L"Folder", 62, 430, 76, 22);
  s->pinTarget = MakePageControl(s, kPageTargets, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                 142, 426, 262, 28, IDC_PIN_TARGET, s->font, WS_EX_CLIENTEDGE);
  MakePageControl(s, kPageTargets, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 414, 424, 100, 32, IDC_PICK_PIN_TARGET);
  MakeLabel(s, kPageTargets, L"Operation", 62, 468, 76, 22);
  s->opCombo = MakePageControl(s, kPageTargets, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               142, 464, 156, 150, IDC_OP_COMBO, s->font);
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy + Move"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Move only"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy only"));
  SendMessageW(s->opCombo, CB_SETCURSEL, 0, 0);
  MakePageControl(s, kPageTargets, L"BUTTON", L"Add target", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 314, 462, 96, 34, IDC_ADD_PIN);
  MakePageControl(s, kPageTargets, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 418, 462, 96, 34, IDC_REMOVE_PIN);
  MakePageControl(s, kPageTargets, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 62, 536, 140, 34, IDC_BUILD_CACHE);
  MakePageControl(s, kPageTargets, L"BUTTON", L"Cleanup old records", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 214, 536, 156, 34, IDC_CLEANUP);

  MakeGroup(s, kPageTargets, L"Detected suggestions", 558, 132, 504, 482);
  MakeLabel(s, kPageTargets, L"Learned from Explorer operations, clipboard activity, and pinned destinations.", 578, 162, 450, 22, s->smallFont);
  s->suggestions = MakePageControl(s, kPageTargets, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
                                   578, 196, 464, 374, IDC_SUGGESTIONS, s->smallFont, WS_EX_CLIENTEDGE);

  MakeGroup(s, kPageQueue, L"Review and apply path clipboard queue", 42, 132, 1020, 482);
  s->queueSummary = MakePageControl(s, kPageQueue, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 62, 162, 958, 34, IDC_QUEUE_SUMMARY, s->font);
  s->fileQueue = MakePageControl(s, kPageQueue, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                 62, 208, 600, 216, IDC_CLIPBOARD_FILES, s->smallFont, WS_EX_CLIENTEDGE);
  s->fileQueueDetails = MakePageControl(s, kPageQueue, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                        682, 208, 340, 216, IDC_QUEUE_DETAILS, s->smallFont, WS_EX_CLIENTEDGE);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Set Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 62, 438, 100, 32, IDC_QUEUE_COPY);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Set Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 174, 438, 100, 32, IDC_QUEUE_MOVE);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 286, 438, 86, 32, IDC_QUEUE_SKIP);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 384, 438, 104, 32, IDC_QUEUE_ACTIVATE);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Save selection", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 500, 438, 132, 32, IDC_SAVE_FILE_QUEUE);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Mark active as history", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 644, 438, 172, 32, IDC_CLEAR_FILE_QUEUE);
  MakeLabel(s, kPageQueue, L"Target folder", 62, 494, 120, 24);
  s->queueTarget = MakePageControl(s, kPageQueue, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                   174, 490, 428, 30, IDC_QUEUE_TARGET, s->font, WS_EX_CLIENTEDGE);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Choose...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 614, 488, 104, 34, IDC_QUEUE_PICK_TARGET);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Apply all", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 730, 488, 92, 34, IDC_QUEUE_APPLY_ALL);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Copy only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 834, 488, 92, 34, IDC_QUEUE_APPLY_COPY);
  MakePageControl(s, kPageQueue, L"BUTTON", L"Move only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 938, 488, 92, 34, IDC_QUEUE_APPLY_MOVE);

  MakeGroup(s, kPageText, L"Text history", 42, 132, 414, 482);
  s->textHistory = MakePageControl(s, kPageText, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                   62, 162, 374, 408, IDC_CLIPBOARD_TEXTS, s->smallFont, WS_EX_CLIENTEDGE);
  MakeGroup(s, kPageText, L"Text editor", 476, 132, 586, 482);
  s->textMeta = MakePageControl(s, kPageText, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 496, 162, 540, 22, IDC_TEXT_META, s->smallFont);
  s->textEditor = MakePageControl(s, kPageText, L"EDIT", L"", WS_CHILD | WS_VISIBLE | kEditorStyle,
                                  496, 192, 546, 310, IDC_TEXT_EDITOR, s->font, WS_EX_CLIENTEDGE);
  MakePageControl(s, kPageText, L"BUTTON", L"Load file", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 496, 522, 96, 32, IDC_TEXT_LOAD);
  MakePageControl(s, kPageText, L"BUTTON", L"Save as", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 602, 522, 96, 32, IDC_TEXT_SAVE);
  MakePageControl(s, kPageText, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 708, 522, 80, 32, IDC_TEXT_CLEAR);
  MakePageControl(s, kPageText, L"BUTTON", L"Copy edited", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 798, 522, 112, 32, IDC_COPY_TEXT);
  MakePageControl(s, kPageText, L"BUTTON", L"Paste edited", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 920, 522, 112, 32, IDC_PASTE_TEXT);

  MakeGroup(s, kPageSystem, L"Installation and integration", 42, 132, 1020, 482);
  s->status = MakePageControl(s, kPageSystem, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                              62, 162, 960, 256, IDC_STATUS, s->smallFont, WS_EX_CLIENTEDGE);
  MakePageControl(s, kPageSystem, L"BUTTON", L"Open data folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 62, 444, 142, 34, IDC_OPEN_DATA);
  MakePageControl(s, kPageSystem, L"BUTTON", L"Register menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 216, 444, 126, 34, IDC_REGISTER_MENU);
  MakePageControl(s, kPageSystem, L"BUTTON", L"Unregister menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 354, 444, 142, 34, IDC_UNREGISTER_MENU);
  MakePageControl(s, kPageSystem, L"BUTTON", L"Open installer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 508, 444, 130, 34, IDC_OPEN_INSTALLER);
  MakePageControl(s, kPageSystem, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 444, 126, 34, IDC_BUILD_CACHE);
  MakePageControl(s, kPageSystem, L"BUTTON", L"Cleanup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 788, 444, 96, 34, IDC_CLEANUP);

  MakeControl(s, L"STATIC", L"Per-user install. DPAPI-protected store. Classic Explorer integration.", WS_CHILD | WS_VISIBLE,
              28, 712, 640, 22, -1, s->smallFont);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 986, 706, 96, 32, IDC_CLOSE);

  Refresh(s);
  SetText(s->fileQueueDetails, L"Select one queue entry to inspect paths and status.");
  ShowPage(s, kPageHome);
}

void OnCreateQueue(UiState* s) {
  CreateFonts(s, 32);

  HWND title = MakeControl(s, L"STATIC", L"Path Clip Queue", WS_CHILD | WS_VISIBLE, 28, 20, 320, 40, -1, s->heroFont);
  SetControlFont(title, s->heroFont);
  MakeControl(s, L"STATIC", L"Review active clipboard path groups, choose intent, then apply them to a folder.", WS_CHILD | WS_VISIBLE,
              30, 62, 720, 24, -1, s->font);
  MakeControl(s, L"BUTTON", L"Control Panel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 784, 28, 128, 34, IDC_OPEN_PANEL);
  MakeControl(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 922, 28, 94, 34, IDC_REFRESH);

  MakeGroup(s, kPageHome, L"Queue", 28, 112, 996, 420);
  s->queueSummary = MakeControl(s, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 142, 940, 36, IDC_QUEUE_SUMMARY, s->font);
  s->fileQueue = MakeControl(s, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                             48, 190, 600, 260, IDC_CLIPBOARD_FILES, s->smallFont);
  s->fileQueueDetails = MakeControl(s, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                    670, 190, 330, 260, IDC_QUEUE_DETAILS, s->smallFont);
  MakeControl(s, L"BUTTON", L"Set Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 48, 466, 104, 34, IDC_QUEUE_COPY);
  MakeControl(s, L"BUTTON", L"Set Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 164, 466, 104, 34, IDC_QUEUE_MOVE);
  MakeControl(s, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 280, 466, 86, 34, IDC_QUEUE_SKIP);
  MakeControl(s, L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 378, 466, 104, 34, IDC_QUEUE_ACTIVATE);
  MakeControl(s, L"BUTTON", L"Save selection", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 494, 466, 134, 34, IDC_SAVE_FILE_QUEUE);
  MakeControl(s, L"BUTTON", L"Mark active as history", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 646, 466, 174, 34, IDC_CLEAR_FILE_QUEUE);

  MakeGroup(s, kPageHome, L"Apply", 28, 552, 996, 96);
  MakeControl(s, L"STATIC", L"Target folder", WS_CHILD | WS_VISIBLE, 48, 586, 110, 24, -1, s->font);
  s->queueTarget = MakeControl(s, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               162, 582, 476, 30, IDC_QUEUE_TARGET, s->font);
  MakeControl(s, L"BUTTON", L"Choose...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 580, 104, 34, IDC_QUEUE_PICK_TARGET);
  MakeControl(s, L"BUTTON", L"Apply all", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 768, 580, 94, 34, IDC_QUEUE_APPLY_ALL);
  MakeControl(s, L"BUTTON", L"Copy only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 874, 580, 94, 34, IDC_QUEUE_APPLY_COPY);
  MakeControl(s, L"BUTTON", L"Move only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 768, 620, 94, 34, IDC_QUEUE_APPLY_MOVE);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 874, 620, 94, 34, IDC_CLOSE);

  Refresh(s);
  SetText(s->fileQueueDetails, L"Select one queue entry to inspect paths and status.");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  UiState* s = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      s = reinterpret_cast<UiState*>(cs->lpCreateParams);
      s->hwnd = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
      return TRUE;
    }
    case WM_CREATE:
      if (s && s->queueMode) OnCreateQueue(s);
      else OnCreate(s);
      return 0;
    case WM_NOTIFY: {
      auto* hdr = reinterpret_cast<NMHDR*>(lp);
      if (hdr && s && s->tab && hdr->idFrom == IDC_TAB && hdr->code == TCN_SELCHANGE) {
        ShowPage(s, TabCtrl_GetCurSel(s->tab));
        return 0;
      }
      break;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      int notify = HIWORD(wp);
      if (id == IDC_CLIPBOARD_TEXTS && notify == LBN_SELCHANGE) {
        OnTextSelectionChanged(s);
        return 0;
      }
      if (id == IDC_CLIPBOARD_FILES && notify == LBN_SELCHANGE) {
        OnQueueSelectionChanged(s);
        return 0;
      }
      if (id == IDC_TEXT_EDITOR && notify == EN_CHANGE) {
        if (s && s->textMeta) SetText(s->textMeta, TextMetaText(s));
        return 0;
      }
      switch (id) {
        case IDC_PICK_PIN_TARGET: OnPickPinTarget(s); return 0;
        case IDC_ADD_PIN: OnAddPin(s); return 0;
        case IDC_REMOVE_PIN: OnRemovePin(s); return 0;
        case IDC_BUILD_CACHE: OnBuildCache(s); return 0;
        case IDC_CLEANUP: OnCleanup(s); return 0;
        case IDC_SAVE_FILE_QUEUE: OnSaveFileQueueSelection(s); return 0;
        case IDC_CLEAR_FILE_QUEUE: OnClearFileQueue(s); return 0;
        case IDC_QUEUE_COPY: OnQueueCopy(s); return 0;
        case IDC_QUEUE_MOVE: OnQueueMove(s); return 0;
        case IDC_QUEUE_SKIP: OnQueueSkip(s); return 0;
        case IDC_QUEUE_ACTIVATE: OnQueueActivate(s); return 0;
        case IDC_QUEUE_PICK_TARGET: OnPickQueueTarget(s); return 0;
        case IDC_QUEUE_APPLY_ALL: OnApplyQueue(s, L"both"); return 0;
        case IDC_QUEUE_APPLY_COPY: OnApplyQueue(s, L"copy"); return 0;
        case IDC_QUEUE_APPLY_MOVE: OnApplyQueue(s, L"move"); return 0;
        case IDC_OPEN_PANEL: OnOpenControlPanel(s); return 0;
        case IDC_OPEN_QUEUE: OnOpenQueueWorkspace(s); return 0;
        case IDC_COPY_TEXT: OnCopyEditedText(s, false); return 0;
        case IDC_PASTE_TEXT: OnCopyEditedText(s, true); return 0;
        case IDC_TEXT_LOAD: OnLoadTextFile(s); return 0;
        case IDC_TEXT_SAVE: OnSaveTextFile(s); return 0;
        case IDC_TEXT_CLEAR: OnClearTextEditor(s); return 0;
        case IDC_REFRESH: Refresh(s); return 0;
        case IDC_OPEN_DATA: LaunchPath(PathCombineSimple(GetKnownFolderLocalAppData(), L"ClipCue")); return 0;
        case IDC_REGISTER_MENU: OnRegisterMenu(s, true); return 0;
        case IDC_UNREGISTER_MENU: OnRegisterMenu(s, false); return 0;
        case IDC_OPEN_INSTALLER: LaunchPath(InstallerPath()); return 0;
        case IDC_CLOSE: DestroyWindow(hwnd); return 0;
      }
      break;
    }
    case WM_CTLCOLORSTATIC:
      if (s && s->background) {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kBodyText);
        return reinterpret_cast<LRESULT>(s->background);
      }
      break;
    case WM_ERASEBKGND:
      if (s && s->background) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, s->background);
        return 1;
      }
      break;
    case WM_DESTROY:
      if (s) {
        if (s->font) DeleteObject(s->font);
        if (s->smallFont) DeleteObject(s->smallFont);
        if (s->titleFont) DeleteObject(s->titleFont);
        if (s->heroFont) DeleteObject(s->heroFont);
        if (s->background) DeleteObject(s->background);
        if (s->panelBrush) DeleteObject(s->panelBrush);
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&icc);

  UiState state;
  state.previousForeground = GetForegroundWindow();
  auto args = SplitCommandLineArgs();
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"queue" || args[i] == L"--queue" || args[i] == L"/queue") state.queueMode = true;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = instance;
  wc.lpszClassName = state.queueMode ? L"ClipCueQueueWindow" : L"ClipCueUIWindow";
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  int width = state.queueMode ? kQueueWidth : kMainWidth;
  int height = state.queueMode ? kQueueHeight : kMainHeight;
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, state.queueMode ? L"ClipCue Path Clip Queue" : L"ClipCue Control Panel",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, &state);
  if (!hwnd) return 1;
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
