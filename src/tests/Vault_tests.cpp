#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../CAS/CAS.hpp"
#include "../DB/Database.hpp"
#include "../Vault.hpp"
#include "TestSupport.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;
using Docmasys::Tests::TempDir;

namespace
{
  fs::path MakeFile(const fs::path &p, std::string_view content)
  {
    Tests::WriteFile(p, content);
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
  outVault.Pop(MaterializationOptions{.RelativeFilePath = fs::path("app.txt"), .VersionNumber = 1, .RelationScope = DB::RelationScope::Strong, .Kind = DB::MaterializationKind::ReadOnlyCopy});

  EXPECT_EQ(ReadFile(output / "app.txt"), "v1");
  EXPECT_EQ(ReadFile(output / "dep.txt"), "dep-v1");
  EXPECT_FALSE(fs::exists(output / "ROOT"));
}

TEST(Vault, CheckoutLocksFileAndMaterializesWritableCopy)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  auto output = td.dir / "output";
  fs::create_directories(source);
  fs::create_directories(archive);
  fs::create_directories(output);

  Vault sourceVault(source, archive);
  MakeFile(source / "part.txt", "v1");
  sourceVault.Push();

  Vault outVault(output, archive);
  outVault.Checkout(CheckoutOptions{.RelativeFilePath = fs::path("part.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .User = "simo", .Environment = "ws1"});

  EXPECT_EQ(ReadFile(output / "part.txt"), "v1");

  auto db = DB::Database::Open(archive / "content.db", output);
  auto file = db->GetFileByRelativePath("ROOT/part.txt");
  auto lock = db->GetCheckoutLock(file);
  ASSERT_TRUE(lock.has_value());
  EXPECT_EQ(lock->User, "simo");
  EXPECT_EQ(lock->Environment, "ws1");

  auto entry = db->GetWorkspaceEntry(output, file);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->Kind, DB::MaterializationKind::CheckoutCopy);
}

TEST(Vault, ReadOnlySymlinkMaterializationTracksWorkspace)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  auto output = td.dir / "output";
  fs::create_directories(source);
  fs::create_directories(archive);
  fs::create_directories(output);

  Vault sourceVault(source, archive);
  MakeFile(source / "view.txt", "hello");
  sourceVault.Push();

  Vault outVault(output, archive);
  outVault.Pop(MaterializationOptions{.RelativeFilePath = fs::path("view.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .Kind = DB::MaterializationKind::ReadOnlySymlink});

  EXPECT_TRUE(fs::is_symlink(output / "view.txt"));

  auto db = DB::Database::Open(archive / "content.db", output);
  auto file = db->GetFileByRelativePath("ROOT/view.txt");
  auto entry = db->GetWorkspaceEntry(output, file);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->Kind, DB::MaterializationKind::ReadOnlySymlink);
}

TEST(Vault, StatusRepairAndCheckinFlow)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  auto readonlyRoot = td.dir / "readonly";
  auto checkoutRoot = td.dir / "checkout";
  fs::create_directories(source);
  fs::create_directories(archive);
  fs::create_directories(readonlyRoot);
  fs::create_directories(checkoutRoot);

  Vault sourceVault(source, archive);
  MakeFile(source / "doc.txt", "v1");
  sourceVault.Push();

  Vault readonlyVault(readonlyRoot, archive);
  readonlyVault.Pop(MaterializationOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .Kind = DB::MaterializationKind::ReadOnlyCopy});
  fs::permissions(readonlyRoot / "doc.txt", fs::perms::owner_write, fs::perm_options::add);
  MakeFile(readonlyRoot / "doc.txt", "tampered");

  const auto readonlyStatus = readonlyVault.Status();
  ASSERT_EQ(readonlyStatus.size(), 1u);
  EXPECT_EQ(readonlyStatus.front().State, DB::WorkspaceEntryState::Modified);

  readonlyVault.Repair();
  EXPECT_EQ(ReadFile(readonlyRoot / "doc.txt"), "v1");

  Vault checkoutVault(checkoutRoot, archive);
  checkoutVault.Checkout(CheckoutOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .User = "simo", .Environment = "ws1"});
  MakeFile(checkoutRoot / "doc.txt", "v2");
  checkoutVault.Checkin(CheckinOptions{.RelativeFilePath = fs::path("doc.txt"), .User = "simo", .Environment = "ws1", .ReleaseLock = true});

