#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <strsafe.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <new>

// {B2E319BE-8572-4E17-BF00-F3AF942707A7}
static const CLSID CLSID_ClipCueShellClassic =
{0xb2e319be, 0x8572, 0x4e17, {0xbf, 0x00, 0xf3, 0xaf, 0x94, 0x27, 0x07, 0xa7}};

HINSTANCE g_instance = nullptr;
volatile long g_dllRef = 0;

namespace {

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(len), L'\0');
  if (len) MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len);
  return out;
}

std::string WideToUtf8(const std::wstring& input) {
  if (input.empty()) return "";
  int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(len), '\0');
  if (len) WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len, nullptr, nullptr);
  return out;
}

std::wstring PathCombineSimple(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::wstring ModuleDir() {
  wchar_t path[32768]{};
  GetModuleFileNameW(g_instance, path, static_cast<DWORD>(_countof(path)));
  PathRemoveFileSpecW(path);
  return path;
}

std::wstring LocalAppData() {
  wchar_t buffer[32768]{};
  DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(_countof(buffer)));
  if (len > 0 && len < _countof(buffer)) return buffer;
  return L".";
}

std::wstring QuoteArg(const std::wstring& value) {
  std::wstring out = L"\"";
  for (wchar_t ch : value) {
    if (ch == L'\"') out += L"\\\"";
    else out += ch;
  }
  out += L"\"";
  return out;
}

std::wstring ParentPath(std::wstring path) {
  while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
  size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L"";
  if (pos == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
  return path.substr(0, pos);
}

bool DirectoryExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring NormalizePath(const std::wstring& path) {
  wchar_t full[32768]{};
  DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(_countof(full)), full, nullptr);
  if (len > 0 && len < _countof(full)) return full;
  return path;
}

std::wstring HashPathForCache(const std::wstring& path) {
  std::wstring lower = NormalizePath(path);
  std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
  unsigned long long h = 1469598103934665603ull;
  for (wchar_t c : lower) {
    h ^= static_cast<unsigned long long>(c);
    h *= 1099511628211ull;
  }
  std::wstringstream ss;
  ss << std::hex << h;
  return ss.str();
}

std::wstring AgentPath() {
  wchar_t installDir[32768]{};
  DWORD cb = sizeof(installDir);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ClipCue", L"InstallDir", RRF_RT_REG_SZ, nullptr, installDir, &cb) == ERROR_SUCCESS) {
    std::wstring p = PathCombineSimple(installDir, L"ClipCue.Agent.exe");
    if (PathFileExistsW(p.c_str())) return p;
  }
  std::wstring dir = ModuleDir();
  std::wstring p = PathCombineSimple(dir, L"ClipCue.Agent.exe");
  if (PathFileExistsW(p.c_str())) return p;
  return p;
}

std::wstring UiPath() {
  wchar_t installDir[32768]{};
  DWORD cb = sizeof(installDir);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ClipCue", L"InstallDir", RRF_RT_REG_SZ, nullptr, installDir, &cb) == ERROR_SUCCESS) {
    std::wstring p = PathCombineSimple(installDir, L"ClipCue.UI.exe");
    if (PathFileExistsW(p.c_str())) return p;
  }
  std::wstring dir = ModuleDir();
  std::wstring p = PathCombineSimple(dir, L"ClipCue.UI.exe");
  if (PathFileExistsW(p.c_str())) return p;
  return p;
}

std::wstring TempFile(const std::wstring& ext) {
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  wchar_t tempFile[MAX_PATH]{};
  GetTempFileNameW(tempDir, L"ccq", 0, tempFile);
  std::wstring out(tempFile);
  if (!ext.empty()) {
    std::wstring renamed = out + ext;
    MoveFileExW(out.c_str(), renamed.c_str(), MOVEFILE_REPLACE_EXISTING);
    out = renamed;
  }
  return out;
}

bool WriteUtf8(const std::wstring& file, const std::wstring& text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  std::string utf8 = WideToUtf8(text);
  out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  return true;
}

struct CacheEntry {
  std::wstring op;
  std::wstring label;
  std::wstring dest;
};

struct QueueSummary {
  int copyFiles = 0;
  int moveFiles = 0;
  int historyEntries = 0;
};

