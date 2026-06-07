#include "clipcue/History.h"
#include "clipcue/CryptoStore.h"
#include "clipcue/PathUtils.h"
#include "clipcue/WinUtils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <cstdlib>
#include <cstddef>

namespace clipcue {
namespace {

std::vector<std::wstring> SplitTabs(const std::wstring& s) {
  std::vector<std::wstring> out;
  std::wstring cur;
  for (wchar_t ch : s) {
    if (ch == L'\t') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  out.push_back(cur);
  return out;
}

std::wstring Escape(const std::wstring& s) {
  std::wstringstream ss;
  for (wchar_t ch : s) {
    if (ch == L'%' || ch == L'\t' || ch == L'\n' || ch == L'\r') {
      ss << L'%' << std::uppercase << std::hex << std::setw(4) << std::setfill(L'0') << static_cast<int>(ch)
         << std::nouppercase << std::dec;
    } else {
      ss << ch;
    }
  }
  return ss.str();
}

std::wstring Unescape(const std::wstring& s) {
  std::wstring out;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == L'%' && i + 4 < s.size()) {
      std::wstring hex = s.substr(i + 1, 4);
      wchar_t* end = nullptr;
      long value = wcstol(hex.c_str(), &end, 16);
      if (end && *end == L'\0') {
        out.push_back(static_cast<wchar_t>(value));
        i += 5;
        continue;
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

std::wstring SerializeOperation(const OperationRecord& r) {
  std::wstringstream ss;
  ss << L"OP\t" << r.timestamp << L"\t" << Escape(ToString(r.op)) << L"\t"
     << Escape(r.sourceParent) << L"\t" << Escape(r.destParent) << L"\t"
     << Escape(r.result) << L"\t" << r.confidence << L"\t"
     << Escape(r.observedBy) << L"\t" << Escape(r.conflictPolicy);
  return ss.str();
}

bool ParseOperation(const std::wstring& line, OperationRecord* r) {
  auto parts = SplitTabs(line);
  if (parts.size() < 9 || parts[0] != L"OP") return false;
  r->timestamp = _wtoi64(parts[1].c_str());
  r->op = OperationFromString(Unescape(parts[2]));
  r->sourceParent = Unescape(parts[3]);
  r->destParent = Unescape(parts[4]);
  r->result = Unescape(parts[5]);
  r->confidence = _wtof(parts[6].c_str());
  r->observedBy = Unescape(parts[7]);
  r->conflictPolicy = Unescape(parts[8]);
  return true;
}

std::wstring SerializePin(const PinnedTarget& p) {
  std::wstringstream ss;
  ss << L"PIN\t" << Escape(ToString(p.op)) << L"\t" << Escape(p.destParent) << L"\t"
     << Escape(p.label) << L"\t" << p.priority;
  return ss.str();
}

bool ParsePin(const std::wstring& line, PinnedTarget* p) {
  auto parts = SplitTabs(line);
  if (parts.size() < 5 || parts[0] != L"PIN") return false;
  p->op = OperationFromString(Unescape(parts[1]));
  p->destParent = Unescape(parts[2]);
  p->label = Unescape(parts[3]);
  p->priority = _wtoi(parts[4].c_str());
  return true;
}

std::wstring MakeClipboardId(const ClipboardHistoryEntry& entry) {
  std::wstringstream ss;
  ss << entry.timestamp << L"-" << entry.sequence << L"-" << ToString(entry.kind);
  if (entry.kind == ClipboardContentKind::Files) ss << L"-" << ToString(entry.op);
  return ss.str();
}

std::wstring SerializeClipboardEntry(const ClipboardHistoryEntry& entry) {
  std::wstringstream ss;
  ss << L"CLIP\t" << Escape(entry.id) << L"\t" << entry.timestamp << L"\t" << entry.sequence << L"\t"
     << Escape(ToString(entry.kind)) << L"\t" << Escape(ToString(entry.op)) << L"\t"
     << (entry.selected ? L"1" : L"0") << L"\t" << (entry.stale ? L"1" : L"0") << L"\t"
     << Escape(entry.sourceApp) << L"\t"
     << entry.repeatCount << L"\t"
     << Escape(entry.text) << L"\t" << Escape(JoinLines(entry.files));
  return ss.str();
}

bool ParseClipboardEntry(const std::wstring& line, ClipboardHistoryEntry* entry) {
  auto parts = SplitTabs(line);
  if (parts.size() < 10 || parts[0] != L"CLIP") return false;
  entry->id = Unescape(parts[1]);
  entry->timestamp = _wtoi64(parts[2].c_str());
  entry->sequence = static_cast<std::uint32_t>(_wtoi(parts[3].c_str()));
  entry->kind = ClipboardContentKindFromString(Unescape(parts[4]));
  entry->op = OperationFromString(Unescape(parts[5]));
  entry->selected = parts[6] == L"1" || parts[6] == L"true";
  if (parts.size() >= 12) {
    entry->stale = parts[7] == L"1" || parts[7] == L"true";
    entry->sourceApp = Unescape(parts[8]);
    entry->repeatCount = std::max(1, _wtoi(parts[9].c_str()));
    entry->text = Unescape(parts[10]);
    entry->files = SplitLines(Unescape(parts[11]));
  } else if (parts.size() >= 11) {
    entry->stale = false;
    entry->sourceApp = Unescape(parts[7]);
    entry->repeatCount = std::max(1, _wtoi(parts[8].c_str()));
    entry->text = Unescape(parts[9]);
    entry->files = SplitLines(Unescape(parts[10]));
  } else {
    entry->stale = false;
    entry->repeatCount = 1;
    entry->text = Unescape(parts[8]);
    entry->files = SplitLines(Unescape(parts[9]));
  }
  return entry->kind != ClipboardContentKind::Unknown;
}

bool SameClipboardFiles(const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!IsSamePathCaseInsensitive(a[i], b[i])) return false;
  }
  return true;
}

bool IsSameClipboardOperation(const ClipboardHistoryEntry& a, const ClipboardHistoryEntry& b) {
  if (a.kind != b.kind) return false;
  if (a.kind == ClipboardContentKind::Files) {
    return a.op == b.op && SameClipboardFiles(a.files, b.files);
  }
  if (a.kind == ClipboardContentKind::Text) return a.text == b.text;
  return false;
}

bool OpMatches(OperationKind requested, OperationKind candidate) {
  if (requested == OperationKind::Both || candidate == OperationKind::Both) return true;
  return requested == candidate;
}

bool RouteMatches(OperationKind requested, OperationKind candidate) {
  if (requested == OperationKind::Unknown || candidate == OperationKind::Unknown) return false;
  if (requested == OperationKind::Both || candidate == OperationKind::Both) return true;
  if ((requested == OperationKind::Copy || requested == OperationKind::Move) &&
      (candidate == OperationKind::Copy || candidate == OperationKind::Move)) {
    return true;
  }
  return requested == candidate;
}

std::wstring CandidateKey(const std::wstring& path) {
  std::wstring key = NormalizePathForDisplay(path);
  std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return key;
}

void AddSupportedOperation(OperationKind op, bool* copy, bool* move) {
  if (op == OperationKind::Copy) *copy = true;
  else if (op == OperationKind::Move) *move = true;
  else if (op == OperationKind::Both) {
    *copy = true;
    *move = true;
  }
}

OperationKind CandidateOperation(OperationKind requested, bool copy, bool move) {
  if (requested == OperationKind::Copy || requested == OperationKind::Move) return requested;
  if (copy && move) return OperationKind::Both;
  if (move) return OperationKind::Move;
  if (copy) return OperationKind::Copy;
  return OperationKind::Unknown;
}

int CommonPrefixPathDepth(const std::wstring& a, const std::wstring& b) {
  auto la = NormalizePathForDisplay(a);
  auto lb = NormalizePathForDisplay(b);
  std::transform(la.begin(), la.end(), la.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  std::transform(lb.begin(), lb.end(), lb.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  int depth = 0;
  size_t i = 0;
  while (i < la.size() && i < lb.size() && la[i] == lb[i]) {
    if (la[i] == L'\\' || la[i] == L'/') ++depth;
    ++i;
  }
  return depth;
}

}  // namespace

std::wstring ToString(OperationKind op) {
  switch (op) {
    case OperationKind::Copy: return L"copy";
    case OperationKind::Move: return L"move";
    case OperationKind::Both: return L"both";
    default: return L"unknown";
  }
}

OperationKind OperationFromString(const std::wstring& op) {
  if (op == L"copy" || op == L"c" || op == L"COPY") return OperationKind::Copy;
  if (op == L"move" || op == L"m" || op == L"MOVE") return OperationKind::Move;
  if (op == L"both" || op == L"any") return OperationKind::Both;
  return OperationKind::Unknown;
}

std::wstring ToString(ClipboardContentKind kind) {
  switch (kind) {
    case ClipboardContentKind::Files: return L"files";
    case ClipboardContentKind::Text: return L"text";
    default: return L"unknown";
  }
}

ClipboardContentKind ClipboardContentKindFromString(const std::wstring& kind) {
  if (kind == L"files" || kind == L"FILES") return ClipboardContentKind::Files;
  if (kind == L"text" || kind == L"TEXT") return ClipboardContentKind::Text;
  return ClipboardContentKind::Unknown;
}

std::int64_t UnixNow() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::wstring FormatMenuLabel(const std::wstring& path) {
  std::wstring p = NormalizePathForDisplay(path);
  if (p.size() <= 48) return p;
  return L"..." + p.substr(p.size() - 45);
}

HistoryDatabase::HistoryDatabase(std::wstring storePath)
    : storePath_(storePath.empty() ? EncryptedRecordStore::DefaultStorePath() : std::move(storePath)) {}

bool HistoryDatabase::Load(std::wstring* error) {
  operations_.clear();
  pinned_.clear();
  clipboardEntries_.clear();
  EncryptedRecordStore store(storePath_);
  std::vector<std::wstring> records;
  if (!store.LoadRecords(&records, error)) return false;
  for (const auto& line : records) {
    OperationRecord op;
    if (ParseOperation(line, &op)) {
      operations_.push_back(std::move(op));
      continue;
    }
    PinnedTarget pin;
    if (ParsePin(line, &pin)) {
      pinned_.push_back(std::move(pin));
      continue;
    }
    ClipboardHistoryEntry clip;
    if (ParseClipboardEntry(line, &clip)) {
      clipboardEntries_.push_back(std::move(clip));
    }
  }
  return true;
}

bool HistoryDatabase::AppendOperation(const OperationRecord& record, std::wstring* error) {
  OperationRecord normalized = record;
  normalized.sourceParent = NormalizePathForDisplay(normalized.sourceParent);
  normalized.destParent = NormalizePathForDisplay(normalized.destParent);
  if (normalized.timestamp == 0) normalized.timestamp = UnixNow();
  EncryptedRecordStore store(storePath_);
  if (!store.AppendRecord(SerializeOperation(normalized), error)) return false;
  operations_.push_back(normalized);
  return true;
}

bool HistoryDatabase::AddPinnedTarget(const PinnedTarget& target, std::wstring* error) {
  PinnedTarget pin = target;
  pin.destParent = NormalizePathForDisplay(pin.destParent);
  if (pin.label.empty()) pin.label = FormatMenuLabel(pin.destParent);
  EncryptedRecordStore store(storePath_);
  if (!store.AppendRecord(SerializePin(pin), error)) return false;
  pinned_.push_back(pin);
  return true;
}

bool HistoryDatabase::RewriteAllRecords(std::wstring* error) const {
  std::vector<std::wstring> records;
  for (const auto& p : pinned_) records.push_back(SerializePin(p));
  for (const auto& op : operations_) records.push_back(SerializeOperation(op));
  for (const auto& entry : clipboardEntries_) records.push_back(SerializeClipboardEntry(entry));
  EncryptedRecordStore store(storePath_);
  return store.RewriteRecords(records, error);
}

bool HistoryDatabase::RemovePinnedTarget(std::size_t index, std::wstring* error) {
  if (index >= pinned_.size()) {
    if (error) *error = L"Pinned target index is out of range";
    return false;
  }
  auto removed = pinned_[index];
  pinned_.erase(pinned_.begin() + static_cast<std::ptrdiff_t>(index));
  if (!RewriteAllRecords(error)) {
    pinned_.insert(pinned_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
    return false;
  }
  return true;
}

bool HistoryDatabase::AppendClipboardEntry(const ClipboardHistoryEntry& entry, std::wstring* error) {
  ClipboardHistoryEntry normalized = entry;
  if (normalized.timestamp == 0) normalized.timestamp = UnixNow();
  if (normalized.id.empty()) normalized.id = MakeClipboardId(normalized);
  normalized.repeatCount = std::max(1, normalized.repeatCount);
  for (auto& file : normalized.files) file = NormalizePathForDisplay(file);

  if (!clipboardEntries_.empty()) {
    ClipboardHistoryEntry& last = clipboardEntries_.back();
    if (IsSameClipboardOperation(last, normalized)) {
      ClipboardHistoryEntry before = last;
      last.timestamp = normalized.timestamp;
      last.sequence = normalized.sequence;
      last.selected = last.selected || normalized.selected;
      if (normalized.selected) last.stale = false;
      last.repeatCount = std::max(1, last.repeatCount) + 1;
      if (!normalized.sourceApp.empty()) last.sourceApp = normalized.sourceApp;
      if (!RewriteAllRecords(error)) {
        last = std::move(before);
        return false;
      }
      return true;
    }
  }

  if (normalized.kind == ClipboardContentKind::Files && normalized.selected) {
    auto before = clipboardEntries_;
    for (auto& existing : clipboardEntries_) {
      if (existing.kind == ClipboardContentKind::Files && existing.selected && !existing.stale) {
        existing.selected = false;
        existing.stale = true;
      }
    }
    clipboardEntries_.push_back(std::move(normalized));
    if (!RewriteAllRecords(error)) {
      clipboardEntries_ = std::move(before);
      return false;
    }
    return true;
  }

  EncryptedRecordStore store(storePath_);
  if (!store.AppendRecord(SerializeClipboardEntry(normalized), error)) return false;
  clipboardEntries_.push_back(std::move(normalized));
  return true;
}

bool HistoryDatabase::UpdateClipboardEntry(const ClipboardHistoryEntry& entry, std::wstring* error) {
  for (auto& existing : clipboardEntries_) {
    if (existing.id != entry.id) continue;
    existing = entry;
    existing.repeatCount = std::max(1, existing.repeatCount);
    for (auto& file : existing.files) file = NormalizePathForDisplay(file);
    return RewriteAllRecords(error);
  }
  if (error) *error = L"Clipboard entry not found";
  return false;
}

bool HistoryDatabase::SetClipboardEntriesSelected(const std::vector<std::wstring>& ids, bool selected, std::wstring* error) {
  if (ids.empty()) return true;
  bool changed = false;
  for (auto& entry : clipboardEntries_) {
    if (std::find(ids.begin(), ids.end(), entry.id) == ids.end()) continue;
    if (entry.selected != selected) {
      entry.selected = selected;
      if (selected) entry.stale = false;
      changed = true;
    }
  }
  return changed ? RewriteAllRecords(error) : true;
}

bool HistoryDatabase::ClearSelectedFileClipboardEntries(std::wstring* error) {
  bool changed = false;
  for (auto& entry : clipboardEntries_) {
    if (entry.kind == ClipboardContentKind::Files && entry.selected) {
      entry.selected = false;
      changed = true;
    }
  }
  return changed ? RewriteAllRecords(error) : true;
}

bool HistoryDatabase::MarkSelectedFileClipboardEntriesStale(std::wstring* error) {
  bool changed = false;
  for (auto& entry : clipboardEntries_) {
    if (entry.kind == ClipboardContentKind::Files && entry.selected && !entry.stale) {
      entry.selected = false;
      entry.stale = true;
      changed = true;
    }
  }
  return changed ? RewriteAllRecords(error) : true;
}

bool HistoryDatabase::HasClipboardEntry(std::uint32_t sequence, ClipboardContentKind kind) const {
  for (const auto& entry : clipboardEntries_) {
    if (entry.sequence == sequence && entry.kind == kind) return true;
  }
  return false;
}

std::vector<ClipboardHistoryEntry> HistoryDatabase::GetSelectedFileClipboardEntries(OperationKind op) const {
  std::vector<ClipboardHistoryEntry> out;
  for (const auto& entry : clipboardEntries_) {
    if (entry.kind != ClipboardContentKind::Files || !entry.selected || entry.stale || entry.files.empty()) continue;
    if (!OpMatches(op, entry.op)) continue;
    out.push_back(entry);
  }
  std::sort(out.begin(), out.end(), [](const ClipboardHistoryEntry& a, const ClipboardHistoryEntry& b) {
    if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
    return a.sequence < b.sequence;
  });
  return out;
}

std::vector<MenuCandidate> HistoryDatabase::GetCandidates(const std::wstring& sourceParent, OperationKind op, std::size_t maxItems) const {
  struct Agg {
    std::wstring dest;
    std::wstring label;
    double score = 0.0;
    int count = 0;
    bool pinned = false;
    bool supportsCopy = false;
    bool supportsMove = false;
  };
  std::map<std::wstring, Agg> agg;
  std::int64_t now = UnixNow();
  bool hasSource = !sourceParent.empty();
  std::wstring normalizedSource = hasSource ? NormalizePathForDisplay(sourceParent) : L"";

  for (const auto& pin : pinned_) {
    if (!RouteMatches(op, pin.op)) continue;
    auto& a = agg[CandidateKey(pin.destParent)];
    a.dest = pin.destParent;
    if (a.label.empty()) a.label = pin.label;
    AddSupportedOperation(OperationKind::Both, &a.supportsCopy, &a.supportsMove);
    a.pinned = true;
    a.score += 1000.0 + pin.priority;
  }

  for (const auto& r : operations_) {
    if (!RouteMatches(op, r.op)) continue;
    if (r.result != L"success" && r.result != L"inferred") continue;
    double ageDays = static_cast<double>(now - r.timestamp) / 86400.0;
    double recency = std::exp(-ageDays / 90.0);
    double exact = hasSource && IsSamePathCaseInsensitive(normalizedSource, r.sourceParent) ? 1.0 : 0.0;
    double ancestor = hasSource ? static_cast<double>(CommonPrefixPathDepth(normalizedSource, r.sourceParent)) : 0.0;
    auto& a = agg[CandidateKey(r.destParent)];
    a.dest = r.destParent;
    AddSupportedOperation(OperationKind::Both, &a.supportsCopy, &a.supportsMove);
    a.count += 1;
    a.score += 700.0 * exact + 35.0 * ancestor + 90.0 * recency + 60.0 * r.confidence;
  }

  std::vector<MenuCandidate> out;
  for (const auto& [_, a] : agg) {
    MenuCandidate c;
    c.op = CandidateOperation(op, a.supportsCopy, a.supportsMove);
    c.destParent = a.dest;
    c.label = a.label.empty() ? FormatMenuLabel(a.dest) : a.label;
    c.score = a.score + std::log(1.0 + a.count) * 70.0;
    c.pinned = a.pinned;
    if (DirectoryExists(c.destParent)) c.score += 30.0;
    else c.score -= 300.0;
    out.push_back(std::move(c));
  }
  std::sort(out.begin(), out.end(), [](const MenuCandidate& a, const MenuCandidate& b) { return a.score > b.score; });
  if (out.size() > maxItems) out.resize(maxItems);
  return out;
}

std::vector<MenuCandidate> HistoryDatabase::GetGlobalCandidates(OperationKind op, std::size_t maxItems) const {
  return GetCandidates(L"", op, maxItems);
}

bool HistoryDatabase::WriteMenuCache(const std::wstring& cachePath, std::wstring* error) const {
  std::wstring path = cachePath.empty() ? EncryptedRecordStore::DefaultMenuCachePath() : cachePath;
  EnsureDirectory(EncryptedRecordStore::DefaultCacheDirectory());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error) *error = L"Unable to write menu cache: " + path;
    return false;
  }
  auto writeLine = [&file](const std::wstring& line) {
    std::string utf8 = WideToUtf8(line);
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    file.put('\n');
  };
  writeLine(L"# ClipCue legacy cache. Plaintext by design for Explorer menu speed; disable by deleting this file.");
  std::map<std::wstring, bool> sources;
  for (const auto& op : operations_) sources[op.sourceParent] = true;
  for (const auto& [source, _] : sources) {
    for (auto kind : {OperationKind::Move, OperationKind::Copy}) {
      auto candidates = GetCandidates(source, kind, 6);
      for (const auto& c : candidates) {
        writeLine(HashPathForCache(source) + L"\t" + ToString(kind) + L"\t" + c.label + L"\t" + c.destParent);
      }
    }
  }
  for (auto kind : {OperationKind::Move, OperationKind::Copy}) {
    auto candidates = GetGlobalCandidates(kind, 8);
    for (const auto& c : candidates) {
      writeLine(L"*\t" + ToString(kind) + L"\t" + c.label + L"\t" + c.destParent);
    }
  }
  std::map<OperationKind, int> queuedFiles;
  int fileQueueHistoryCount = 0;
  int textHistoryCount = 0;
  for (const auto& entry : clipboardEntries_) {
    if (entry.kind == ClipboardContentKind::Files) {
      ++fileQueueHistoryCount;
      if (entry.selected && !entry.stale && (entry.op == OperationKind::Copy || entry.op == OperationKind::Move)) {
        queuedFiles[entry.op] += static_cast<int>(entry.files.size());
      }
    } else if (entry.kind == ClipboardContentKind::Text) {
      ++textHistoryCount;
    }
  }
  for (const auto& [kind, count] : queuedFiles) {
    if (count > 0) writeLine(L"QUEUE\t" + ToString(kind) + L"\t" + std::to_wstring(count));
  }
  if (fileQueueHistoryCount > 0) writeLine(L"QUEUE_HISTORY\t" + std::to_wstring(fileQueueHistoryCount));
  if (textHistoryCount > 0) writeLine(L"TEXT\t" + std::to_wstring(textHistoryCount));
  return true;
}

bool HistoryDatabase::CleanupExpired(int operationDays, int, std::wstring* error) {
  const std::int64_t cutoff = UnixNow() - static_cast<std::int64_t>(operationDays) * 86400;
  std::vector<std::wstring> records;
  for (const auto& p : pinned_) records.push_back(SerializePin(p));
  for (const auto& op : operations_) {
    if (op.timestamp >= cutoff) records.push_back(SerializeOperation(op));
  }
  for (const auto& entry : clipboardEntries_) {
    if (entry.selected || entry.timestamp >= cutoff) records.push_back(SerializeClipboardEntry(entry));
  }
  EncryptedRecordStore store(storePath_);
  return store.RewriteRecords(records, error);
}

}  // namespace clipcue
