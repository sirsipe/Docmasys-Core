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
  const auto version = Detail::GetUserVersion(m_Database->m_db);

  if (version == 0)
  {
    ExecSQL(DB_SCHEMA);
    Detail::SetUserVersion(m_Database->m_db, DB_SCHEMA_VERSION);
    return;
  }

  MigrateSchemaIfNeeded(version);
  ExecSQL(DB_SCHEMA);
}

void Database::MigrateSchemaIfNeeded(int version)
{
  if (version == DB_SCHEMA_VERSION)
    return;

  if (version > DB_SCHEMA_VERSION)
    throw std::runtime_error("database schema version is newer than this build supports");

  throw std::runtime_error("unsupported pre-release database schema version; recreate the archive database");
}

bool Database::TryGetRelativePath(const fs::path &file, fs::path &out) const
{
  return Common::TryMakeVaultRelativePath(m_LocalVaultRoot, file, out);
}
