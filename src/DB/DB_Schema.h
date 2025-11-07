#pragma once
#include "../Types.hpp"
#include <optional>

namespace Docmasys::DB
{

  /// @brief Status of the @ref Blob
  /// - is the file upload still pending or ready.
  enum class BlobStatus : std::uint8_t
  {
    Pending = 0,
    Ready = 1
  };

  /// @brief Database entry corresponding to a CAS file
  struct Blob
  {
    Blob(const ID &id, const Identity &hash, const BlobStatus &status)
        : Id(id),
          Hash(hash),
          Status(status)
    {
    }

    const ID Id{};
    const Identity Hash{};
    const BlobStatus Status{BlobStatus::Pending};
  };

  /// @brief A folder that has a name and a parent.
  /// Only the root folder doesn't have a parent.
  struct Folder
  {
    Folder(ID id, std::optional<ID> parent_id, const std::string &name)
        : Id(id),
          ParentId(parent_id),
          Name(name)
    {
    }

    const ID Id{};
    const std::optional<ID> ParentId;
    const std::string Name;
  };

  /// @brief Representation of a @ref Blob in a specific @ref Folder by some name.
  struct File
  {
    File(ID id, std::optional<ID> parent_id, ID blobId, const std::string &name)
        : Id(id),
          ParentId(parent_id),
          BlobId(blobId),
          Name(name)
    {
    }

    const ID Id{};
    const std::optional<ID> ParentId;
    const ID BlobId{};
    const std::string Name;
  };

  /// @brief The Database Schema that is always executed on DB load.
  static const char *DB_SCHEMA = R"SQL(

    CREATE TABLE IF NOT EXISTS blobs (
      id        INTEGER PRIMARY KEY,
      hash      BLOB NOT NULL CHECK (length(hash) = 32),
      status    INT NOT NULL CHECK (status IN (0,1)), -- 0 not ready, 1 ready
      UNIQUE(hash)
    );

    CREATE INDEX IF NOT EXISTS idx_blobs ON blobs(hash);

    CREATE TABLE IF NOT EXISTS folders (
      id        INTEGER PRIMARY KEY,
      parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,
      name      TEXT NOT NULL COLLATE NOCASE
    );

    CREATE UNIQUE INDEX IF NOT EXISTS uq_folders_parent_name
      ON folders(parent_id, name)
      WHERE parent_id IS NOT NULL;

    CREATE UNIQUE INDEX IF NOT EXISTS uq_folders_root_name
      ON folders(name)
      WHERE parent_id IS NULL;

    CREATE INDEX IF NOT EXISTS idx_folders_parent ON folders(parent_id);

    CREATE TABLE IF NOT EXISTS files (
      id        INTEGER PRIMARY KEY,
      parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,
      name      TEXT NOT NULL COLLATE NOCASE,
      blob_id   INTEGER NOT NULL REFERENCES blobs(id) ON DELETE RESTRICT,
      UNIQUE(parent_id, name)
    );

    CREATE INDEX IF NOT EXISTS idx_files_parent ON files(parent_id);
    CREATE INDEX IF NOT EXISTS idx_files_blob ON files(blob_id);

    -- After DELETE on files: remove orphaned blob
    CREATE TRIGGER IF NOT EXISTS trg_files_ad_del_orphan_blob
    AFTER DELETE ON files
    BEGIN
      DELETE FROM blobs
      WHERE id = OLD.blob_id
      AND NOT EXISTS (SELECT 1 FROM files WHERE blob_id = OLD.blob_id);
    END;

    -- After UPDATE of blob_id: remove old blob if now orphaned
    CREATE TRIGGER IF NOT EXISTS trg_files_au_blob_orphan
    AFTER UPDATE OF blob_id ON files
    BEGIN
      DELETE FROM blobs
      WHERE id = OLD.blob_id
      AND NOT EXISTS (SELECT 1 FROM files WHERE blob_id = OLD.blob_id);
    END;
  
  )SQL";
}