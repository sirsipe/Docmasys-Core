#include "DatabaseInternal.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

std::vector<MaterializedFile> Database::ResolveMaterialization(const std::shared_ptr<FileVersion> &rootVersion, RelationScope scope)
{
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    WITH RECURSIVE selected(version_id) AS (
      SELECT ?1
      UNION
      SELECT vr.to_version_id
      FROM version_relations vr
      JOIN selected s ON s.version_id=vr.from_version_id
      WHERE (?2=1 AND vr.relation_type=0)
         OR (?2=2 AND vr.relation_type IN (0,1))
         OR (?2=3 AND vr.relation_type IN (0,1,2))
    )
    SELECT f.id,f.parent_id,f.name,f.current_version_id,
           fv.id,fv.file_id,fv.blob_id,fv.version_number,
           b.id,b.hash,b.status
    FROM selected s
    JOIN file_versions fv ON fv.id=s.version_id
    JOIN files f ON f.id=fv.file_id
    JOIN blobs b ON b.id=fv.blob_id
    ORDER BY f.id,fv.version_number;
  )SQL");
  statement.BindInt64(1, rootVersion->Id);
  statement.BindInt64(2, static_cast<int>(scope));

  std::vector<MaterializedFile> files;
  std::unordered_map<ID, ID> seenByLogicalFile;
  while (statement.Step() == SQLITE_ROW)
  {
    auto item = Detail::ReadMaterializedFile(statement.get());
    item.RelativePath = BuildRelativePath(item.LogicalFile);
    const auto [it, inserted] = seenByLogicalFile.emplace(item.LogicalFile->Id, item.Version->Id);
    if (!inserted && it->second != item.Version->Id)
      throw std::runtime_error("materialization conflict: multiple versions selected for logical path '" + item.RelativePath.generic_string() + "'");
    files.push_back(item);
  }
  return files;
}

void Database::AddRelation(const std::shared_ptr<FileVersion> &from, const std::shared_ptr<FileVersion> &to, RelationType type)
{
  Sqlite::Statement statement(m_Database->m_db, "INSERT OR IGNORE INTO version_relations(from_version_id,to_version_id,relation_type) VALUES(?1,?2,?3);");
  statement.BindInt64(1, from->Id);
  statement.BindInt64(2, to->Id);
  statement.BindInt64(3, static_cast<int>(type));
  statement.ExpectDone();
}

std::vector<VersionRelationView> Database::GetOutgoingRelations(const std::shared_ptr<FileVersion> &from, std::optional<RelationType> typeFilter)
{
  Sqlite::Statement statement(
      m_Database->m_db,
      typeFilter
          ? "SELECT vr.from_version_id, vr.to_version_id, vr.relation_type, fv.file_id, fv.blob_id, fv.version_number FROM version_relations vr JOIN file_versions fv ON fv.id=vr.to_version_id WHERE vr.from_version_id=?1 AND vr.relation_type=?2 ORDER BY vr.relation_type, fv.file_id, fv.version_number;"
          : "SELECT vr.from_version_id, vr.to_version_id, vr.relation_type, fv.file_id, fv.blob_id, fv.version_number FROM version_relations vr JOIN file_versions fv ON fv.id=vr.to_version_id WHERE vr.from_version_id=?1 ORDER BY vr.relation_type, fv.file_id, fv.version_number;");
  statement.BindInt64(1, from->Id);
  if (typeFilter)
    statement.BindInt64(2, static_cast<int>(*typeFilter));

  std::vector<VersionRelationView> relations;
  while (statement.Step() == SQLITE_ROW)
  {
    auto to = std::make_shared<FileVersion>(sqlite3_column_int64(statement.get(), 1), sqlite3_column_int64(statement.get(), 3), sqlite3_column_int64(statement.get(), 4), sqlite3_column_int64(statement.get(), 5));
    relations.push_back(VersionRelationView{from, to, static_cast<RelationType>(sqlite3_column_int64(statement.get(), 2))});
  }
  return relations;
}
