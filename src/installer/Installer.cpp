#include "clipcue/PathUtils.h"
#include "clipcue/WinUtils.h"
#include "clipcue/Version.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

using namespace clipcue;

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
constexpr int IDC_START_MONITOR = 2011;
constexpr int IDC_DEFAULT_DIR = 2012;
constexpr int IDC_PLAN = 2013;
constexpr int IDC_SOURCE_FOLDER = 2014;

constexpr COLORREF kPageBg = RGB(243, 247, 252);
constexpr COLORREF kRailBg = RGB(29, 41, 57);
constexpr COLORREF kText = RGB(51, 65, 85);

constexpr wchar_t kMonitorRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kMonitorRunValue[] = L"ClipCue Monitor";
constexpr wchar_t kMonitorWindowClass[] = L"ClipCueMonitorWindow";
constexpr UINT kMonitorQuitMessage = WM_APP + 3;

struct InstallerState {
  HWND hwnd = nullptr;
  HWND installDir = nullptr;
  HWND registerShell = nullptr;
  HWND shortcuts = nullptr;
  HWND startMonitor = nullptr;
  HWND launchUi = nullptr;
  HWND status = nullptr;
  HWND plan = nullptr;
  HFONT font = nullptr;
  HFONT smallFont = nullptr;
  HFONT titleFont = nullptr;
  HFONT heroFont = nullptr;
  HBRUSH background = nullptr;
  HBRUSH railBrush = nullptr;
};

std::wstring ModuleDir() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : exe.substr(0, pos);
}

std::wstring DefaultInstallDir() {
  return PathCombineSimple(GetKnownFolderLocalAppData(), L"Programs\\ClipCue");
}

std::wstring ReadInstallDirFromRegistry() {
  wchar_t value[32768]{};
  DWORD cb = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ClipCue", L"InstallDir", RRF_RT_REG_SZ, nullptr, value, &cb) == ERROR_SUCCESS) {
    return value;
  }
  return DefaultInstallDir();
}

void SetFont(HWND h, HFONT f) {
  if (h && f) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
}

