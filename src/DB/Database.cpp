#include "Database.hpp"
#include <algorithm>
#include <cstring>
#include <sqlite3.h>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

namespace
{
Identity ReadBlob(sqlite3_stmt *s, int c)
{
  Identity r{};
  auto *b = sqlite3_column_blob(s, c);
  const auto n = sqlite3_column_bytes(s, c);
  if (!b || n != 32)
    throw std::runtime_error("bad blob");
  std::memcpy(r.data(), b, 32);
  return r;
}

std::optional<ID> OptId(sqlite3_stmt *s, int c)
{
  return sqlite3_column_type(s, c) == SQLITE_NULL ? std::nullopt : std::optional<ID>(sqlite3_column_int64(s, c));
}

bool HasCol(sqlite3 *db, const char *t, const char *c)
{
  sqlite3_stmt *st = nullptr;
  const auto q = std::string("PRAGMA table_info(") + t + ");";
  if (sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr) != SQLITE_OK)
    return false;
  bool found = false;
  while (sqlite3_step(st) == SQLITE_ROW)
  {
    auto *n = reinterpret_cast<const char *>(sqlite3_column_text(st, 1));
    if (n && std::string_view(n) == c)
    {
      found = true;
      break;
    }
  }
  sqlite3_finalize(st);
  return found;
}

int GetUserVersion(sqlite3 *db)
{
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &st, nullptr) != SQLITE_OK)
    throw std::runtime_error("failed to read schema version");
  if (sqlite3_step(st) != SQLITE_ROW)
  {
    sqlite3_finalize(st);
    throw std::runtime_error("failed to read schema version");
  }
  const auto version = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return version;
}

std::string NormalizePropertyName(const std::string &name)
{
  if (name.empty())
    throw std::runtime_error("property name cannot be empty");
  std::string normalized = name;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                 { return static_cast<char>(std::tolower(ch)); });
  return normalized;
}

PropertyValueType PropertyTypeOf(const PropertyValue &value)
{
  if (std::holds_alternative<std::string>(value)) return PropertyValueType::String;
  if (std::holds_alternative<std::int64_t>(value)) return PropertyValueType::Int;
  return PropertyValueType::Bool;
}

VersionProperty ReadVersionProperty(sqlite3_stmt *st)
{
  const auto type = static_cast<PropertyValueType>(sqlite3_column_int(st, 2));
  PropertyValue value;
  switch (type)
  {
  case PropertyValueType::String:
    value = std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 3)));
    break;
  case PropertyValueType::Int:
    value = static_cast<std::int64_t>(sqlite3_column_int64(st, 4));
    break;
  case PropertyValueType::Bool:
    value = sqlite3_column_int(st, 5) != 0;
    break;
  }
  return VersionProperty{sqlite3_column_int64(st, 0), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 1))), type, value};
}

MaterializedFile ReadMaterializedFile(sqlite3_stmt *st)
{
  auto file = std::make_shared<File>(sqlite3_column_int64(st, 0), OptId(st, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 2))), OptId(st, 3));
  auto ver = std::make_shared<FileVersion>(sqlite3_column_int64(st, 4), sqlite3_column_int64(st, 5), sqlite3_column_int64(st, 6), sqlite3_column_int64(st, 7));
  auto blob = std::make_shared<Blob>(sqlite3_column_int64(st, 8), ReadBlob(st, 9), static_cast<BlobStatus>(sqlite3_column_int64(st, 10)));
  return MaterializedFile{file, ver, blob, {}};
}
} // namespace

struct Database::Impl
{
  sqlite3 *m_db = nullptr;
  explicit Impl(sqlite3 *db) : m_db(db) {}
  ~Impl()
  {
    if (m_db)
      sqlite3_close(m_db);
  }
};

