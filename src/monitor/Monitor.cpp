#include "pathcue/CryptoStore.h"
#include "pathcue/History.h"
#include "pathcue/PathUtils.h"
#include "pathcue/WinUtils.h"
#include "pathcue/Version.h"

#include <windows.h>
#include <oleidl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif

#ifndef CFSTR_PREFERREDDROPEFFECT
#define CFSTR_PREFERREDDROPEFFECT L"Preferred DropEffect"
#endif

#ifndef SHCNRF_InterruptLevel
#define SHCNRF_InterruptLevel 0x0001
#endif

#ifndef SHCNRF_ShellLevel
#define SHCNRF_ShellLevel 0x0002
#endif

#ifndef SHCNRF_RecursiveInterrupt
#define SHCNRF_RecursiveInterrupt 0x1000
#endif

#ifndef SHCNRF_NewDelivery
#define SHCNRF_NewDelivery 0x8000
#endif

using namespace pathcue;

namespace {

constexpr wchar_t kWindowClass[] = L"PathCueMonitorWindow";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"PathCue Monitor";
constexpr UINT kTrayId = 1;
constexpr UINT kTimerRefresh = 1;
constexpr UINT kTimerClipboardRead = 2;
constexpr UINT kTimerCacheFlush = 3;
constexpr UINT kMsgTray = WM_APP + 1;
constexpr UINT kMsgShowStatus = WM_APP + 2;
constexpr UINT kMsgQuit = WM_APP + 3;
constexpr UINT kMsgShellChange = WM_APP + 4;
constexpr UINT kMsgShellChangeOld = WM_APP + 5;

constexpr DWORD kClipboardReadDelayMs = 180;
constexpr DWORD kClipboardRetryDelayMs = 250;
constexpr DWORD kClipboardTtlMs = 30 * 60 * 1000;
constexpr DWORD kRecentRecordTtlMs = 20 * 1000;
constexpr DWORD kCacheFlushDelayMs = 2500;
constexpr int kMaxClipboardRetries = 8;

constexpr int IDM_STATUS = 3001;
constexpr int IDM_OPEN_UI = 3002;
constexpr int IDM_OPEN_DATA = 3003;
constexpr int IDM_BUILD_CACHE = 3004;
constexpr int IDM_CLEANUP = 3005;
constexpr int IDM_AUTOSTART = 3006;
constexpr int IDM_EXIT = 3007;

enum class ClipboardListenMode { None, FormatListener, ViewerChain };

struct ClipboardSource {
  std::wstring path;
  std::wstring parent;
  std::wstring name;
};

struct FileClipboardSnapshot {
  bool active = false;
  OperationKind op = OperationKind::Unknown;
  std::vector<ClipboardSource> sources;
  DWORD sequence = 0;
  DWORD capturedTick = 0;
  FILETIME capturedTime{};
  int retryCount = 0;
};

struct RecentRecord {
  OperationKind op = OperationKind::Unknown;
  std::wstring sourcePath;
  std::wstring destParent;
  std::wstring destName;
  DWORD tick = 0;
};

struct ShellRegistration {
  ULONG id = 0;
  PIDLIST_ABSOLUTE pidl = nullptr;
  bool newDelivery = true;
};

struct MonitorState {
  HWND hwnd = nullptr;
  HICON icon = nullptr;
  UINT taskbarCreated = 0;
  ClipboardListenMode clipboardMode = ClipboardListenMode::None;
  HWND nextClipboardViewer = nullptr;
  std::vector<ShellRegistration> shellRegistrations;
  FileClipboardSnapshot clipboard;
  std::vector<RecentRecord> recentRecords;
  bool cacheDirty = false;
  int externalRecordsThisSession = 0;
  std::wstring lastExternalRecord;
};

using AddClipboardFormatListenerFn = BOOL(WINAPI*)(HWND);
using RemoveClipboardFormatListenerFn = BOOL(WINAPI*)(HWND);

std::wstring ModuleDir() {
  std::wstring exe = GetProgramPath();
  size_t pos = exe.find_last_of(L"\\/");
  return pos == std::wstring::npos ? L"." : exe.substr(0, pos);
}

std::wstring UiPath() { return PathCombineSimple(ModuleDir(), L"PathCue.UI.exe"); }

std::wstring ReadInstallDirFromRegistry() {
  wchar_t value[32768]{};
  DWORD cb = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\PathCue", L"InstallDir", RRF_RT_REG_SZ, nullptr, value, &cb) == ERROR_SUCCESS) {
    return value;
  }
  return ModuleDir();
}

bool IsClassicMenuRegistered() {
  HKEY h{};
  LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\PathCue", 0, KEY_READ, &h);
  if (rc == ERROR_SUCCESS) RegCloseKey(h);
  return rc == ERROR_SUCCESS;
}

bool IsAutoStartEnabled() {
  wchar_t value[32768]{};
  DWORD cb = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &cb) != ERROR_SUCCESS) return false;
  std::wstring text = value;
  std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return text.find(L"pathcue.monitor.exe") != std::wstring::npos;
}

