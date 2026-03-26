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
  enum class MaterializationKind : std::uint8_t { ReadOnlyCopy = 0, ReadOnlySymlink = 1, CheckoutCopy = 2 };
  enum class WorkspaceEntryState : std::uint8_t { Ok = 0, Missing = 1, Modified = 2, Replaced = 3 };

  struct Blob { Blob(const ID &id, const Identity &hash, const BlobStatus &status): Id(id), Hash(hash), Status(status) {} ID Id{}; Identity Hash{}; BlobStatus Status{BlobStatus::Pending}; };
  struct Folder { Folder(ID id, std::optional<ID> parent_id, const std::string &name): Id(id), ParentId(parent_id), Name(name) {} ID Id{}; std::optional<ID> ParentId; std::string Name; };
  struct File { File(ID id, std::optional<ID> parent_id, const std::string &name, std::optional<ID> currentVersionId): Id(id), ParentId(parent_id), Name(name), CurrentVersionId(currentVersionId) {} ID Id{}; std::optional<ID> ParentId; std::string Name; std::optional<ID> CurrentVersionId; };
  struct FileVersion { FileVersion(ID id, ID fileId, ID blobId, std::int64_t versionNumber): Id(id), FileId(fileId), BlobId(blobId), VersionNumber(versionNumber) {} ID Id{}; ID FileId{}; ID BlobId{}; std::int64_t VersionNumber{}; };
  struct MaterializedFile { std::shared_ptr<File> LogicalFile; std::shared_ptr<FileVersion> Version; std::shared_ptr<Blob> BlobRef; std::filesystem::path RelativePath; };
  struct ImportResult { std::shared_ptr<FileVersion> Version; bool CreatedNewVersion{false}; };
  struct VersionProperty { ID VersionId{}; std::string Name; PropertyValueType Type{PropertyValueType::String}; PropertyValue Value{std::string{}}; };
  struct WorkspaceEntry
  {
    std::filesystem::path RelativePath;
    std::shared_ptr<File> LogicalFile;
    std::shared_ptr<FileVersion> Version;
    MaterializationKind Kind{MaterializationKind::ReadOnlyCopy};
  };
  struct CheckoutLock
  {
    std::shared_ptr<File> LogicalFile;
    std::string User;
    std::string Environment;
    std::filesystem::path WorkspaceRoot;
  };
  struct WorkspaceEntryStatus
  {
    WorkspaceEntry Entry;
    WorkspaceEntryState State{WorkspaceEntryState::Ok};
  };

  static constexpr int DB_SCHEMA_VERSION = 3;
  inline constexpr const char DB_SCHEMA_LEGACY[] = R"SQL(
    CREATE TABLE IF NOT EXISTS blobs (id INTEGER PRIMARY KEY, hash BLOB NOT NULL CHECK (length(hash) = 32), status INT NOT NULL CHECK (status IN (0,1)), UNIQUE(hash));
    CREATE TABLE IF NOT EXISTS folders (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE, name TEXT NOT NULL COLLATE NOCASE);
    CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE, name TEXT NOT NULL COLLATE NOCASE, blob_id INTEGER NOT NULL REFERENCES blobs(id) ON DELETE RESTRICT, UNIQUE(parent_id, name));
  )SQL";
  inline constexpr const char DB_SCHEMA[] = R"SQL(
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
    CREATE TABLE IF NOT EXISTS version_properties (
      version_id INTEGER NOT NULL REFERENCES file_versions(id) ON DELETE CASCADE,
      name TEXT NOT NULL,
      normalized_name TEXT NOT NULL,
      value_type INTEGER NOT NULL CHECK (value_type IN (0,1,2)),
      string_value TEXT,
      int_value INTEGER,
      bool_value INTEGER,
      CHECK ((value_type = 0 AND string_value IS NOT NULL AND int_value IS NULL AND bool_value IS NULL) OR
             (value_type = 1 AND string_value IS NULL AND int_value IS NOT NULL AND bool_value IS NULL) OR
             (value_type = 2 AND string_value IS NULL AND int_value IS NULL AND bool_value IN (0,1))),
      PRIMARY KEY(version_id, normalized_name)
    );
    CREATE INDEX IF NOT EXISTS idx_version_properties_version ON version_properties(version_id);
    CREATE TABLE IF NOT EXISTS workspace_entries (
      workspace_root TEXT NOT NULL,
      file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
      version_id INTEGER NOT NULL REFERENCES file_versions(id) ON DELETE CASCADE,
      relative_path TEXT NOT NULL,
      materialization_kind INTEGER NOT NULL CHECK (materialization_kind IN (0,1,2)),
      PRIMARY KEY(workspace_root, file_id),
      UNIQUE(workspace_root, relative_path)
    );
    CREATE INDEX IF NOT EXISTS idx_workspace_entries_workspace ON workspace_entries(workspace_root);
    CREATE TABLE IF NOT EXISTS checkout_locks (
      file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
      version_id INTEGER NOT NULL REFERENCES file_versions(id) ON DELETE CASCADE,
      user_name TEXT NOT NULL,
      environment_name TEXT NOT NULL,
      workspace_root TEXT NOT NULL
    );
  )SQL";
}