Database::Database(const fs::path &databaseFile, const fs::path &localVaultRoot) : m_DatabaseFile(databaseFile), m_LocalVaultRoot(localVaultRoot)
{
  if (!m_DatabaseFile.parent_path().empty())
    fs::create_directories(m_DatabaseFile.parent_path());
  sqlite3 *db = nullptr;
  if (sqlite3_open(m_DatabaseFile.string().c_str(), &db) != SQLITE_OK || !db)
    throw std::runtime_error("SQLite open failed");
  m_Database = std::make_unique<Impl>(db);
  ExecSQL("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys = ON;");
  MigrateLegacySchemaIfNeeded();
  MigrateSchemaIfNeeded();
  ExecSQL(DB_SCHEMA);
  ExecSQL("PRAGMA user_version = 2;");
}

Database::~Database() = default;

void Database::ExecSQL(const char *sql)
{
  char *err = nullptr;
  if (sqlite3_exec(m_Database->m_db, sql, nullptr, nullptr, &err) != SQLITE_OK)
  {
    const std::string msg = err ? err : "sql error";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

void Database::OpenTransaction() { ExecSQL("BEGIN IMMEDIATE;"); }
void Database::Commit() { ExecSQL("COMMIT;"); }
void Database::Rollback() { sqlite3_exec(m_Database->m_db, "ROLLBACK;", nullptr, nullptr, nullptr); }

void Database::MigrateLegacySchemaIfNeeded()
{
  if (!HasCol(m_Database->m_db, "files", "blob_id") || HasCol(m_Database->m_db, "files", "current_version_id"))
    return;
  OpenTransaction();
  try
  {
    ExecSQL("ALTER TABLE files RENAME TO files_legacy;");
    ExecSQL(DB_SCHEMA);
    ExecSQL("INSERT INTO files(id,parent_id,name,current_version_id) SELECT id,parent_id,name,NULL FROM files_legacy;");
    ExecSQL("INSERT INTO file_versions(file_id,version_number,blob_id) SELECT id,1,blob_id FROM files_legacy;");
    ExecSQL("UPDATE files SET current_version_id=(SELECT fv.id FROM file_versions fv WHERE fv.file_id=files.id AND fv.version_number=1);");
    ExecSQL("DROP TABLE files_legacy;");
    ExecSQL("PRAGMA user_version = 1;");
    Commit();
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

void Database::MigrateSchemaIfNeeded()
{
  const auto version = GetUserVersion(m_Database->m_db);
  if (version >= DB_SCHEMA_VERSION)
    return;
  OpenTransaction();
  try
  {
    if (version < 2)
    {
      ExecSQL(R"SQL(
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
      )SQL");
    }
    ExecSQL("PRAGMA user_version = 2;");
    Commit();
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

std::shared_ptr<Blob> Database::GetBlobByHashOrId(const std::optional<ID> &id, const std::optional<Identity> &hash)
{
  sqlite3_stmt *st = nullptr;
  const char *sql = id ? "SELECT id,hash,status FROM blobs WHERE id=?1;" : "SELECT id,hash,status FROM blobs WHERE hash=?1;";
  sqlite3_prepare_v2(m_Database->m_db, sql, -1, &st, nullptr);
  if (id)
    sqlite3_bind_int64(st, 1, *id);
  else
    sqlite3_bind_blob(st, 1, hash->data(), 32, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW)
    throw std::runtime_error("blob select failed");
  auto r = std::make_shared<Blob>(sqlite3_column_int64(st, 0), ReadBlob(st, 1), static_cast<BlobStatus>(sqlite3_column_int64(st, 2)));
  sqlite3_finalize(st);
  return r;
}

std::shared_ptr<Blob> Database::GetBlob(ID blobId) { return GetBlobByHashOrId(blobId, std::nullopt); }

std::shared_ptr<Folder> Database::GetOrCreateFolder(const std::string &name, const std::shared_ptr<Folder> &parent)
{
  sqlite3_stmt *i = nullptr, *s = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "INSERT INTO folders(parent_id,name) VALUES(?1,?2) ON CONFLICT DO NOTHING;", -1, &i, nullptr);
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE parent_id IS ?1 AND name=?2;", -1, &s, nullptr);
  if (parent)
    sqlite3_bind_int64(i, 1, parent->Id);
  else
    sqlite3_bind_null(i, 1);
  sqlite3_bind_text(i, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(i);
  if (parent)
    sqlite3_bind_int64(s, 1, parent->Id);
  else
    sqlite3_bind_null(s, 1);
  sqlite3_bind_text(s, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(s) != SQLITE_ROW)
    throw std::runtime_error("folder select failed");
  auto r = std::make_shared<Folder>(sqlite3_column_int64(s, 0), OptId(s, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(s, 2))));
  sqlite3_finalize(i);
  sqlite3_finalize(s);
  return r;
}

std::shared_ptr<Blob> Database::GetOrCreateBlob(const Identity &h)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "INSERT INTO blobs(hash,status) VALUES(?1,?2) ON CONFLICT(hash) DO NOTHING;", -1, &st, nullptr);
  sqlite3_bind_blob(st, 1, h.data(), 32, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, static_cast<int>(BlobStatus::Pending));
  sqlite3_step(st);
  sqlite3_finalize(st);
  return GetBlobByHashOrId(std::nullopt, h);
}

std::shared_ptr<File> Database::GetOrCreateFile(const std::string &name, const std::shared_ptr<Folder> &folder)
{
  sqlite3_stmt *i = nullptr, *s = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "INSERT INTO files(parent_id,name,current_version_id) VALUES(?1,?2,NULL) ON CONFLICT(parent_id,name) DO NOTHING;", -1, &i, nullptr);
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name,current_version_id FROM files WHERE parent_id IS ?1 AND name=?2;", -1, &s, nullptr);
  sqlite3_bind_int64(i, 1, folder->Id);
  sqlite3_bind_text(i, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(i);
  sqlite3_bind_int64(s, 1, folder->Id);
  sqlite3_bind_text(s, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(s) != SQLITE_ROW)
    throw std::runtime_error("file select failed");
  auto r = std::make_shared<File>(sqlite3_column_int64(s, 0), OptId(s, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(s, 2))), OptId(s, 3));
  sqlite3_finalize(i);
  sqlite3_finalize(s);
  return r;
}

std::shared_ptr<File> Database::GetFileById(ID id)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name,current_version_id FROM files WHERE id=?1;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, id);
  if (sqlite3_step(st) != SQLITE_ROW)
    throw std::runtime_error("file not found");
  auto r = std::make_shared<File>(sqlite3_column_int64(st, 0), OptId(st, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 2))), OptId(st, 3));
  sqlite3_finalize(st);
  return r;
}

