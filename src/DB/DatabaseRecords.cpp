#include "DatabaseInternal.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

std::shared_ptr<Blob> Database::GetBlobByHashOrId(const std::optional<ID> &id, const std::optional<Identity> &hash)
{
  Sqlite::Statement statement(m_Database->m_db, id ? "SELECT id,hash,status FROM blobs WHERE id=?1;" : "SELECT id,hash,status FROM blobs WHERE hash=?1;");
  if (id)
    statement.BindInt64(1, *id);
  else
    statement.BindBlob(1, hash->data(), 32);

  if (statement.Step() != SQLITE_ROW)
    throw std::runtime_error("blob select failed");

  return std::make_shared<Blob>(sqlite3_column_int64(statement.get(), 0), Detail::ReadBlob(statement.get(), 1), static_cast<BlobStatus>(sqlite3_column_int64(statement.get(), 2)));
}

std::shared_ptr<Blob> Database::GetBlob(ID blobId) { return GetBlobByHashOrId(blobId, std::nullopt); }

std::shared_ptr<Folder> Database::GetOrCreateFolder(const std::string &name, const std::shared_ptr<Folder> &parent)
{
  Sqlite::Statement insert(m_Database->m_db, "INSERT INTO folders(parent_id,name) VALUES(?1,?2) ON CONFLICT DO NOTHING;");
  if (parent)
    insert.BindInt64(1, parent->Id);
  else
    insert.BindNull(1);
  insert.BindText(2, name);
  insert.ExpectDone();

  Sqlite::Statement select(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE parent_id IS ?1 AND name=?2;");
  if (parent)
    select.BindInt64(1, parent->Id);
  else
    select.BindNull(1);
  select.BindText(2, name);
  if (select.Step() != SQLITE_ROW)
    throw std::runtime_error("folder select failed");

  return std::make_shared<Folder>(sqlite3_column_int64(select.get(), 0), Detail::OptId(select.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(select.get(), 2))));
}

std::shared_ptr<Blob> Database::GetOrCreateBlob(const Identity &hash)
{
  Sqlite::Statement statement(m_Database->m_db, "INSERT INTO blobs(hash,status) VALUES(?1,?2) ON CONFLICT(hash) DO NOTHING;");
  statement.BindBlob(1, hash.data(), 32);
  statement.BindInt(2, static_cast<int>(BlobStatus::Pending));
  statement.ExpectDone();
  return GetBlobByHashOrId(std::nullopt, hash);
}

std::shared_ptr<File> Database::GetOrCreateFile(const std::string &name, const std::shared_ptr<Folder> &folder)
{
  Sqlite::Statement insert(m_Database->m_db, "INSERT INTO files(parent_id,name,current_version_id) VALUES(?1,?2,NULL) ON CONFLICT(parent_id,name) DO NOTHING;");
  insert.BindInt64(1, folder->Id);
  insert.BindText(2, name);
  insert.ExpectDone();

  Sqlite::Statement select(m_Database->m_db, "SELECT id,parent_id,name,current_version_id FROM files WHERE parent_id IS ?1 AND name=?2;");
  select.BindInt64(1, folder->Id);
  select.BindText(2, name);
  if (select.Step() != SQLITE_ROW)
    throw std::runtime_error("file select failed");

  return std::make_shared<File>(sqlite3_column_int64(select.get(), 0), Detail::OptId(select.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(select.get(), 2))), Detail::OptId(select.get(), 3));
}

std::shared_ptr<File> Database::GetFileById(ID id)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT id,parent_id,name,current_version_id FROM files WHERE id=?1;");
  statement.BindInt64(1, id);
  if (statement.Step() != SQLITE_ROW)
    throw std::runtime_error("file not found");
  return std::make_shared<File>(sqlite3_column_int64(statement.get(), 0), Detail::OptId(statement.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 2))), Detail::OptId(statement.get(), 3));
}

std::shared_ptr<Folder> Database::GetFolderById(ID id)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE id=?1;");
  statement.BindInt64(1, id);
  if (statement.Step() != SQLITE_ROW)
    throw std::runtime_error("folder not found");
  return std::make_shared<Folder>(sqlite3_column_int64(statement.get(), 0), Detail::OptId(statement.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 2))));
}

