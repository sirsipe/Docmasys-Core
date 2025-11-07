#include "Database.hpp"
#include <sqlite3.h>
#include <vector>
#include <optional>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

struct Database::Impl
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

Identity ReadBlobColumnAsIdentity(sqlite3_stmt *stmt, int col)
{
  Identity result{};

  const void *blob = sqlite3_column_blob(stmt, col);
  int size = sqlite3_column_bytes(stmt, col);

  if (!blob)
    throw std::runtime_error("NULL BLOB column");

  if (size != static_cast<int>(std::tuple_size_v<Identity>))
    throw std::runtime_error("Unexpected blob size: " + std::to_string(size));

  std::memcpy(result.data(), blob, std::tuple_size_v<Identity>);
  return result;
}

Database::Database(const fs::path &databaseFile, const fs::path &localVaultRoot)
    : m_DatabaseFile(databaseFile),
      m_LocalVaultRoot(localVaultRoot)
{

  if (!m_DatabaseFile.parent_path().empty())
    fs::create_directories(m_DatabaseFile.parent_path());

  sqlite3 *poDatabase = nullptr;
  if (sqlite3_open(m_DatabaseFile.string().c_str(), &poDatabase) == SQLITE_OK && poDatabase)
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
  ExecSQL(DB_SCHEMA);

  std::cout << "Database: " << m_DatabaseFile << std::endl;
}

Database::~Database() = default;

void Database::ExecSQL(const char *sql)
{
  char *err = nullptr;
  if (sqlite3_exec(m_Database->m_db, sql, nullptr, nullptr, &err) != SQLITE_OK)
  {
    std::string msg = err ? err : "unknown sql error";
    sqlite3_free(err);
    throw std::runtime_error("SQLite exec failed: " + msg);
  }
}

std::shared_ptr<Blob> Database::Import(const std::filesystem::path &file, const Identity &blobHash)
{
  fs::path rRelativePath;
  if (!TryGetRelativePath(file, rRelativePath))
    throw std::runtime_error("File is outside vault");

  return InsertToDB(rRelativePath, blobHash);
}

