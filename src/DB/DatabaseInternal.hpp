#pragma once

#include "Database.hpp"
#include "SqliteHelpers.hpp"
#include "../CAS/CAS.hpp"
#include "../Common/PathUtils.hpp"

#include <algorithm>
#include <cstring>
#include <sqlite3.h>
#include <stdexcept>
#include <unordered_map>

namespace Docmasys::DB
{
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

  namespace Detail
  {
    inline Identity ReadBlob(sqlite3_stmt *statement, int column)
    {
      Identity result{};
      auto *blob = sqlite3_column_blob(statement, column);
      const auto size = sqlite3_column_bytes(statement, column);
      if (!blob || size != 32)
        throw std::runtime_error("bad blob");
      std::memcpy(result.data(), blob, 32);
      return result;
    }

    inline std::optional<ID> OptId(sqlite3_stmt *statement, int column)
    {
      return sqlite3_column_type(statement, column) == SQLITE_NULL
                 ? std::nullopt
                 : std::optional<ID>(sqlite3_column_int64(statement, column));
    }

    inline bool HasColumn(sqlite3 *db, const char *table, const char *column)
    {
      sqlite3_stmt *statement = nullptr;
      const auto pragma = std::string("PRAGMA table_info(") + table + ");";
      if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return false;

      bool found = false;
      while (sqlite3_step(statement) == SQLITE_ROW)
      {
        auto *name = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
        if (name && std::string_view(name) == column)
        {
          found = true;
          break;
        }
      }
      sqlite3_finalize(statement);
      return found;
    }

    inline int GetUserVersion(sqlite3 *db)
    {
      Sqlite::Statement statement(db, "PRAGMA user_version;");
      if (statement.Step() != SQLITE_ROW)
        throw std::runtime_error("failed to read schema version");
      return sqlite3_column_int(statement.get(), 0);
    }

    inline void SetUserVersion(sqlite3 *db, int version)
    {
      char *error = nullptr;
      const auto sql = std::string("PRAGMA user_version = ") + std::to_string(version) + ";";
      if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
      {
        const std::string message = error ? error : "failed to set schema version";
        sqlite3_free(error);
        throw std::runtime_error(message);
      }
    }

    inline std::string NormalizePropertyName(const std::string &name)
    {
      if (name.empty())
        throw std::runtime_error("property name cannot be empty");

      std::string normalized = name;
      std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                     { return static_cast<char>(std::tolower(ch)); });
      return normalized;
    }

    inline PropertyValueType PropertyTypeOf(const PropertyValue &value)
    {
      if (std::holds_alternative<std::string>(value)) return PropertyValueType::String;
      if (std::holds_alternative<std::int64_t>(value)) return PropertyValueType::Int;
      return PropertyValueType::Bool;
    }

    inline VersionProperty ReadVersionProperty(sqlite3_stmt *statement)
    {
      const auto type = static_cast<PropertyValueType>(sqlite3_column_int(statement, 2));
      PropertyValue value;
      switch (type)
      {
      case PropertyValueType::String:
        value = std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 3)));
        break;
      case PropertyValueType::Int:
        value = static_cast<std::int64_t>(sqlite3_column_int64(statement, 4));
        break;
      case PropertyValueType::Bool:
        value = sqlite3_column_int(statement, 5) != 0;
        break;
      }
      return VersionProperty{sqlite3_column_int64(statement, 0), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 1))), type, value};
    }

    inline MaterializedFile ReadMaterializedFile(sqlite3_stmt *statement)
    {
      auto file = std::make_shared<File>(sqlite3_column_int64(statement, 0), OptId(statement, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 2))), OptId(statement, 3));
      auto version = std::make_shared<FileVersion>(sqlite3_column_int64(statement, 4), sqlite3_column_int64(statement, 5), sqlite3_column_int64(statement, 6), sqlite3_column_int64(statement, 7));
      auto blob = std::make_shared<Blob>(sqlite3_column_int64(statement, 8), ReadBlob(statement, 9), static_cast<BlobStatus>(sqlite3_column_int64(statement, 10)));
      return MaterializedFile{file, version, blob, {}};
    }

    inline WorkspaceEntry ReadWorkspaceEntry(sqlite3_stmt *statement)
    {
      auto file = std::make_shared<File>(sqlite3_column_int64(statement, 0), OptId(statement, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 2))), OptId(statement, 3));
      auto version = std::make_shared<FileVersion>(sqlite3_column_int64(statement, 4), sqlite3_column_int64(statement, 5), sqlite3_column_int64(statement, 6), sqlite3_column_int64(statement, 7));
      return WorkspaceEntry{
          std::filesystem::path(reinterpret_cast<const char *>(sqlite3_column_text(statement, 8))),
          file,
          version,
          static_cast<MaterializationKind>(sqlite3_column_int(statement, 9))};
    }

    inline CheckoutLock ReadCheckoutLock(sqlite3_stmt *statement)
    {
      auto file = std::make_shared<File>(sqlite3_column_int64(statement, 0), OptId(statement, 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 2))), OptId(statement, 3));
      return CheckoutLock{
          file,
          std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 4))),
          std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 5))),
          std::filesystem::path(reinterpret_cast<const char *>(sqlite3_column_text(statement, 6)))};
    }

    inline WorkspaceEntryState DetectWorkspaceState(const std::filesystem::path &workspaceRoot,
                                                    const std::filesystem::path &archiveRoot,
                                                    const WorkspaceEntry &entry,
                                                    const Identity &expectedHash)
    {
      const auto fullPath = workspaceRoot / entry.RelativePath;
      std::error_code ec;
      const bool exists = std::filesystem::exists(fullPath, ec);
      const bool symlink = std::filesystem::is_symlink(fullPath, ec);
      if (!exists && !symlink)
        return WorkspaceEntryState::Missing;

      if (entry.Kind == MaterializationKind::ReadOnlySymlink)
      {
        if (!symlink)
          return WorkspaceEntryState::Replaced;
        const auto target = std::filesystem::read_symlink(fullPath, ec);
        if (ec)
          return WorkspaceEntryState::Replaced;
        if (std::filesystem::weakly_canonical(target, ec) != std::filesystem::weakly_canonical(CAS::BlobPath(archiveRoot, expectedHash), ec))
          return WorkspaceEntryState::Replaced;
        return WorkspaceEntryState::Ok;
      }

      if (symlink)
        return WorkspaceEntryState::Replaced;

      if (entry.Kind == MaterializationKind::ReadOnlyCopy)
      {
        const auto perms = std::filesystem::status(fullPath, ec).permissions();
        if (!ec && ((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none ||
                    (perms & std::filesystem::perms::group_write) != std::filesystem::perms::none ||
                    (perms & std::filesystem::perms::others_write) != std::filesystem::perms::none))
          return WorkspaceEntryState::Modified;
      }

      const auto actualHash = CAS::Identify(fullPath);
      if (actualHash != expectedHash)
        return WorkspaceEntryState::Modified;
      return WorkspaceEntryState::Ok;
    }
  }
}
