#pragma once

#include <string>
#include <vector>

namespace clipcue {

std::wstring NormalizePathForDisplay(const std::wstring& path);
std::wstring ParentPath(const std::wstring& path);
std::wstring FileNameFromPath(const std::wstring& path);
std::wstring RemoveTrailingSlashes(const std::wstring& path);
std::wstring MakeDestinationPath(const std::wstring& targetDir, const std::wstring& sourcePath);
std::wstring MakeUniquePath(const std::wstring& path);
std::wstring HashPathForCache(const std::wstring& path);

bool IsLikelyDirectoryPath(const std::wstring& path);
bool IsSamePathCaseInsensitive(const std::wstring& a, const std::wstring& b);

std::wstring JoinLines(const std::vector<std::wstring>& lines);
std::vector<std::wstring> SplitLines(const std::wstring& text);

}  // namespace clipcue
