#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pathcue {

enum class OperationKind { Copy, Move, Both, Unknown };

std::wstring ToString(OperationKind op);
OperationKind OperationFromString(const std::wstring& op);

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

class HistoryDatabase {
 public:
  explicit HistoryDatabase(std::wstring storePath = L"");

  bool Load(std::wstring* error = nullptr);
  bool AppendOperation(const OperationRecord& record, std::wstring* error = nullptr);
  bool AddPinnedTarget(const PinnedTarget& target, std::wstring* error = nullptr);
  bool RemovePinnedTarget(std::size_t index, std::wstring* error = nullptr);

  std::vector<MenuCandidate> GetCandidates(const std::wstring& sourceParent,
                                           OperationKind op,
                                           std::size_t maxItems = 8) const;
  bool WriteMenuCache(const std::wstring& cachePath = L"", std::wstring* error = nullptr) const;
  bool CleanupExpired(int operationDays, int routeDays, std::wstring* error = nullptr);

  const std::vector<OperationRecord>& operations() const { return operations_; }
  const std::vector<PinnedTarget>& pinnedTargets() const { return pinned_; }

 private:
  std::wstring storePath_;
  std::vector<OperationRecord> operations_;
  std::vector<PinnedTarget> pinned_;
};

std::int64_t UnixNow();
std::wstring FormatMenuLabel(const std::wstring& path);

}  // namespace pathcue
