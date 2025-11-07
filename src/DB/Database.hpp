#pragma once
#include <memory>
#include <filesystem>
#include "DB_Schema.h"
#include "../Types.hpp"
#include <vector>
#include <unordered_map>

namespace Docmasys::DB
{
  class Database
  {

  public:
    [[nodiscard]] static std::unique_ptr<Database> Open(const std::filesystem::path &databaseFile, const std::filesystem::path &localVaultRoot)
    {
      return std::unique_ptr<Database>(new Database(databaseFile, localVaultRoot));
    }

  public:
    ~Database();

    [[nodiscard]] inline const std::filesystem::path &DatabaseFile() const noexcept
    {
      return m_DatabaseFile;
    }

    [[nodiscard]] inline const std::filesystem::path &VaultRoot() const noexcept
    {
      return m_LocalVaultRoot;
    }

    std::shared_ptr<Blob> Import(const std::filesystem::path &file, const Identity &blobHash);
    std::shared_ptr<Blob> UpdateBlobStatus(const std::shared_ptr<Blob> &blob, const BlobStatus &newStatus);
    std::vector<std::shared_ptr<File>> GetFiles(const std::shared_ptr<Folder> &folder);
    std::vector<std::shared_ptr<Folder>> GetFolders(const std::shared_ptr<Folder> &folder);
    std::unordered_map<std::shared_ptr<File>, std::shared_ptr<Blob>> GetFilesAndBlobs(const std::shared_ptr<Folder> &folder);

  private:
    Database(const std::filesystem::path &databaseFile, const std::filesystem::path &localVaultRoot);

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&) = delete;
    Database &operator=(Database &&) = delete;

    void ExecSQL(const char *sql);

    void OpenTransaction();
    void Commit();
    void Rollback();

    std::shared_ptr<Blob> GetBlobByHashOrId(const std::optional<ID> &id, const std::optional<Identity> &blobHash);
    std::shared_ptr<Folder> GetOrCreateFolder(const std::string &name, const std::shared_ptr<Folder> &parent);
    std::shared_ptr<Blob> GetOrCreateBlob(const Identity &blobHash);
    std::shared_ptr<File> GetOrCreateFile(const std::string &name, const std::shared_ptr<Folder> &folder, const std::shared_ptr<Blob> &blob);

    bool TryGetRelativePath(const std::filesystem::path &file, std::filesystem::path &outRelative) const;
    std::shared_ptr<Blob> InsertToDB(const std::filesystem::path &relativeFilePath, const Identity &blobHash);

  private:
    const std::filesystem::path m_DatabaseFile;
    const std::filesystem::path m_LocalVaultRoot;
    struct Impl;
    std::unique_ptr<Impl> m_Database;
  };
}