std::shared_ptr<Folder> Database::GetFolderById(ID id)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE id=?1;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, id);
  if (sqlite3_step(st) != SQLITE_ROW)
    throw std::runtime_error("folder not found");
  auto r = std::make_shared<Folder>(sqlite3_column_int64(st, 0), OptId(st, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 2))));
  sqlite3_finalize(st);
  return r;
}

std::shared_ptr<FileVersion> Database::CreateFileVersion(const std::shared_ptr<File> &file, const std::shared_ptr<Blob> &blob)
{
  sqlite3_stmt *i = nullptr, *s = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "INSERT INTO file_versions(file_id,version_number,blob_id) VALUES(?1, COALESCE((SELECT MAX(version_number)+1 FROM file_versions WHERE file_id=?1),1), ?2);", -1, &i, nullptr);
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,file_id,blob_id,version_number FROM file_versions WHERE file_id=?1 ORDER BY version_number DESC LIMIT 1;", -1, &s, nullptr);
  sqlite3_bind_int64(i, 1, file->Id);
  sqlite3_bind_int64(i, 2, blob->Id);
  sqlite3_step(i);
  sqlite3_bind_int64(s, 1, file->Id);
  if (sqlite3_step(s) != SQLITE_ROW)
    throw std::runtime_error("version select failed");
  auto r = std::make_shared<FileVersion>(sqlite3_column_int64(s, 0), sqlite3_column_int64(s, 1), sqlite3_column_int64(s, 2), sqlite3_column_int64(s, 3));
  sqlite3_finalize(i);
  sqlite3_finalize(s);
  return r;
}

