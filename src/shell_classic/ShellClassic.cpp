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

// {7BC73D01-CE9F-44C6-A50D-7724C0B0C6B1}
static const CLSID CLSID_PathCueShellClassic =
{0x7bc73d01, 0xce9f, 0x44c6, {0xa5, 0x0d, 0x77, 0x24, 0xc0, 0xb0, 0xc6, 0xb1}};

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
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\PathCue", L"InstallDir", RRF_RT_REG_SZ, nullptr, installDir, &cb) == ERROR_SUCCESS) {
    std::wstring p = PathCombineSimple(installDir, L"PathCue.Agent.exe");
    if (PathFileExistsW(p.c_str())) return p;
  }
  std::wstring dir = ModuleDir();
  std::wstring p = PathCombineSimple(dir, L"PathCue.Agent.exe");
  if (PathFileExistsW(p.c_str())) return p;
  return p;
}

std::wstring TempFile(const std::wstring& ext) {
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  wchar_t tempFile[MAX_PATH]{};
  GetTempFileNameW(tempDir, L"pcq", 0, tempFile);
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

std::vector<CacheEntry> LoadCache(const std::wstring& sourceParent) {
  std::vector<CacheEntry> entries;
  std::wstring cache = PathCombineSimple(LocalAppData(), L"PathCue\\Cache\\menu_cache.tsv");
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
    if (entries.size() >= 12) break;
  }
  return entries;
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

class PathCueShellExt : public IShellExtInit, public IContextMenu {
 public:
  PathCueShellExt() { InterlockedIncrement(&g_dllRef); }
  ~PathCueShellExt() { InterlockedDecrement(&g_dllRef); }

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

  HRESULT STDMETHODCALLTYPE Initialize(PCIDLIST_ABSOLUTE, IDataObject* data, HKEY) override {
    selected_.clear();
    if (!data) return E_INVALIDARG;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg{};
    if (FAILED(data->GetData(&fmt, &stg))) return E_INVALIDARG;
    HDROP drop = reinterpret_cast<HDROP>(GlobalLock(stg.hGlobal));
    if (drop) {
      UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), len + 1);
        path.resize(len);
        selected_.push_back(path);
      }
      GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return selected_.empty() ? E_INVALIDARG : S_OK;
  }

  HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT indexMenu, UINT idCmdFirst, UINT, UINT flags) override {
    if (flags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    commands_.clear();
    HMENU sub = CreatePopupMenu();
    UINT id = idCmdFirst;

    std::wstring sourceParent = ParentPath(selected_.front());
    auto cache = LoadCache(sourceParent);
    int added = 0;
    for (const auto& e : cache) {
      if (added >= 8) break;
      Command cmd;
      cmd.op = e.op;
      cmd.dest = e.dest;
      cmd.pick = false;
      commands_.push_back(cmd);
      std::wstring label = (e.op == L"move" ? L"Move to " : e.op == L"copy" ? L"Copy to " : L"To ") + e.label;
      InsertMenuW(sub, added, MF_BYPOSITION | MF_STRING, id++, label.c_str());
      ++added;
    }
    if (added) InsertMenuW(sub, added++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    Command movePick; movePick.op = L"move"; movePick.pick = true; commands_.push_back(movePick);
    InsertMenuW(sub, added++, MF_BYPOSITION | MF_STRING, id++, L"Move to..." );
    Command copyPick; copyPick.op = L"copy"; copyPick.pick = true; commands_.push_back(copyPick);
    InsertMenuW(sub, added++, MF_BYPOSITION | MF_STRING, id++, L"Copy to..." );

    InsertMenuW(menu, indexMenu, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"PathCue");
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<USHORT>(commands_.size()));
  }

  HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
    if (HIWORD(info->lpVerb)) return E_FAIL;
    UINT offset = LOWORD(info->lpVerb);
    if (offset >= commands_.size()) return E_INVALIDARG;
    const Command& c = commands_[offset];
    std::wstring sourcesFile = TempFile(L".sources");
    std::wstring text;
    for (const auto& s : selected_) text += s + L"\n";
    WriteUtf8(sourcesFile, text);

    std::wstring params;
    if (c.pick) {
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
    if (flags == GCS_HELPTEXTA && name) StringCchCopyA(name, cchMax, "PathCue quick copy/move target menu");
    return S_OK;
  }

 private:
  struct Command {
    std::wstring op;
    std::wstring dest;
    bool pick = false;
  };
  volatile LONG ref_ = 1;
  std::vector<std::wstring> selected_;
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
    auto* ext = new (std::nothrow) PathCueShellExt();
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
  if (clsid != CLSID_PathCueShellClassic) return CLASS_E_CLASSNOTAVAILABLE;
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
  StringFromCLSID(CLSID_PathCueShellClassic, &clsidText);
  std::wstring clsid(clsidText);
  CoTaskMemFree(clsidText);

  HRESULT hr = SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid, L"", L"PathCue Classic Shell Extension");
  if (FAILED(hr)) return hr;
  hr = SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"", module);
  if (FAILED(hr)) return hr;
  hr = SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
  if (FAILED(hr)) return hr;
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\PathCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\PathCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Folder\\shellex\\ContextMenuHandlers\\PathCue", L"", clsid);
  SetRegString(HKEY_CURRENT_USER, L"Software\\PathCue", L"InstallDir", ModuleDir());
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
  LPOLESTR clsidText = nullptr;
  StringFromCLSID(CLSID_PathCueShellClassic, &clsidText);
  std::wstring clsid(clsidText);
  CoTaskMemFree(clsidText);
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid);
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\PathCue");
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\PathCue");
  DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\Folder\\shellex\\ContextMenuHandlers\\PathCue");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}
