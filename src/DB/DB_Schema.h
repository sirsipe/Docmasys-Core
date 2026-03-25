#pragma once
#include "../Types.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Docmasys::DB
{
  enum class BlobStatus : std::uint8_t { Pending = 0, Ready = 1 };
  enum class RelationType : std::uint8_t { Strong = 0, Weak = 1, Optional = 2 };
  enum class RelationScope : std::uint8_t { None = 0, Strong = 1, StrongAndWeak = 2, All = 3 };

  struct Blob { Blob(const ID &id, const Identity &hash, const BlobStatus &status): Id(id), Hash(hash), Status(status) {} ID Id{}; Identity Hash{}; BlobStatus Status{BlobStatus::Pending}; };
  struct Folder { Folder(ID id, std::optional<ID> parent_id, const std::string &name): Id(id), ParentId(parent_id), Name(name) {} ID Id{}; std::optional<ID> ParentId; std::string Name; };
  struct File { File(ID id, std::optional<ID> parent_id, const std::string &name, std::optional<ID> currentVersionId): Id(id), ParentId(parent_id), Name(name), CurrentVersionId(currentVersionId) {} ID Id{}; std::optional<ID> ParentId; std::string Name; std::optional<ID> CurrentVersionId; };
  struct FileVersion { FileVersion(ID id, ID fileId, ID blobId, std::int64_t versionNumber): Id(id), FileId(fileId), BlobId(blobId), VersionNumber(versionNumber) {} ID Id{}; ID FileId{}; ID BlobId{}; std::int64_t VersionNumber{}; };
  struct MaterializedFile { std::shared_ptr<File> LogicalFile; std::shared_ptr<FileVersion> Version; std::shared_ptr<Blob> BlobRef; std::filesystem::path RelativePath; };

  static constexpr int DB_SCHEMA_VERSION = 1;
  static const char *DB_SCHEMA_LEGACY = R"SQL(
    CREATE TABLE IF NOT EXISTS blobs (id INTEGER PRIMARY KEY, hash BLOB NOT NULL CHECK (length(hash) = 32), status INT NOT NULL CHECK (status IN (0,1)), UNIQUE(hash));
    CREATE TABLE IF NOT EXISTS folders (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE, name TEXT NOT NULL COLLATE NOCASE);
    CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE, name TEXT NOT NULL COLLATE NOCASE, blob_id INTEGER NOT NULL REFERENCES blobs(id) ON DELETE RESTRICT, UNIQUE(parent_id, name));
  )SQL";
  static const char *DB_SCHEMA = R"SQL(
    CREATE TABLE IF NOT EXISTS blobs (id INTEGER PRIMARY KEY, hash BLOB NOT NULL CHECK (length(hash) = 32), status INT NOT NULL CHECK (status IN (0,1)), UNIQUE(hash));
    CREATE INDEX IF NOT EXISTS idx_blobs ON blobs(hash);
    CREATE TABLE IF NOT EXISTS folders (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE, name TEXT NOT NULL COLLATE NOCASE);
    CREATE UNIQUE INDEX IF NOT EXISTS uq_folders_parent_name ON folders(parent_id, name) WHERE parent_id IS NOT NULL;
    CREATE UNIQUE INDEX IF NOT EXISTS uq_folders_root_name ON folders(name) WHERE parent_id IS NULL;
    CREATE INDEX IF NOT EXISTS idx_folders_parent ON folders(parent_id);
    CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE, name TEXT NOT NULL COLLATE NOCASE, current_version_id INTEGER, UNIQUE(parent_id, name), FOREIGN KEY(current_version_id) REFERENCES file_versions(id) ON DELETE RESTRICT);
    CREATE INDEX IF NOT EXISTS idx_files_parent ON files(parent_id);
    CREATE INDEX IF NOT EXISTS idx_files_current_version ON files(current_version_id);
    CREATE TABLE IF NOT EXISTS file_versions (id INTEGER PRIMARY KEY, file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, version_number INTEGER NOT NULL, blob_id INTEGER NOT NULL REFERENCES blobs(id) ON DELETE RESTRICT, UNIQUE(file_id, version_number));
    CREATE INDEX IF NOT EXISTS idx_file_versions_file ON file_versions(file_id);
    CREATE INDEX IF NOT EXISTS idx_file_versions_blob ON file_versions(blob_id);
    CREATE TABLE IF NOT EXISTS version_relations (from_version_id INTEGER NOT NULL REFERENCES file_versions(id) ON DELETE CASCADE, to_version_id INTEGER NOT NULL REFERENCES file_versions(id) ON DELETE CASCADE, relation_type INTEGER NOT NULL CHECK (relation_type IN (0,1,2)), PRIMARY KEY(from_version_id, to_version_id, relation_type));
    CREATE TRIGGER IF NOT EXISTS trg_version_relations_no_self_loop BEFORE INSERT ON version_relations FOR EACH ROW WHEN NEW.from_version_id = NEW.to_version_id BEGIN SELECT RAISE(ABORT, 'version relation cycle detected'); END;
    CREATE TRIGGER IF NOT EXISTS trg_version_relations_no_cycle BEFORE INSERT ON version_relations FOR EACH ROW WHEN EXISTS (WITH RECURSIVE reach(id) AS (SELECT NEW.to_version_id UNION SELECT vr.to_version_id FROM version_relations vr JOIN reach ON vr.from_version_id = reach.id) SELECT 1 FROM reach WHERE id = NEW.from_version_id) BEGIN SELECT RAISE(ABORT, 'version relation cycle detected'); END;
  )SQL";
}
