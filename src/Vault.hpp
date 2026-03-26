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
    DB::MaterializationKind Kind{DB::MaterializationKind::ReadOnlyCopy};
  };

  struct CheckoutOptions
  {
    std::filesystem::path RelativeFilePath;
    std::optional<std::int64_t> VersionNumber;
    DB::RelationScope RelationScope{DB::RelationScope::None};
    std::string User;
    std::string Environment;
  };

  struct CheckinOptions
  {
    std::filesystem::path RelativeFilePath;
    std::string User;
    std::string Environment;
    bool ReleaseLock{true};
  };

  class Vault
  {
  public:
    Vault(const std::filesystem::path &root, const std::filesystem::path &archive);
    void Push();
    void Pop();
    void Pop(const MaterializationOptions &options);
    void Checkout(const CheckoutOptions &options);
    void Checkin(const CheckinOptions &options);
    std::vector<DB::WorkspaceEntryStatus> Status() const;
    void Repair();
    void Unlock(const std::filesystem::path &relativeFilePath);

  private:
    void MaterializeFiles(const std::vector<DB::MaterializedFile> &files, DB::MaterializationKind kind);
    void MaterializeFolderTree(const std::shared_ptr<DB::Folder> &folder, const std::filesystem::path &localFolder, DB::MaterializationKind kind);

    std::unique_ptr<DB::Database> m_Database;
    const std::filesystem::path m_LocalRoot;
    const std::filesystem::path m_ArchiveRoot;
    Extensions::ImportExtensionRegistry m_Extensions;
  };
}