std::vector<std::wstring> SplitTabs(const std::wstring& s) {
  std::vector<std::wstring> out;
  std::wstring cur;
  for (wchar_t ch : s) {
    if (ch == L'\t') { out.push_back(cur); cur.clear(); }
    else cur.push_back(ch);
  }
  out.push_back(cur);
  return out;
}

std::wstring PidlToPath(PCIDLIST_ABSOLUTE pidl) {
  if (!pidl) return L"";
  wchar_t path[32768]{};
  if (!SHGetPathFromIDListW(pidl, path)) return L"";
  return NormalizePath(path);
}

std::vector<CacheEntry> LoadCache(const std::wstring& sourceParent) {
  std::vector<CacheEntry> entries;
  std::wstring cache = PathCombineSimple(LocalAppData(), L"ClipCue\\Cache\\menu_cache.tsv");
  std::ifstream in(cache, std::ios::binary);
  if (!in) return entries;
  std::string line8;
  std::wstring hash = HashPathForCache(sourceParent);
  while (std::getline(in, line8)) {
    if (line8.empty() || line8[0] == '#') continue;
    std::wstring line = Utf8ToWide(line8);
    auto p = SplitTabs(line);
    if (p.size() < 4) continue;
    if (p[0] != hash && p[0] != L"*") continue;
    CacheEntry e;
    e.op = p[1];
    e.label = p[2];
    e.dest = p[3];
    entries.push_back(e);
    if (entries.size() >= 32) break;
  }
  return entries;
}

QueueSummary LoadQueueSummary() {
  QueueSummary summary;
  std::wstring cache = PathCombineSimple(LocalAppData(), L"ClipCue\\Cache\\menu_cache.tsv");
  std::ifstream in(cache, std::ios::binary);
  if (!in) return summary;
  std::string line8;
  while (std::getline(in, line8)) {
    if (line8.empty() || line8[0] == '#') continue;
    std::wstring line = Utf8ToWide(line8);
    auto p = SplitTabs(line);
    if (p.size() >= 3 && p[0] == L"QUEUE") {
      int count = _wtoi(p[2].c_str());
      if (p[1] == L"copy") summary.copyFiles += count;
      else if (p[1] == L"move") summary.moveFiles += count;
    } else if (p.size() >= 2 && p[0] == L"QUEUE_HISTORY") {
      summary.historyEntries += _wtoi(p[1].c_str());
    }
  }
  return summary;
}

HRESULT SetRegString(HKEY root, const std::wstring& key, const std::wstring& name, const std::wstring& value) {
  HKEY h = nullptr;
  LONG rc = RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h, nullptr);
  if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);
  rc = RegSetValueExW(h, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                      reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(h);
  return HRESULT_FROM_WIN32(rc);
}

void DeleteTree(HKEY root, const std::wstring& key) {
  SHDeleteKeyW(root, key.c_str());
}

}  // namespace

