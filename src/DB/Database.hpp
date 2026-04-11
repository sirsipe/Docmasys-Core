#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "../Types.hpp"
#include "DB_Schema.h"

namespace Docmasys::DB
{
  struct VersionRelationView
  {
    std::shared_ptr<FileVersion> From;
    std::shared_ptr<FileVersion> To;
    RelationType Type;
  };

  class Database
  {
  public:
    [[nodiscard]] static std::unique_ptr<Database> Open(const std::filesystem::path &databaseFile, const std::filesystem::path &localVaultRoot)
    {
      return std::unique_ptr<Database>(new Database(databaseFile, localVaultRoot));
    }

    ~Database();
    [[nodiscard]] inline const std::filesystem::path &DatabaseFile() const noexcept { return m_DatabaseFile; }
    [[nodiscard]] inline const std::filesystem::path &VaultRoot() const noexcept { return m_LocalVaultRoot; }

    ImportResult Import(const std::filesystem::path &file, const Identity &blobHash);
    std::shared_ptr<Blob> UpdateBlobStatus(const std::shared_ptr<Blob> &blob, const BlobStatus &newStatus);
    std::shared_ptr<Blob> GetBlob(ID blobId);
    std::vector<std::shared_ptr<Folder>> GetFolders(const std::shared_ptr<Folder> &folder);
    std::vector<MaterializedFile> GetMaterializedFiles(const std::shared_ptr<Folder> &folder);
    std::shared_ptr<File> GetFileByRelativePath(const std::filesystem::path &relativeFilePath);
    std::shared_ptr<File> GetFileById(ID fileId);
    std::shared_ptr<FileVersion> GetFileVersion(const std::shared_ptr<File> &file, const std::optional<std::int64_t> &versionNumber);
    std::vector<std::shared_ptr<FileVersion>> GetFileVersions(const std::shared_ptr<File> &file);
    std::vector<VersionRelationView> GetOutgoingRelations(const std::shared_ptr<FileVersion> &from, std::optional<RelationType> typeFilter);
    std::vector<MaterializedFile> ResolveMaterialization(const std::shared_ptr<FileVersion> &rootVersion, RelationScope scope);
    void AddRelation(const std::shared_ptr<FileVersion> &from, const std::shared_ptr<FileVersion> &to, RelationType type);
    std::filesystem::path BuildRelativePath(const std::shared_ptr<File> &file);
    std::vector<MaterializedFile> InspectCurrentFiles();
    void SetVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name, const PropertyValue &value);
    std::optional<VersionProperty> GetVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name);
    std::vector<VersionProperty> ListVersionProperties(const std::shared_ptr<FileVersion> &version);
    bool RemoveVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name);

    void UpsertWorkspaceEntry(const std::filesystem::path &workspaceRoot,
                              const std::shared_ptr<File> &file,
                              const std::shared_ptr<FileVersion> &version,
                              const std::filesystem::path &relativePath,
                              MaterializationKind kind);
    std::optional<WorkspaceEntry> GetWorkspaceEntry(const std::filesystem::path &workspaceRoot,
                                                    const std::shared_ptr<File> &file);
    std::vector<WorkspaceEntry> ListWorkspaceEntries(const std::filesystem::path &workspaceRoot);
    void AcquireCheckoutLock(const std::shared_ptr<File> &file,
                             const std::shared_ptr<FileVersion> &version,
                             const std::string &user,
                             const std::string &environment,
                             const std::filesystem::path &workspaceRoot);
    std::optional<CheckoutLock> GetCheckoutLock(const std::shared_ptr<File> &file);
    std::vector<CheckoutLock> ListCheckoutLocks();
    bool ReleaseCheckoutLock(const std::shared_ptr<File> &file,
                             const std::string &user,
                             const std::string &environment,
                             const std::filesystem::path &workspaceRoot);
    bool ForceReleaseCheckoutLock(const std::shared_ptr<File> &file);
    std::vector<WorkspaceEntryStatus> GetWorkspaceStatus(const std::filesystem::path &workspaceRoot);

  private:
    Database(const std::filesystem::path &databaseFile, const std::filesystem::path &localVaultRoot);
    void ExecSQL(const char *sql); void OpenTransaction(); void Commit(); void Rollback(); void EnsureSchema(); void MigrateLegacySchemaIfNeeded(); void MigrateSchemaIfNeeded();
    std::shared_ptr<Blob> GetBlobByHashOrId(const std::optional<ID> &id, const std::optional<Identity> &blobHash);
    std::shared_ptr<Folder> GetOrCreateFolder(const std::string &name, const std::shared_ptr<Folder> &parent);
    std::shared_ptr<Blob> GetOrCreateBlob(const Identity &blobHash);
    std::shared_ptr<File> GetOrCreateFile(const std::string &name, const std::shared_ptr<Folder> &folder);
    std::shared_ptr<FileVersion> CreateFileVersion(const std::shared_ptr<File> &file, const std::shared_ptr<Blob> &blob);
    std::shared_ptr<File> SetCurrentVersion(const std::shared_ptr<File> &file, const std::shared_ptr<FileVersion> &version);
    std::shared_ptr<Folder> GetFolderById(ID folderId);
    bool TryGetRelativePath(const std::filesystem::path &file, std::filesystem::path &outRelative) const;
    ImportResult InsertToDB(const std::filesystem::path &relativeFilePath, const Identity &blobHash);

    Database(const Database &) = delete; Database &operator=(const Database &) = delete; Database(Database &&) = delete; Database &operator=(Database &&) = delete;
    const std::filesystem::path m_DatabaseFile; const std::filesystem::path m_LocalVaultRoot; struct Impl; std::unique_ptr<Impl> m_Database;
  };
}
