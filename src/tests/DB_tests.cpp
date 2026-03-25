#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <unistd.h>

#include "../DB/Database.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::DB;

namespace
{
struct TempDir
{
  fs::path dir;
  TempDir()
  {
    auto base = fs::temp_directory_path();
    for (int i = 0; i < 1000; ++i)
    {
      auto cand = base / ("db_test_" + std::to_string(::getpid()) + "_" + std::to_string(i));
      if (fs::create_directory(cand))
      {
        dir = cand;
        break;
      }
    }
    if (dir.empty())
      throw std::runtime_error("TempDir: failed to create");
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

Identity MakeIdentity(std::uint8_t seed)
{
  Identity id{};
  for (size_t i = 0; i < id.size(); ++i)
    id[i] = static_cast<std::uint8_t>(seed + i);
  return id;
}

int ReadUserVersion(const fs::path &dbPath)
{
  sqlite3 *db = nullptr;
  if (sqlite3_open(dbPath.string().c_str(), &db) != SQLITE_OK)
    throw std::runtime_error("open sqlite failed");
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr);
  sqlite3_step(stmt);
  const int version = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return version;
}
} // namespace

TEST(DB, FreshDatabaseSetsSchemaVersion)
{
  TempDir td;
  const auto dbPath = td.dir / "content.db";
  auto db = Database::Open(dbPath, td.dir / "vault");
  EXPECT_EQ(ReadUserVersion(dbPath), DB_SCHEMA_VERSION);
}

TEST(DB, ImportCreatesVersionHistoryAndPreservesBlobs)
{
  TempDir td;
  const auto vaultRoot = td.dir / "vault";
  fs::create_directories(vaultRoot / "sub");
  std::ofstream(vaultRoot / "sub" / "file.txt") << "v1";

  auto db = Database::Open(td.dir / "content.db", vaultRoot);
  auto v1 = db->Import(vaultRoot / "sub" / "file.txt", MakeIdentity(1));
  auto blob1 = db->GetBlob(v1->BlobId);
  db->UpdateBlobStatus(blob1, BlobStatus::Ready);

  auto v1Again = db->Import(vaultRoot / "sub" / "file.txt", MakeIdentity(1));
  EXPECT_EQ(v1->Id, v1Again->Id);

  auto v2 = db->Import(vaultRoot / "sub" / "file.txt", MakeIdentity(2));
  EXPECT_NE(v1->Id, v2->Id);
  EXPECT_EQ(v2->VersionNumber, 2);

  auto file = db->GetFileByRelativePath("ROOT/sub/file.txt");
  auto current = db->GetFileVersion(file, std::nullopt);
  EXPECT_EQ(current->Id, v2->Id);
  EXPECT_EQ(db->GetBlob(v1->BlobId)->Id, v1->BlobId);
  EXPECT_EQ(db->GetBlob(v2->BlobId)->Id, v2->BlobId);
}

TEST(DB, RelationsTraverseAndCyclesAreRejected)
{
  TempDir td;
  const auto vaultRoot = td.dir / "vault";
  fs::create_directories(vaultRoot);
  std::ofstream(vaultRoot / "a.txt") << "a";
  std::ofstream(vaultRoot / "b.txt") << "b";
  std::ofstream(vaultRoot / "c.txt") << "c";

  auto db = Database::Open(td.dir / "content.db", vaultRoot);
  auto va = db->Import(vaultRoot / "a.txt", MakeIdentity(10));
  auto vb = db->Import(vaultRoot / "b.txt", MakeIdentity(20));
  auto vc = db->Import(vaultRoot / "c.txt", MakeIdentity(30));

  db->AddRelation(va, vb, RelationType::Strong);
  db->AddRelation(vb, vc, RelationType::Weak);

  EXPECT_EQ(db->ResolveMaterialization(va, RelationScope::Strong).size(), 2u);
  EXPECT_EQ(db->ResolveMaterialization(va, RelationScope::StrongAndWeak).size(), 3u);
  EXPECT_THROW(db->AddRelation(vc, va, RelationType::Optional), std::runtime_error);
}

TEST(DB, LegacySchemaIsMigrated)
{
  TempDir td;
  const auto dbPath = td.dir / "content.db";
  sqlite3 *raw = nullptr;
  ASSERT_EQ(sqlite3_open(dbPath.string().c_str(), &raw), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(raw, DB_SCHEMA_LEGACY, nullptr, nullptr, nullptr), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(raw, "INSERT INTO folders(id, parent_id, name) VALUES(1, NULL, 'ROOT');", nullptr, nullptr, nullptr), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(raw, "INSERT INTO blobs(id, hash, status) VALUES(1, zeroblob(32), 1);", nullptr, nullptr, nullptr), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(raw, "INSERT INTO files(id, parent_id, name, blob_id) VALUES(1, 1, 'legacy.txt', 1);", nullptr, nullptr, nullptr), SQLITE_OK);
  sqlite3_close(raw);

  auto db = Database::Open(dbPath, td.dir / "vault");
  EXPECT_EQ(ReadUserVersion(dbPath), DB_SCHEMA_VERSION);

  auto file = db->GetFileByRelativePath("ROOT/legacy.txt");
  auto version = db->GetFileVersion(file, std::nullopt);
  EXPECT_EQ(version->VersionNumber, 1);
  EXPECT_EQ(version->BlobId, 1);
}
