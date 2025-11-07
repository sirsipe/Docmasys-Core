#pragma once
#include "DB/Database.hpp"
#include <filesystem>

namespace Docmasys
{
  class Vault
  {
  public:
    Vault(const std::filesystem::path &root, const std::filesystem::path &archive);

    void Push();
    void Pop();

  private:
    void Materialize(const std::shared_ptr<DB::Folder> &folder, const std::filesystem::path &localFolder);

  private:
    std::unique_ptr<DB::Database> m_Database;
    const std::filesystem::path m_LocalRoot;
    const std::filesystem::path m_ArchiveRoot;
  };
}