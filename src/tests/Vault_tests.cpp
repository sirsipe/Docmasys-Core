#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../CAS/CAS.hpp"
#include "../DB/Database.hpp"
#include "../Vault.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;

namespace
{
  struct TempDir
  {
    fs::path dir;
    TempDir()
    {
      dir = fs::temp_directory_path() / fs::path("docmasys_vault_test_" + std::to_string(::getpid()) + "_" + std::to_string(std::rand()));
      fs::create_directories(dir);
    }
    ~TempDir()
    {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }
  };

  fs::path MakeFile(const fs::path &p, std::string_view content)
  {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
    return p;
  }

  std::string ReadFile(const fs::path &p)
  {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
  }
}

TEST(VaultDatabase, ImportSkipsDuplicateContentButVersionsChangedContent)
{
  TempDir td;
  auto local = td.dir / "local";
  auto archive = td.dir / "archive";
  fs::create_directories(local);
  fs::create_directories(archive);

  auto db = DB::Database::Open(archive / "content.db", local);
  auto file = MakeFile(local / "docs" / "note.txt", "one");

  auto v1 = db->Import(file, CAS::Identify(file));
  auto v1b = db->Import(file, CAS::Identify(file));
  EXPECT_EQ(v1.Version->Id, v1b.Version->Id);
  EXPECT_EQ(v1.Version->VersionNumber, 1);

  MakeFile(file, "two");
  auto v2 = db->Import(file, CAS::Identify(file));
  EXPECT_NE(v1.Version->Id, v2.Version->Id);
  EXPECT_EQ(v2.Version->VersionNumber, 2);

  auto logical = db->GetFileByRelativePath("ROOT/docs/note.txt");
  auto latest = db->GetFileVersion(logical, std::nullopt);
  EXPECT_EQ(latest->Id, v2.Version->Id);
}

TEST(VaultDatabase, ResolveMaterializationByScope)
{
  TempDir td;
  auto local = td.dir / "local";
  auto archive = td.dir / "archive";
  fs::create_directories(local);
  fs::create_directories(archive);

  auto db = DB::Database::Open(archive / "content.db", local);

  auto rootPath = MakeFile(local / "root.txt", "root");
  auto strongPath = MakeFile(local / "strong.txt", "strong");
  auto weakPath = MakeFile(local / "weak.txt", "weak");
  auto optPath = MakeFile(local / "optional.txt", "optional");

  auto rootVersion = db->Import(rootPath, CAS::Identify(rootPath)).Version;
  auto strongVersion = db->Import(strongPath, CAS::Identify(strongPath)).Version;
  auto weakVersion = db->Import(weakPath, CAS::Identify(weakPath)).Version;
  auto optionalVersion = db->Import(optPath, CAS::Identify(optPath)).Version;

  db->AddRelation(rootVersion, strongVersion, DB::RelationType::Strong);
  db->AddRelation(strongVersion, weakVersion, DB::RelationType::Weak);
  db->AddRelation(weakVersion, optionalVersion, DB::RelationType::Optional);

  EXPECT_EQ(db->ResolveMaterialization(rootVersion, DB::RelationScope::None).size(), 1u);
  EXPECT_EQ(db->ResolveMaterialization(rootVersion, DB::RelationScope::Strong).size(), 2u);
  EXPECT_EQ(db->ResolveMaterialization(rootVersion, DB::RelationScope::StrongAndWeak).size(), 3u);
  EXPECT_EQ(db->ResolveMaterialization(rootVersion, DB::RelationScope::All).size(), 4u);
}

TEST(VaultDatabase, ResolveMaterializationRejectsConflictingVersions)
{
  TempDir td;
  auto local = td.dir / "local";
  auto archive = td.dir / "archive";
  fs::create_directories(local);
  fs::create_directories(archive);

  auto db = DB::Database::Open(archive / "content.db", local);

  auto rootPath = MakeFile(local / "root.txt", "root");
  auto depPath = MakeFile(local / "dep.txt", "dep-v1");

  auto rootVersion = db->Import(rootPath, CAS::Identify(rootPath)).Version;
  auto depV1 = db->Import(depPath, CAS::Identify(depPath)).Version;
  MakeFile(depPath, "dep-v2");
  auto depV2 = db->Import(depPath, CAS::Identify(depPath)).Version;

  db->AddRelation(rootVersion, depV1, DB::RelationType::Strong);
  db->AddRelation(rootVersion, depV2, DB::RelationType::Weak);

  EXPECT_THROW(db->ResolveMaterialization(rootVersion, DB::RelationScope::All), std::runtime_error);
}

TEST(Vault, PopMaterializesSelectedVersionClosure)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  auto output = td.dir / "output";
  fs::create_directories(source);
  fs::create_directories(archive);
  fs::create_directories(output);

  Vault sourceVault(source, archive);
  MakeFile(source / "app.txt", "v1");
  MakeFile(source / "dep.txt", "dep-v1");
  sourceVault.Push();

  auto db = DB::Database::Open(archive / "content.db", source);
  auto appFile = db->GetFileByRelativePath("ROOT/app.txt");
  auto depFile = db->GetFileByRelativePath("ROOT/dep.txt");
  auto appV1 = db->GetFileVersion(appFile, 1);
  auto depV1 = db->GetFileVersion(depFile, 1);
  db->AddRelation(appV1, depV1, DB::RelationType::Strong);

  MakeFile(source / "app.txt", "v2");
  sourceVault.Push();

  Vault outVault(output, archive);
  outVault.Pop(MaterializationOptions{.RelativeFilePath = fs::path("app.txt"), .VersionNumber = 1, .RelationScope = DB::RelationScope::Strong});

  EXPECT_EQ(ReadFile(output / "app.txt"), "v1");
  EXPECT_EQ(ReadFile(output / "dep.txt"), "dep-v1");
  EXPECT_FALSE(fs::exists(output / "ROOT"));
}

TEST(Vault, ImportExtensionsCanAttachPropertiesAndRelations)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  fs::create_directories(source);
  fs::create_directories(archive);

  MakeFile(source / "target.txt", "target");
  Vault vault(source, archive);
  vault.Push();

  MakeFile(source / "rules.dmsrel", "strong target.txt@1\n");
  vault.Push();

  auto db = DB::Database::Open(archive / "content.db", source);
  auto manifestFile = db->GetFileByRelativePath("ROOT/rules.dmsrel");
  auto manifestVersion = db->GetFileVersion(manifestFile, 1);
  const auto extensionProperty = db->GetVersionProperty(manifestVersion, "file.extension");
  ASSERT_TRUE(extensionProperty.has_value());
  EXPECT_EQ(std::get<std::string>(extensionProperty->Value), ".dmsrel");

  const auto relations = db->GetOutgoingRelations(manifestVersion, std::nullopt);
  ASSERT_EQ(relations.size(), 1u);
  auto targetFile = db->GetFileById(relations.front().To->FileId);
  EXPECT_EQ(db->BuildRelativePath(targetFile), fs::path("ROOT/target.txt"));
}
