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
#include <cwctype>
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
constexpr int IDC_DASHBOARD = 1036;
constexpr int IDC_USE_SELECTED_TARGET = 1037;
constexpr int IDC_OPEN_SELECTED_TARGET = 1038;
constexpr int IDC_SELECT_ALL_TEXT = 1039;
constexpr int IDC_CLEAR_TEXT = 1040;
constexpr int IDC_NORMALIZE_TEXT = 1041;
constexpr int IDC_TEXT_META = 1042;

constexpr COLORREF kWindowBg = RGB(241, 245, 249);
constexpr COLORREF kPanelBg = RGB(255, 255, 255);
constexpr COLORREF kPanelBorder = RGB(211, 219, 231);
constexpr COLORREF kHeaderBg = RGB(248, 250, 252);
constexpr COLORREF kAccent = RGB(37, 99, 235);
constexpr COLORREF kText = RGB(15, 23, 42);
constexpr int kPageTop = 128;

enum UiPage {
  kPagePaths = 0,
  kPageText = 1,
  kPageStatus = 2,
  kPageCount = 3,
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
  HWND queueSummary = nullptr;
  HWND queueTarget = nullptr;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HFONT sectionFont = nullptr;
  HFONT monoFont = nullptr;
  HBRUSH background = nullptr;
  HBRUSH editBrush = nullptr;
  HWND previousForeground = nullptr;
  int activePage = kPagePaths;
  bool queueMode = false;
  std::vector<HWND> pageControls[kPageCount];
  std::vector<PinnedTarget> pinsData;
  std::vector<std::wstring> suggestionTargetsData;
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

HFONT MakeFont(int pointSize, int weight = FW_NORMAL, const wchar_t* family = L"Segoe UI") {
  HDC hdc = GetDC(nullptr);
  int pixels = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
  ReleaseDC(nullptr, hdc);
  return CreateFontW(pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, family);
}

HWND MakeControlEx(UiState* s,
                   DWORD exStyle,
                   const wchar_t* klass,
                   const wchar_t* text,
                   DWORD style,
                   int x,
                   int y,
                   int w,
                   int h,
                   int id,
                   HFONT font = nullptr) {
  HWND ctrl = CreateWindowExW(exStyle, klass, text, style, x, y, w, h, s->hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              GetModuleHandleW(nullptr), nullptr);
  SetControlFont(ctrl, font ? font : s->font);
  return ctrl;
}

HWND MakeControl(UiState* s, const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font = nullptr) {
  return MakeControlEx(s, 0, klass, text, style, x, y, w, h, id, font);
}

HWND MakePageControlEx(UiState* s,
                       int page,
                       DWORD exStyle,
                       const wchar_t* klass,
                       const wchar_t* text,
                       DWORD style,
                       int x,
                       int y,
                       int w,
                       int h,
                       int id,
                       HFONT font = nullptr) {
  HWND ctrl = MakeControlEx(s, exStyle, klass, text, style, x, y, w, h, id, font);
  if (ctrl && page >= 0 && page < kPageCount) s->pageControls[page].push_back(ctrl);
  return ctrl;
}

HWND MakePageControl(UiState* s, int page, const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font = nullptr) {
  return MakePageControlEx(s, page, 0, klass, text, style, x, y, w, h, id, font);
}

HWND MakeLabel(UiState* s, int page, const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr) {
  return MakePageControl(s, page, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, -1, font);
}