bool SetAutoStart(bool enable, std::wstring* error = nullptr) {
  HKEY h{};
  LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &h, nullptr);
  if (rc != ERROR_SUCCESS) {
    if (error) *error = L"Unable to open current-user Run key: " + GetLastErrorMessage(static_cast<DWORD>(rc));
    return false;
  }

  if (enable) {
    std::wstring command = QuoteArg(GetProgramPath()) + L" --background";
    rc = RegSetValueExW(h, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    rc = RegDeleteValueW(h, kRunValue);
    if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
  }
  RegCloseKey(h);
  if (rc != ERROR_SUCCESS) {
    if (error) *error = L"Unable to update monitor auto-start: " + GetLastErrorMessage(static_cast<DWORD>(rc));
    return false;
  }
  return true;
}

std::wstring YesNo(bool value) { return value ? L"Yes" : L"No"; }

std::wstring ToLowerCopy(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return s;
}

std::wstring OperationLabel(OperationKind op) {
  switch (op) {
    case OperationKind::Copy: return L"copy";
    case OperationKind::Move: return L"move";
    case OperationKind::Both: return L"both";
    default: return L"unknown";
  }
}

bool PathExistsAny(const std::wstring& path) {
  return FileExists(path) || DirectoryExists(path);
}

ULONGLONG FileTimeToUInt64(const FILETIME& ft) {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

FILETIME CurrentFileTime() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  return ft;
}

bool PathLooksRecentlyCreated(const std::wstring& path, const FILETIME& after, DWORD toleranceMs = 3000) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
  ULONGLONG threshold = FileTimeToUInt64(after);
  const ULONGLONG tolerance = static_cast<ULONGLONG>(toleranceMs) * 10000ull;
  threshold = threshold > tolerance ? threshold - tolerance : 0;
  return FileTimeToUInt64(data.ftCreationTime) >= threshold || FileTimeToUInt64(data.ftLastWriteTime) >= threshold;
}

