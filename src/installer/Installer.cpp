#include "pathcue/PathUtils.h"
#include "pathcue/WinUtils.h"
#include "pathcue/Version.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <objbase.h>

#include <sstream>
#include <string>
#include <vector>

using namespace pathcue;

namespace {

constexpr int IDC_INSTALL_DIR = 2001;
constexpr int IDC_BROWSE = 2002;
constexpr int IDC_REGISTER_SHELL = 2003;
constexpr int IDC_SHORTCUTS = 2004;
constexpr int IDC_LAUNCH_UI = 2005;
constexpr int IDC_INSTALL = 2006;
constexpr int IDC_UNINSTALL = 2007;
constexpr int IDC_STATUS = 2008;
constexpr int IDC_CLOSE = 2009;
constexpr int IDC_OPEN_FOLDER = 2010;

struct InstallerState {
  HWND hwnd = nullptr;
  HWND installDir = nullptr;
  HWND registerShell = nullptr;
  HWND shortcuts = nullptr;
  HWND launchUi = nullptr;
  HWND status = nullptr;
  HFONT font = nullptr;
};

std::wstring ModuleDir() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : exe.substr(0, pos);
}

std::wstring DefaultInstallDir() {
  return PathCombineSimple(GetKnownFolderLocalAppData(), L"Programs\\PathCue");
}

std::wstring ReadInstallDirFromRegistry() {
  wchar_t value[32768]{};
  DWORD cb = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\PathCue", L"InstallDir", RRF_RT_REG_SZ, nullptr, value, &cb) == ERROR_SUCCESS) {
    return value;
  }
  return DefaultInstallDir();
}

void SetFont(HWND h, HFONT f) {
  if (h && f) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
}

