#include "PathUtils.hpp"

#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;

namespace Docmasys::Common
{
  fs::path EnsureRootedVaultPath(const fs::path &path)
  {
    auto normalized = path.lexically_normal();
    if (!normalized.empty() && normalized.begin()->string() == "ROOT")
      return normalized;
    return fs::path("ROOT") / normalized;
  }

  fs::path RequireRootedVaultPath(const fs::path &path, const char *errorMessage)
  {
    const auto normalized = path.lexically_normal();
    if (normalized.empty())
      throw std::runtime_error(errorMessage);
    return EnsureRootedVaultPath(normalized);
  }

  fs::path WorkspacePathFromVaultPath(const fs::path &rootedVaultPath)
  {
    return RequireRootedVaultPath(rootedVaultPath).lexically_relative("ROOT");
  }

  std::string CanonicalWorkspaceRoot(const fs::path &path)
  {
    return fs::weakly_canonical(path).generic_string();
  }

  bool TryMakeVaultRelativePath(const fs::path &vaultRoot,
                                const fs::path &file,
                                fs::path &outRelative)
  {
    fs::path root;
    fs::path full;
    try
    {
      root = fs::weakly_canonical(vaultRoot);
      full = fs::weakly_canonical(file);
    }
    catch (...)
    {
      return false;
    }

    const auto mismatch = std::mismatch(root.begin(), root.end(), full.begin(), full.end());
    if (mismatch.first != root.end())
      return false;

    outRelative = fs::path("ROOT") / fs::relative(full, root);
    return true;
  }
}