bool FilesLookEquivalent(const std::wstring& left, const std::wstring& right) {
  WIN32_FILE_ATTRIBUTE_DATA a{};
  WIN32_FILE_ATTRIBUTE_DATA b{};
  if (!GetFileAttributesExW(left.c_str(), GetFileExInfoStandard, &a)) return false;
  if (!GetFileAttributesExW(right.c_str(), GetFileExInfoStandard, &b)) return false;
  if ((a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (b.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
  if (a.nFileSizeHigh != b.nFileSizeHigh || a.nFileSizeLow != b.nFileSizeLow) return false;
  ULONGLONG at = FileTimeToUInt64(a.ftLastWriteTime);
  ULONGLONG bt = FileTimeToUInt64(b.ftLastWriteTime);
  ULONGLONG diff = at > bt ? at - bt : bt - at;
  return diff <= 2ull * 1000ull * 10000ull;
}

void SplitStemExtension(const std::wstring& name, std::wstring* stem, std::wstring* ext) {
  size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot != 0) {
    *stem = name.substr(0, dot);
    *ext = name.substr(dot);
  } else {
    *stem = name;
    ext->clear();
  }
}

bool StartsWith(const std::wstring& text, const std::wstring& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool LooksLikeExplorerCopyName(const std::wstring& sourceName, const std::wstring& destName, OperationKind op) {
  std::wstring source = ToLowerCopy(sourceName);
  std::wstring dest = ToLowerCopy(destName);
  if (source == dest) return true;
  if (op != OperationKind::Copy) return false;

  std::wstring sourceStem;
  std::wstring sourceExt;
  std::wstring destStem;
  std::wstring destExt;
  SplitStemExtension(source, &sourceStem, &sourceExt);
  SplitStemExtension(dest, &destStem, &destExt);
  if (sourceExt != destExt) return false;

  const std::wstring copySuffix = L" - copy";
  if (destStem == sourceStem + copySuffix) return true;
  if (StartsWith(destStem, sourceStem + copySuffix + L" (")) return true;
  if (StartsWith(destStem, sourceStem + L" (")) return true;
  return false;
}

DWORD ReadClipboardSequence() {
  return GetClipboardSequenceNumber();
}

OperationKind OperationFromPreferredDropEffect(DWORD effect, bool hasEffect) {
  if (hasEffect && (effect & DROPEFFECT_MOVE)) return OperationKind::Move;
  if (hasEffect && (effect & DROPEFFECT_COPY)) return OperationKind::Copy;
  return OperationKind::Copy;
}

DWORD ReadPreferredDropEffectFromOpenClipboard(bool* found) {
  *found = false;
  UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
  if (!format || !IsClipboardFormatAvailable(format)) return DROPEFFECT_COPY;
  HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(format));
  if (!memory) return DROPEFFECT_COPY;
  void* raw = GlobalLock(memory);
  if (!raw) return DROPEFFECT_COPY;
  DWORD effect = *static_cast<DWORD*>(raw);
  GlobalUnlock(memory);
  *found = true;
  return effect;
}

void ClearFileClipboard(MonitorState* s) {
  s->clipboard.active = false;
  s->clipboard.op = OperationKind::Unknown;
  s->clipboard.sources.clear();
  s->clipboard.sequence = ReadClipboardSequence();
  s->clipboard.capturedTick = GetTickCount();
  s->clipboard.capturedTime = CurrentFileTime();
}

void ScheduleClipboardRead(MonitorState* s, DWORD delayMs = kClipboardReadDelayMs) {
  if (!s || !s->hwnd) return;
  SetTimer(s->hwnd, kTimerClipboardRead, delayMs, nullptr);
}

void ReadFileClipboard(MonitorState* s) {
  if (!s) return;
  KillTimer(s->hwnd, kTimerClipboardRead);
  if (!OpenClipboard(s->hwnd)) {
    if (s->clipboard.retryCount++ < kMaxClipboardRetries) ScheduleClipboardRead(s, kClipboardRetryDelayMs);
    return;
  }

  s->clipboard.retryCount = 0;
  DWORD sequence = ReadClipboardSequence();
  if (!IsClipboardFormatAvailable(CF_HDROP)) {
    CloseClipboard();
    ClearFileClipboard(s);
    return;
  }

  HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  if (!drop) {
    CloseClipboard();
    ClearFileClipboard(s);
    return;
  }

  std::vector<ClipboardSource> sources;
  UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  sources.reserve(count);
  for (UINT i = 0; i < count; ++i) {
    UINT len = DragQueryFileW(drop, i, nullptr, 0);
    if (!len) continue;
    std::wstring path(len + 1, L'\0');
    DragQueryFileW(drop, i, path.data(), len + 1);
    while (!path.empty() && path.back() == L'\0') path.pop_back();
    if (path.empty()) continue;

    ClipboardSource source;
    source.path = NormalizePathForDisplay(path);
    source.parent = ParentPath(source.path);
    source.name = FileNameFromPath(source.path);
    sources.push_back(std::move(source));
  }

  bool hasEffect = false;
  DWORD effect = ReadPreferredDropEffectFromOpenClipboard(&hasEffect);
  CloseClipboard();

  if (sources.empty()) {
    ClearFileClipboard(s);
    return;
  }

  s->clipboard.active = true;
  s->clipboard.op = OperationFromPreferredDropEffect(effect, hasEffect);
  s->clipboard.sources = std::move(sources);
  s->clipboard.sequence = sequence;
  s->clipboard.capturedTick = GetTickCount();
  s->clipboard.capturedTime = CurrentFileTime();
}

bool ClipboardSnapshotIsFresh(const MonitorState* s) {
  return s && s->clipboard.active && (GetTickCount() - s->clipboard.capturedTick) <= kClipboardTtlMs;
}

int SourceNameCount(const FileClipboardSnapshot& clipboard, const std::wstring& sourceName) {
  std::wstring needle = ToLowerCopy(sourceName);
  int count = 0;
  for (const auto& source : clipboard.sources) {
    if (ToLowerCopy(source.name) == needle) ++count;
  }
  return count;
}

int FindClipboardSourceForCreatedPath(const MonitorState* s, const std::wstring& createdPath) {
  if (!ClipboardSnapshotIsFresh(s)) return -1;
  std::wstring createdName = FileNameFromPath(createdPath);
  int match = -1;
  for (std::size_t i = 0; i < s->clipboard.sources.size(); ++i) {
    const auto& source = s->clipboard.sources[i];
    if (!LooksLikeExplorerCopyName(source.name, createdName, s->clipboard.op)) continue;
    if (SourceNameCount(s->clipboard, source.name) > 1) return -1;
    if (match != -1) return -1;
    match = static_cast<int>(i);
  }
  return match;
}

int FindClipboardSourceForSourcePath(const MonitorState* s, const std::wstring& sourcePath) {
  if (!ClipboardSnapshotIsFresh(s)) return -1;
  for (std::size_t i = 0; i < s->clipboard.sources.size(); ++i) {
    if (IsSamePathCaseInsensitive(s->clipboard.sources[i].path, sourcePath)) return static_cast<int>(i);
  }
  return -1;
}

void MarkCacheDirty(MonitorState* s) {
  s->cacheDirty = true;
  SetTimer(s->hwnd, kTimerCacheFlush, kCacheFlushDelayMs, nullptr);
}

void FlushMenuCacheIfNeeded(MonitorState* s) {
  if (!s || !s->cacheDirty) return;
  s->cacheDirty = false;
  KillTimer(s->hwnd, kTimerCacheFlush);
  HistoryDatabase db;
  if (db.Load(nullptr)) db.WriteMenuCache(L"", nullptr);
}

void PruneRecentRecords(MonitorState* s) {
  DWORD now = GetTickCount();
  s->recentRecords.erase(std::remove_if(s->recentRecords.begin(), s->recentRecords.end(),
                                        [now](const RecentRecord& record) {
                                          return now - record.tick > kRecentRecordTtlMs;
                                        }),
                         s->recentRecords.end());
}

bool WasRecentlyRecorded(MonitorState* s,
                         OperationKind op,
                         const std::wstring& sourcePath,
                         const std::wstring& destParent,
                         const std::wstring& destName) {
  PruneRecentRecords(s);
  for (const auto& record : s->recentRecords) {
    if (record.op == op &&
        IsSamePathCaseInsensitive(record.sourcePath, sourcePath) &&
        IsSamePathCaseInsensitive(record.destParent, destParent) &&
        ToLowerCopy(record.destName) == ToLowerCopy(destName)) {
      return true;
    }
  }
  return false;
}

bool AppendExternalOperation(MonitorState* s,
                             OperationKind op,
                             const std::wstring& sourcePath,
                             const std::wstring& destParent,
                             double confidence,
                             const std::wstring& observedBy,
                             const std::wstring& destName) {
  if (op != OperationKind::Copy && op != OperationKind::Move) return false;
  std::wstring normalizedSource = NormalizePathForDisplay(sourcePath);
  std::wstring sourceParent = ParentPath(normalizedSource);
  std::wstring normalizedDest = NormalizePathForDisplay(destParent);
  if (sourceParent.empty() || normalizedDest.empty()) return false;
  if (IsSamePathCaseInsensitive(sourceParent, normalizedDest)) return false;
  if (WasRecentlyRecorded(s, op, normalizedSource, normalizedDest, destName)) return false;

  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  OperationRecord record;
  record.timestamp = UnixNow();
  record.op = op;
  record.sourceParent = sourceParent;
  record.destParent = normalizedDest;
  record.result = L"inferred";
  record.confidence = confidence;
  record.observedBy = observedBy;
  record.conflictPolicy = L"external";
  if (!db.AppendOperation(record, &err)) return false;

  RecentRecord recent;
  recent.op = op;
  recent.sourcePath = normalizedSource;
  recent.destParent = normalizedDest;
  recent.destName = destName;
  recent.tick = GetTickCount();
  s->recentRecords.push_back(std::move(recent));

  std::wstringstream ss;
  ss << OperationLabel(op) << L": " << sourceParent << L" -> " << normalizedDest;
  s->lastExternalRecord = ss.str();
  ++s->externalRecordsThisSession;
  MarkCacheDirty(s);
  return true;
}

void HandleCreatedPath(MonitorState* s, const std::wstring& createdPath) {
  if (!PathExistsAny(createdPath)) return;
  int index = FindClipboardSourceForCreatedPath(s, createdPath);
  if (index < 0) return;
  const auto& source = s->clipboard.sources[static_cast<std::size_t>(index)];
  std::wstring destParent = ParentPath(createdPath);
  double confidence = s->clipboard.op == OperationKind::Move ? 0.86 : 0.90;
  AppendExternalOperation(s,
                          s->clipboard.op,
                          source.path,
                          destParent,
                          confidence,
                          L"external_clipboard_shell_create",
                          FileNameFromPath(createdPath));
}

void HandleUpdatedDirectory(MonitorState* s, const std::wstring& dir) {
  if (!ClipboardSnapshotIsFresh(s) || !DirectoryExists(dir)) return;
  if (s->clipboard.sources.size() > 64) return;

  for (const auto& source : s->clipboard.sources) {
    if (SourceNameCount(s->clipboard, source.name) > 1) continue;
    std::wstring exact = PathCombineSimple(dir, source.name);
    bool exactExists = PathExistsAny(exact);
    bool equivalentCopiedFile = s->clipboard.op == OperationKind::Copy &&
                                (GetTickCount() - s->clipboard.capturedTick) <= 2 * 60 * 1000 &&
                                FilesLookEquivalent(source.path, exact);
    bool looksCopiedHere = exactExists && (PathLooksRecentlyCreated(exact, s->clipboard.capturedTime) || equivalentCopiedFile);
    bool looksMovedHere = exactExists && s->clipboard.op == OperationKind::Move && !PathExistsAny(source.path);
    if (looksCopiedHere || looksMovedHere) {
      AppendExternalOperation(s,
                              s->clipboard.op,
                              source.path,
                              dir,
                              looksMovedHere ? 0.80 : (equivalentCopiedFile ? 0.68 : 0.74),
                              L"external_clipboard_shell_updatedir",
                              source.name);
      continue;
    }
  }
}

void HandleRenamedPath(MonitorState* s, const std::wstring& oldPath, const std::wstring& newPath) {
  if (oldPath.empty() || newPath.empty()) return;
  std::wstring oldParent = ParentPath(oldPath);
  std::wstring newParent = ParentPath(newPath);
  if (oldParent.empty() || newParent.empty()) return;
  if (IsSamePathCaseInsensitive(oldParent, newParent)) return;

  int clipboardIndex = FindClipboardSourceForSourcePath(s, oldPath);
  double confidence = clipboardIndex >= 0 && s->clipboard.op == OperationKind::Move ? 0.98 : 0.82;
  std::wstring observedBy = clipboardIndex >= 0 && s->clipboard.op == OperationKind::Move
                                ? L"external_clipboard_shell_rename"
                                : L"external_shell_rename";
  AppendExternalOperation(s, OperationKind::Move, oldPath, newParent, confidence, observedBy, FileNameFromPath(newPath));
}

std::wstring PidlToPath(PCIDLIST_ABSOLUTE pidl) {
  if (!pidl) return L"";
  wchar_t path[MAX_PATH]{};
  if (!SHGetPathFromIDListW(pidl, path)) return L"";
  return NormalizePathForDisplay(path);
}

void HandleShellChange(MonitorState* s, LONG eventId, const std::wstring& path1, const std::wstring& path2) {
  if (!s) return;
  if ((eventId & (SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER)) && !path1.empty() && !path2.empty()) {
    HandleRenamedPath(s, path1, path2);
  }
  if ((eventId & (SHCNE_CREATE | SHCNE_MKDIR)) && !path1.empty()) {
    HandleCreatedPath(s, path1);
  }
  if ((eventId & SHCNE_UPDATEDIR) && !path1.empty()) {
    HandleUpdatedDirectory(s, path1);
  }
}

void HandleShellChangeMessage(MonitorState* s, WPARAM wp, LPARAM lp, bool newDelivery) {
  if (!s) return;
  LONG eventId = 0;
  std::wstring path1;
  std::wstring path2;

  if (newDelivery) {
    PIDLIST_ABSOLUTE* pidls = nullptr;
    HANDLE lock = SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wp), static_cast<DWORD>(lp), &pidls, &eventId);
    if (!lock) return;
    if (pidls) {
      path1 = PidlToPath(pidls[0]);
      path2 = PidlToPath(pidls[1]);
    }
    SHChangeNotification_Unlock(lock);
  } else {
    auto* pidls = reinterpret_cast<PIDLIST_ABSOLUTE*>(wp);
    eventId = static_cast<LONG>(lp);
    if (pidls) {
      path1 = PidlToPath(pidls[0]);
      path2 = PidlToPath(pidls[1]);
    }
  }

  HandleShellChange(s, eventId, path1, path2);
}