void Database::OpenTransaction()
{
  char *err = nullptr;
  if (sqlite3_exec(m_Database->m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err) != SQLITE_OK)
  {
    std::string msg = err ? err : "unknown";
    sqlite3_free(err);
    throw std::runtime_error("BEGIN failed: " + msg);
  }
}
void Database::Commit()
{
  char *err = nullptr;
  if (sqlite3_exec(m_Database->m_db, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK)
  {
    std::string msg = err ? err : "unknown";
    sqlite3_free(err);
    throw std::runtime_error("COMMIT failed: " + msg);
  }
}
void Database::Rollback()
{
  sqlite3_exec(m_Database->m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
}

std::vector<std::shared_ptr<Folder>> Database::GetFolders(const std::shared_ptr<Folder> &folder)
{
  sqlite3_stmt *selFolder = nullptr;
  const char *kSelFolder =
      "SELECT id, parent_id, name FROM folders "
      "WHERE parent_id IS ?1";

  if (sqlite3_prepare_v2(m_Database->m_db, kSelFolder, -1, &selFolder, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kSelFolder failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(selFolder);
  sqlite3_clear_bindings(selFolder);

  if (folder)
    sqlite3_bind_int64(selFolder, 1, folder->Id);
  else
    sqlite3_bind_null(selFolder, 1);

  std::vector<std::shared_ptr<Folder>> res;
  while (sqlite3_step(selFolder) == SQLITE_ROW)
  {
    res.push_back(std::make_shared<Folder>(
        static_cast<ID>(sqlite3_column_int64(selFolder, 0)),
        static_cast<ID>(sqlite3_column_int64(selFolder, 1)),
        std::string{reinterpret_cast<const char *>(sqlite3_column_text(selFolder, 2))}));
  }
  sqlite3_finalize(selFolder);

  return res;
}

std::vector<std::shared_ptr<File>> Database::GetFiles(const std::shared_ptr<Folder> &folder)
{
  sqlite3_stmt *selFile = nullptr;
  const char *kSelFile =
      "SELECT id, parent_id, blob_id, name FROM files WHERE parent_id = ?1;";

  if (sqlite3_prepare_v2(m_Database->m_db, kSelFile, -1, &selFile, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kSelFile failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(selFile);
  sqlite3_clear_bindings(selFile);

  sqlite3_bind_int64(selFile, 1, folder->Id);

  std::vector<std::shared_ptr<File>> res;
  while (sqlite3_step(selFile) == SQLITE_ROW)
  {
    res.push_back(std::make_shared<File>(
        static_cast<ID>(sqlite3_column_int64(selFile, 0)),
        static_cast<ID>(sqlite3_column_int64(selFile, 1)),
        static_cast<ID>(sqlite3_column_int64(selFile, 2)),
        std::string{reinterpret_cast<const char *>(sqlite3_column_text(selFile, 3))}));
  }
  sqlite3_finalize(selFile);

  return res;
}

std::unordered_map<std::shared_ptr<File>, std::shared_ptr<Blob>> Database::GetFilesAndBlobs(const std::shared_ptr<Folder> &folder)
{
  sqlite3_stmt *selFile = nullptr;
  const char *kSelFile =
      "SELECT files.id, files.parent_id, files.name, blobs.id, blobs.hash, blobs.status "
      "FROM files JOIN blobs ON files.blob_id = blobs.id WHERE parent_id = ?1;";

  if (sqlite3_prepare_v2(m_Database->m_db, kSelFile, -1, &selFile, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kSelFile failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(selFile);
  sqlite3_clear_bindings(selFile);

  sqlite3_bind_int64(selFile, 1, folder->Id);

  std::unordered_map<std::shared_ptr<File>, std::shared_ptr<Blob>> res;
  std::unordered_map<ID, std::shared_ptr<Blob>> blobs;

  while (sqlite3_step(selFile) == SQLITE_ROW)
  {
    auto file = std::make_shared<File>(
        static_cast<ID>(sqlite3_column_int64(selFile, 0)),
        static_cast<ID>(sqlite3_column_int64(selFile, 1)),
        static_cast<ID>(sqlite3_column_int64(selFile, 3)),
        std::string{reinterpret_cast<const char *>(sqlite3_column_text(selFile, 2))});

    if (!blobs.contains(file->BlobId))
    {
      auto blob = std::make_shared<Blob>(
          static_cast<ID>(sqlite3_column_int64(selFile, 3)),
          ReadBlobColumnAsIdentity(selFile, 4),
          static_cast<BlobStatus>(sqlite3_column_int64(selFile, 5)));

      blobs[blob->Id] = blob;
    }

    res[file] = blobs[file->BlobId];
  }
  sqlite3_finalize(selFile);

  return res;
}

std::shared_ptr<Folder> Database::GetOrCreateFolder(const std::string &name, const std::shared_ptr<Folder> &parent)
{
  sqlite3_stmt *insFolder = nullptr;
  sqlite3_stmt *selFolder = nullptr;

  const char *kInsFolder =
      "INSERT INTO folders(parent_id, name) VALUES(?1, ?2) "
      "ON CONFLICT DO NOTHING;";

  if (sqlite3_prepare_v2(m_Database->m_db, kInsFolder, -1, &insFolder, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kInsFolder failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(insFolder);
  sqlite3_clear_bindings(insFolder);

  if (parent)
    sqlite3_bind_int64(insFolder, 1, parent->Id);
  else
    sqlite3_bind_null(insFolder, 1);
  sqlite3_bind_text(insFolder, 2, name.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(insFolder) != SQLITE_DONE)
    throw std::runtime_error(std::string("folders insert failed: ") + sqlite3_errmsg(m_Database->m_db));

  const char *kSelFolder =
      "SELECT id, parent_id, name FROM folders "
      "WHERE parent_id IS ?1 AND name = ?2;";

  if (sqlite3_prepare_v2(m_Database->m_db, kSelFolder, -1, &selFolder, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kSelFolder failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(selFolder);
  sqlite3_clear_bindings(selFolder);

  if (parent)
    sqlite3_bind_int64(selFolder, 1, parent->Id);
  else
    sqlite3_bind_null(selFolder, 1);
  sqlite3_bind_text(selFolder, 2, name.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(selFolder) != SQLITE_ROW)
    throw std::runtime_error(std::string("folders select failed: ") + sqlite3_errmsg(m_Database->m_db));

  auto res = std::make_shared<Folder>(
      static_cast<ID>(sqlite3_column_int64(selFolder, 0)),
      static_cast<ID>(sqlite3_column_int64(selFolder, 1)),
      std::string{reinterpret_cast<const char *>(sqlite3_column_text(selFolder, 2))});

  sqlite3_finalize(insFolder);
  sqlite3_finalize(selFolder);

  return res;
}

std::shared_ptr<Blob> Database::GetBlobByHashOrId(const std::optional<ID> &id, const std::optional<Identity> &blobHash)
{
  sqlite3_stmt *selBlob = nullptr;

  const char *kSelBlob =
      id ? "SELECT id, hash, status FROM blobs WHERE id = ?1;" : blobHash ? "SELECT id, hash, status FROM blobs WHERE hash = ?1;"
                                                                          : throw std::runtime_error("Missing ID or Hash");

  if (sqlite3_prepare_v2(m_Database->m_db, kSelBlob, -1, &selBlob, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kSelBlob failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(selBlob);
  sqlite3_clear_bindings(selBlob);
  if (id)
    sqlite3_bind_int64(selBlob, 1, *id);
  else
    sqlite3_bind_blob(selBlob, 1, (*blobHash).data(), static_cast<int>(std::tuple_size_v<Identity>), SQLITE_TRANSIENT);

  if (sqlite3_step(selBlob) != SQLITE_ROW)
    throw std::runtime_error("blob select failed");

  auto res = std::make_shared<Blob>(
      static_cast<ID>(sqlite3_column_int64(selBlob, 0)),
      ReadBlobColumnAsIdentity(selBlob, 1),
      static_cast<BlobStatus>(sqlite3_column_int64(selBlob, 2)));

  sqlite3_finalize(selBlob);

  return res;
}

std::shared_ptr<Blob> Database::GetOrCreateBlob(const Identity &blobHash)
{
  sqlite3_stmt *insBlob = nullptr;

  const char *kInsBlob =
      "INSERT INTO blobs(hash, status) VALUES(?1, ?2) "
      "ON CONFLICT(hash) DO NOTHING;";

  if (sqlite3_prepare_v2(m_Database->m_db, kInsBlob, -1, &insBlob, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kInsBlob failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(insBlob);
  sqlite3_clear_bindings(insBlob);

  sqlite3_bind_blob(insBlob, 1, blobHash.data(), static_cast<int>(std::tuple_size_v<Identity>), SQLITE_TRANSIENT);
  sqlite3_bind_int(insBlob, 2, static_cast<int>(BlobStatus::Pending));

  if (sqlite3_step(insBlob) != SQLITE_DONE)
    throw std::runtime_error(std::string("blob insert failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_finalize(insBlob);

  std::optional<ID> id;
  return GetBlobByHashOrId(id, blobHash);
}

std::shared_ptr<File> Database::GetOrCreateFile(const std::string &name, const std::shared_ptr<Folder> &folder, const std::shared_ptr<Blob> &blob)
{
  sqlite3_stmt *upsertFile = nullptr;
  sqlite3_stmt *selFile = nullptr;

  const char *kUpsertFile =
      "INSERT INTO files(parent_id, name, blob_id) VALUES(?1, ?2, ?3) "
      "ON CONFLICT(parent_id, name) DO UPDATE SET blob_id = excluded.blob_id;";

  if (sqlite3_prepare_v2(m_Database->m_db, kUpsertFile, -1, &upsertFile, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kUpsertFile failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(upsertFile);
  sqlite3_clear_bindings(upsertFile);

  sqlite3_bind_int64(upsertFile, 1, folder->Id);
  sqlite3_bind_text(upsertFile, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(upsertFile, 3, blob->Id);

  if (sqlite3_step(upsertFile) != SQLITE_DONE)
    throw std::runtime_error(std::string("file upsert failed: ") + sqlite3_errmsg(m_Database->m_db));

  const char *kSelFile =
      "SELECT id, parent_id, blob_id, name FROM files WHERE parent_id IS ?1 AND name = ?2;";
  if (sqlite3_prepare_v2(m_Database->m_db, kSelFile, -1, &selFile, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("prepare kSelFile failed: ") + sqlite3_errmsg(m_Database->m_db));

  sqlite3_reset(selFile);
  sqlite3_clear_bindings(selFile);

  sqlite3_bind_int64(selFile, 1, folder->Id);
  sqlite3_bind_text(selFile, 2, name.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(selFile) != SQLITE_ROW)
    throw std::runtime_error("file select failed");

  auto res = std::make_shared<File>(
      static_cast<ID>(sqlite3_column_int64(selFile, 0)),
      static_cast<ID>(sqlite3_column_int64(selFile, 1)),
      static_cast<ID>(sqlite3_column_int64(selFile, 2)),
      std::string{reinterpret_cast<const char *>(sqlite3_column_text(selFile, 3))});

  sqlite3_finalize(upsertFile);
  sqlite3_finalize(selFile);

  return res;
}

std::shared_ptr<Blob> Database::UpdateBlobStatus(const std::shared_ptr<Blob> &blob, const BlobStatus &newStatus)
{
  OpenTransaction();
  try
  {

    sqlite3_stmt *updateBlob = nullptr;
    const char *kUpdBlob =
        "UPDATE blobs SET status=?2 WHERE id = ?1;";

    if (sqlite3_prepare_v2(m_Database->m_db, kUpdBlob, -1, &updateBlob, nullptr) != SQLITE_OK)
      throw std::runtime_error(std::string("prepare kUpdBlob failed: ") + sqlite3_errmsg(m_Database->m_db));

    sqlite3_reset(updateBlob);
    sqlite3_clear_bindings(updateBlob);

    sqlite3_bind_int64(updateBlob, 1, blob->Id);
    sqlite3_bind_int64(updateBlob, 2, static_cast<int>(newStatus));

    if (sqlite3_step(updateBlob) != SQLITE_DONE)
      throw std::runtime_error("file select failed");

    sqlite3_finalize(updateBlob);

    auto res = GetBlobByHashOrId(blob->Id, blob->Hash);
    Commit();
    return res;
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

std::shared_ptr<Blob> Database::InsertToDB(const fs::path &relativeFilePath,
                                           const Identity &blobHash)
{
  if (relativeFilePath.empty() || relativeFilePath.filename().empty())
    throw std::invalid_argument("InsertToDB: relativeFilePath must include a filename");

  const fs::path parentDir = relativeFilePath.parent_path();
  const std::string fileName = relativeFilePath.filename().generic_string();

  std::cout << "Sending: " << relativeFilePath << std::endl;

  // Build folder components from parentDir (relative, normalized)
  std::vector<std::string> parts;
  parts.reserve(8);
  for (const auto &p : parentDir.lexically_normal())
  {
    if (p == "." || p == "/" || p.empty())
      continue;
    parts.push_back(p.generic_string());
  }

  OpenTransaction();

  try
  {

    auto blob = GetOrCreateBlob(blobHash);

    std::cout << "Blob (ID: " << blob->Id << ") Status: " << (blob->Status == BlobStatus::Ready ? "READY" : "PENDING") << std::endl;

    std::shared_ptr<Folder> leafFolder;
    std::cout << "Folder: ";
    for (const auto &name : parts)
    {
      leafFolder = GetOrCreateFolder(name, leafFolder);
      std::cout << leafFolder->Name << " (ID: " << leafFolder->Id << ") / ";
    }
    std::cout << std::endl;

    auto file = GetOrCreateFile(fileName, leafFolder, blob);
    std::cout << "File: " << file->Name << " (ID: " << file->Id << ")" << std::endl;

    Commit();

    return blob;
  }
  catch (...)
  {
    Rollback();
    throw;
  }
}

bool Database::TryGetRelativePath(const fs::path &file, fs::path &outRelative) const
{
  // Canonicalize both paths (resolves symlinks and "..")
  fs::path canonicalRoot, canonicalFile;
  try
  {
    canonicalRoot = fs::weakly_canonical(m_LocalVaultRoot);
    canonicalFile = fs::weakly_canonical(file);
  }
  catch (const fs::filesystem_error &)
  {
    return false; // inaccessible path
  }

  // Check if the file is inside the root (or equal to it)
  auto mismatchPair = std::mismatch(canonicalRoot.begin(), canonicalRoot.end(),
                                    canonicalFile.begin(), canonicalFile.end());

  if (mismatchPair.first == canonicalRoot.end())
  {
    // The entire root path matched
    outRelative = "ROOT" / fs::relative(canonicalFile, canonicalRoot);
    return true;
  }

  // File is outside root
  return false;
}
