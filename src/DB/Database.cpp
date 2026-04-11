#include "DatabaseInternal.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

Database::Database(const fs::path &databaseFile, const fs::path &localVaultRoot) : m_DatabaseFile(databaseFile), m_LocalVaultRoot(localVaultRoot)
{
  if (!m_DatabaseFile.parent_path().empty())
    fs::create_directories(m_DatabaseFile.parent_path());

  sqlite3 *db = nullptr;
  if (sqlite3_open(m_DatabaseFile.string().c_str(), &db) != SQLITE_OK || !db)
    throw std::runtime_error("SQLite open failed");

  m_Database = std::make_unique<Impl>(db);
  ExecSQL("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys = ON;");
  EnsureSchema();
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

void Database::EnsureSchema()
{
  MigrateLegacySchemaIfNeeded();
  MigrateSchemaIfNeeded();
  ExecSQL(DB_SCHEMA);
  Detail::SetUserVersion(m_Database->m_db, DB_SCHEMA_VERSION);
}

void Database::MigrateLegacySchemaIfNeeded()
{
  if (!Detail::HasColumn(m_Database->m_db, "files", "blob_id") || Detail::HasColumn(m_Database->m_db, "files", "current_version_id"))
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
    Detail::SetUserVersion(m_Database->m_db, 1);
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
  const auto version = Detail::GetUserVersion(m_Database->m_db);
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

    if (version < 3)
    {
      ExecSQL(R"SQL(
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
      )SQL");
    }

    Detail::SetUserVersion(m_Database->m_db, DB_SCHEMA_VERSION);
    Commit();
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

bool Database::TryGetRelativePath(const fs::path &file, fs::path &out) const
{
  return Common::TryMakeVaultRelativePath(m_LocalVaultRoot, file, out);
}