bool RegisterClipboardMonitor(MonitorState* s) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  auto addListener = user32 ? reinterpret_cast<AddClipboardFormatListenerFn>(GetProcAddress(user32, "AddClipboardFormatListener")) : nullptr;
  if (addListener && addListener(s->hwnd)) {
    s->clipboardMode = ClipboardListenMode::FormatListener;
    ScheduleClipboardRead(s, 100);
    return true;
  }

  s->nextClipboardViewer = SetClipboardViewer(s->hwnd);
  s->clipboardMode = ClipboardListenMode::ViewerChain;
  ScheduleClipboardRead(s, 100);
  return true;
}

void UnregisterClipboardMonitor(MonitorState* s) {
  if (!s) return;
  if (s->clipboardMode == ClipboardListenMode::FormatListener) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto removeListener = user32 ? reinterpret_cast<RemoveClipboardFormatListenerFn>(GetProcAddress(user32, "RemoveClipboardFormatListener")) : nullptr;
    if (removeListener) removeListener(s->hwnd);
  } else if (s->clipboardMode == ClipboardListenMode::ViewerChain) {
    ChangeClipboardChain(s->hwnd, s->nextClipboardViewer);
  }
  s->clipboardMode = ClipboardListenMode::None;
  s->nextClipboardViewer = nullptr;
}

