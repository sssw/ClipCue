#include "clipcue/CryptoStore.h"
#include "clipcue/History.h"
#include "clipcue/PathUtils.h"
#include "clipcue/WinUtils.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

using namespace clipcue;

namespace {

constexpr int IDC_NAV_PATHS = 1001;
constexpr int IDC_NAV_TEXT = 1002;
constexpr int IDC_NAV_STATUS = 1003;
constexpr int IDC_DASH = 1004;
constexpr int IDC_PINS = 1010;
constexpr int IDC_SUGGEST = 1011;
constexpr int IDC_OP = 1012;
constexpr int IDC_ADD_PIN = 1013;
constexpr int IDC_REMOVE_PIN = 1014;
constexpr int IDC_USE_TARGET = 1015;
constexpr int IDC_OPEN_TARGET = 1016;
constexpr int IDC_TARGET = 1017;
constexpr int IDC_PICK_TARGET = 1018;
constexpr int IDC_QUEUE = 1020;
constexpr int IDC_QUEUE_DETAILS = 1021;
constexpr int IDC_QUEUE_COPY = 1022;
constexpr int IDC_QUEUE_MOVE = 1023;
constexpr int IDC_QUEUE_SKIP = 1024;
constexpr int IDC_QUEUE_ACTIVE = 1025;
constexpr int IDC_QUEUE_ARCHIVE = 1026;
constexpr int IDC_APPLY_ALL = 1027;
constexpr int IDC_APPLY_COPY = 1028;
constexpr int IDC_APPLY_MOVE = 1029;
constexpr int IDC_TEXT_LIST = 1030;
constexpr int IDC_TEXT_EDIT = 1031;
constexpr int IDC_TEXT_META = 1032;
constexpr int IDC_TEXT_ALL = 1033;
constexpr int IDC_TEXT_CLEAR = 1034;
constexpr int IDC_TEXT_NORMALIZE = 1035;
constexpr int IDC_TEXT_COPY = 1036;
constexpr int IDC_TEXT_PASTE = 1037;
constexpr int IDC_STATUS = 1040;
constexpr int IDC_HISTORY = 1041;
constexpr int IDC_REGISTER = 1042;
constexpr int IDC_UNREGISTER = 1043;
constexpr int IDC_BUILD_CACHE = 1044;
constexpr int IDC_CLEANUP = 1045;
constexpr int IDC_OPEN_DATA = 1046;
constexpr int IDC_INSTALLER = 1047;
constexpr int IDC_REFRESH = 1048;
constexpr int IDC_CLOSE = 1049;

constexpr COLORREF kBg = RGB(241, 245, 249);
constexpr COLORREF kCard = RGB(255, 255, 255);
constexpr COLORREF kBorder = RGB(203, 213, 225);
constexpr COLORREF kAccent = RGB(37, 99, 235);
constexpr COLORREF kText = RGB(15, 23, 42);

enum Page { kPaths = 0, kText = 1, kStatus = 2, kPageCount = 3 };

struct State {
  HWND hwnd = nullptr;
  HWND dash = nullptr;
  HWND pins = nullptr;
  HWND suggest = nullptr;
  HWND op = nullptr;
  HWND target = nullptr;
  HWND queue = nullptr;
  HWND queueDetails = nullptr;
  HWND textList = nullptr;
  HWND textEdit = nullptr;
  HWND textMeta = nullptr;
  HWND status = nullptr;
  HWND history = nullptr;
  HWND previous = nullptr;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HFONT sectionFont = nullptr;
  HFONT monoFont = nullptr;
  HBRUSH white = nullptr;
  int page = kPaths;
  bool queueOnly = false;
  std::vector<HWND> pages[kPageCount];
  std::vector<PinnedTarget> pinsData;
  std::vector<std::wstring> suggestionTargets;
  std::vector<ClipboardHistoryEntry> fileData;
  std::vector<ClipboardHistoryEntry> textData;
};

std::wstring ModuleDir() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : exe.substr(0, pos);
}
std::wstring AgentPath() { return PathCombineSimple(ModuleDir(), L"ClipCue.Agent.exe"); }
std::wstring InstallerPath() { return PathCombineSimple(ModuleDir(), L"ClipCue.Installer.exe"); }
std::wstring ShellDllPath() { return PathCombineSimple(ModuleDir(), L"ClipCue.ShellClassic.dll"); }