std::shared_ptr<File> Database::SetCurrentVersion(const std::shared_ptr<File> &file, const std::shared_ptr<FileVersion> &version)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "UPDATE files SET current_version_id=?2 WHERE id=?1;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, file->Id);
  sqlite3_bind_int64(st, 2, version->Id);
  sqlite3_step(st);
  sqlite3_finalize(st);
  return GetFileById(file->Id);
}

ImportResult Database::Import(const fs::path &file, const Identity &hash)
{
  fs::path rel;
  if (!TryGetRelativePath(file, rel))
    throw std::runtime_error("File is outside vault");
  return InsertToDB(rel, hash);
}

ImportResult Database::InsertToDB(const fs::path &rel, const Identity &hash)
{
  std::vector<std::string> parts;
  for (const auto &p : rel.parent_path().lexically_normal())
    if (p != "." && p != "/" && !p.empty())
      parts.push_back(p.generic_string());

  OpenTransaction();
  try
  {
    const auto blob = GetOrCreateBlob(hash);
    std::shared_ptr<Folder> leaf;
    for (const auto &n : parts)
      leaf = GetOrCreateFolder(n, leaf);
    const auto file = GetOrCreateFile(rel.filename().generic_string(), leaf);
    if (file->CurrentVersionId)
    {
      const auto cur = GetFileVersion(file, std::nullopt);
      if (cur->BlobId == blob->Id)
      {
        Commit();
        return {cur, false};
      }
    }
    const auto ver = CreateFileVersion(file, blob);
    SetCurrentVersion(file, ver);
    Commit();
    return {ver, true};
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

std::shared_ptr<Blob> Database::UpdateBlobStatus(const std::shared_ptr<Blob> &blob, const BlobStatus &status)
{
  sqlite3_stmt *st = nullptr;
  OpenTransaction();
  try
  {
    sqlite3_prepare_v2(m_Database->m_db, "UPDATE blobs SET status=?2 WHERE id=?1;", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, blob->Id);
    sqlite3_bind_int64(st, 2, static_cast<int>(status));
    sqlite3_step(st);
    sqlite3_finalize(st);
    const auto r = GetBlob(blob->Id);
    Commit();
    return r;
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

std::vector<std::shared_ptr<Folder>> Database::GetFolders(const std::shared_ptr<Folder> &folder)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE parent_id IS ?1 ORDER BY name;", -1, &st, nullptr);
  if (folder)
    sqlite3_bind_int64(st, 1, folder->Id);
  else
    sqlite3_bind_null(st, 1);
  std::vector<std::shared_ptr<Folder>> out;
  while (sqlite3_step(st) == SQLITE_ROW)
    out.push_back(std::make_shared<Folder>(sqlite3_column_int64(st, 0), OptId(st, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 2)))));
  sqlite3_finalize(st);
  return out;
}

std::vector<MaterializedFile> Database::GetMaterializedFiles(const std::shared_ptr<Folder> &folder)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT f.id,f.parent_id,f.name,f.current_version_id,fv.id,fv.file_id,fv.blob_id,fv.version_number,b.id,b.hash,b.status FROM files f JOIN file_versions fv ON fv.id=f.current_version_id JOIN blobs b ON b.id=fv.blob_id WHERE f.parent_id=?1 ORDER BY f.name;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, folder->Id);
  std::vector<MaterializedFile> out;
  while (sqlite3_step(st) == SQLITE_ROW)
  {
    auto item = ReadMaterializedFile(st);
    item.RelativePath = BuildRelativePath(item.LogicalFile);
    out.push_back(item);
  }
  sqlite3_finalize(st);
  return out;
}