bool TryRegisterShellPidl(MonitorState* s, PIDLIST_ABSOLUTE pidl) {
  if (!pidl) return false;
  SHChangeNotifyEntry entry{};
  entry.pidl = pidl;
  entry.fRecursive = TRUE;

  const LONG events = SHCNE_CREATE | SHCNE_MKDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER | SHCNE_UPDATEDIR;
  int sources = SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_RecursiveInterrupt | SHCNRF_NewDelivery;
  ULONG id = SHChangeNotifyRegister(s->hwnd, sources, events, kMsgShellChange, 1, &entry);
  bool newDelivery = true;
  if (!id) {
    sources = SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_RecursiveInterrupt;
    id = SHChangeNotifyRegister(s->hwnd, sources, events, kMsgShellChangeOld, 1, &entry);
    newDelivery = false;
  }
  if (!id) {
    CoTaskMemFree(pidl);
    return false;
  }

  ShellRegistration reg;
  reg.id = id;
  reg.pidl = pidl;
  reg.newDelivery = newDelivery;
  s->shellRegistrations.push_back(reg);
  return true;
}

bool ShouldWatchDriveType(UINT type) {
  return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_REMOTE || type == DRIVE_RAMDISK;
}

bool RegisterShellMonitor(MonitorState* s) {
  wchar_t desktopPath[MAX_PATH]{};
  if (SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktopPath) == S_OK) {
    TryRegisterShellPidl(s, ILCreateFromPathW(desktopPath));
  }

  DWORD chars = GetLogicalDriveStringsW(0, nullptr);
  if (chars) {
    std::vector<wchar_t> drives(chars + 1, L'\0');
    if (GetLogicalDriveStringsW(static_cast<DWORD>(drives.size()), drives.data())) {
      const wchar_t* drive = drives.data();
      while (*drive) {
        std::wstring root = drive;
        if (ShouldWatchDriveType(GetDriveTypeW(root.c_str()))) {
          TryRegisterShellPidl(s, ILCreateFromPathW(root.c_str()));
        }
        drive += root.size() + 1;
      }
    }
  }

  if (s->shellRegistrations.empty()) {
    PIDLIST_ABSOLUTE desktop = nullptr;
    if (SUCCEEDED(SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &desktop)) && desktop) {
      TryRegisterShellPidl(s, desktop);
    }
  }
  return !s->shellRegistrations.empty();
}