class ClipCueShellExt : public IShellExtInit, public IContextMenu {
 public:
  ClipCueShellExt() { InterlockedIncrement(&g_dllRef); }
  ~ClipCueShellExt() { InterlockedDecrement(&g_dllRef); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IShellExtInit) *ppv = static_cast<IShellExtInit*>(this);
    else if (riid == IID_IContextMenu) *ppv = static_cast<IContextMenu*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG v = InterlockedDecrement(&ref_);
    if (!v) delete this;
    return v;
  }

  HRESULT STDMETHODCALLTYPE Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* data, HKEY) override {
    selected_.clear();
    contextFolder_ = PidlToPath(pidlFolder);
    if (data) {
      FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
      STGMEDIUM stg{};
      if (SUCCEEDED(data->GetData(&fmt, &stg))) {
        HDROP drop = reinterpret_cast<HDROP>(GlobalLock(stg.hGlobal));
        if (drop) {
          UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
          for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(len + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), len + 1);
            path.resize(len);
            selected_.push_back(NormalizePath(path));
          }
          GlobalUnlock(stg.hGlobal);
        }
        ReleaseStgMedium(&stg);
      }
    }
    return (!selected_.empty() || !contextFolder_.empty()) ? S_OK : E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT indexMenu, UINT idCmdFirst, UINT, UINT flags) override {
    if (flags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    commands_.clear();
    UINT id = idCmdFirst;
    UINT inserted = 0;

    std::wstring targetFolder = QueueTargetFolder();
    if (!targetFolder.empty()) {
      HMENU queueSub = CreatePopupMenu();
      AddQueueSubmenu(queueSub, targetFolder, id);
      InsertMenuW(menu, indexMenu + inserted++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(queueSub), L"ClipCue Clipboard Queue...");
    }

    if (!selected_.empty()) {
      HMENU moveSub = CreatePopupMenu();
      HMENU copySub = CreatePopupMenu();
      std::wstring sourceParent = ParentPath(selected_.front());
      auto cache = LoadCache(sourceParent);
      AddOperationSubmenu(moveSub, L"move", cache, id);
      AddOperationSubmenu(copySub, L"copy", cache, id);

      InsertMenuW(menu, indexMenu + inserted++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(moveSub), L"ClipCue Move to...");
      InsertMenuW(menu, indexMenu + inserted++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(copySub), L"ClipCue Copy to...");
    }
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<USHORT>(commands_.size()));
  }

  HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
    if (HIWORD(info->lpVerb)) return E_FAIL;
    UINT offset = LOWORD(info->lpVerb);
    if (offset >= commands_.size()) return E_INVALIDARG;
    const Command& c = commands_[offset];

    std::wstring params;
    if (c.history) {
      SHELLEXECUTEINFOW sei{};
      sei.cbSize = sizeof(sei);
      std::wstring ui = UiPath();
      sei.lpFile = ui.c_str();
      sei.lpParameters = L"queue";
      sei.nShow = SW_SHOWNORMAL;
      return ShellExecuteExW(&sei) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    } else if (c.queue) {
      params = L"--paste-queue --target " + QuoteArg(c.dest);
      if (!c.op.empty()) params += L" --op " + c.op;
    } else if (c.pick) {
      std::wstring sourcesFile = TempFile(L".sources");
      std::wstring text;
      for (const auto& s : selected_) text += s + L"\n";
      WriteUtf8(sourcesFile, text);
      params = L"--pick-dest --op " + c.op + L" --sources-file " + QuoteArg(sourcesFile);
    } else {
      std::wstring jobFile = TempFile(L".job");
      std::wstringstream job;
      job << L"op=" << c.op << L"\n";
      job << L"target=" << c.dest << L"\n";
      job << L"conflict=ask\n";
      job << L"overwrite=ask\n";
      job << L"allow_elevation=1\n";
      for (const auto& s : selected_) job << L"source=" << s << L"\n";
      WriteUtf8(jobFile, job.str());
      params = L"--shell-job " + QuoteArg(jobFile);
    }
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    std::wstring agent = AgentPath();
    sei.lpFile = agent.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
  }

  HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR, UINT flags, UINT*, LPSTR name, UINT cchMax) override {
    if (flags == GCS_HELPTEXTA && name) StringCchCopyA(name, cchMax, "ClipCue quick copy/move target menu");
    return S_OK;
  }

 private:
  struct Command {
    std::wstring op;
    std::wstring dest;
    bool pick = false;
    bool queue = false;
    bool history = false;
  };

  std::wstring QueueTargetFolder() const {
    if (selected_.size() == 1 && DirectoryExists(selected_[0])) return selected_[0];
    if (!contextFolder_.empty() && DirectoryExists(contextFolder_)) return contextFolder_;
    return L"";
  }

  bool HasSeenDestination(const std::vector<std::wstring>& seen, const std::wstring& dest) const {
    for (const auto& item : seen) {
      if (StrCmpIW(item.c_str(), dest.c_str()) == 0) return true;
    }
    return false;
  }

  void AddCommandItem(HMENU menu, int* position, UINT* id, const Command& command, const std::wstring& label) {
    commands_.push_back(command);
    InsertMenuW(menu, (*position)++, MF_BYPOSITION | MF_STRING, (*id)++, label.c_str());
  }

  void AddOperationSubmenu(HMENU menu, const std::wstring& op, const std::vector<CacheEntry>& cache, UINT& id) {
    int position = 0;
    int quickCount = 0;
    std::vector<std::wstring> seen;
    for (const auto& entry : cache) {
      if (entry.op != op) continue;
      if (entry.dest.empty() || HasSeenDestination(seen, entry.dest)) continue;
      if (quickCount >= 8) break;
      Command quick;
      quick.op = op;
      quick.dest = entry.dest;
      quick.pick = false;
      AddCommandItem(menu, &position, &id, quick, entry.label.empty() ? entry.dest : entry.label);
      seen.push_back(entry.dest);
      ++quickCount;
    }
    if (quickCount == 0) {
      InsertMenuW(menu, position++, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, L"No quick paths yet");
    } else {
      InsertMenuW(menu, position++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    }

    Command pick;
    pick.op = op;
    pick.pick = true;
    AddCommandItem(menu, &position, &id, pick, L"Choose target...");
  }

  void AddQueueCommand(HMENU menu, int* position, UINT* id, const std::wstring& target, const std::wstring& op, const std::wstring& label) {
    Command command;
    command.queue = true;
    command.op = op;
    command.dest = target;
    AddCommandItem(menu, position, id, command, label);
  }

  void AddHistoryCommand(HMENU menu, int* position, UINT* id, const std::wstring& label) {
    Command command;
    command.history = true;
    AddCommandItem(menu, position, id, command, label);
  }

  void AddQueueSubmenu(HMENU menu, const std::wstring& target, UINT& id) {
    int position = 0;
    QueueSummary summary = LoadQueueSummary();
    int total = summary.copyFiles + summary.moveFiles;
    if (total <= 0) {
      InsertMenuW(menu, position++, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, L"No active file queue");
    } else {
      AddQueueCommand(menu, &position, &id, target, L"both", L"Preview and apply all here...");
      InsertMenuW(menu, position++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
      if (summary.copyFiles > 0) {
        std::wstringstream label;
        label << L"Copy " << summary.copyFiles << L" queued file(s) here...";
        AddQueueCommand(menu, &position, &id, target, L"copy", label.str());
      }
      if (summary.moveFiles > 0) {
        std::wstringstream label;
        label << L"Move " << summary.moveFiles << L" queued file(s) here...";
        AddQueueCommand(menu, &position, &id, target, L"move", label.str());
      }
    }
    InsertMenuW(menu, position++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    std::wstringstream label;
    label << L"Open Path Clip Queue";
    if (summary.historyEntries > 0) label << L" (" << summary.historyEntries << L")";
    label << L"...";
    AddHistoryCommand(menu, &position, &id, label.str());
  }

  volatile LONG ref_ = 1;
  std::vector<std::wstring> selected_;
  std::wstring contextFolder_;
  std::vector<Command> commands_;
};

class ClassFactory : public IClassFactory {
 public:
  ClassFactory() { InterlockedIncrement(&g_dllRef); }
  ~ClassFactory() { InterlockedDecrement(&g_dllRef); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) *ppv = static_cast<IClassFactory*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG v = InterlockedDecrement(&ref_);
    if (!v) delete this;
    return v;
  }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* ext = new (std::nothrow) ClipCueShellExt();
    if (!ext) return E_OUTOFMEMORY;
    HRESULT hr = ext->QueryInterface(riid, ppv);
    ext->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
    if (lock) InterlockedIncrement(&g_dllRef); else InterlockedDecrement(&g_dllRef);
    return S_OK;
  }
 private:
  volatile LONG ref_ = 1;
};

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_instance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
  return g_dllRef == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (clsid != CLSID_ClipCueShellClassic) return CLASS_E_CLASSNOTAVAILABLE;
  auto* factory = new (std::nothrow) ClassFactory();
  if (!factory) return E_OUTOFMEMORY;
  HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  return hr;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
  wchar_t module[32768]{};
  GetModuleFileNameW(g_instance, module, static_cast<DWORD>(_countof(module)));
  LPOLESTR clsidText = nullptr;
  StringFromCLSID(CLSID_ClipCueShellClassic, &clsidText);
  std::wstring clsid(clsidText);
  CoTaskMemFree(clsidText);

  HRESULT hr = SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid, L"", L"ClipCue Classic Shell Extension");
  if (FAILED(hr)) return hr;
  hr = SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"", module);
  if (FAILED(hr)) return hr;
  hr = SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
  if (FAILED(hr)) return hr;
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\ClipCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\ClipCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\ClipCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Folder\\shellex\\ContextMenuHandlers\\ClipCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\ClipCue", L"InstallDir", ModuleDir());
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
  LPOLESTR clsidText = nullptr;
  StringFromCLSID(CLSID_ClipCueShellClassic, &clsidText);
  std::wstring clsid(clsidText);
  CoTaskMemFree(clsidText);
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid);
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\ClipCue");
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\ClipCue");
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\ClipCue");
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\Folder\\shellex\\ContextMenuHandlers\\ClipCue");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}
