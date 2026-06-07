#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace clipcue {

enum class OperationKind { Copy, Move, Both, Unknown };
enum class ClipboardContentKind { Files, Text, Unknown };

std::wstring ToString(OperationKind op);
OperationKind OperationFromString(const std::wstring& op);
std::wstring ToString(ClipboardContentKind kind);
ClipboardContentKind ClipboardContentKindFromString(const std::wstring& kind);

struct OperationRecord {
  std::int64_t timestamp = 0;
  OperationKind op = OperationKind::Unknown;
  std::wstring sourceParent;
  std::wstring destParent;
  std::wstring result = L"success";
  double confidence = 1.0;
  std::wstring observedBy = L"own_ifileoperation";
  std::wstring conflictPolicy = L"ask";
};

struct PinnedTarget {
  OperationKind op = OperationKind::Both;
  std::wstring destParent;
  std::wstring label;
  int priority = 100;
};

struct MenuCandidate {
  OperationKind op = OperationKind::Unknown;
  std::wstring destParent;
  std::wstring label;
  double score = 0.0;
  bool pinned = false;
};

struct ClipboardHistoryEntry {
  std::wstring id;
  std::int64_t timestamp = 0;
  std::uint32_t sequence = 0;
  ClipboardContentKind kind = ClipboardContentKind::Unknown;
  OperationKind op = OperationKind::Unknown;
  bool selected = false;
  bool stale = false;
  int repeatCount = 1;
  std::wstring text;
  std::vector<std::wstring> files;
  std::wstring sourceApp;
};

class HistoryDatabase {
 public:
  explicit HistoryDatabase(std::wstring storePath = L"");

  bool Load(std::wstring* error = nullptr);
  bool AppendOperation(const OperationRecord& record, std::wstring* error = nullptr);
  bool AddPinnedTarget(const PinnedTarget& target, std::wstring* error = nullptr);
  bool RemovePinnedTarget(std::size_t index, std::wstring* error = nullptr);
  bool AppendClipboardEntry(const ClipboardHistoryEntry& entry, std::wstring* error = nullptr);
  bool UpdateClipboardEntry(const ClipboardHistoryEntry& entry, std::wstring* error = nullptr);
  bool SetClipboardEntriesSelected(const std::vector<std::wstring>& ids, bool selected, std::wstring* error = nullptr);
  bool ClearSelectedFileClipboardEntries(std::wstring* error = nullptr);
  bool MarkSelectedFileClipboardEntriesStale(std::wstring* error = nullptr);
  bool HasClipboardEntry(std::uint32_t sequence, ClipboardContentKind kind) const;

  std::vector<MenuCandidate> GetCandidates(const std::wstring& sourceParent,
                                           OperationKind op,
                                           std::size_t maxItems = 8) const;
  std::vector<MenuCandidate> GetGlobalCandidates(OperationKind op,
                                                 std::size_t maxItems = 8) const;
  std::vector<ClipboardHistoryEntry> GetSelectedFileClipboardEntries(OperationKind op = OperationKind::Both) const;
  bool WriteMenuCache(const std::wstring& cachePath = L"", std::wstring* error = nullptr) const;
  bool CleanupExpired(int operationDays, int routeDays, std::wstring* error = nullptr);

  const std::vector<OperationRecord>& operations() const { return operations_; }
  const std::vector<PinnedTarget>& pinnedTargets() const { return pinned_; }
  const std::vector<ClipboardHistoryEntry>& clipboardEntries() const { return clipboardEntries_; }

 private:
  bool RewriteAllRecords(std::wstring* error = nullptr) const;

  std::wstring storePath_;
  std::vector<OperationRecord> operations_;
  std::vector<PinnedTarget> pinned_;
  std::vector<ClipboardHistoryEntry> clipboardEntries_;
};

std::int64_t UnixNow();
std::wstring FormatMenuLabel(const std::wstring& path);

}  // namespace clipcue