void UnregisterShellMonitor(MonitorState* s) {
  if (!s) return;
  for (auto& reg : s->shellRegistrations) {
    if (reg.id) SHChangeNotifyDeregister(reg.id);
    if (reg.pidl) CoTaskMemFree(reg.pidl);
  }
  s->shellRegistrations.clear();
}

std::wstring ClipboardModeText(ClipboardListenMode mode) {
  switch (mode) {
    case ClipboardListenMode::FormatListener: return L"AddClipboardFormatListener";
    case ClipboardListenMode::ViewerChain: return L"SetClipboardViewer fallback";
    default: return L"Unavailable";
  }
}

std::wstring ClipboardStatusText(const MonitorState* s) {
  if (!s || !s->clipboard.active) return L"No file clipboard";
  std::wstringstream ss;
  ss << OperationLabel(s->clipboard.op) << L", " << s->clipboard.sources.size() << L" source(s)";
  DWORD ageSec = (GetTickCount() - s->clipboard.capturedTick) / 1000;
  ss << L", " << ageSec << L"s old";
  return ss.str();
}

void LaunchPath(const std::wstring& path) {
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenControlPanel() {
  std::wstring ui = UiPath();
  ShellExecuteW(nullptr, L"open", ui.c_str(), nullptr, ModuleDir().c_str(), SW_SHOWNORMAL);
}

std::wstring StatusText(const MonitorState* s) {
  HistoryDatabase db;
  std::wstring err;
  bool loaded = db.Load(&err);

  std::wstringstream ss;
  ss << L"PathCue Monitor " << PATHCUE_VERSION << L"\r\n";
  ss << L"Status: Running\r\n";
  ss << L"Install folder: " << ReadInstallDirFromRegistry() << L"\r\n";
  ss << L"Auto-start: " << YesNo(IsAutoStartEnabled()) << L"\r\n";
  ss << L"Classic Explorer menu: " << YesNo(IsClassicMenuRegistered()) << L"\r\n";
  ss << L"Clipboard monitor: " << ClipboardModeText(s ? s->clipboardMode : ClipboardListenMode::None) << L"\r\n";
  ss << L"File change monitor: " << (s && !s->shellRegistrations.empty() ? L"Yes" : L"No");
  if (s && !s->shellRegistrations.empty()) ss << L" (" << s->shellRegistrations.size() << L" root(s))";
  ss << L"\r\n";
  ss << L"Observed file clipboard: " << ClipboardStatusText(s) << L"\r\n";
  ss << L"External records this session: " << (s ? s->externalRecordsThisSession : 0) << L"\r\n";
  if (s && !s->lastExternalRecord.empty()) ss << L"Last external record: " << s->lastExternalRecord << L"\r\n";
  ss << L"Store: " << EncryptedRecordStore::DefaultStorePath() << L"\r\n";
  ss << L"Menu cache: " << EncryptedRecordStore::DefaultMenuCachePath() << L"\r\n";
  ss << L"Menu cache exists: " << YesNo(FileExists(EncryptedRecordStore::DefaultMenuCachePath())) << L"\r\n";
  if (loaded) {
    ss << L"Pinned targets: " << db.pinnedTargets().size() << L"\r\n";
    ss << L"Operation records: " << db.operations().size();
  } else {
    ss << L"History store: " << (err.empty() ? L"Unable to load" : err);
  }
  return ss.str();
}

std::wstring TrayTip(const MonitorState* s) {
  HistoryDatabase db;
  std::wstring err;
  db.Load(&err);
  std::wstringstream ss;
  ss << L"PathCue monitor running";
  if (s && s->clipboard.active) {
    ss << L"\nClipboard: " << OperationLabel(s->clipboard.op) << L", " << s->clipboard.sources.size() << L" source(s)";
  }
  if (err.empty()) {
    ss << L"\nPins: " << db.pinnedTargets().size() << L"  Records: " << db.operations().size();
  }
  return ss.str();
}

void ShowStatus(HWND owner, const MonitorState* s) {
  MessageBoxW(owner, StatusText(s).c_str(), L"PathCue Status", MB_OK | MB_ICONINFORMATION);
}

bool RebuildMenuCache(HWND owner) {
  HistoryDatabase db;
  std::wstring err;
  if (!db.Load(&err) || !db.WriteMenuCache(L"", &err)) {
    MessageBoxW(owner, err.empty() ? L"Unable to rebuild the PathCue menu cache." : err.c_str(), L"PathCue", MB_OK | MB_ICONERROR);
    return false;
  }
  MessageBoxW(owner, L"PathCue menu cache rebuilt.", L"PathCue", MB_OK | MB_ICONINFORMATION);
  return true;
}

bool CleanupHistory(HWND owner) {
  HistoryDatabase db;
  std::wstring err;
  if (!db.Load(&err) || !db.CleanupExpired(90, 365, &err)) {
    MessageBoxW(owner, err.empty() ? L"Unable to clean PathCue history." : err.c_str(), L"PathCue", MB_OK | MB_ICONERROR);
    return false;
  }
  db.Load(nullptr);
  db.WriteMenuCache(L"", nullptr);
  MessageBoxW(owner, L"Expired PathCue operation records cleaned.", L"PathCue", MB_OK | MB_ICONINFORMATION);
  return true;
}

void PutPixel(std::uint32_t* pixels, int x, int y, BYTE r, BYTE g, BYTE b, BYTE a = 255) {
  if (x < 0 || x >= 32 || y < 0 || y >= 32) return;
  pixels[y * 32 + x] = (static_cast<std::uint32_t>(a) << 24) |
                       (static_cast<std::uint32_t>(r) << 16) |
                       (static_cast<std::uint32_t>(g) << 8) |
                       static_cast<std::uint32_t>(b);
}

void FillRectPixels(std::uint32_t* pixels, int left, int top, int right, int bottom, BYTE r, BYTE g, BYTE b) {
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) PutPixel(pixels, x, y, r, g, b);
  }
}