HWND MakeControl(InstallerState* s, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font = nullptr, DWORD exStyle = 0) {
  HWND ctrl = CreateWindowExW(exStyle, cls, text, style, x, y, w, h, s->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SetFont(ctrl, font ? font : s->font);
  return ctrl;
}

HWND MakeLabel(InstallerState* s, const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr) {
  return MakeControl(s, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, -1, font);
}

HWND MakeGroup(InstallerState* s, const wchar_t* text, int x, int y, int w, int h) {
  return MakeControl(s, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, w, h, -1, s->font);
}

void AppendLog(HWND edit, const std::wstring& line) {
  if (!edit) {
    OutputDebugStringW((line + L"\r\n").c_str());
    wchar_t temp[MAX_PATH]{};
    DWORD len = GetTempPathW(static_cast<DWORD>(_countof(temp)), temp);
    if (len > 0 && len < _countof(temp)) {
      std::wstring path = PathCombineSimple(temp, L"ClipCue.Installer.log");
      Handle file(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
      if (file) {
        std::string utf8 = WideToUtf8(line + L"\r\n");
        DWORD written = 0;
        WriteFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
      }
    }
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
    std::wstring name = FileNameFromPath(dst);
    bool isDll = name.size() >= 4 && (name.compare(name.size() - 4, 4, L".dll") == 0 || name.compare(name.size() - 4, 4, L".DLL") == 0);
    if (isDll && FileExists(dst) && (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED)) {
      AppendLog(log, L"File is in use; keeping installed DLL until Explorer releases it: " + name);
      return true;
    }
    std::wstringstream ss;
    ss << L"Copy failed: " << src << L" -> " << dst << L" (" << GetLastErrorMessage(err) << L")";
    AppendLog(log, ss.str());
    return false;
  }
  AppendLog(log, L"Copied: " + FileNameFromPath(dst));
  return true;
}

bool CopyPattern(const std::wstring& sourceDir, const std::wstring& targetDir, const std::wstring& pattern, HWND log) {
  WIN32_FIND_DATAW fd{};
  std::wstring search = PathCombineSimple(sourceDir, pattern);
  HANDLE find = FindFirstFileW(search.c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return true;
  bool ok = true;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    std::wstring src = PathCombineSimple(sourceDir, fd.cFileName);
    std::wstring dst = PathCombineSimple(targetDir, fd.cFileName);
    ok = CopyOne(src, dst, log) && ok;
  } while (FindNextFileW(find, &fd));
  FindClose(find);
  return ok;
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
  std::wstring dll = PathCombineSimple(installDir, L"ClipCue.ShellClassic.dll");
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
  return PathCombineSimple(base, L"Programs\\ClipCue");
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
  CreateShortcut(PathCombineSimple(menu, L"ClipCue Control Panel.lnk"), PathCombineSimple(installDir, L"ClipCue.UI.exe"), L"", L"Configure ClipCue");
  CreateShortcut(PathCombineSimple(menu, L"ClipCue Path Clip Queue.lnk"), PathCombineSimple(installDir, L"ClipCue.UI.exe"), L"queue", L"Open ClipCue path clipboard queue");
  CreateShortcut(PathCombineSimple(menu, L"ClipCue Monitor.lnk"), PathCombineSimple(installDir, L"ClipCue.Monitor.exe"), L"", L"Show ClipCue monitor status");
  CreateShortcut(PathCombineSimple(menu, L"ClipCue Installer.lnk"), PathCombineSimple(installDir, L"ClipCue.Installer.exe"), L"", L"Install or repair ClipCue");
  CreateShortcut(PathCombineSimple(menu, L"Uninstall ClipCue.lnk"), PathCombineSimple(installDir, L"ClipCue.Installer.exe"), L"/uninstall", L"Uninstall ClipCue");
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
  std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipCue";
  std::wstring installer = PathCombineSimple(installDir, L"ClipCue.Installer.exe");
  SetRegString(HKEY_CURRENT_USER, key, L"DisplayName", L"ClipCue");
  SetRegString(HKEY_CURRENT_USER, key, L"DisplayVersion", CLIPCUE_VERSION);
  SetRegString(HKEY_CURRENT_USER, key, L"Publisher", L"ClipCue contributors");
  SetRegString(HKEY_CURRENT_USER, key, L"InstallLocation", installDir);
  SetRegString(HKEY_CURRENT_USER, key, L"DisplayIcon", installer);
  SetRegString(HKEY_CURRENT_USER, key, L"UninstallString", QuoteArg(installer) + L" /uninstall");
  SetRegString(HKEY_CURRENT_USER, key, L"QuietUninstallString", QuoteArg(installer) + L" /uninstall /silent");
  SetRegDWORD(HKEY_CURRENT_USER, key, L"NoModify", 1);
  SetRegDWORD(HKEY_CURRENT_USER, key, L"NoRepair", 1);
}

std::wstring ToLowerCopy(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return text;
}

std::wstring MonitorPath(const std::wstring& installDir) {
  return PathCombineSimple(installDir, L"ClipCue.Monitor.exe");
}

void DeleteRegValue(HKEY root, const std::wstring& key, const std::wstring& name) {
  HKEY h{};
  if (RegOpenKeyExW(root, key.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
    RegDeleteValueW(h, name.c_str());
    RegCloseKey(h);
  }
}

bool MonitorAutoStartLooksCorrect(const std::wstring& installDir) {
  wchar_t value[32768]{};
  DWORD cb = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kMonitorRunKey, kMonitorRunValue, RRF_RT_REG_SZ, nullptr, value, &cb) != ERROR_SUCCESS) return false;
  std::wstring lower = ToLowerCopy(value);
  return lower.find(ToLowerCopy(MonitorPath(installDir))) != std::wstring::npos && lower.find(L"--background") != std::wstring::npos;
}

bool EnableMonitorAutoStart(const std::wstring& installDir, HWND log) {
  std::wstring monitor = MonitorPath(installDir);
  if (!FileExists(monitor)) {
    AppendLog(log, L"Background monitor missing: " + monitor);
    return false;
  }

  HKEY h{};
  LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kMonitorRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &h, nullptr);
  if (rc != ERROR_SUCCESS) {
    AppendLog(log, L"Unable to open current-user Run key: " + GetLastErrorMessage(static_cast<DWORD>(rc)));
    return false;
  }
  std::wstring command = QuoteArg(monitor) + L" --background";
  rc = RegSetValueExW(h, kMonitorRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(h);
  if (rc != ERROR_SUCCESS) {
    AppendLog(log, L"Unable to enable background monitor auto-start: " + GetLastErrorMessage(static_cast<DWORD>(rc)));
    return false;
  }

  SetRegDWORD(HKEY_CURRENT_USER, L"Software\\ClipCue", L"MonitorEnabled", 1);
  AppendLog(log, L"Background monitor auto-start enabled.");
  return true;
}

void DisableMonitorAutoStart(HWND log) {
  DeleteRegValue(HKEY_CURRENT_USER, kMonitorRunKey, kMonitorRunValue);
  AppendLog(log, L"Background monitor auto-start removed.");
}

void StopMonitor(HWND log) {
  HWND monitor = FindWindowW(kMonitorWindowClass, nullptr);
  if (monitor) {
    DWORD pid = 0;
    GetWindowThreadProcessId(monitor, &pid);
    Handle process(pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr);
    PostMessageW(monitor, kMonitorQuitMessage, 0, 0);
    AppendLog(log, L"Background monitor asked to exit.");
    if (process && WaitForSingleObject(process.get(), 5000) == WAIT_TIMEOUT) {
      AppendLog(log, L"Background monitor did not exit before the copy step.");
    }
  }
}

bool StartMonitor(const std::wstring& installDir, HWND log) {
  std::wstring monitor = MonitorPath(installDir);
  if (!FileExists(monitor)) {
    AppendLog(log, L"Background monitor missing: " + monitor);
    return false;
  }
  HINSTANCE launched = ShellExecuteW(nullptr, L"open", monitor.c_str(), L"--background", installDir.c_str(), SW_SHOWNORMAL);
  INT_PTR launchCode = reinterpret_cast<INT_PTR>(launched);
  if (launchCode <= 32) {
    AppendLog(log, L"Unable to start background monitor: " + FormatHResult(HRESULT_FROM_WIN32(static_cast<DWORD>(launchCode))));
    return false;
  }
  AppendLog(log, L"Background monitor started. A tree tray icon should appear in the notification area.");
  return true;
}

bool ConfigureMonitor(const std::wstring& installDir, bool enabled, HWND log) {
  if (!enabled) {
    StopMonitor(log);
    DisableMonitorAutoStart(log);
    SetRegDWORD(HKEY_CURRENT_USER, L"Software\\ClipCue", L"MonitorEnabled", 0);
    return true;
  }
  if (!EnableMonitorAutoStart(installDir, log)) return false;
  if (!MonitorAutoStartLooksCorrect(installDir)) {
    AppendLog(log, L"Background monitor auto-start verification failed.");
    return false;
  }
  return StartMonitor(installDir, log);
}

bool InstallTo(const std::wstring& sourceDir, const std::wstring& installDir, bool registerShell, bool shortcuts, bool startMonitor, HWND log) {
  AppendLog(log, L"Installing ClipCue to: " + installDir);
  StopMonitor(log);
  EnsureDirectory(installDir);
  bool copied = true;
  copied = CopyPattern(sourceDir, installDir, L"ClipCue.*.exe", log) && copied;
  copied = CopyPattern(sourceDir, installDir, L"ClipCue.*.dll", log) && copied;
  const wchar_t* filesToCopy[] = {L"LICENSE", L"NOTICE", L"README.md"};
  for (const wchar_t* file : filesToCopy) {
    std::wstring src = PathCombineSimple(sourceDir, file);
    if (FileExists(src)) copied = CopyOne(src, PathCombineSimple(installDir, file), log) && copied;
  }
  if (!copied) {
    AppendLog(log, L"Install failed because one or more required files could not be copied.");
    return false;
  }
  CopyDirectoryRecursive(PathCombineSimple(sourceDir, L"scripts"), PathCombineSimple(installDir, L"scripts"), log);
  CopyDirectoryRecursive(PathCombineSimple(sourceDir, L"docs"), PathCombineSimple(installDir, L"docs"), log);

  SetRegString(HKEY_CURRENT_USER, L"Software\\ClipCue", L"InstallDir", installDir);
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
  if (!ConfigureMonitor(installDir, startMonitor, log)) return false;
  AppendLog(log, L"Install complete. Restart Explorer or sign out/in if the context menu does not appear immediately.");
  return true;
}

bool UninstallFrom(const std::wstring& installDir, HWND log) {
  AppendLog(log, L"Uninstalling ClipCue from: " + installDir);
  StopMonitor(log);
  DisableMonitorAutoStart(log);
  HRESULT hr = CallShellRegistration(installDir, false);
  if (FAILED(hr)) AppendLog(log, L"Shell unregister returned: " + FormatHResult(hr));
  else AppendLog(log, L"Classic Explorer context menu unregistered.");

  SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipCue");
  SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\ClipCue");
  DeleteDirectoryRecursive(StartMenuPath(), log);

  std::wstring runningDir = ModuleDir();
  if (IsSamePathCaseInsensitive(runningDir, installDir)) {
    AppendLog(log, L"Installer is running from the install directory; remaining files will be removed after reboot if locked.");
  }
  DeleteDirectoryRecursive(installDir, log);
  AppendLog(log, L"Uninstall complete. User data under %LOCALAPPDATA%\\ClipCue is left intact.");
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
  dialog->SetTitle(L"Choose ClipCue install folder");
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

void SetText(HWND hwnd, const std::wstring& text) {
  if (hwnd) SetWindowTextW(hwnd, text.c_str());
}

std::wstring BuildPlan(InstallerState* s) {
  std::wstring installDir = s && s->installDir ? GetEditText(s->installDir) : ReadInstallDirFromRegistry();
  std::wstringstream ss;
  ss << L"Install destination\r\n  " << installDir << L"\r\n\r\n";
  ss << L"Actions selected\r\n";
  ss << L"  " << (IsChecked(s->registerShell) ? L"[x]" : L"[ ]") << L" Register Explorer classic context menu\r\n";
  ss << L"  " << (IsChecked(s->shortcuts) ? L"[x]" : L"[ ]") << L" Create Start Menu shortcuts\r\n";
  ss << L"  " << (IsChecked(s->startMonitor) ? L"[x]" : L"[ ]") << L" Start background monitor at sign-in\r\n";
  ss << L"  " << (IsChecked(s->launchUi) ? L"[x]" : L"[ ]") << L" Launch Control Panel after install\r\n\r\n";
  ss << L"This is a per-user install under HKCU and does not require administrator rights.";
  return ss.str();
}

void RefreshPlan(InstallerState* s) {
  if (s && s->plan) SetText(s->plan, BuildPlan(s));
}

bool ValidateInstallDir(HWND owner, const std::wstring& target) {
  if (target.empty()) {
    MessageBoxW(owner, L"Choose an install folder first.", L"ClipCue", MB_ICONINFORMATION);
    return false;
  }
  if (target.size() < 3) {
    MessageBoxW(owner, L"Choose a specific folder, not a drive root.", L"ClipCue", MB_ICONINFORMATION);
    return false;
  }
  return true;
}

void CreateFonts(InstallerState* s) {
  s->background = CreateSolidBrush(kPageBg);
  s->railBrush = CreateSolidBrush(kRailBg);
  s->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->smallFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  s->heroFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void OnCreate(InstallerState* s) {
  CreateFonts(s);
  MakeControl(s, L"STATIC", L"ClipCue", WS_CHILD | WS_VISIBLE, 24, 24, 190, 36, -1, s->heroFont);
  MakeControl(s, L"STATIC", L"Modern installer", WS_CHILD | WS_VISIBLE, 26, 62, 180, 24, -1, s->font);
  MakeControl(s, L"STATIC", L"1  Choose path", WS_CHILD | WS_VISIBLE, 28, 128, 160, 24, -1, s->font);
  MakeControl(s, L"STATIC", L"2  Pick options", WS_CHILD | WS_VISIBLE, 28, 164, 160, 24, -1, s->font);
  MakeControl(s, L"STATIC", L"3  Install / repair", WS_CHILD | WS_VISIBLE, 28, 200, 170, 24, -1, s->font);
  MakeControl(s, L"STATIC", L"Per-user. No admin rights required.", WS_CHILD | WS_VISIBLE, 26, 530, 190, 40, -1, s->smallFont);

  MakeControl(s, L"STATIC", L"Install ClipCue", WS_CHILD | WS_VISIBLE, 242, 26, 280, 32, -1, s->heroFont);
  MakeControl(s, L"STATIC", L"Set the destination, integrations, and post-install flow in one screen.", WS_CHILD | WS_VISIBLE,
              244, 62, 560, 24, -1, s->font);

  MakeGroup(s, L"Destination", 242, 104, 610, 108);
  MakeLabel(s, L"Install folder", 264, 136, 120, 24, s->font);
  s->installDir = MakeControl(s, L"EDIT", ReadInstallDirFromRegistry().c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              386, 132, 328, 30, IDC_INSTALL_DIR, s->font, WS_EX_CLIENTEDGE);
  MakeControl(s, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 724, 130, 100, 34, IDC_BROWSE);
  MakeControl(s, L"BUTTON", L"Default", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 386, 170, 88, 30, IDC_DEFAULT_DIR);
  MakeControl(s, L"BUTTON", L"Open source folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 486, 170, 148, 30, IDC_SOURCE_FOLDER);
  MakeControl(s, L"BUTTON", L"Open install folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 646, 170, 148, 30, IDC_OPEN_FOLDER);

  MakeGroup(s, L"Options", 242, 232, 294, 178);
  s->registerShell = MakeControl(s, L"BUTTON", L"Explorer classic context menu", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 264, 264, 240, 24, IDC_REGISTER_SHELL);
  s->shortcuts = MakeControl(s, L"BUTTON", L"Start Menu shortcuts", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             264, 294, 210, 24, IDC_SHORTCUTS);
  s->startMonitor = MakeControl(s, L"BUTTON", L"Background monitor at sign-in", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                264, 324, 250, 24, IDC_START_MONITOR);
  s->launchUi = MakeControl(s, L"BUTTON", L"Launch Control Panel after install", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                            264, 354, 250, 24, IDC_LAUNCH_UI);
  SendMessageW(s->registerShell, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(s->shortcuts, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(s->startMonitor, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(s->launchUi, BM_SETCHECK, BST_CHECKED, 0);

  MakeGroup(s, L"Plan", 558, 232, 294, 178);
  s->plan = MakeControl(s, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                        578, 262, 252, 128, IDC_PLAN, s->smallFont, WS_EX_CLIENTEDGE);

  MakeControl(s, L"BUTTON", L"Install / Repair", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 242, 432, 150, 38, IDC_INSTALL);
  MakeControl(s, L"BUTTON", L"Uninstall", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 404, 432, 108, 38, IDC_UNINSTALL);
  MakeControl(s, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 744, 432, 108, 38, IDC_CLOSE);

  MakeGroup(s, L"Activity", 242, 492, 610, 118);
  s->status = MakeControl(s, L"EDIT", L"Ready. This installer performs a per-user install and keeps user data under %LOCALAPPDATA%\\ClipCue during uninstall.\r\n",
                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                          264, 520, 566, 72, IDC_STATUS, s->smallFont, WS_EX_CLIENTEDGE);
  RefreshPlan(s);
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
      int id = LOWORD(wp);
      int notify = HIWORD(wp);
      if ((id == IDC_INSTALL_DIR && notify == EN_CHANGE) || id == IDC_REGISTER_SHELL || id == IDC_SHORTCUTS || id == IDC_START_MONITOR || id == IDC_LAUNCH_UI) {
        RefreshPlan(s);
      }
      switch (id) {
        case IDC_BROWSE: {
          std::wstring folder = PickFolder(hwnd, GetEditText(s->installDir));
          if (!folder.empty()) SetText(s->installDir, folder);
          RefreshPlan(s);
          return 0;
        }
        case IDC_DEFAULT_DIR:
          SetText(s->installDir, DefaultInstallDir());
          RefreshPlan(s);
          return 0;
        case IDC_SOURCE_FOLDER:
          ShellExecuteW(hwnd, L"open", ModuleDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        case IDC_INSTALL: {
          SetCursor(LoadCursorW(nullptr, IDC_WAIT));
          std::wstring target = GetEditText(s->installDir);
          bool ok = ValidateInstallDir(hwnd, target) && InstallTo(ModuleDir(), target, IsChecked(s->registerShell), IsChecked(s->shortcuts), IsChecked(s->startMonitor), s->status);
          SetCursor(LoadCursorW(nullptr, IDC_ARROW));
          if (ok && IsChecked(s->launchUi)) {
            ShellExecuteW(hwnd, L"open", PathCombineSimple(target, L"ClipCue.UI.exe").c_str(), nullptr, target.c_str(), SW_SHOWNORMAL);
          }
          RefreshPlan(s);
          return 0;
        }
        case IDC_UNINSTALL: {
          if (MessageBoxW(hwnd, L"Uninstall ClipCue? User data and history are left intact.", L"ClipCue", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            UninstallFrom(GetEditText(s->installDir), s->status);
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
          }
          RefreshPlan(s);
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
    case WM_CTLCOLORSTATIC:
      if (s && s->background) {
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND control = reinterpret_cast<HWND>(lp);
        RECT controlRect{};
        GetWindowRect(control, &controlRect);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&controlRect), 2);
        SetBkMode(dc, TRANSPARENT);
        if (controlRect.left < 216) {
          SetTextColor(dc, RGB(255, 255, 255));
          return reinterpret_cast<LRESULT>(s->railBrush ? s->railBrush : s->background);
        }
        SetTextColor(dc, kText);
        return reinterpret_cast<LRESULT>(s->background);
      }
      break;
    case WM_ERASEBKGND:
      if (s && s->background) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, s->background);
        RECT rail = rc;
        rail.right = 216;
        FillRect(reinterpret_cast<HDC>(wp), &rail, s->railBrush ? s->railBrush : s->background);
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
        if (s->railBrush) DeleteObject(s->railBrush);
      }
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
  if (install) return InstallTo(ModuleDir(), target, true, true, true, log) ? 0 : 1;
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
  wc.lpszClassName = L"ClipCueInstallerWindow";
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"ClipCue Installer", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 890, 660, nullptr, nullptr, instance, &state);
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
