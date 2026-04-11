#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Docmasys::Common
{
  std::filesystem::path EnsureRootedVaultPath(const std::filesystem::path &path);
  std::filesystem::path RequireRootedVaultPath(const std::filesystem::path &path, const char *errorMessage = "relative file path is required");
  std::filesystem::path WorkspacePathFromVaultPath(const std::filesystem::path &rootedVaultPath);
  std::string CanonicalWorkspaceRoot(const std::filesystem::path &path);
  bool TryMakeVaultRelativePath(const std::filesystem::path &vaultRoot,
                                const std::filesystem::path &file,
                                std::filesystem::path &outRelative);
}
