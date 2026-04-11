#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Docmasys::Common
{
  std::filesystem::path EnsureRootedVaultPath(const std::filesystem::path &path);
  std::string CanonicalWorkspaceRoot(const std::filesystem::path &path);
  bool TryMakeVaultRelativePath(const std::filesystem::path &vaultRoot,
                                const std::filesystem::path &file,
                                std::filesystem::path &outRelative);
}
