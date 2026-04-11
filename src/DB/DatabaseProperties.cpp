#include "DatabaseInternal.hpp"

using namespace Docmasys;
using namespace Docmasys::DB;

void Database::SetVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name, const PropertyValue &value)
{
  const auto normalizedName = Detail::NormalizePropertyName(name);
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    INSERT INTO version_properties(version_id,name,normalized_name,value_type,string_value,int_value,bool_value)
    VALUES(?1,?2,?3,?4,?5,?6,?7)
    ON CONFLICT(version_id, normalized_name) DO UPDATE SET
      name=excluded.name,
      value_type=excluded.value_type,
      string_value=excluded.string_value,
      int_value=excluded.int_value,
      bool_value=excluded.bool_value;
  )SQL");
  statement.BindInt64(1, version->Id);
  statement.BindText(2, name);
  statement.BindText(3, normalizedName);

  const auto type = Detail::PropertyTypeOf(value);
  statement.BindInt64(4, static_cast<int>(type));
  switch (type)
  {
  case PropertyValueType::String:
    statement.BindText(5, std::get<std::string>(value));
    statement.BindNull(6);
    statement.BindNull(7);
    break;
  case PropertyValueType::Int:
    statement.BindNull(5);
    statement.BindInt64(6, std::get<std::int64_t>(value));
    statement.BindNull(7);
    break;
  case PropertyValueType::Bool:
    statement.BindNull(5);
    statement.BindNull(6);
    statement.BindInt(7, std::get<bool>(value) ? 1 : 0);
    break;
  }
  statement.ExpectDone();
}

std::optional<VersionProperty> Database::GetVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT version_id,name,value_type,string_value,int_value,bool_value FROM version_properties WHERE version_id=?1 AND normalized_name=?2;");
  statement.BindInt64(1, version->Id);
  statement.BindText(2, Detail::NormalizePropertyName(name));
  if (statement.Step() != SQLITE_ROW)
    return std::nullopt;
  return Detail::ReadVersionProperty(statement.get());
}

std::vector<VersionProperty> Database::ListVersionProperties(const std::shared_ptr<FileVersion> &version)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT version_id,name,value_type,string_value,int_value,bool_value FROM version_properties WHERE version_id=?1 ORDER BY normalized_name;");
  statement.BindInt64(1, version->Id);

  std::vector<VersionProperty> properties;
  while (statement.Step() == SQLITE_ROW)
    properties.push_back(Detail::ReadVersionProperty(statement.get()));
  return properties;
}

bool Database::RemoveVersionProperty(const std::shared_ptr<FileVersion> &version, const std::string &name)
{
  Sqlite::Statement statement(m_Database->m_db, "DELETE FROM version_properties WHERE version_id=?1 AND normalized_name=?2;");
  statement.BindInt64(1, version->Id);
  statement.BindText(2, Detail::NormalizePropertyName(name));
  statement.ExpectDone();
  return sqlite3_changes(m_Database->m_db) > 0;
}
