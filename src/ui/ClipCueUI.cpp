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

enum UiPage {
  kPagePaths = 0,
  kPageText = 1,
  kPageStatus = 2,
  kPageCount = 3,
};

struct UiState {
  HWND hwnd = nullptr;
  HWND tab = nullptr;
  HWND status = nullptr;
  HWND pins = nullptr;
  HWND suggestions = nullptr;
  HWND history = nullptr;
  HWND fileQueue = nullptr;
  HWND fileQueueDetails = nullptr;
  HWND textHistory = nullptr;
  HWND textEditor = nullptr;
  HWND opCombo = nullptr;
  HWND queueSummary = nullptr;
  HWND queueTarget = nullptr;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HBRUSH background = nullptr;
  HWND previousForeground = nullptr;
  int activePage = kPagePaths;
  bool queueMode = false;
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

HWND MakeControl(UiState* s, const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
  HWND ctrl = CreateWindowExW(0, klass, text, style, x, y, w, h, s->hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SetControlFont(ctrl, s->font);
  return ctrl;
}

HWND MakePageControl(UiState* s, int page, const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
  HWND ctrl = MakeControl(s, klass, text, style, x, y, w, h, id);
  if (ctrl && page >= 0 && page < kPageCount) s->pageControls[page].push_back(ctrl);
  return ctrl;
}

HWND MakeLabel(UiState* s, int page, const wchar_t* text, int x, int y, int w, int h) {
  return MakePageControl(s, page, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, -1);
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
    std::wstring state = entry.selected && !entry.stale ? L"[active] " : (entry.stale ? L"[history] " : L"[off] ");
    ss << state << L"[" << OperationDisplay(entry.op) << L"] "
       << entry.files.size() << L" file(s)" << RepeatSuffix(entry.repeatCount);
    if (!entry.files.empty()) ss << L"  " << FormatMenuLabel(entry.files.front());
  } else if (entry.kind == ClipboardContentKind::Text) {
    ss << L"[Text] " << entry.text.size() << L" char(s)" << RepeatSuffix(entry.repeatCount)
       << L"  " << CompactTextPreview(entry.text);
  }
  return ss.str();
}

std::wstring ClipboardEntryDetails(const ClipboardHistoryEntry& entry) {
  std::wstringstream ss;
  ss << L"State: " << (entry.selected && !entry.stale ? L"active" : (entry.stale ? L"history" : L"skip")) << L"\r\n";
  ss << L"Action: " << OperationDisplay(entry.op) << L"\r\n";
  ss << L"Repeated: " << std::max(1, entry.repeatCount) << L"\r\n";
  ss << L"Last seen: " << FormatTime(entry.timestamp) << L"\r\n\r\n";
  ss << L"Paths:\r\n";
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
  if (!extra.empty()) ss << L"\r\n" << extra;
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
  ss << L"Active: " << activeEntries << L" entr" << (activeEntries == 1 ? L"y" : L"ies")
     << L" / " << activeFiles << L" path(s)    Copy: " << copyFiles
     << L"    Move: " << moveFiles << L"    History: " << historyEntries
     << L"    Skipped: " << skippedEntries << L"    Clipboard events: " << repeatedEvents;
  if (!extra.empty()) ss << L"\r\n" << extra;
  return ss.str();
}

void AddCandidateLines(HWND list, const HistoryDatabase& db, OperationKind op, const std::wstring& title) {
  auto candidates = db.GetGlobalCandidates(op, 8);
  if (candidates.empty()) return;
  SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
  for (const auto& c : candidates) {
    std::wstring origin = c.pinned ? L"pinned" : L"detected";
    std::wstring label = c.label.empty() ? FormatMenuLabel(c.destParent) : c.label;
    std::wstring line = L"  [" + origin + L"]  " + label + L" -> " + c.destParent;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
}

void Refresh(UiState* s, const std::wstring& extra = L"") {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  std::wstring message = !err.empty() && extra.empty() ? err : extra;
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
      SendMessageW(s->suggestions, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No detected quick targets yet."));
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
  if (s->fileQueue && s->fileEntriesData.empty()) SendMessageW(s->fileQueue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No path clipboard queue yet."));
  if (s->textHistory && s->textEntriesData.empty()) SendMessageW(s->textHistory, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No text clipboard history yet."));
}

std::wstring PickFolder(HWND owner, const std::wstring& title) {
  CoInitializeScope co;
  if (FAILED(co.hr())) return L"";
  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) return L"";
  DWORD opts = 0;
  dialog->GetOptions(&opts);
  dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(title.c_str());
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

void OnAddPin(UiState* s) {
  std::wstring folder = PickFolder(s->hwnd, L"Choose a ClipCue target folder");
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
    SetWindowTextW(s->fileQueueDetails, L"Select one queue entry to inspect or edit it.");
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
  UpdateSelectedQueueEntries(s, OperationKind::Copy, true, false, L"Selected queue entr" L"ies set to Copy.");
}

void OnQueueMove(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Move, true, false, L"Selected queue entr" L"ies set to Move.");
}

void OnQueueSkip(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Unknown, false, false, L"Selected queue entr" L"ies set to Skip.");
}

void OnQueueActivate(UiState* s) {
  UpdateSelectedQueueEntries(s, OperationKind::Unknown, true, false, L"Selected queue entr" L"ies activated.");
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

std::wstring GetWindowTextString(HWND hwnd);

std::wstring QueueTargetText(UiState* s) {
  return s && s->queueTarget ? GetWindowTextString(s->queueTarget) : L"";
}

void OnPickQueueTarget(UiState* s) {
  std::wstring folder = PickFolder(s->hwnd, L"Choose a target folder");
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

std::wstring GetWindowTextString(HWND hwnd) {
  int len = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
  if (len > 0) GetWindowTextW(hwnd, text.data(), len + 1);
  text.resize(static_cast<std::size_t>(len));
  return text;
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
    if (!combined.empty()) combined += L"\r\n";
    combined += s->textEntriesData[static_cast<std::size_t>(index)].text;
  }
  if (!combined.empty()) SetWindowTextW(s->textEditor, combined.c_str());
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

void OnRegisterMenu(UiState* s, bool reg) {
  HRESULT hr = CallShellRegistration(reg);
  if (FAILED(hr)) {
    std::wstring msg = (reg ? L"Register failed: " : L"Unregister failed: ") + FormatHResult(hr) + L"\r\nDLL: " + ShellDllPath();
    MessageBoxW(s->hwnd, msg.c_str(), L"ClipCue", MB_ICONERROR);
    return;
  }
  Refresh(s, reg ? L"Classic Explorer context menu registered for the current user." : L"Classic Explorer context menu unregistered for the current user.");
}

void OnCreate(UiState* s) {
  s->background = CreateSolidBrush(RGB(245, 247, 250));
  s->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->titleFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  HWND title = MakeControl(s, L"STATIC", L"ClipCue", WS_CHILD | WS_VISIBLE, 14, 10, 220, 28, -1);
  SetControlFont(title, s->titleFont);
  MakeControl(s, L"STATIC", L"Clipboard paths and text, organized for repeated work", WS_CHILD | WS_VISIBLE, 236, 16, 420, 20, -1);

  s->tab = MakeControl(s, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 14, 44, 850, 622, IDC_TAB);
  AddTab(s->tab, kPagePaths, L"Paths");
  AddTab(s->tab, kPageText, L"Text");
  AddTab(s->tab, kPageStatus, L"Status");
  TabCtrl_SetCurSel(s->tab, kPagePaths);

  MakeLabel(s, kPagePaths, L"Pinned quick targets", 30, 86, 260, 20);
  s->pins = MakePageControl(s, kPagePaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                            30, 110, 812, 96, IDC_PINS);

  MakeLabel(s, kPagePaths, L"New target operation", 30, 222, 140, 22);
  s->opCombo = MakePageControl(s, kPagePaths, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               174, 218, 160, 160, IDC_OP_COMBO);
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy + Move"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Move only"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy only"));
  SendMessageW(s->opCombo, CB_SETCURSEL, 0, 0);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Add target...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 350, 216, 112, 28, IDC_ADD_PIN);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Remove selected", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 470, 216, 132, 28, IDC_REMOVE_PIN);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 610, 216, 112, 28, IDC_BUILD_CACHE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Cleanup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 730, 216, 112, 28, IDC_CLEANUP);

  MakeLabel(s, kPagePaths, L"Detected quick targets", 30, 262, 260, 20);
  s->suggestions = MakePageControl(s, kPagePaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
                                   30, 286, 812, 118, IDC_SUGGESTIONS);

  MakeLabel(s, kPagePaths, L"Path clipboard queue", 30, 424, 260, 20);
  s->fileQueue = MakePageControl(s, kPagePaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                 30, 448, 512, 118, IDC_CLIPBOARD_FILES);
  s->fileQueueDetails = MakePageControl(s, kPagePaths, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                        556, 448, 286, 118, IDC_QUEUE_DETAILS);
  MakePageControl(s, kPagePaths, L"BUTTON", L"\u21B7 Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 578, 92, 30, IDC_QUEUE_COPY);
  MakePageControl(s, kPagePaths, L"BUTTON", L"\u21E2 Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 132, 578, 92, 30, IDC_QUEUE_MOVE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"\u2298 Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 234, 578, 92, 30, IDC_QUEUE_SKIP);
  MakePageControl(s, kPagePaths, L"BUTTON", L"\u21BB Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 336, 578, 112, 30, IDC_QUEUE_ACTIVATE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Save selection", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 464, 578, 128, 30, IDC_SAVE_FILE_QUEUE);
  MakePageControl(s, kPagePaths, L"BUTTON", L"Mark active as history", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 604, 578, 170, 30, IDC_CLEAR_FILE_QUEUE);

  MakeLabel(s, kPageText, L"Text clipboard history", 30, 86, 300, 20);
  s->textHistory = MakePageControl(s, kPageText, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                   30, 110, 386, 456, IDC_CLIPBOARD_TEXTS);
  MakeLabel(s, kPageText, L"Edited combined text", 436, 86, 300, 20);
  s->textEditor = MakePageControl(s, kPageText, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
                                  436, 110, 406, 456, IDC_TEXT_EDITOR);
  MakePageControl(s, kPageText, L"BUTTON", L"Copy edited text", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 436, 578, 142, 30, IDC_COPY_TEXT);
  MakePageControl(s, kPageText, L"BUTTON", L"Paste edited text", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 588, 578, 142, 30, IDC_PASTE_TEXT);

  MakeLabel(s, kPageStatus, L"Monitor and store status", 30, 86, 300, 20);
  s->status = MakePageControl(s, kPageStatus, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                              30, 110, 812, 158, IDC_STATUS);
  MakeLabel(s, kPageStatus, L"Recent ClipCue operations", 30, 290, 300, 20);
  s->history = MakePageControl(s, kPageStatus, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
                               30, 314, 812, 252, IDC_HISTORY);

  MakeControl(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 14, 682, 94, 30, IDC_REFRESH);
  MakeControl(s, L"BUTTON", L"Open data folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 118, 682, 130, 30, IDC_OPEN_DATA);
  MakeControl(s, L"BUTTON", L"Register menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 258, 682, 118, 30, IDC_REGISTER_MENU);
  MakeControl(s, L"BUTTON", L"Unregister menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 386, 682, 128, 30, IDC_UNREGISTER_MENU);
  MakeControl(s, L"BUTTON", L"Open installer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 524, 682, 122, 30, IDC_OPEN_INSTALLER);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 770, 682, 94, 30, IDC_CLOSE);

  Refresh(s);
  SetWindowTextW(s->fileQueueDetails, L"Select one queue entry to inspect or edit it.");
  ShowPage(s, kPagePaths);
}

void OnCreateQueue(UiState* s) {
  s->background = CreateSolidBrush(RGB(241, 244, 248));
  s->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->titleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  HWND title = MakeControl(s, L"STATIC", L"Path Clip Queue", WS_CHILD | WS_VISIBLE, 24, 18, 280, 34, -1);
  SetControlFont(title, s->titleFont);
  MakeControl(s, L"STATIC", L"ClipCue", WS_CHILD | WS_VISIBLE, 316, 28, 120, 22, -1);

  s->queueSummary = MakeControl(s, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 24, 66, 920, 42, IDC_QUEUE_SUMMARY);
  MakeControl(s, L"STATIC", L"Queue", WS_CHILD | WS_VISIBLE, 24, 124, 120, 22, -1);
  MakeControl(s, L"STATIC", L"Details", WS_CHILD | WS_VISIBLE, 604, 124, 120, 22, -1);

  s->fileQueue = MakeControl(s, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                             24, 150, 558, 384, IDC_CLIPBOARD_FILES);
  s->fileQueueDetails = MakeControl(s, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                    604, 150, 340, 384, IDC_QUEUE_DETAILS);

  MakeControl(s, L"BUTTON", L"\u21B7 Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 550, 104, 34, IDC_QUEUE_COPY);
  MakeControl(s, L"BUTTON", L"\u21E2 Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 140, 550, 104, 34, IDC_QUEUE_MOVE);
  MakeControl(s, L"BUTTON", L"\u2298 Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 256, 550, 104, 34, IDC_QUEUE_SKIP);
  MakeControl(s, L"BUTTON", L"\u21BB Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 372, 550, 128, 34, IDC_QUEUE_ACTIVATE);
  MakeControl(s, L"BUTTON", L"Mark active as history", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 604, 550, 176, 34, IDC_CLEAR_FILE_QUEUE);
  MakeControl(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 792, 550, 104, 34, IDC_REFRESH);

  MakeControl(s, L"STATIC", L"Target folder", WS_CHILD | WS_VISIBLE, 24, 606, 120, 22, -1);
  s->queueTarget = MakeControl(s, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               140, 602, 442, 28, IDC_QUEUE_TARGET);
  MakeControl(s, L"BUTTON", L"Choose...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 594, 600, 104, 32, IDC_QUEUE_PICK_TARGET);
  MakeControl(s, L"BUTTON", L"Apply all...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 710, 600, 104, 32, IDC_QUEUE_APPLY_ALL);
  MakeControl(s, L"BUTTON", L"Copy only...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 824, 600, 104, 32, IDC_QUEUE_APPLY_COPY);
  MakeControl(s, L"BUTTON", L"Move only...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 710, 642, 104, 32, IDC_QUEUE_APPLY_MOVE);
  MakeControl(s, L"BUTTON", L"Control Panel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 824, 642, 104, 32, IDC_OPEN_PANEL);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 642, 104, 32, IDC_CLOSE);

  Refresh(s);
  SetWindowTextW(s->fileQueueDetails, L"Select one queue entry to inspect or edit it.");
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
      switch (id) {
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
        case IDC_COPY_TEXT: OnCopyEditedText(s, false); return 0;
        case IDC_PASTE_TEXT: OnCopyEditedText(s, true); return 0;
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
        SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
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
        if (s->titleFont) DeleteObject(s->titleFont);
        if (s->background) DeleteObject(s->background);
        s->font = nullptr;
        s->titleFont = nullptr;
        s->background = nullptr;
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

  auto args = SplitCommandLineArgs();
  bool queueMode = std::any_of(args.begin(), args.end(), [](const std::wstring& arg) {
    return arg == L"queue" || arg == L"--queue" || arg == L"/queue" || arg == L"path-queue" || arg == L"--path-queue";
  });

  UiState state;
  state.previousForeground = GetForegroundWindow();
  state.queueMode = queueMode;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = instance;
  wc.lpszClassName = L"ClipCueControlPanelWindow";
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, queueMode ? L"ClipCue Path Clip Queue" : L"ClipCue Control Panel",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, queueMode ? 990 : 900, queueMode ? 730 : 760, nullptr, nullptr, instance, &state);
  if (!hwnd) return 1;
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);

  MSG m{};
  while (GetMessageW(&m, nullptr, 0, 0)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return static_cast<int>(m.wParam);
}