HFONT MakeFont(int pt, int weight = FW_NORMAL, const wchar_t* face = L"Segoe UI") {
  HDC dc = GetDC(nullptr);
  int px = -MulDiv(pt, GetDeviceCaps(dc, LOGPIXELSY), 72);
  ReleaseDC(nullptr, dc);
  return CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, face);
}
void SetFont(HWND h, HFONT f) { if (h && f) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE); }
HWND Make(State* s, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = nullptr, DWORD ex = 0) {
  HWND ctrl = CreateWindowExW(ex, cls, text, style, x, y, w, h, s->hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SetFont(ctrl, f ? f : s->font);
  return ctrl;
}
HWND Add(State* s, int page, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = nullptr, DWORD ex = 0) {
  HWND ctrl = Make(s, cls, text, style, x, y, w, h, id, f, ex);
  if (page >= 0 && page < kPageCount) s->pages[page].push_back(ctrl);
  return ctrl;
}
HWND Label(State* s, int page, const wchar_t* text, int x, int y, int w, int h, HFONT f = nullptr) {
  return Add(s, page, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, -1, f);
}

std::wstring GetText(HWND h) {
  if (!h) return L"";
  int len = GetWindowTextLengthW(h);
  std::wstring value(static_cast<size_t>(len) + 1, L'\0');
  if (len) GetWindowTextW(h, value.data(), len + 1);
  value.resize(static_cast<size_t>(len));
  return value;
}
void ShowPage(State* s, int page) {
  if (!s || page < 0 || page >= kPageCount) return;
  s->page = page;
  for (int i = 0; i < kPageCount; ++i) for (HWND h : s->pages[i]) ShowWindow(h, i == page ? SW_SHOW : SW_HIDE);
  InvalidateRect(s->hwnd, nullptr, TRUE);
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
std::wstring OpName(OperationKind op) {
  switch (op) {
    case OperationKind::Copy: return L"Copy";
    case OperationKind::Move: return L"Move";
    case OperationKind::Both: return L"Copy + Move";
    default: return L"Unknown";
  }
}
OperationKind ChosenOp(HWND combo) {
  int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (sel == 1) return OperationKind::Move;
  if (sel == 2) return OperationKind::Copy;
  return OperationKind::Both;
}
std::wstring OneLine(std::wstring text) {
  for (wchar_t& ch : text) if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
  while (text.find(L"  ") != std::wstring::npos) text.erase(text.find(L"  "), 1);
  return text.size() > 90 ? text.substr(0, 87) + L"..." : text;
}
std::wstring ClipLine(const ClipboardHistoryEntry& e) {
  std::wstringstream ss;
  ss << FormatTime(e.timestamp) << L"  ";
  if (e.kind == ClipboardContentKind::Files) {
    ss << L"[" << (e.selected && !e.stale ? L"Active" : (e.stale ? L"History" : L"Skipped")) << L"] [" << OpName(e.op) << L"] " << e.files.size() << L" path(s)";
    if (e.repeatCount > 1) ss << L" x" << e.repeatCount;
    if (!e.files.empty()) ss << L"  " << FormatMenuLabel(e.files.front());
  } else {
    ss << L"[Text] " << e.text.size() << L" chars";
    if (e.repeatCount > 1) ss << L" x" << e.repeatCount;
    ss << L"  " << OneLine(e.text);
  }
  return ss.str();
}
std::wstring FileDetails(const ClipboardHistoryEntry& e) {
  std::wstringstream ss;
  ss << L"State: " << (e.selected && !e.stale ? L"active" : (e.stale ? L"history" : L"skipped")) << L"\r\n";
  ss << L"Action: " << OpName(e.op) << L"\r\nRepeated: " << std::max(1, e.repeatCount) << L"\r\nLast seen: " << FormatTime(e.timestamp) << L"\r\n";
  if (!e.sourceApp.empty()) ss << L"Source app: " << e.sourceApp << L"\r\n";
  ss << L"\r\nPaths:\r\n";
  for (const auto& f : e.files) ss << L"  " << f << L"\r\n";
  return ss.str();
}
std::wstring DashText(const HistoryDatabase& db, const std::wstring& extra) {
  int active = 0, files = 0, texts = 0;
  for (const auto& e : db.clipboardEntries()) {
    if (e.kind == ClipboardContentKind::Files && e.selected && !e.stale) { ++active; files += static_cast<int>(e.files.size()); }
    if (e.kind == ClipboardContentKind::Text) ++texts;
  }
  std::wstringstream ss;
  ss << L"Pinned " << db.pinnedTargets().size() << L"  |  Queue " << active << L" / " << files << L" paths  |  Text " << texts << L"  |  Records " << db.operations().size();
  ss << L"\r\n" << (extra.empty() ? L"Select a target, tune the queue, clean text, and apply work from one control center." : extra);
  return ss.str();
}
std::wstring StatusText(const HistoryDatabase& db, const std::wstring& extra) {
  std::wstringstream ss;
  ss << L"Store: " << EncryptedRecordStore::DefaultStorePath() << L"\r\n";
  ss << L"Menu cache: " << EncryptedRecordStore::DefaultMenuCachePath() << L"\r\n";
  ss << L"Pinned targets: " << db.pinnedTargets().size() << L"    Operations: " << db.operations().size() << L"\r\n";
  ss << L"Clipboard records: " << db.clipboardEntries().size() << L"\r\n";
  ss << L"Protection: DPAPI CurrentUser encrypted records; Explorer menu cache stores only fast-render metadata.";
  if (!extra.empty()) ss << L"\r\n\r\nLatest action: " << extra;
  return ss.str();
}
std::vector<int> Selections(HWND list) {
  std::vector<int> rows;
  int count = static_cast<int>(SendMessageW(list, LB_GETSELCOUNT, 0, 0));
  if (count > 0) {
    rows.resize(static_cast<size_t>(count));
    SendMessageW(list, LB_GETSELITEMS, static_cast<WPARAM>(count), reinterpret_cast<LPARAM>(rows.data()));
  } else {
    int cur = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (cur >= 0) rows.push_back(cur);
  }
  return rows;
}

void AddSuggestions(State* s, HWND list, HistoryDatabase& db, OperationKind op, const wchar_t* title) {
  auto candidates = db.GetGlobalCandidates(op, 8);
  if (candidates.empty()) return;
  SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title));
  s->suggestionTargets.push_back(L"");
  for (const auto& c : candidates) {
    std::wstring label = c.label.empty() ? FormatMenuLabel(c.destParent) : c.label;
    std::wstring line = L"  [" + std::wstring(c.pinned ? L"Pinned" : L"Detected") + L"] " + label + L" -> " + c.destParent;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    s->suggestionTargets.push_back(c.destParent);
  }
}
void TextMeta(State* s) {
  if (!s || !s->textEdit || !s->textMeta) return;
  std::wstring text = GetText(s->textEdit);
  size_t lines = text.empty() ? 0 : 1 + static_cast<size_t>(std::count(text.begin(), text.end(), L'\n'));
  std::wstringstream ss;
  ss << text.size() << L" characters";
  if (!text.empty()) ss << L" across " << lines << L" line" << (lines == 1 ? L"" : L"s");
  ss << L". Select clips to merge, normalize, copy, or paste.";
  SetWindowTextW(s->textMeta, ss.str().c_str());
}
void Refresh(State* s, const std::wstring& extra = L"") {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  std::wstring note = extra.empty() ? err : extra;
  if (s->dash) SetWindowTextW(s->dash, DashText(db, note).c_str());
  if (s->status) SetWindowTextW(s->status, StatusText(db, note).c_str());

  s->pinsData = db.pinnedTargets();
  if (s->pins) {
    SendMessageW(s->pins, LB_RESETCONTENT, 0, 0);
    for (const auto& pin : s->pinsData) {
      std::wstring line = L"[" + OpName(pin.op) + L"] " + (pin.label.empty() ? FormatMenuLabel(pin.destParent) : pin.label) + L" -> " + pin.destParent;
      SendMessageW(s->pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (s->pinsData.empty()) SendMessageW(s->pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No pinned targets yet. Add a trusted folder."));
  }

  s->suggestionTargets.clear();
  if (s->suggest) {
    SendMessageW(s->suggest, LB_RESETCONTENT, 0, 0);
    AddSuggestions(s, s->suggest, db, OperationKind::Move, L"Move suggestions");
    AddSuggestions(s, s->suggest, db, OperationKind::Copy, L"Copy suggestions");
    if (s->suggestionTargets.empty()) {
      SendMessageW(s->suggest, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No suggestions yet. Use ClipCue from Explorer to teach routes."));
      s->suggestionTargets.push_back(L"");
    }
  }

  s->fileData.clear();
  s->textData.clear();
  if (s->queue) SendMessageW(s->queue, LB_RESETCONTENT, 0, 0);
  if (s->textList) SendMessageW(s->textList, LB_RESETCONTENT, 0, 0);
  for (auto it = db.clipboardEntries().rbegin(); it != db.clipboardEntries().rend(); ++it) {
    if (it->kind == ClipboardContentKind::Files) {
      int idx = static_cast<int>(s->fileData.size());
      s->fileData.push_back(*it);
      if (s->queue) {
        std::wstring line = ClipLine(*it);
        SendMessageW(s->queue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        SendMessageW(s->queue, LB_SETSEL, (it->selected && !it->stale) ? TRUE : FALSE, idx);
      }
    } else if (it->kind == ClipboardContentKind::Text && s->textData.size() < 300) {
      s->textData.push_back(*it);
      if (s->textList) {
        std::wstring line = ClipLine(*it);
        SendMessageW(s->textList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
      }
    }
  }
  if (s->queue && s->fileData.empty()) SendMessageW(s->queue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No path queue yet. Copy/cut files in Explorer while ClipCue Monitor runs."));
  if (s->textList && s->textData.empty()) SendMessageW(s->textList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No text history yet. Copy text while ClipCue Monitor runs."));

  if (s->history) {
    SendMessageW(s->history, LB_RESETCONTENT, 0, 0);
    int shown = 0;
    for (auto it = db.operations().rbegin(); it != db.operations().rend() && shown < 120; ++it, ++shown) {
      std::wstring line = FormatTime(it->timestamp) + L"  [" + OpName(it->op) + L"] " + FormatMenuLabel(it->sourceParent) + L" -> " + FormatMenuLabel(it->destParent) + L"  (" + it->result + L")";
      SendMessageW(s->history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (!shown) SendMessageW(s->history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No operations recorded yet."));
  }
  TextMeta(s);
}

std::wstring PickFolder(HWND owner, const std::wstring& title, const std::wstring& initial = L"") {
  CoInitializeScope co;
  if (FAILED(co.hr())) return L"";
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return L"";
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(title.c_str());
  if (!initial.empty()) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item)))) { dlg->SetFolder(item); item->Release(); }
  }
  std::wstring out;
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR raw = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) { out = raw; CoTaskMemFree(raw); }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}
std::wstring CurrentTarget(State* s) {
  if (!s) return L"";
  HWND focus = GetFocus();
  if (s->suggest && focus == s->suggest) {
    int sel = static_cast<int>(SendMessageW(s->suggest, LB_GETCURSEL, 0, 0));
    if (sel >= 0 && static_cast<size_t>(sel) < s->suggestionTargets.size() && !s->suggestionTargets[static_cast<size_t>(sel)].empty()) return s->suggestionTargets[static_cast<size_t>(sel)];
  }
  if (s->pins) {
    int pin = static_cast<int>(SendMessageW(s->pins, LB_GETCURSEL, 0, 0));
    if (pin >= 0 && static_cast<size_t>(pin) < s->pinsData.size()) return s->pinsData[static_cast<size_t>(pin)].destParent;
  }
  if (s->suggest) {
    int sug = static_cast<int>(SendMessageW(s->suggest, LB_GETCURSEL, 0, 0));
    if (sug >= 0 && static_cast<size_t>(sug) < s->suggestionTargets.size() && !s->suggestionTargets[static_cast<size_t>(sug)].empty()) return s->suggestionTargets[static_cast<size_t>(sug)];
  }
  return GetText(s->target);
}
void AddPin(State* s) {
  std::wstring folder = PickFolder(s->hwnd, L"Pin a ClipCue target folder", GetText(s->target));
  if (folder.empty()) return;
  HistoryDatabase db; std::wstring err; db.Load(&err);
  PinnedTarget pin; pin.destParent = folder; pin.op = ChosenOp(s->op); pin.label = FormatMenuLabel(folder);
  if (!db.AddPinnedTarget(pin, &err)) { MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR); return; }
  db.WriteMenuCache(L"", nullptr);
  SetWindowTextW(s->target, folder.c_str());
  Refresh(s, L"Pinned target added and selected as the active queue target.");
}
void RemovePin(State* s) {
  int sel = static_cast<int>(SendMessageW(s->pins, LB_GETCURSEL, 0, 0));
  if (sel < 0 || static_cast<size_t>(sel) >= s->pinsData.size()) { MessageBoxW(s->hwnd, L"Select a pinned target first.", L"ClipCue", MB_ICONINFORMATION); return; }
  HistoryDatabase db; std::wstring err; db.Load(&err);
  if (!db.RemovePinnedTarget(static_cast<size_t>(sel), &err)) { MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR); return; }
  db.WriteMenuCache(L"", nullptr); Refresh(s, L"Pinned target removed.");
}
void QueueUpdate(State* s, OperationKind op, bool selected, bool stale, const wchar_t* note) {
  std::vector<int> rows = Selections(s->queue);
  if (rows.empty()) { MessageBoxW(s->hwnd, L"Select one or more queue entries first.", L"ClipCue", MB_ICONINFORMATION); return; }
  HistoryDatabase db; std::wstring err; db.Load(&err);
  for (int row : rows) if (row >= 0 && static_cast<size_t>(row) < s->fileData.size()) {
    ClipboardHistoryEntry e = s->fileData[static_cast<size_t>(row)];
    e.selected = selected; e.stale = stale; if (op != OperationKind::Unknown) e.op = op;
    if (!db.UpdateClipboardEntry(e, &err)) { MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR); return; }
  }
  db.WriteMenuCache(L"", nullptr); Refresh(s, note);
}
void ApplyQueue(State* s, const wchar_t* op) {
  std::wstring target = GetText(s->target);
  if (target.empty() || !DirectoryExists(target)) { MessageBoxW(s->hwnd, L"Choose an existing target folder first.", L"ClipCue", MB_ICONINFORMATION); return; }
  std::wstring args = L"--paste-queue --target " + QuoteArg(target) + L" --op " + op;
  ShellExecuteW(s->hwnd, L"open", AgentPath().c_str(), args.c_str(), ModuleDir().c_str(), SW_SHOWNORMAL);
}
void ArchiveQueue(State* s) {
  HistoryDatabase db; std::wstring err; db.Load(&err);
  if (!db.MarkSelectedFileClipboardEntriesStale(&err)) { MessageBoxW(s->hwnd, err.c_str(), L"ClipCue", MB_ICONERROR); return; }
  db.WriteMenuCache(L"", nullptr); Refresh(s, L"Active path queue archived into history.");
}
void QueueDetails(State* s) {
  auto rows = Selections(s->queue);
  if (rows.size() == 1 && rows[0] >= 0 && static_cast<size_t>(rows[0]) < s->fileData.size()) SetWindowTextW(s->queueDetails, FileDetails(s->fileData[static_cast<size_t>(rows[0])]).c_str());
  else SetWindowTextW(s->queueDetails, L"Select one queue entry to inspect its action, repeat count, source app, and full paths.");
}
std::wstring Normalize(std::wstring text) {
  std::wstring out; bool space = false;
  for (wchar_t ch : text) {
    if (std::iswspace(ch)) { if (!space) out.push_back(L' '); space = true; }
    else { out.push_back(ch); space = false; }
  }
  while (!out.empty() && out.front() == L' ') out.erase(out.begin());
  while (!out.empty() && out.back() == L' ') out.pop_back();
  return out;
}
bool PutClipboard(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) return false;
  EmptyClipboard();
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  if (!h) { CloseClipboard(); return false; }
  void* raw = GlobalLock(h);
  if (!raw) { GlobalFree(h); CloseClipboard(); return false; }
  std::memcpy(raw, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(h);
  if (!SetClipboardData(CF_UNICODETEXT, h)) { GlobalFree(h); CloseClipboard(); return false; }
  CloseClipboard(); return true;
}
void SendPaste() {
  INPUT input[4]{};
  input[0].type = INPUT_KEYBOARD; input[0].ki.wVk = VK_CONTROL;
  input[1].type = INPUT_KEYBOARD; input[1].ki.wVk = 'V';
  input[2].type = INPUT_KEYBOARD; input[2].ki.wVk = 'V'; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
  input[3].type = INPUT_KEYBOARD; input[3].ki.wVk = VK_CONTROL; input[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, input, sizeof(INPUT));
}
void MergeText(State* s) {
  std::wstring combined;
  for (int row : Selections(s->textList)) if (row >= 0 && static_cast<size_t>(row) < s->textData.size()) {
    if (!combined.empty()) combined += L"\r\n\r\n";
    combined += s->textData[static_cast<size_t>(row)].text;
  }
  if (!combined.empty()) SetWindowTextW(s->textEdit, combined.c_str());
  TextMeta(s);
}
void CopyText(State* s, bool paste) {
  std::wstring text = GetText(s->textEdit);
  if (text.empty()) { MessageBoxW(s->hwnd, L"Select or enter text first.", L"ClipCue", MB_ICONINFORMATION); return; }
  if (!PutClipboard(s->hwnd, text)) { MessageBoxW(s->hwnd, L"Unable to set clipboard text.", L"ClipCue", MB_ICONERROR); return; }
  if (paste && s->previous && IsWindow(s->previous) && s->previous != s->hwnd) { SetForegroundWindow(s->previous); Sleep(120); SendPaste(); }
  else Refresh(s, paste ? L"Edited text copied; switch to the destination app and paste." : L"Edited text copied to clipboard.");
}
void RegisterMenu(State* s, bool reg) {
  HMODULE h = LoadLibraryW(ShellDllPath().c_str());
  if (!h) { MessageBoxW(s->hwnd, (L"Unable to load shell DLL: " + GetLastErrorMessage()).c_str(), L"ClipCue", MB_ICONERROR); return; }
  using Fn = HRESULT(__stdcall*)();
  Fn fn = reinterpret_cast<Fn>(GetProcAddress(h, reg ? "DllRegisterServer" : "DllUnregisterServer"));
  HRESULT hr = fn ? fn() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
  FreeLibrary(h);
  if (FAILED(hr)) { MessageBoxW(s->hwnd, FormatHResult(hr).c_str(), L"ClipCue", MB_ICONERROR); return; }
  Refresh(s, reg ? L"Explorer classic context menu registered." : L"Explorer classic context menu unregistered.");
}

void DrawCard(HDC dc, int x, int y, int w, int h) {
  HBRUSH b = CreateSolidBrush(kCard); HPEN p = CreatePen(PS_SOLID, 1, kBorder);
  HGDIOBJ ob = SelectObject(dc, b); HGDIOBJ op = SelectObject(dc, p);
  RoundRect(dc, x, y, x + w, y + h, 14, 14);
  SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
}
void Paint(State* s, HDC dc) {
  RECT rc{}; GetClientRect(s->hwnd, &rc);
  HBRUSH bg = CreateSolidBrush(kBg); FillRect(dc, &rc, bg); DeleteObject(bg);
  RECT head{0, 0, rc.right, 84}; HBRUSH hb = CreateSolidBrush(RGB(248, 250, 252)); FillRect(dc, &head, hb); DeleteObject(hb);
  RECT line{0, 82, rc.right, 84}; HBRUSH ab = CreateSolidBrush(kAccent); FillRect(dc, &line, ab); DeleteObject(ab);
  if (s->queueOnly) { DrawCard(dc, 22, 112, 580, 430); DrawCard(dc, 620, 112, 360, 430); DrawCard(dc, 22, 560, 958, 112); return; }
  if (s->page == kPaths) { DrawCard(dc, 26, 120, 460, 190); DrawCard(dc, 500, 120, 474, 190); DrawCard(dc, 26, 326, 640, 268); DrawCard(dc, 682, 326, 292, 268); }
  if (s->page == kText) { DrawCard(dc, 26, 120, 428, 472); DrawCard(dc, 470, 120, 504, 472); }
  if (s->page == kStatus) { DrawCard(dc, 26, 120, 948, 214); DrawCard(dc, 26, 350, 948, 244); }
}
void Header(State* s, const wchar_t* title, const wchar_t* subtitle) {
  Make(s, L"STATIC", title, WS_CHILD | WS_VISIBLE, 24, 15, 330, 34, -1, s->titleFont);
  Make(s, L"STATIC", subtitle, WS_CHILD | WS_VISIBLE, 26, 52, 720, 22, -1);
  s->dash = Make(s, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 590, 16, 410, 58, IDC_DASH);
}
void CreateFull(State* s) {
  Header(s, L"ClipCue", L"Professional control center for targets, file queues, Explorer integration, and clipboard text");
  Make(s, L"BUTTON", L"Targets && Path Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 88, 168, 28, IDC_NAV_PATHS);
  Make(s, L"BUTTON", L"Text Editor", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 202, 88, 118, 28, IDC_NAV_TEXT);
  Make(s, L"BUTTON", L"System Status", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 330, 88, 128, 28, IDC_NAV_STATUS);

  Label(s, kPaths, L"Pinned quick targets", 44, 132, 220, 22, s->sectionFont);
  Label(s, kPaths, L"Pin trusted destinations and choose whether they appear for copy, move, or both.", 44, 154, 410, 18);
  s->pins = Add(s, kPaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 44, 178, 422, 76, IDC_PINS, nullptr, WS_EX_CLIENTEDGE);
  Label(s, kPaths, L"New target", 44, 268, 80, 22);
  s->op = Add(s, kPaths, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 128, 264, 132, 120, IDC_OP);
  SendMessageW(s->op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy + Move"));
  SendMessageW(s->op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Move only"));
  SendMessageW(s->op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy only"));
  SendMessageW(s->op, CB_SETCURSEL, 0, 0);
  Add(s, kPaths, L"BUTTON", L"Add...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 270, 262, 78, 28, IDC_ADD_PIN);
  Add(s, kPaths, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 356, 262, 88, 28, IDC_REMOVE_PIN);
  Add(s, kPaths, L"BUTTON", L"Use", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 44, 284, 68, 24, IDC_USE_TARGET);
  Add(s, kPaths, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 122, 284, 68, 24, IDC_OPEN_TARGET);

  Label(s, kPaths, L"Smart suggestions", 518, 132, 220, 22, s->sectionFont);
  Label(s, kPaths, L"Detected from previous ClipCue and Explorer work; double-click to set target.", 518, 154, 420, 18);
  s->suggest = Add(s, kPaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 518, 178, 436, 98, IDC_SUGGEST, nullptr, WS_EX_CLIENTEDGE);
  Add(s, kPaths, L"BUTTON", L"Use suggestion", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 518, 284, 120, 26, IDC_USE_TARGET);
  Add(s, kPaths, L"BUTTON", L"Rebuild cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 648, 284, 124, 26, IDC_BUILD_CACHE);

  Label(s, kPaths, L"Path clipboard queue", 44, 336, 250, 22, s->sectionFont);
  Label(s, kPaths, L"Review copied/cut paths, tune copy vs. move, then replay the prepared batch.", 44, 358, 590, 18);
  s->queue = Add(s, kPaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 44, 384, 390, 134, IDC_QUEUE, nullptr, WS_EX_CLIENTEDGE);
  s->queueDetails = Add(s, kPaths, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 446, 384, 202, 134, IDC_QUEUE_DETAILS, s->monoFont, WS_EX_CLIENTEDGE);
  Add(s, kPaths, L"BUTTON", L"Set Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 44, 532, 82, 28, IDC_QUEUE_COPY);
  Add(s, kPaths, L"BUTTON", L"Set Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 134, 532, 82, 28, IDC_QUEUE_MOVE);
  Add(s, kPaths, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 224, 532, 68, 28, IDC_QUEUE_SKIP);
  Add(s, kPaths, L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 300, 532, 82, 28, IDC_QUEUE_ACTIVE);
  Add(s, kPaths, L"BUTTON", L"Archive active", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, 532, 112, 28, IDC_QUEUE_ARCHIVE);

  Label(s, kPaths, L"Apply workflow", 702, 336, 210, 22, s->sectionFont);
  Label(s, kPaths, L"Target folder", 702, 368, 180, 18);
  s->target = Add(s, kPaths, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 702, 390, 198, 26, IDC_TARGET, nullptr, WS_EX_CLIENTEDGE);
  Add(s, kPaths, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 910, 388, 42, 30, IDC_PICK_TARGET);
  Add(s, kPaths, L"BUTTON", L"Apply all", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 702, 432, 78, 30, IDC_APPLY_ALL);
  Add(s, kPaths, L"BUTTON", L"Copy only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 788, 432, 82, 30, IDC_APPLY_COPY);
  Add(s, kPaths, L"BUTTON", L"Move only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 702, 470, 82, 30, IDC_APPLY_MOVE);
  Add(s, kPaths, L"BUTTON", L"Open target", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 792, 470, 96, 30, IDC_OPEN_TARGET);

  Label(s, kText, L"Clipboard text history", 44, 132, 250, 22, s->sectionFont);
  Label(s, kText, L"Select one or more clips; ClipCue merges them into the editor.", 44, 154, 380, 18);
  s->textList = Add(s, kText, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 44, 180, 390, 326, IDC_TEXT_LIST, nullptr, WS_EX_CLIENTEDGE);
  Add(s, kText, L"BUTTON", L"Select all", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 44, 522, 92, 28, IDC_TEXT_ALL);
  Add(s, kText, L"BUTTON", L"Clear editor", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 144, 522, 108, 28, IDC_TEXT_CLEAR);
  Label(s, kText, L"Text editing lab", 488, 132, 250, 22, s->sectionFont);
  Label(s, kText, L"Clean, combine, copy, or paste text back to the previous app.", 488, 154, 430, 18);
  s->textEdit = Add(s, kText, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_AUTOVSCROLL, 488, 180, 468, 326, IDC_TEXT_EDIT, s->monoFont, WS_EX_CLIENTEDGE);
  s->textMeta = Add(s, kText, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 488, 514, 468, 32, IDC_TEXT_META);
  Add(s, kText, L"BUTTON", L"Normalize spaces", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 488, 552, 132, 30, IDC_TEXT_NORMALIZE);
  Add(s, kText, L"BUTTON", L"Copy edited text", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 630, 552, 136, 30, IDC_TEXT_COPY);
  Add(s, kText, L"BUTTON", L"Paste to previous app", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 776, 552, 158, 30, IDC_TEXT_PASTE);

  Label(s, kStatus, L"Monitor, store, and Explorer integration", 44, 132, 360, 22, s->sectionFont);
  s->status = Add(s, kStatus, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 44, 166, 912, 142, IDC_STATUS, s->monoFont, WS_EX_CLIENTEDGE);
  Label(s, kStatus, L"Recent operations", 44, 360, 250, 22, s->sectionFont);
  s->history = Add(s, kStatus, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL, 44, 390, 912, 150, IDC_HISTORY, nullptr, WS_EX_CLIENTEDGE);
  Add(s, kStatus, L"BUTTON", L"Register Explorer menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 44, 556, 168, 30, IDC_REGISTER);
  Add(s, kStatus, L"BUTTON", L"Unregister menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 222, 556, 138, 30, IDC_UNREGISTER);
  Add(s, kStatus, L"BUTTON", L"Cleanup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 370, 556, 92, 30, IDC_CLEANUP);

  Make(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 696, 88, 30, IDC_REFRESH);
  Make(s, L"BUTTON", L"Open data", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 122, 696, 98, 30, IDC_OPEN_DATA);
  Make(s, L"BUTTON", L"Installer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 230, 696, 92, 30, IDC_INSTALLER);
  Make(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 900, 696, 92, 30, IDC_CLOSE);
}
void CreateQueueOnly(State* s) {
  Header(s, L"Path Clip Queue", L"Focused workspace for replaying multiple copied or cut Explorer paths into a target folder");
  Label(s, kPaths, L"Queue entries", 42, 128, 200, 22, s->sectionFont);
  s->queue = Add(s, kPaths, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 42, 158, 540, 326, IDC_QUEUE, nullptr, WS_EX_CLIENTEDGE);
  Label(s, kPaths, L"Details", 638, 128, 200, 22, s->sectionFont);
  s->queueDetails = Add(s, kPaths, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 638, 158, 320, 326, IDC_QUEUE_DETAILS, s->monoFont, WS_EX_CLIENTEDGE);
  Add(s, kPaths, L"BUTTON", L"Set Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 42, 504, 92, 32, IDC_QUEUE_COPY);
  Add(s, kPaths, L"BUTTON", L"Set Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 146, 504, 92, 32, IDC_QUEUE_MOVE);
  Add(s, kPaths, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 250, 504, 76, 32, IDC_QUEUE_SKIP);
  Add(s, kPaths, L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 338, 504, 92, 32, IDC_QUEUE_ACTIVE);
  Add(s, kPaths, L"BUTTON", L"Archive active", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 638, 504, 130, 32, IDC_QUEUE_ARCHIVE);
  Add(s, kPaths, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 780, 504, 92, 32, IDC_REFRESH);
  Label(s, kPaths, L"Target folder", 42, 584, 120, 22, s->sectionFont);
  s->target = Add(s, kPaths, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 42, 612, 540, 28, IDC_TARGET, nullptr, WS_EX_CLIENTEDGE);
  Add(s, kPaths, L"BUTTON", L"Choose...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 596, 610, 102, 32, IDC_PICK_TARGET);
  Add(s, kPaths, L"BUTTON", L"Apply all", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 718, 610, 92, 32, IDC_APPLY_ALL);
  Add(s, kPaths, L"BUTTON", L"Copy only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 820, 610, 92, 32, IDC_APPLY_COPY);
  Add(s, kPaths, L"BUTTON", L"Move only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 718, 650, 92, 32, IDC_APPLY_MOVE);
  Add(s, kPaths, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 820, 650, 92, 32, IDC_CLOSE);
}
void Create(State* s) {
  s->white = CreateSolidBrush(RGB(255, 255, 255));
  s->font = MakeFont(9); s->titleFont = MakeFont(s->queueOnly ? 21 : 20, FW_SEMIBOLD); s->sectionFont = MakeFont(11, FW_SEMIBOLD); s->monoFont = MakeFont(9, FW_NORMAL, L"Consolas");
  if (s->queueOnly) CreateQueueOnly(s); else CreateFull(s);
  Refresh(s); QueueDetails(s); ShowPage(s, kPaths);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: { auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp); s = reinterpret_cast<State*>(cs->lpCreateParams); s->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s)); return TRUE; }
    case WM_CREATE: Create(s); return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp), notify = HIWORD(wp);
      if (id == IDC_QUEUE && notify == LBN_SELCHANGE) { QueueDetails(s); return 0; }
      if (id == IDC_TEXT_LIST && notify == LBN_SELCHANGE) { MergeText(s); return 0; }
      if ((id == IDC_PINS || id == IDC_SUGGEST) && notify == LBN_DBLCLK) { std::wstring target = CurrentTarget(s); if (!target.empty()) SetWindowTextW(s->target, target.c_str()); return 0; }
      if (id == IDC_TEXT_EDIT && notify == EN_CHANGE) { TextMeta(s); return 0; }
      switch (id) {
        case IDC_NAV_PATHS: ShowPage(s, kPaths); return 0;
        case IDC_NAV_TEXT: ShowPage(s, kText); return 0;
        case IDC_NAV_STATUS: ShowPage(s, kStatus); return 0;
        case IDC_ADD_PIN: AddPin(s); return 0;
        case IDC_REMOVE_PIN: RemovePin(s); return 0;
        case IDC_USE_TARGET: { std::wstring target = CurrentTarget(s); if (!target.empty()) SetWindowTextW(s->target, target.c_str()); return 0; }
        case IDC_OPEN_TARGET: { std::wstring target = CurrentTarget(s); if (!target.empty()) ShellExecuteW(hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        case IDC_PICK_TARGET: { std::wstring target = PickFolder(hwnd, L"Choose target folder", GetText(s->target)); if (!target.empty()) SetWindowTextW(s->target, target.c_str()); return 0; }
        case IDC_QUEUE_COPY: QueueUpdate(s, OperationKind::Copy, true, false, L"Selected queue entries set to Copy."); return 0;
        case IDC_QUEUE_MOVE: QueueUpdate(s, OperationKind::Move, true, false, L"Selected queue entries set to Move."); return 0;
        case IDC_QUEUE_SKIP: QueueUpdate(s, OperationKind::Unknown, false, false, L"Selected queue entries skipped."); return 0;
        case IDC_QUEUE_ACTIVE: QueueUpdate(s, OperationKind::Unknown, true, false, L"Selected queue entries activated."); return 0;
        case IDC_QUEUE_ARCHIVE: ArchiveQueue(s); return 0;
        case IDC_APPLY_ALL: ApplyQueue(s, L"both"); return 0;
        case IDC_APPLY_COPY: ApplyQueue(s, L"copy"); return 0;
        case IDC_APPLY_MOVE: ApplyQueue(s, L"move"); return 0;
        case IDC_TEXT_ALL: SendMessageW(s->textList, LB_SETSEL, TRUE, static_cast<LPARAM>(-1)); MergeText(s); return 0;
        case IDC_TEXT_CLEAR: SetWindowTextW(s->textEdit, L""); TextMeta(s); return 0;
        case IDC_TEXT_NORMALIZE: SetWindowTextW(s->textEdit, Normalize(GetText(s->textEdit)).c_str()); return 0;
        case IDC_TEXT_COPY: CopyText(s, false); return 0;
        case IDC_TEXT_PASTE: CopyText(s, true); return 0;
        case IDC_REGISTER: RegisterMenu(s, true); return 0;
        case IDC_UNREGISTER: RegisterMenu(s, false); return 0;
        case IDC_BUILD_CACHE: { HistoryDatabase db; std::wstring err; db.Load(&err); if (!db.WriteMenuCache(L"", &err)) MessageBoxW(hwnd, err.c_str(), L"ClipCue", MB_ICONERROR); else Refresh(s, L"Explorer menu cache rebuilt."); return 0; }
        case IDC_CLEANUP: { HistoryDatabase db; std::wstring err; db.Load(&err); if (!db.CleanupExpired(90, 365, &err)) MessageBoxW(hwnd, err.c_str(), L"ClipCue", MB_ICONERROR); else { db.WriteMenuCache(L"", nullptr); Refresh(s, L"Expired history cleaned."); } return 0; }
        case IDC_OPEN_DATA: ShellExecuteW(hwnd, L"open", PathCombineSimple(GetKnownFolderLocalAppData(), L"ClipCue").c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
        case IDC_INSTALLER: ShellExecuteW(hwnd, L"open", InstallerPath().c_str(), nullptr, ModuleDir().c_str(), SW_SHOWNORMAL); return 0;
        case IDC_REFRESH: Refresh(s); return 0;
        case IDC_CLOSE: DestroyWindow(hwnd); return 0;
      }
      break;
    }
    case WM_CTLCOLORSTATIC: { HDC dc = reinterpret_cast<HDC>(wp); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kText); return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH)); }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: { HDC dc = reinterpret_cast<HDC>(wp); SetTextColor(dc, kText); SetBkColor(dc, RGB(255,255,255)); return reinterpret_cast<LRESULT>(s && s->white ? s->white : GetStockObject(WHITE_BRUSH)); }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); if (s) Paint(s, dc); EndPaint(hwnd, &ps); return 0; }
    case WM_DESTROY:
      if (s) { if (s->font) DeleteObject(s->font); if (s->titleFont) DeleteObject(s->titleFont); if (s->sectionFont) DeleteObject(s->sectionFont); if (s->monoFont) DeleteObject(s->monoFont); if (s->white) DeleteObject(s->white); }
      PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
  auto args = SplitCommandLineArgs();
  State state; state.previous = GetForegroundWindow();
  state.queueOnly = std::any_of(args.begin(), args.end(), [](const std::wstring& a) { return a == L"queue" || a == L"--queue" || a == L"/queue" || a == L"path-queue" || a == L"--path-queue"; });
  WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance; wc.lpszClassName = L"ClipCueModernControlCenter"; wc.lpfnWndProc = WndProc; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = nullptr; RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, state.queueOnly ? L"ClipCue Path Clip Queue" : L"ClipCue Control Center", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, state.queueOnly ? 1018 : 1024, state.queueOnly ? 728 : 770, nullptr, nullptr, instance, &state);
  if (!hwnd) return 1;
  ShowWindow(hwnd, show); UpdateWindow(hwnd);
  MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return static_cast<int>(msg.wParam);
}