std::shared_ptr<File> Database::GetFileByRelativePath(const fs::path &p)
{
  std::shared_ptr<Folder> folder;
  for (const auto &part : p.lexically_normal().parent_path())
  {
    if (part == "." || part == "/" || part.empty())
      continue;
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE parent_id IS ?1 AND name=?2;", -1, &st, nullptr);
    if (folder)
      sqlite3_bind_int64(st, 1, folder->Id);
    else
      sqlite3_bind_null(st, 1);
    const auto name = part.generic_string();
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_ROW)
      throw std::runtime_error("folder path not found");
    folder = std::make_shared<Folder>(sqlite3_column_int64(st, 0), OptId(st, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 2))));
    sqlite3_finalize(st);
  }
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,parent_id,name,current_version_id FROM files WHERE parent_id IS ?1 AND name=?2;", -1, &st, nullptr);
  if (folder)
    sqlite3_bind_int64(st, 1, folder->Id);
  else
    sqlite3_bind_null(st, 1);
  const auto name = p.lexically_normal().filename().generic_string();
  sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW)
    throw std::runtime_error("file path not found");
  auto r = std::make_shared<File>(sqlite3_column_int64(st, 0), OptId(st, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, 2))), OptId(st, 3));
  sqlite3_finalize(st);
  return r;
}

std::shared_ptr<FileVersion> Database::GetFileVersion(const std::shared_ptr<File> &file, const std::optional<std::int64_t> &n)
{
  sqlite3_stmt *st = nullptr;
  const char *sql = n ? "SELECT id,file_id,blob_id,version_number FROM file_versions WHERE file_id=?1 AND version_number=?2;" : "SELECT fv.id,fv.file_id,fv.blob_id,fv.version_number FROM file_versions fv JOIN files f ON f.current_version_id=fv.id WHERE f.id=?1;";
  sqlite3_prepare_v2(m_Database->m_db, sql, -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, file->Id);
  if (n)
    sqlite3_bind_int64(st, 2, *n);
  if (sqlite3_step(st) != SQLITE_ROW)
    throw std::runtime_error("requested file version not found");
  auto r = std::make_shared<FileVersion>(sqlite3_column_int64(st, 0), sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 2), sqlite3_column_int64(st, 3));
  sqlite3_finalize(st);
  return r;
}

std::vector<std::shared_ptr<FileVersion>> Database::GetFileVersions(const std::shared_ptr<File> &file)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT id,file_id,blob_id,version_number FROM file_versions WHERE file_id=?1 ORDER BY version_number;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, file->Id);
  std::vector<std::shared_ptr<FileVersion>> out;
  while (sqlite3_step(st) == SQLITE_ROW)
    out.push_back(std::make_shared<FileVersion>(sqlite3_column_int64(st, 0), sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 2), sqlite3_column_int64(st, 3)));
  sqlite3_finalize(st);
  return out;
}

fs::path Database::BuildRelativePath(const std::shared_ptr<File> &file)
{
  std::vector<std::string> parts{file->Name};
  auto pid = file->ParentId;
  while (pid)
  {
    const auto f = GetFolderById(*pid);
    parts.push_back(f->Name);
    pid = f->ParentId;
  }
  fs::path r;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it)
    r /= *it;
  return r;
}