void FillTriangle(std::uint32_t* pixels, int apexY, int baseY, int halfBase, BYTE r, BYTE g, BYTE b) {
  const int center = 16;
  const int height = baseY - apexY;
  if (height <= 0) return;
  for (int y = apexY; y <= baseY; ++y) {
    int half = ((y - apexY) * halfBase) / height;
    for (int x = center - half; x <= center + half; ++x) PutPixel(pixels, x, y, r, g, b);
  }
}

HICON CreateFallbackIcon() {
  return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}

HICON CreateTreeIcon() {
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = 32;
  bi.bV5Height = -32;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;

  void* raw = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &raw, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!color || !raw) return CreateFallbackIcon();

  auto* pixels = static_cast<std::uint32_t*>(raw);
  std::fill(pixels, pixels + 32 * 32, 0);
  FillRectPixels(pixels, 14, 18, 18, 28, 118, 75, 38);
  FillRectPixels(pixels, 12, 25, 20, 28, 118, 75, 38);
  FillTriangle(pixels, 4, 17, 9, 20, 112, 64);
  FillTriangle(pixels, 9, 22, 11, 22, 137, 72);
  FillTriangle(pixels, 14, 27, 13, 31, 156, 82);
  FillTriangle(pixels, 6, 16, 7, 55, 173, 91);
  PutPixel(pixels, 16, 5, 116, 215, 128);
  PutPixel(pixels, 15, 6, 116, 215, 128);
  PutPixel(pixels, 17, 6, 116, 215, 128);

  std::vector<BYTE> maskBytes((32 * 32 + 7) / 8, 0);
  HBITMAP mask = CreateBitmap(32, 32, 1, 1, maskBytes.data());
  if (!mask) {
    DeleteObject(color);
    return CreateFallbackIcon();
  }

  ICONINFO info{};
  info.fIcon = TRUE;
  info.hbmColor = color;
  info.hbmMask = mask;
  HICON icon = CreateIconIndirect(&info);
  DeleteObject(color);
  DeleteObject(mask);
  return icon ? icon : CreateFallbackIcon();
}

void FillNotifyIconData(MonitorState* s, NOTIFYICONDATAW* nid) {
  ZeroMemory(nid, sizeof(*nid));
  nid->cbSize = sizeof(*nid);
  nid->hWnd = s->hwnd;
  nid->uID = kTrayId;
  nid->uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid->uCallbackMessage = kMsgTray;
  nid->hIcon = s->icon;
  std::wstring tip = TrayTip(s);
  wcsncpy_s(nid->szTip, tip.c_str(), _TRUNCATE);
}

