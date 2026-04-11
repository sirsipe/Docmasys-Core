#include "DatabaseInternal.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

void Database::UpsertWorkspaceEntry(const fs::path &workspaceRoot,
                                    const std::shared_ptr<File> &file,
                                    const std::shared_ptr<FileVersion> &version,
                                    const fs::path &relativePath,
                                    MaterializationKind kind)
{
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    INSERT INTO workspace_entries(workspace_root,file_id,version_id,relative_path,materialization_kind)
    VALUES(?1,?2,?3,?4,?5)
    ON CONFLICT(workspace_root, file_id) DO UPDATE SET
      version_id=excluded.version_id,
      relative_path=excluded.relative_path,
      materialization_kind=excluded.materialization_kind;
  )SQL");
  statement.BindText(1, Common::CanonicalWorkspaceRoot(workspaceRoot));
  statement.BindInt64(2, file->Id);
  statement.BindInt64(3, version->Id);
  statement.BindText(4, relativePath.generic_string());
  statement.BindInt(5, static_cast<int>(kind));
  statement.ExpectDone();
}

std::optional<WorkspaceEntry> Database::GetWorkspaceEntry(const fs::path &workspaceRoot, const std::shared_ptr<File> &file)
{
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    SELECT f.id,f.parent_id,f.name,f.current_version_id,
           fv.id,fv.file_id,fv.blob_id,fv.version_number,
           we.relative_path,we.materialization_kind
    FROM workspace_entries we
    JOIN files f ON f.id=we.file_id
    JOIN file_versions fv ON fv.id=we.version_id
    WHERE we.workspace_root=?1 AND we.file_id=?2;
  )SQL");
  statement.BindText(1, Common::CanonicalWorkspaceRoot(workspaceRoot));
  statement.BindInt64(2, file->Id);
  if (statement.Step() != SQLITE_ROW)
    return std::nullopt;
  return Detail::ReadWorkspaceEntry(statement.get());
}

std::vector<WorkspaceEntry> Database::ListWorkspaceEntries(const fs::path &workspaceRoot)
{
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    SELECT f.id,f.parent_id,f.name,f.current_version_id,
           fv.id,fv.file_id,fv.blob_id,fv.version_number,
           we.relative_path,we.materialization_kind
    FROM workspace_entries we
    JOIN files f ON f.id=we.file_id
    JOIN file_versions fv ON fv.id=we.version_id
    WHERE we.workspace_root=?1
    ORDER BY we.relative_path;
  )SQL");
  statement.BindText(1, Common::CanonicalWorkspaceRoot(workspaceRoot));

  std::vector<WorkspaceEntry> entries;
  while (statement.Step() == SQLITE_ROW)
    entries.push_back(Detail::ReadWorkspaceEntry(statement.get()));
  return entries;
}

void Database::AcquireCheckoutLock(const std::shared_ptr<File> &file,
                                   const std::shared_ptr<FileVersion> &version,
                                   const std::string &user,
                                   const std::string &environment,
                                   const fs::path &workspaceRoot)
{
  if (user.empty())
    throw std::runtime_error("checkout lock requires user");
  if (environment.empty())
    throw std::runtime_error("checkout lock requires environment");

  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    INSERT INTO checkout_locks(file_id,version_id,user_name,environment_name,workspace_root)
    VALUES(?1,?2,?3,?4,?5)
    ON CONFLICT(file_id) DO UPDATE SET
      version_id=excluded.version_id,
      user_name=excluded.user_name,
      environment_name=excluded.environment_name,
      workspace_root=excluded.workspace_root
    WHERE checkout_locks.user_name=excluded.user_name
      AND checkout_locks.environment_name=excluded.environment_name
      AND checkout_locks.workspace_root=excluded.workspace_root;
  )SQL");
  statement.BindInt64(1, file->Id);
  statement.BindInt64(2, version->Id);
  statement.BindText(3, user);
  statement.BindText(4, environment);
  statement.BindText(5, Common::CanonicalWorkspaceRoot(workspaceRoot));
  statement.ExpectDone();

  if (sqlite3_changes(m_Database->m_db) == 0)
  {
    const auto lock = GetCheckoutLock(file);
    throw std::runtime_error("file is already checked out by user '" + lock->User + "' in environment '" + lock->Environment + "'");
  }
}

std::optional<CheckoutLock> Database::GetCheckoutLock(const std::shared_ptr<File> &file)
{
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    SELECT f.id,f.parent_id,f.name,f.current_version_id,
           cl.user_name,cl.environment_name,cl.workspace_root
    FROM checkout_locks cl
    JOIN files f ON f.id=cl.file_id
    WHERE cl.file_id=?1;
  )SQL");
  statement.BindInt64(1, file->Id);
  if (statement.Step() != SQLITE_ROW)
    return std::nullopt;
  return Detail::ReadCheckoutLock(statement.get());
}

std::vector<CheckoutLock> Database::ListCheckoutLocks()
{
  Sqlite::Statement statement(m_Database->m_db, R"SQL(
    SELECT f.id,f.parent_id,f.name,f.current_version_id,
           cl.user_name,cl.environment_name,cl.workspace_root
    FROM checkout_locks cl
    JOIN files f ON f.id=cl.file_id
    ORDER BY cl.file_id;
  )SQL");

  std::vector<CheckoutLock> locks;
  while (statement.Step() == SQLITE_ROW)
    locks.push_back(Detail::ReadCheckoutLock(statement.get()));
  return locks;
}

bool Database::ReleaseCheckoutLock(const std::shared_ptr<File> &file,
                                   const std::string &user,
                                   const std::string &environment,
                                   const fs::path &workspaceRoot)
{
  Sqlite::Statement statement(m_Database->m_db, "DELETE FROM checkout_locks WHERE file_id=?1 AND user_name=?2 AND environment_name=?3 AND workspace_root=?4;");
  statement.BindInt64(1, file->Id);
  statement.BindText(2, user);
  statement.BindText(3, environment);
  statement.BindText(4, Common::CanonicalWorkspaceRoot(workspaceRoot));
  statement.ExpectDone();
  return sqlite3_changes(m_Database->m_db) > 0;
}

bool Database::ForceReleaseCheckoutLock(const std::shared_ptr<File> &file)
{
  Sqlite::Statement statement(m_Database->m_db, "DELETE FROM checkout_locks WHERE file_id=?1;");
  statement.BindInt64(1, file->Id);
  statement.ExpectDone();
  return sqlite3_changes(m_Database->m_db) > 0;
}

std::vector<WorkspaceEntryStatus> Database::GetWorkspaceStatus(const fs::path &workspaceRoot)
{
  std::vector<WorkspaceEntryStatus> statuses;
  const auto archiveRoot = m_DatabaseFile.parent_path();
  for (const auto &entry : ListWorkspaceEntries(workspaceRoot))
  {
    const auto blob = GetBlob(entry.Version->BlobId);
    statuses.push_back(WorkspaceEntryStatus{entry, Detail::DetectWorkspaceState(workspaceRoot, archiveRoot, entry, blob->Hash)});
  }
  return statuses;
}
