#include "DB.hpp"
#include <sqlite3.h>
#include "DB_Schema.h"

struct DB::Impl
{
  sqlite3 *m_db = nullptr;

  explicit Impl(sqlite3 *db)
      : m_db(std::move(db))
  {
  }

  ~Impl()
  {
    if (m_db)
      sqlite3_close(m_db);
    m_db = nullptr;
  }
};

DB::DB(const std::filesystem::path &path)
    : m_Path(path)
{
  if (!m_Path.parent_path().empty())
    std::filesystem::create_directories(m_Path.parent_path());

  sqlite3 *poDatabase = nullptr;
  if (sqlite3_open(path.string().c_str(), &poDatabase) == SQLITE_OK && poDatabase)
  {
    m_Database = std::make_unique<Impl>(poDatabase);
  }
  else
  {
    std::string msg = "SQLite open failed";
    if (poDatabase)
    {
      msg += std::string(": ") + sqlite3_errmsg(poDatabase);
      sqlite3_close(poDatabase);
      poDatabase = nullptr;
    }

    throw std::runtime_error(msg);
  }

  ExecSQL("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys = ON;");
  ExecSQL(DB_Schema::DB_SCHEMA);
}

DB::~DB() = default;

void DB::ExecSQL(const char *sql)
{
  char *err = nullptr;
  if (sqlite3_exec(m_Database->m_db, sql, nullptr, nullptr, &err) != SQLITE_OK)
  {
    std::string msg = err ? err : "unknown sql error";
    sqlite3_free(err);
    throw std::runtime_error("SQLite exec failed: " + msg);
  }
}