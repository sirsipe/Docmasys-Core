#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Docmasys::DB::Sqlite
{
  class Statement
  {
  public:
    Statement(sqlite3 *db, const char *sql) : m_Db(db)
    {
      if (sqlite3_prepare_v2(db, sql, -1, &m_Stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    ~Statement()
    {
      if (m_Stmt)
        sqlite3_finalize(m_Stmt);
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    Statement(Statement &&other) noexcept : m_Db(other.m_Db), m_Stmt(other.m_Stmt)
    {
      other.m_Db = nullptr;
      other.m_Stmt = nullptr;
    }
    Statement &operator=(Statement &&other) noexcept
    {
      if (this == &other)
        return *this;
      if (m_Stmt)
        sqlite3_finalize(m_Stmt);
      m_Db = other.m_Db;
      m_Stmt = other.m_Stmt;
      other.m_Db = nullptr;
      other.m_Stmt = nullptr;
      return *this;
    }

    [[nodiscard]] sqlite3_stmt *get() const noexcept { return m_Stmt; }

    void BindInt64(int index, sqlite3_int64 value)
    {
      if (sqlite3_bind_int64(m_Stmt, index, value) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

    void BindInt(int index, int value)
    {
      if (sqlite3_bind_int(m_Stmt, index, value) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

    void BindNull(int index)
    {
      if (sqlite3_bind_null(m_Stmt, index) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

    void BindText(int index, std::string_view value)
    {
      if (sqlite3_bind_text(m_Stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

    void BindBlob(int index, const void *data, int size)
    {
      if (sqlite3_bind_blob(m_Stmt, index, data, size, SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

    [[nodiscard]] int Step() const noexcept { return sqlite3_step(m_Stmt); }

    void ExpectDone() const
    {
      if (sqlite3_step(m_Stmt) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

    void ExpectRow() const
    {
      if (sqlite3_step(m_Stmt) != SQLITE_ROW)
        throw std::runtime_error(sqlite3_errmsg(m_Db));
    }

  private:
    sqlite3 *m_Db{};
    sqlite3_stmt *m_Stmt{};
  };
}
