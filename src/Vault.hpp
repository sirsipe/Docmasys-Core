#pragma once
#include "DB/Database.hpp"
#include <filesystem>
#include <vector>

namespace Docmasys
{
  class Vault
  {
  public:
    Vault(const std::filesystem::path &root, const std::filesystem::path &archive);
    void Push();
    void Pop();

  private:
    void MaterializeFiles(const std::vector<DB::MaterializedFile> &files);
    void MaterializeFolderTree(const std::shared_ptr<DB::Folder> &folder, const std::filesystem::path &localFolder);

    std::unique_ptr<DB::Database> m_Database;
    const std::filesystem::path m_LocalRoot;
    const std::filesystem::path m_ArchiveRoot;
  };
}