std::shared_ptr<FileVersion> Database::CreateFileVersion(const std::shared_ptr<File> &file, const std::shared_ptr<Blob> &blob)
{
  Sqlite::Statement insert(m_Database->m_db, "INSERT INTO file_versions(file_id,version_number,blob_id) VALUES(?1, COALESCE((SELECT MAX(version_number)+1 FROM file_versions WHERE file_id=?1),1), ?2);");
  insert.BindInt64(1, file->Id);
  insert.BindInt64(2, blob->Id);
  insert.ExpectDone();

  Sqlite::Statement select(m_Database->m_db, "SELECT id,file_id,blob_id,version_number FROM file_versions WHERE file_id=?1 ORDER BY version_number DESC LIMIT 1;");
  select.BindInt64(1, file->Id);
  if (select.Step() != SQLITE_ROW)
    throw std::runtime_error("version select failed");

  return std::make_shared<FileVersion>(sqlite3_column_int64(select.get(), 0), sqlite3_column_int64(select.get(), 1), sqlite3_column_int64(select.get(), 2), sqlite3_column_int64(select.get(), 3));
}

std::shared_ptr<File> Database::SetCurrentVersion(const std::shared_ptr<File> &file, const std::shared_ptr<FileVersion> &version)
{
  Sqlite::Statement statement(m_Database->m_db, "UPDATE files SET current_version_id=?2 WHERE id=?1;");
  statement.BindInt64(1, file->Id);
  statement.BindInt64(2, version->Id);
  statement.ExpectDone();
  return GetFileById(file->Id);
}

ImportResult Database::Import(const fs::path &file, const Identity &hash)
{
  fs::path relative;
  if (!TryGetRelativePath(file, relative))
    throw std::runtime_error("File is outside vault");
  return InsertToDB(relative, hash);
}

ImportResult Database::InsertToDB(const fs::path &relativeFilePath, const Identity &hash)
{
  std::vector<std::string> parts;
  for (const auto &part : relativeFilePath.parent_path().lexically_normal())
    if (part != "." && part != "/" && !part.empty())
      parts.push_back(part.generic_string());

  OpenTransaction();
  try
  {
    const auto blob = GetOrCreateBlob(hash);
    std::shared_ptr<Folder> leaf;
    for (const auto &name : parts)
      leaf = GetOrCreateFolder(name, leaf);

    const auto file = GetOrCreateFile(relativeFilePath.filename().generic_string(), leaf);
    if (file->CurrentVersionId)
    {
      const auto current = GetFileVersion(file, std::nullopt);
      if (current->BlobId == blob->Id)
      {
        Commit();
        return {current, false};
      }
    }

    const auto version = CreateFileVersion(file, blob);
    SetCurrentVersion(file, version);
    Commit();
    return {version, true};
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

std::shared_ptr<Blob> Database::UpdateBlobStatus(const std::shared_ptr<Blob> &blob, const BlobStatus &status)
{
  OpenTransaction();
  try
  {
    Sqlite::Statement statement(m_Database->m_db, "UPDATE blobs SET status=?2 WHERE id=?1;");
    statement.BindInt64(1, blob->Id);
    statement.BindInt64(2, static_cast<int>(status));
    statement.ExpectDone();
    const auto updated = GetBlob(blob->Id);
    Commit();
    return updated;
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

std::vector<std::shared_ptr<Folder>> Database::GetFolders(const std::shared_ptr<Folder> &folder)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE parent_id IS ?1 ORDER BY name;");
  if (folder)
    statement.BindInt64(1, folder->Id);
  else
    statement.BindNull(1);

  std::vector<std::shared_ptr<Folder>> folders;
  while (statement.Step() == SQLITE_ROW)
    folders.push_back(std::make_shared<Folder>(sqlite3_column_int64(statement.get(), 0), Detail::OptId(statement.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 2)))));
  return folders;
}

std::vector<MaterializedFile> Database::GetMaterializedFiles(const std::shared_ptr<Folder> &folder)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT f.id,f.parent_id,f.name,f.current_version_id,fv.id,fv.file_id,fv.blob_id,fv.version_number,b.id,b.hash,b.status FROM files f JOIN file_versions fv ON fv.id=f.current_version_id JOIN blobs b ON b.id=fv.blob_id WHERE f.parent_id=?1 ORDER BY f.name;");
  statement.BindInt64(1, folder->Id);

  std::vector<MaterializedFile> files;
  while (statement.Step() == SQLITE_ROW)
  {
    auto item = Detail::ReadMaterializedFile(statement.get());
    item.RelativePath = BuildRelativePath(item.LogicalFile);
    files.push_back(item);
  }
  return files;
}