void AddTrayIcon(MonitorState* s) {
  NOTIFYICONDATAW nid{};
  FillNotifyIconData(s, &nid);
  Shell_NotifyIconW(NIM_ADD, &nid);
  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void UpdateTrayIcon(MonitorState* s) {
  NOTIFYICONDATAW nid{};
  FillNotifyIconData(s, &nid);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void RemoveTrayIcon(MonitorState* s) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = s->hwnd;
  nid.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayMenu(MonitorState* s) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"PathCue Monitor: Running");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_STATUS, L"Status...");
  AppendMenuW(menu, MF_STRING, IDM_OPEN_UI, L"Open Control Panel");
  AppendMenuW(menu, MF_STRING, IDM_OPEN_DATA, L"Open Data Folder");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_BUILD_CACHE, L"Rebuild Menu Cache");
  AppendMenuW(menu, MF_STRING, IDM_CLEANUP, L"Cleanup History");
  AppendMenuW(menu, MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART, L"Start with Windows");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

  POINT pt{};
  GetCursorPos(&pt);
  SetForegroundWindow(s->hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, s->hwnd, nullptr);
  DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  MonitorState* s = reinterpret_cast<MonitorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (s && msg == s->taskbarCreated) {
    AddTrayIcon(s);
    return 0;
  }

  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      s = reinterpret_cast<MonitorState*>(cs->lpCreateParams);
      s->hwnd = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
      return TRUE;
    }
    case WM_CREATE:
      AddTrayIcon(s);
      RegisterClipboardMonitor(s);
      RegisterShellMonitor(s);
      SetTimer(hwnd, kTimerRefresh, 30000, nullptr);
      return 0;
    case WM_TIMER:
      if (wp == kTimerRefresh) {
        if (s && s->clipboard.active && !ClipboardSnapshotIsFresh(s)) s->clipboard.active = false;
        UpdateTrayIcon(s);
      } else if (wp == kTimerClipboardRead) {
        ReadFileClipboard(s);
        UpdateTrayIcon(s);
      } else if (wp == kTimerCacheFlush) {
        FlushMenuCacheIfNeeded(s);
        UpdateTrayIcon(s);
      }
      return 0;
    case WM_CLIPBOARDUPDATE:
      ScheduleClipboardRead(s);
      return 0;
    case WM_DRAWCLIPBOARD:
      ScheduleClipboardRead(s);
      if (s && s->nextClipboardViewer) SendMessageW(s->nextClipboardViewer, msg, wp, lp);
      return 0;
    case WM_CHANGECBCHAIN:
      if (s) {
        if (reinterpret_cast<HWND>(wp) == s->nextClipboardViewer) {
          s->nextClipboardViewer = reinterpret_cast<HWND>(lp);
        } else if (s->nextClipboardViewer) {
          SendMessageW(s->nextClipboardViewer, msg, wp, lp);
        }
      }
      return 0;
    case kMsgShellChange:
      HandleShellChangeMessage(s, wp, lp, true);
      UpdateTrayIcon(s);
      return 0;
    case kMsgShellChangeOld:
      HandleShellChangeMessage(s, wp, lp, false);
      UpdateTrayIcon(s);
      return 0;
    case WM_DEVICECHANGE:
      if (s) {
        UnregisterShellMonitor(s);
        RegisterShellMonitor(s);
        UpdateTrayIcon(s);
      }
      return 0;
    case kMsgTray:
      if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == WM_RBUTTONUP) {
        ShowTrayMenu(s);
      } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
        OpenControlPanel();
      }
      return 0;
    case WM_COMMAND: {
      std::wstring err;
      switch (LOWORD(wp)) {
        case IDM_STATUS: ShowStatus(hwnd, s); return 0;
        case IDM_OPEN_UI: OpenControlPanel(); return 0;
        case IDM_OPEN_DATA: {
          std::wstring dir = PathCombineSimple(GetKnownFolderLocalAppData(), L"PathCue");
          EnsureDirectory(dir);
          LaunchPath(dir);
          return 0;
        }
        case IDM_BUILD_CACHE: RebuildMenuCache(hwnd); UpdateTrayIcon(s); return 0;
        case IDM_CLEANUP: CleanupHistory(hwnd); UpdateTrayIcon(s); return 0;
        case IDM_AUTOSTART:
          if (!SetAutoStart(!IsAutoStartEnabled(), &err)) MessageBoxW(hwnd, err.c_str(), L"PathCue", MB_OK | MB_ICONERROR);
          UpdateTrayIcon(s);
          return 0;
        case IDM_EXIT: DestroyWindow(hwnd); return 0;
      }
      break;
    }
    case kMsgShowStatus:
      ShowStatus(hwnd, s);
      return 0;
    case kMsgQuit:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      FlushMenuCacheIfNeeded(s);
      UnregisterShellMonitor(s);
      UnregisterClipboardMonitor(s);
      RemoveTrayIcon(s);
      KillTimer(hwnd, kTimerRefresh);
      KillTimer(hwnd, kTimerClipboardRead);
      KillTimer(hwnd, kTimerCacheFlush);
      if (s && s->icon) {
        DestroyIcon(s->icon);
        s->icon = nullptr;
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool HasArg(const std::vector<std::wstring>& args, const std::wstring& key) {
  for (const auto& arg : args) {
    if (arg == key) return true;
  }
  return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  CoInitializeScope co;
  auto args = SplitCommandLineArgs();
  HWND existing = FindWindowW(kWindowClass, nullptr);
  if (HasArg(args, L"--quit")) {
    if (existing) PostMessageW(existing, kMsgQuit, 0, 0);
    return 0;
  }
  if (existing) {
    if (!HasArg(args, L"--background")) PostMessageW(existing, kMsgShowStatus, 0, 0);
    return 0;
  }

  Handle mutex(CreateMutexW(nullptr, TRUE, L"Local\\PathCue.Monitor.Singleton"));
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  MonitorState state;
  state.icon = CreateTreeIcon();
  state.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = instance;
  wc.lpszClassName = kWindowClass;
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = state.icon;
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"PathCue Monitor", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, instance, &state);
  if (!hwnd) return 1;
  if (!HasArg(args, L"--background") && show != SW_HIDE) ShowStatus(hwnd, &state);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