  auto db = DB::Database::Open(archive / "content.db", checkoutRoot);
  auto file = db->GetFileByRelativePath("ROOT/doc.txt");
  auto versions = db->GetFileVersions(file);
  ASSERT_EQ(versions.size(), 2u);
  EXPECT_EQ(versions.back()->VersionNumber, 2);
  EXPECT_FALSE(db->GetCheckoutLock(file).has_value());
}

TEST(Vault, PushRejectsTamperedReadonlyFilesAndUnlockCanClearStaleLock)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  auto readonlyRoot = td.dir / "readonly";
  auto checkoutRoot = td.dir / "checkout";
  fs::create_directories(source);
  fs::create_directories(archive);
  fs::create_directories(readonlyRoot);
  fs::create_directories(checkoutRoot);

  Vault sourceVault(source, archive);
  MakeFile(source / "doc.txt", "v1");
  sourceVault.Push();

  Vault readonlyVault(readonlyRoot, archive);
  readonlyVault.Pop(MaterializationOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .Kind = DB::MaterializationKind::ReadOnlyCopy});
  fs::permissions(readonlyRoot / "doc.txt", fs::perms::owner_write, fs::perm_options::add);
  MakeFile(readonlyRoot / "doc.txt", "evil");
  EXPECT_THROW(readonlyVault.Push(), std::runtime_error);

  Vault checkoutVault(checkoutRoot, archive);
  checkoutVault.Checkout(CheckoutOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .User = "simo", .Environment = "ws1"});

  Vault otherVault(td.dir / "other", archive);
  EXPECT_THROW(otherVault.Checkout(CheckoutOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .User = "other", .Environment = "ws2"}), std::runtime_error);

  otherVault.Unlock(fs::path("doc.txt"));
  EXPECT_NO_THROW(otherVault.Checkout(CheckoutOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .User = "other", .Environment = "ws2"}));
}

TEST(Vault, ReadonlyCopiesBecomeNonOkWhenWriteBitsReturn)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  auto readonlyRoot = td.dir / "readonly";
  fs::create_directories(source);
  fs::create_directories(archive);
  fs::create_directories(readonlyRoot);

  Vault sourceVault(source, archive);
  MakeFile(source / "doc.txt", "v1");
  sourceVault.Push();

  Vault readonlyVault(readonlyRoot, archive);
  readonlyVault.Pop(MaterializationOptions{.RelativeFilePath = fs::path("doc.txt"), .VersionNumber = std::nullopt, .RelationScope = DB::RelationScope::None, .Kind = DB::MaterializationKind::ReadOnlyCopy});
  fs::permissions(readonlyRoot / "doc.txt", fs::perms::owner_write, fs::perm_options::add);

  const auto statuses = readonlyVault.Status();
  ASSERT_EQ(statuses.size(), 1u);
  EXPECT_EQ(statuses.front().State, DB::WorkspaceEntryState::Modified);

  readonlyVault.Repair();
  const auto repairedPerms = fs::status(readonlyRoot / "doc.txt").permissions();
  EXPECT_EQ(repairedPerms & fs::perms::owner_write, fs::perms::none);
}

TEST(Vault, PushCanFilterImportedFilesByIncludeAndIgnorePatterns)
{
  TempDir td;
  auto source = td.dir / "source";
  auto archive = td.dir / "archive";
  fs::create_directories(source / "docs");
  fs::create_directories(source / "tmp");
  fs::create_directories(archive);

  MakeFile(source / "docs" / "keep.txt", "keep");
  MakeFile(source / "docs" / "skip.tmp", "skip");
  MakeFile(source / "tmp" / "drop.txt", "drop");

  Vault(source, archive).Push(ImportOptions{
      .IncludePatterns = {"docs/**"},
      .IgnorePatterns = {"**/*.tmp"}});

  auto db = DB::Database::Open(archive / "content.db", source);
  EXPECT_NO_THROW(db->GetFileByRelativePath("ROOT/docs/keep.txt"));
  EXPECT_THROW(db->GetFileByRelativePath("ROOT/docs/skip.tmp"), std::runtime_error);
  EXPECT_THROW(db->GetFileByRelativePath("ROOT/tmp/drop.txt"), std::runtime_error);
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