std::shared_ptr<File> Database::GetFileByRelativePath(const fs::path &path)
{
  std::shared_ptr<Folder> folder;
  for (const auto &part : path.lexically_normal().parent_path())
  {
    if (part == "." || part == "/" || part.empty())
      continue;

    Sqlite::Statement statement(m_Database->m_db, "SELECT id,parent_id,name FROM folders WHERE parent_id IS ?1 AND name=?2;");
    if (folder)
      statement.BindInt64(1, folder->Id);
    else
      statement.BindNull(1);
    statement.BindText(2, part.generic_string());
    if (statement.Step() != SQLITE_ROW)
      throw std::runtime_error("folder path not found");

    folder = std::make_shared<Folder>(sqlite3_column_int64(statement.get(), 0), Detail::OptId(statement.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 2))));
  }

  Sqlite::Statement statement(m_Database->m_db, "SELECT id,parent_id,name,current_version_id FROM files WHERE parent_id IS ?1 AND name=?2;");
  if (folder)
    statement.BindInt64(1, folder->Id);
  else
    statement.BindNull(1);
  statement.BindText(2, path.lexically_normal().filename().generic_string());
  if (statement.Step() != SQLITE_ROW)
    throw std::runtime_error("file path not found");

  return std::make_shared<File>(sqlite3_column_int64(statement.get(), 0), Detail::OptId(statement.get(), 1), std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 2))), Detail::OptId(statement.get(), 3));
}

std::shared_ptr<FileVersion> Database::GetFileVersion(const std::shared_ptr<File> &file, const std::optional<std::int64_t> &versionNumber)
{
  Sqlite::Statement statement(
      m_Database->m_db,
      versionNumber
          ? "SELECT id,file_id,blob_id,version_number FROM file_versions WHERE file_id=?1 AND version_number=?2;"
          : "SELECT fv.id,fv.file_id,fv.blob_id,fv.version_number FROM file_versions fv JOIN files f ON f.current_version_id=fv.id WHERE f.id=?1;");
  statement.BindInt64(1, file->Id);
  if (versionNumber)
    statement.BindInt64(2, *versionNumber);
  if (statement.Step() != SQLITE_ROW)
    throw std::runtime_error("requested file version not found");

  return std::make_shared<FileVersion>(sqlite3_column_int64(statement.get(), 0), sqlite3_column_int64(statement.get(), 1), sqlite3_column_int64(statement.get(), 2), sqlite3_column_int64(statement.get(), 3));
}

std::vector<std::shared_ptr<FileVersion>> Database::GetFileVersions(const std::shared_ptr<File> &file)
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT id,file_id,blob_id,version_number FROM file_versions WHERE file_id=?1 ORDER BY version_number;");
  statement.BindInt64(1, file->Id);

  std::vector<std::shared_ptr<FileVersion>> versions;
  while (statement.Step() == SQLITE_ROW)
    versions.push_back(std::make_shared<FileVersion>(sqlite3_column_int64(statement.get(), 0), sqlite3_column_int64(statement.get(), 1), sqlite3_column_int64(statement.get(), 2), sqlite3_column_int64(statement.get(), 3)));
  return versions;
}

fs::path Database::BuildRelativePath(const std::shared_ptr<File> &file)
{
  std::vector<std::string> parts{file->Name};
  auto parentId = file->ParentId;
  while (parentId)
  {
    const auto folder = GetFolderById(*parentId);
    parts.push_back(folder->Name);
    parentId = folder->ParentId;
  }

  fs::path relative;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it)
    relative /= *it;
  return relative;
}

std::vector<MaterializedFile> Database::InspectCurrentFiles()
{
  Sqlite::Statement statement(m_Database->m_db, "SELECT f.id,f.parent_id,f.name,f.current_version_id,fv.id,fv.file_id,fv.blob_id,fv.version_number,b.id,b.hash,b.status FROM files f JOIN file_versions fv ON fv.id=f.current_version_id JOIN blobs b ON b.id=fv.blob_id ORDER BY f.id;");

  std::vector<MaterializedFile> files;
  while (statement.Step() == SQLITE_ROW)
  {
    auto item = Detail::ReadMaterializedFile(statement.get());
    item.RelativePath = BuildRelativePath(item.LogicalFile);
    files.push_back(item);
  }
  return files;
}
