#include "pathcue/CryptoStore.h"
#include "pathcue/History.h"
#include "pathcue/PathUtils.h"
#include "pathcue/WinUtils.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

using namespace pathcue;

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

struct UiState {
  HWND hwnd = nullptr;
  HWND status = nullptr;
  HWND pins = nullptr;
  HWND suggestions = nullptr;
  HWND history = nullptr;
  HWND opCombo = nullptr;
  HFONT font = nullptr;
  std::vector<PinnedTarget> pinsData;
};

std::wstring ModuleDir() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : exe.substr(0, pos);
}

std::wstring ShellDllPath() { return PathCombineSimple(ModuleDir(), L"PathCue.ShellClassic.dll"); }
std::wstring InstallerPath() { return PathCombineSimple(ModuleDir(), L"PathCue.Installer.exe"); }

void SetControlFont(HWND h, HFONT font) {
  if (h && font) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND MakeControl(UiState* s, const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
  HWND ctrl = CreateWindowExW(0, klass, text, style, x, y, w, h, s->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SetControlFont(ctrl, s->font);
  return ctrl;
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

std::wstring StatusText(const HistoryDatabase& db, const std::wstring& extra = L"") {
  std::wstringstream ss;
  ss << L"Store: " << EncryptedRecordStore::DefaultStorePath() << L"\r\n";
  ss << L"Menu cache: " << EncryptedRecordStore::DefaultMenuCachePath() << L"\r\n";
  ss << L"Pinned targets: " << db.pinnedTargets().size() << L"    Operation records: " << db.operations().size() << L"\r\n";
  ss << L"Protection: DPAPI CurrentUser encrypted record store; menu cache is plaintext for Explorer speed.";
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
  if (!err.empty() && extra.empty()) {
    SetWindowTextW(s->status, StatusText(db, err).c_str());
  } else {
    SetWindowTextW(s->status, StatusText(db, extra).c_str());
  }

  s->pinsData = db.pinnedTargets();
  SendMessageW(s->pins, LB_RESETCONTENT, 0, 0);
  for (const auto& pin : s->pinsData) {
    std::wstring line = L"[" + OperationDisplay(pin.op) + L"]  " + (pin.label.empty() ? FormatMenuLabel(pin.destParent) : pin.label) + L"  →  " + pin.destParent;
    SendMessageW(s->pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }

  SendMessageW(s->suggestions, LB_RESETCONTENT, 0, 0);
  AddCandidateLines(s->suggestions, db, OperationKind::Move, L"PathCue Move to...");
  AddCandidateLines(s->suggestions, db, OperationKind::Copy, L"PathCue Copy to...");
  if (SendMessageW(s->suggestions, LB_GETCOUNT, 0, 0) == 0) {
    SendMessageW(s->suggestions, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No detected quick targets yet."));
  }

  SendMessageW(s->history, LB_RESETCONTENT, 0, 0);
  const auto& ops = db.operations();
  int shown = 0;
  for (auto it = ops.rbegin(); it != ops.rend() && shown < 100; ++it, ++shown) {
    std::wstring line = FormatTime(it->timestamp) + L"  [" + OperationDisplay(it->op) + L"]  " +
                        FormatMenuLabel(it->sourceParent) + L"  →  " + FormatMenuLabel(it->destParent) +
                        L"  (" + it->result + L")";
    SendMessageW(s->history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
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
  std::wstring folder = PickFolder(s->hwnd, L"Choose a PathCue target folder");
  if (folder.empty()) return;
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  PinnedTarget pin;
  pin.op = SelectedOp(s->opCombo);
  pin.destParent = folder;
  pin.label = FormatMenuLabel(folder);
  if (!db.AddPinnedTarget(pin, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"PathCue", MB_ICONERROR);
    return;
  }
  db.WriteMenuCache(L"", &err);
  Refresh(s, L"Pinned target added and menu cache rebuilt.");
}

void OnRemovePin(UiState* s) {
  int sel = static_cast<int>(SendMessageW(s->pins, LB_GETCURSEL, 0, 0));
  if (sel < 0 || static_cast<std::size_t>(sel) >= s->pinsData.size()) {
    MessageBoxW(s->hwnd, L"Select a pinned target first.", L"PathCue", MB_ICONINFORMATION);
    return;
  }
  if (MessageBoxW(s->hwnd, L"Remove the selected pinned target?", L"PathCue", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  if (!db.RemovePinnedTarget(static_cast<std::size_t>(sel), &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"PathCue", MB_ICONERROR);
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
    MessageBoxW(s->hwnd, err.c_str(), L"PathCue", MB_ICONERROR);
    return;
  }
  Refresh(s, L"Menu cache rebuilt.");
}

void OnCleanup(UiState* s) {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  if (!db.CleanupExpired(90, 365, &err)) {
    MessageBoxW(s->hwnd, err.c_str(), L"PathCue", MB_ICONERROR);
    return;
  }
  db.Load(nullptr);
  db.WriteMenuCache(L"", nullptr);
  Refresh(s, L"Expired operation records cleaned. Pinned targets were kept.");
}

void OnRegisterMenu(UiState* s, bool reg) {
  HRESULT hr = CallShellRegistration(reg);
  if (FAILED(hr)) {
    std::wstring msg = (reg ? L"Register failed: " : L"Unregister failed: ") + FormatHResult(hr) + L"\r\nDLL: " + ShellDllPath();
    MessageBoxW(s->hwnd, msg.c_str(), L"PathCue", MB_ICONERROR);
    return;
  }
  Refresh(s, reg ? L"Classic Explorer context menu registered for the current user." : L"Classic Explorer context menu unregistered for the current user.");
}

void OnCreate(UiState* s) {
  s->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  MakeControl(s, L"STATIC", L"PathCue Control Panel", WS_CHILD | WS_VISIBLE, 14, 10, 500, 22, -1);
  s->status = MakeControl(s, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 14, 38, 850, 96, IDC_STATUS);

  MakeControl(s, L"STATIC", L"Pinned quick targets", WS_CHILD | WS_VISIBLE, 14, 146, 300, 18, -1);
  s->pins = MakeControl(s, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 14, 168, 850, 120, IDC_PINS);

  MakeControl(s, L"STATIC", L"Detected quick targets", WS_CHILD | WS_VISIBLE, 14, 300, 300, 18, -1);
  s->suggestions = MakeControl(s, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL, 14, 322, 850, 150, IDC_SUGGESTIONS);

  MakeControl(s, L"STATIC", L"New target operation:", WS_CHILD | WS_VISIBLE, 14, 486, 135, 22, -1);
  s->opCombo = MakeControl(s, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 150, 482, 160, 160, IDC_OP_COMBO);
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy + Move"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Move only"));
  SendMessageW(s->opCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Copy only"));
  SendMessageW(s->opCombo, CB_SETCURSEL, 0, 0);

  MakeControl(s, L"BUTTON", L"Add target...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 324, 482, 112, 28, IDC_ADD_PIN);
  MakeControl(s, L"BUTTON", L"Remove selected", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 444, 482, 130, 28, IDC_REMOVE_PIN);
  MakeControl(s, L"BUTTON", L"Build menu cache", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 584, 482, 130, 28, IDC_BUILD_CACHE);
  MakeControl(s, L"BUTTON", L"Cleanup history", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 724, 482, 140, 28, IDC_CLEANUP);

  MakeControl(s, L"STATIC", L"Recent PathCue operations", WS_CHILD | WS_VISIBLE, 14, 528, 300, 18, -1);
  s->history = MakeControl(s, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL, 14, 550, 850, 118, IDC_HISTORY);

  MakeControl(s, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 14, 688, 94, 30, IDC_REFRESH);
  MakeControl(s, L"BUTTON", L"Open data folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 118, 688, 130, 30, IDC_OPEN_DATA);
  MakeControl(s, L"BUTTON", L"Register menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 258, 688, 118, 30, IDC_REGISTER_MENU);
  MakeControl(s, L"BUTTON", L"Unregister menu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 386, 688, 128, 30, IDC_UNREGISTER_MENU);
  MakeControl(s, L"BUTTON", L"Open installer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 524, 688, 122, 30, IDC_OPEN_INSTALLER);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 770, 688, 94, 30, IDC_CLOSE);

  Refresh(s);
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
      OnCreate(s);
      return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp);
      switch (id) {
        case IDC_ADD_PIN: OnAddPin(s); return 0;
        case IDC_REMOVE_PIN: OnRemovePin(s); return 0;
        case IDC_BUILD_CACHE: OnBuildCache(s); return 0;
        case IDC_CLEANUP: OnCleanup(s); return 0;
        case IDC_REFRESH: Refresh(s); return 0;
        case IDC_OPEN_DATA: LaunchPath(PathCombineSimple(GetKnownFolderLocalAppData(), L"PathCue")); return 0;
        case IDC_REGISTER_MENU: OnRegisterMenu(s, true); return 0;
        case IDC_UNREGISTER_MENU: OnRegisterMenu(s, false); return 0;
        case IDC_OPEN_INSTALLER: LaunchPath(InstallerPath()); return 0;
        case IDC_CLOSE: DestroyWindow(hwnd); return 0;
      }
      break;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&icc);

  UiState state;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = instance;
  wc.lpszClassName = L"PathCueControlPanelWindow";
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"PathCue Control Panel", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 900, 770, nullptr, nullptr, instance, &state);
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