std::vector<MaterializedFile> Database::ResolveMaterialization(const std::shared_ptr<FileVersion> &root, RelationScope scope)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, R"SQL(WITH RECURSIVE selected(version_id) AS (SELECT ?1 UNION SELECT vr.to_version_id FROM version_relations vr JOIN selected s ON s.version_id=vr.from_version_id WHERE (?2=1 AND vr.relation_type=0) OR (?2=2 AND vr.relation_type IN (0,1)) OR (?2=3 AND vr.relation_type IN (0,1,2))) SELECT f.id,f.parent_id,f.name,f.current_version_id,fv.id,fv.file_id,fv.blob_id,fv.version_number,b.id,b.hash,b.status FROM selected s JOIN file_versions fv ON fv.id=s.version_id JOIN files f ON f.id=fv.file_id JOIN blobs b ON b.id=fv.blob_id ORDER BY f.id,fv.version_number;)SQL", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, root->Id);
  sqlite3_bind_int64(st, 2, static_cast<int>(scope));
  std::vector<MaterializedFile> out;
  std::unordered_map<ID, ID> seen;
  while (sqlite3_step(st) == SQLITE_ROW)
  {
    auto item = ReadMaterializedFile(st);
    item.RelativePath = BuildRelativePath(item.LogicalFile);
    const auto [it, inserted] = seen.emplace(item.LogicalFile->Id, item.Version->Id);
    if (!inserted && it->second != item.Version->Id)
    {
      sqlite3_finalize(st);
      throw std::runtime_error("materialization conflict: multiple versions selected for logical path '" + item.RelativePath.generic_string() + "'");
    }
    out.push_back(item);
  }
  sqlite3_finalize(st);
  return out;
}

void Database::AddRelation(const std::shared_ptr<FileVersion> &from, const std::shared_ptr<FileVersion> &to, RelationType type)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "INSERT OR IGNORE INTO version_relations(from_version_id,to_version_id,relation_type) VALUES(?1,?2,?3);", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, from->Id);
  sqlite3_bind_int64(st, 2, to->Id);
  sqlite3_bind_int64(st, 3, static_cast<int>(type));
  if (sqlite3_step(st) != SQLITE_DONE)
  {
    const auto msg = std::string(sqlite3_errmsg(m_Database->m_db));
    sqlite3_finalize(st);
    throw std::runtime_error(msg);
  }
  sqlite3_finalize(st);
}

std::vector<VersionRelationView> Database::GetOutgoingRelations(const std::shared_ptr<FileVersion> &from, std::optional<RelationType> typeFilter)
{
  sqlite3_stmt *st = nullptr;
  const char *sql = typeFilter
                        ? "SELECT vr.from_version_id, vr.to_version_id, vr.relation_type, fv.file_id, fv.blob_id, fv.version_number FROM version_relations vr JOIN file_versions fv ON fv.id=vr.to_version_id WHERE vr.from_version_id=?1 AND vr.relation_type=?2 ORDER BY vr.relation_type, fv.file_id, fv.version_number;"
                        : "SELECT vr.from_version_id, vr.to_version_id, vr.relation_type, fv.file_id, fv.blob_id, fv.version_number FROM version_relations vr JOIN file_versions fv ON fv.id=vr.to_version_id WHERE vr.from_version_id=?1 ORDER BY vr.relation_type, fv.file_id, fv.version_number;";
  sqlite3_prepare_v2(m_Database->m_db, sql, -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, from->Id);
  if (typeFilter)
    sqlite3_bind_int64(st, 2, static_cast<int>(*typeFilter));
  std::vector<VersionRelationView> out;
  while (sqlite3_step(st) == SQLITE_ROW)
  {
    auto to = std::make_shared<FileVersion>(sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 3), sqlite3_column_int64(st, 4), sqlite3_column_int64(st, 5));
    out.push_back(VersionRelationView{from, to, static_cast<RelationType>(sqlite3_column_int64(st, 2))});
  }
  sqlite3_finalize(st);
  return out;
}

std::vector<MaterializedFile> Database::InspectCurrentFiles()
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT f.id,f.parent_id,f.name,f.current_version_id,fv.id,fv.file_id,fv.blob_id,fv.version_number,b.id,b.hash,b.status FROM files f JOIN file_versions fv ON fv.id=f.current_version_id JOIN blobs b ON b.id=fv.blob_id ORDER BY f.id;", -1, &st, nullptr);
  std::vector<MaterializedFile> out;
  while (sqlite3_step(st) == SQLITE_ROW)
  {
    auto item = ReadMaterializedFile(st);
    item.RelativePath = BuildRelativePath(item.LogicalFile);
    out.push_back(item);
  }
  sqlite3_finalize(st);
  return out;
}