HWND MakeControl(InstallerState* s, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
  HWND ctrl = CreateWindowExW(0, cls, text, style, x, y, w, h, s->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SetFont(ctrl, s->font);
  return ctrl;
}

void AppendLog(HWND edit, const std::wstring& line) {
  if (!edit) {
    OutputDebugStringW((line + L"\r\n").c_str());
    return;
  }
  int len = GetWindowTextLengthW(edit);
  SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
  std::wstring text = line + L"\r\n";
  SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void SetRegString(HKEY root, const std::wstring& key, const std::wstring& name, const std::wstring& value) {
  HKEY h{};
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return;
  RegSetValueExW(h, name.empty() ? nullptr : name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(h);
}

void SetRegDWORD(HKEY root, const std::wstring& key, const std::wstring& name, DWORD value) {
  HKEY h{};
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return;
  RegSetValueExW(h, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(h);
}

bool CopyOne(const std::wstring& src, const std::wstring& dst, HWND log) {
  if (IsSamePathCaseInsensitive(src, dst)) {
    AppendLog(log, L"Already in place: " + FileNameFromPath(dst));
    return true;
  }
  EnsureDirectory(ParentPath(dst));
  if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
    DWORD err = GetLastError();
    std::wstringstream ss;
    ss << L"Copy failed: " << src << L" → " << dst << L" (" << GetLastErrorMessage(err) << L")";
    AppendLog(log, ss.str());
    return false;
  }
  AppendLog(log, L"Copied: " + FileNameFromPath(dst));
  return true;
}

void CopyPattern(const std::wstring& sourceDir, const std::wstring& targetDir, const std::wstring& pattern, HWND log) {
  WIN32_FIND_DATAW fd{};
  std::wstring search = PathCombineSimple(sourceDir, pattern);
  HANDLE find = FindFirstFileW(search.c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    std::wstring src = PathCombineSimple(sourceDir, fd.cFileName);
    std::wstring dst = PathCombineSimple(targetDir, fd.cFileName);
    CopyOne(src, dst, log);
  } while (FindNextFileW(find, &fd));
  FindClose(find);
}

void CopyDirectoryRecursive(const std::wstring& source, const std::wstring& target, HWND log) {
  if (!DirectoryExists(source)) return;
  EnsureDirectory(target);
  WIN32_FIND_DATAW fd{};
  HANDLE find = FindFirstFileW(PathCombineSimple(source, L"*").c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return;
  do {
    std::wstring name = fd.cFileName;
    if (name == L"." || name == L"..") continue;
    std::wstring src = PathCombineSimple(source, name);
    std::wstring dst = PathCombineSimple(target, name);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyDirectoryRecursive(src, dst, log);
    else CopyOne(src, dst, log);
  } while (FindNextFileW(find, &fd));
  FindClose(find);
}

HRESULT CallShellRegistration(const std::wstring& installDir, bool reg) {
  std::wstring dll = PathCombineSimple(installDir, L"PathCue.ShellClassic.dll");
  HMODULE h = LoadLibraryW(dll.c_str());
  if (!h) return HRESULT_FROM_WIN32(GetLastError());
  using Fn = HRESULT(__stdcall*)();
  Fn fn = reinterpret_cast<Fn>(GetProcAddress(h, reg ? "DllRegisterServer" : "DllUnregisterServer"));
  HRESULT hr = fn ? fn() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
  FreeLibrary(h);
  return hr;
}

std::wstring StartMenuPath() {
  PWSTR raw = nullptr;
  std::wstring base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_StartMenu, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
    base = raw;
    CoTaskMemFree(raw);
  } else {
    base = PathCombineSimple(GetKnownFolderLocalAppData(), L"Microsoft\\Windows\\Start Menu");
  }
  return PathCombineSimple(base, L"Programs\\PathCue");
}

bool CreateShortcut(const std::wstring& linkPath, const std::wstring& target, const std::wstring& args, const std::wstring& description) {
  CoInitializeScope co;
  if (FAILED(co.hr())) return false;
  IShellLinkW* link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
  link->SetPath(target.c_str());
  if (!args.empty()) link->SetArguments(args.c_str());
  link->SetDescription(description.c_str());
  link->SetWorkingDirectory(ParentPath(target).c_str());
  IPersistFile* file = nullptr;
  HRESULT hr = link->QueryInterface(IID_PPV_ARGS(&file));
  if (SUCCEEDED(hr)) {
    EnsureDirectory(ParentPath(linkPath));
    hr = file->Save(linkPath.c_str(), TRUE);
    file->Release();
  }
  link->Release();
  return SUCCEEDED(hr);
}

void CreateShortcuts(const std::wstring& installDir, HWND log) {
  std::wstring menu = StartMenuPath();
  EnsureDirectory(menu);
  CreateShortcut(PathCombineSimple(menu, L"PathCue Control Panel.lnk"), PathCombineSimple(installDir, L"PathCue.UI.exe"), L"", L"Configure PathCue");
  CreateShortcut(PathCombineSimple(menu, L"PathCue Installer.lnk"), PathCombineSimple(installDir, L"PathCue.Installer.exe"), L"", L"Install or repair PathCue");
  CreateShortcut(PathCombineSimple(menu, L"Uninstall PathCue.lnk"), PathCombineSimple(installDir, L"PathCue.Installer.exe"), L"/uninstall", L"Uninstall PathCue");
  AppendLog(log, L"Start menu shortcuts created.");
}

void DeleteDirectoryRecursive(const std::wstring& dir, HWND log) {
  if (!DirectoryExists(dir)) return;
  WIN32_FIND_DATAW fd{};
  HANDLE find = FindFirstFileW(PathCombineSimple(dir, L"*").c_str(), &fd);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      std::wstring name = fd.cFileName;
      if (name == L"." || name == L"..") continue;
      std::wstring path = PathCombineSimple(dir, name);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteDirectoryRecursive(path, log);
      else if (!DeleteFileW(path.c_str())) MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
  }
  if (!RemoveDirectoryW(dir.c_str())) MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

void WriteUninstallInfo(const std::wstring& installDir) {
  std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PathCue";
  std::wstring installer = PathCombineSimple(installDir, L"PathCue.Installer.exe");
  SetRegString(HKEY_CURRENT_USER, key, L"DisplayName", L"PathCue");
  SetRegString(HKEY_CURRENT_USER, key, L"DisplayVersion", PATHCUE_VERSION);
  SetRegString(HKEY_CURRENT_USER, key, L"Publisher", L"PathCue contributors");
  SetRegString(HKEY_CURRENT_USER, key, L"InstallLocation", installDir);
  SetRegString(HKEY_CURRENT_USER, key, L"DisplayIcon", installer);
  SetRegString(HKEY_CURRENT_USER, key, L"UninstallString", QuoteArg(installer) + L" /uninstall");
  SetRegString(HKEY_CURRENT_USER, key, L"QuietUninstallString", QuoteArg(installer) + L" /uninstall /silent");
  SetRegDWORD(HKEY_CURRENT_USER, key, L"NoModify", 1);
  SetRegDWORD(HKEY_CURRENT_USER, key, L"NoRepair", 1);
}

bool InstallTo(const std::wstring& sourceDir, const std::wstring& installDir, bool registerShell, bool shortcuts, HWND log) {
  AppendLog(log, L"Installing PathCue to: " + installDir);
  EnsureDirectory(installDir);
  CopyPattern(sourceDir, installDir, L"PathCue.*.exe", log);
  CopyPattern(sourceDir, installDir, L"PathCue.*.dll", log);
  const wchar_t* filesToCopy[] = {L"LICENSE", L"NOTICE", L"README.md"};
  for (const wchar_t* file : filesToCopy) {
    std::wstring src = PathCombineSimple(sourceDir, file);
    if (FileExists(src)) CopyOne(src, PathCombineSimple(installDir, file), log);
  }
  CopyDirectoryRecursive(PathCombineSimple(sourceDir, L"scripts"), PathCombineSimple(installDir, L"scripts"), log);
  CopyDirectoryRecursive(PathCombineSimple(sourceDir, L"docs"), PathCombineSimple(installDir, L"docs"), log);

  SetRegString(HKEY_CURRENT_USER, L"Software\\PathCue", L"InstallDir", installDir);
  WriteUninstallInfo(installDir);

  if (registerShell) {
    HRESULT hr = CallShellRegistration(installDir, true);
    if (FAILED(hr)) {
      AppendLog(log, L"Shell registration failed: " + FormatHResult(hr));
      return false;
    }
    AppendLog(log, L"Classic Explorer context menu registered for the current user.");
  }
  if (shortcuts) CreateShortcuts(installDir, log);
  AppendLog(log, L"Install complete. Restart Explorer or sign out/in if the context menu does not appear immediately.");
  return true;
}

bool UninstallFrom(const std::wstring& installDir, HWND log) {
  AppendLog(log, L"Uninstalling PathCue from: " + installDir);
  HRESULT hr = CallShellRegistration(installDir, false);
  if (FAILED(hr)) AppendLog(log, L"Shell unregister returned: " + FormatHResult(hr));
  else AppendLog(log, L"Classic Explorer context menu unregistered.");

  SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PathCue");
  SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\PathCue");
  DeleteDirectoryRecursive(StartMenuPath(), log);

  std::wstring runningDir = ModuleDir();
  if (IsSamePathCaseInsensitive(runningDir, installDir)) {
    AppendLog(log, L"Installer is running from the install directory; remaining files will be removed after reboot if locked.");
  }
  DeleteDirectoryRecursive(installDir, log);
  AppendLog(log, L"Uninstall complete. User data under %LOCALAPPDATA%\\PathCue is left intact.");
  return true;
}

std::wstring PickFolder(HWND owner, const std::wstring& initial) {
  CoInitializeScope co;
  if (FAILED(co.hr())) return L"";
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return L"";
  DWORD opts = 0;
  dialog->GetOptions(&opts);
  dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dialog->SetTitle(L"Choose PathCue install folder");
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

std::wstring GetEditText(HWND edit) {
  int len = GetWindowTextLengthW(edit);
  std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
  GetWindowTextW(edit, text.data(), len + 1);
  text.resize(static_cast<std::size_t>(len));
  return text;
}

bool IsChecked(HWND button) { return SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED; }

void OnCreate(InstallerState* s) {
  s->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  MakeControl(s, L"STATIC", L"PathCue Installer", WS_CHILD | WS_VISIBLE, 16, 12, 400, 24, -1);
  MakeControl(s, L"STATIC", L"Install folder:", WS_CHILD | WS_VISIBLE, 16, 50, 105, 22, -1);
  s->installDir = MakeControl(s, L"EDIT", ReadInstallDirFromRegistry().c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 122, 46, 488, 24, IDC_INSTALL_DIR);
  MakeControl(s, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 620, 45, 90, 27, IDC_BROWSE);

  s->registerShell = MakeControl(s, L"BUTTON", L"Register Explorer classic context menu for current user", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 122, 84, 420, 24, IDC_REGISTER_SHELL);
  s->shortcuts = MakeControl(s, L"BUTTON", L"Create Start Menu shortcuts", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 122, 112, 300, 24, IDC_SHORTCUTS);
  s->launchUi = MakeControl(s, L"BUTTON", L"Launch PathCue Control Panel after install", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 122, 140, 360, 24, IDC_LAUNCH_UI);
  SendMessageW(s->registerShell, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(s->shortcuts, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(s->launchUi, BM_SETCHECK, BST_CHECKED, 0);

  MakeControl(s, L"BUTTON", L"Install / Repair", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 122, 176, 130, 32, IDC_INSTALL);
  MakeControl(s, L"BUTTON", L"Uninstall", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 262, 176, 100, 32, IDC_UNINSTALL);
  MakeControl(s, L"BUTTON", L"Open folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 372, 176, 100, 32, IDC_OPEN_FOLDER);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 610, 176, 100, 32, IDC_CLOSE);

  s->status = MakeControl(s, L"EDIT", L"Ready. This installer performs a per-user install and does not require administrator rights.\r\n", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 16, 224, 694, 226, IDC_STATUS);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  InstallerState* s = reinterpret_cast<InstallerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      s = reinterpret_cast<InstallerState*>(cs->lpCreateParams);
      s->hwnd = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
      return TRUE;
    }
    case WM_CREATE:
      OnCreate(s);
      return 0;
    case WM_COMMAND: {
      switch (LOWORD(wp)) {
        case IDC_BROWSE: {
          std::wstring folder = PickFolder(hwnd, GetEditText(s->installDir));
          if (!folder.empty()) SetWindowTextW(s->installDir, folder.c_str());
          return 0;
        }
        case IDC_INSTALL: {
          SetCursor(LoadCursorW(nullptr, IDC_WAIT));
          std::wstring target = GetEditText(s->installDir);
          bool ok = InstallTo(ModuleDir(), target, IsChecked(s->registerShell), IsChecked(s->shortcuts), s->status);
          SetCursor(LoadCursorW(nullptr, IDC_ARROW));
          if (ok && IsChecked(s->launchUi)) {
            ShellExecuteW(hwnd, L"open", PathCombineSimple(target, L"PathCue.UI.exe").c_str(), nullptr, target.c_str(), SW_SHOWNORMAL);
          }
          return 0;
        }
        case IDC_UNINSTALL: {
          if (MessageBoxW(hwnd, L"Uninstall PathCue? User data and history are left intact.", L"PathCue", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            UninstallFrom(GetEditText(s->installDir), s->status);
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
          }
          return 0;
        }
        case IDC_OPEN_FOLDER:
          ShellExecuteW(hwnd, L"open", GetEditText(s->installDir).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        case IDC_CLOSE:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int RunCommandLine(const std::vector<std::wstring>& args) {
  bool silent = false;
  bool uninstall = false;
  bool install = false;
  std::wstring target = ReadInstallDirFromRegistry();
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"/silent" || args[i] == L"--silent") silent = true;
    else if (args[i] == L"/uninstall" || args[i] == L"--uninstall") uninstall = true;
    else if (args[i] == L"/install" || args[i] == L"--install") install = true;
    else if ((args[i] == L"/dir" || args[i] == L"--dir") && i + 1 < args.size()) target = args[++i];
  }
  HWND log = nullptr;
  if (!silent) AllocConsole();
  if (install) return InstallTo(ModuleDir(), target, true, true, log) ? 0 : 1;
  if (uninstall) return UninstallFrom(target, log) ? 0 : 1;
  return -1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  auto args = SplitCommandLineArgs();
  int cli = RunCommandLine(args);
  if (cli >= 0) return cli;

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  InstallerState state;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = instance;
  wc.lpszClassName = L"PathCueInstallerWindow";
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"PathCue Installer", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 745, 500, nullptr, nullptr, instance, &state);
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
