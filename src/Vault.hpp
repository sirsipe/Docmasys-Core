#pragma once
#include "DB/Database.hpp"
#include "Extensions/Extension.hpp"
#include <filesystem>
#include <optional>
#include <vector>

namespace Docmasys
{
  struct MaterializationOptions
  {
    std::filesystem::path RelativeFilePath;
    std::optional<std::int64_t> VersionNumber;
    DB::RelationScope RelationScope{DB::RelationScope::None};
  };

  class Vault
  {
  public:
    Vault(const std::filesystem::path &root, const std::filesystem::path &archive);
    void Push();
    void Pop();
    void Pop(const MaterializationOptions &options);

  private:
    void MaterializeFiles(const std::vector<DB::MaterializedFile> &files);
    void MaterializeFolderTree(const std::shared_ptr<DB::Folder> &folder, const std::filesystem::path &localFolder);

    std::unique_ptr<DB::Database> m_Database;
    const std::filesystem::path m_LocalRoot;
    const std::filesystem::path m_ArchiveRoot;
    Extensions::ImportExtensionRegistry m_Extensions;
  };
}