void Database::SetVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name, const PropertyValue &value)
{
  const auto normalizedName = NormalizePropertyName(name);
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, R"SQL(
    INSERT INTO version_properties(version_id,name,normalized_name,value_type,string_value,int_value,bool_value)
    VALUES(?1,?2,?3,?4,?5,?6,?7)
    ON CONFLICT(version_id, normalized_name) DO UPDATE SET
      name=excluded.name,
      value_type=excluded.value_type,
      string_value=excluded.string_value,
      int_value=excluded.int_value,
      bool_value=excluded.bool_value;
  )SQL", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, version->Id);
  sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, normalizedName.c_str(), -1, SQLITE_TRANSIENT);
  const auto type = PropertyTypeOf(value);
  sqlite3_bind_int64(st, 4, static_cast<int>(type));
  switch (type)
  {
  case PropertyValueType::String:
    sqlite3_bind_text(st, 5, std::get<std::string>(value).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_null(st, 6);
    sqlite3_bind_null(st, 7);
    break;
  case PropertyValueType::Int:
    sqlite3_bind_null(st, 5);
    sqlite3_bind_int64(st, 6, std::get<std::int64_t>(value));
    sqlite3_bind_null(st, 7);
    break;
  case PropertyValueType::Bool:
    sqlite3_bind_null(st, 5);
    sqlite3_bind_null(st, 6);
    sqlite3_bind_int(st, 7, std::get<bool>(value) ? 1 : 0);
    break;
  }
  if (sqlite3_step(st) != SQLITE_DONE)
  {
    const auto msg = std::string(sqlite3_errmsg(m_Database->m_db));
    sqlite3_finalize(st);
    throw std::runtime_error(msg);
  }
  sqlite3_finalize(st);
}

std::optional<VersionProperty> Database::GetVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name)
{
  const auto normalizedName = NormalizePropertyName(name);
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT version_id,name,value_type,string_value,int_value,bool_value FROM version_properties WHERE version_id=?1 AND normalized_name=?2;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, version->Id);
  sqlite3_bind_text(st, 2, normalizedName.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW)
  {
    sqlite3_finalize(st);
    return std::nullopt;
  }
  auto property = ReadVersionProperty(st);
  sqlite3_finalize(st);
  return property;
}

std::vector<VersionProperty> Database::ListVersionProperties(const std::shared_ptr<FileVersion> &version)
{
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "SELECT version_id,name,value_type,string_value,int_value,bool_value FROM version_properties WHERE version_id=?1 ORDER BY normalized_name;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, version->Id);
  std::vector<VersionProperty> out;
  while (sqlite3_step(st) == SQLITE_ROW)
    out.push_back(ReadVersionProperty(st));
  sqlite3_finalize(st);
  return out;
}

bool Database::RemoveVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name)
{
  const auto normalizedName = NormalizePropertyName(name);
  sqlite3_stmt *st = nullptr;
  sqlite3_prepare_v2(m_Database->m_db, "DELETE FROM version_properties WHERE version_id=?1 AND normalized_name=?2;", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, version->Id);
  sqlite3_bind_text(st, 2, normalizedName.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_DONE)
  {
    const auto msg = std::string(sqlite3_errmsg(m_Database->m_db));
    sqlite3_finalize(st);
    throw std::runtime_error(msg);
  }
  const auto changed = sqlite3_changes(m_Database->m_db) > 0;
  sqlite3_finalize(st);
  return changed;
}

bool Database::TryGetRelativePath(const fs::path &file, fs::path &out) const
{
  fs::path root, full;
  try
  {
    root = fs::weakly_canonical(m_LocalVaultRoot);
    full = fs::weakly_canonical(file);
  }
  catch (...)
  {
    return false;
  }
  const auto mm = std::mismatch(root.begin(), root.end(), full.begin(), full.end());
  if (mm.first == root.end())
  {
    out = "ROOT" / fs::relative(full, root);
    return true;
  }
  return false;
}
