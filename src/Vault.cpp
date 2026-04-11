#include "Vault.hpp"
#include "CAS/CAS.hpp"
#include "Common/PathUtils.hpp"

#include <fstream>
#include <regex>
#include <system_error>

using namespace Docmasys;
namespace fs = std::filesystem;

namespace
{
void RemoveExistingPath(const fs::path &path)
{
  std::error_code ec;
  if (fs::exists(path, ec) || fs::is_symlink(path, ec))
    fs::remove(path, ec);
}

void SetReadOnly(const fs::path &path)
{
  std::error_code ec;
  auto perms = fs::status(path, ec).permissions();
  if (!ec)
    fs::permissions(path,
                    perms & ~fs::perms::owner_write & ~fs::perms::group_write & ~fs::perms::others_write,
                    fs::perm_options::replace,
                    ec);
}

void SetWritable(const fs::path &path)
{
  std::error_code ec;
  auto perms = fs::status(path, ec).permissions();
  if (!ec)
    fs::permissions(path,
                    perms | fs::perms::owner_write,
                    fs::perm_options::replace,
                    ec);
}

std::string GlobToRegex(const std::string &pattern)
{
  std::string regex = "^";
  for (std::size_t i = 0; i < pattern.size(); ++i)
  {
    const char c = pattern[i];
    if (c == '*')
    {
      if (i + 1 < pattern.size() && pattern[i + 1] == '*')
      {
        regex += ".*";
        ++i;
      }
      else
      {
        regex += "[^/]*";
      }
      continue;
    }

    if (c == '?')
    {
      regex += "[^/]";
      continue;
    }

    if (c == '/' || c == '\\')
    {
      regex += "/";
      continue;
    }

    if (std::string(".^$|()[]{}+").find(c) != std::string::npos)
      regex += '\\';
    regex += c;
  }
  regex += "$";
  return regex;
}

bool MatchesAnyPattern(const fs::path &relativePath, const std::vector<std::string> &patterns)
{
  if (patterns.empty())
    return false;

  const auto candidate = relativePath.generic_string();
  for (const auto &pattern : patterns)
  {
    if (std::regex_match(candidate, std::regex(GlobToRegex(pattern), std::regex::ECMAScript | std::regex::icase)))
      return true;
  }
  return false;
}

bool ShouldImportPath(const fs::path &root, const fs::path &path, const ImportOptions &options)
{
  fs::path relative;
  if (!Common::TryMakeVaultRelativePath(root, path, relative))
    return false;

  const auto workspaceRelative = Common::WorkspacePathFromVaultPath(relative);
  const bool included = options.IncludePatterns.empty() || MatchesAnyPattern(workspaceRelative, options.IncludePatterns);
  if (!included)
    return false;
  if (MatchesAnyPattern(workspaceRelative, options.IgnorePatterns))
    return false;
  return true;
}
}

Vault::Vault(const fs::path &root, const fs::path &archive)
    : m_Database(DB::Database::Open(archive / "content.db", root)),
      m_LocalRoot(root),
      m_ArchiveRoot(archive),
      m_Extensions(Extensions::ImportExtensionRegistry::BuiltIn())
{
}

void Vault::Push()
{
  Push(ImportOptions{});
}

void Vault::Push(const ImportOptions &options)
{
  const auto statuses = Status();
  for (const auto &status : statuses)
  {
    if (status.Entry.Kind == DB::MaterializationKind::CheckoutCopy)
      continue;
    if (status.State == DB::WorkspaceEntryState::Ok)
      continue;
    throw std::runtime_error("workspace contains tampered readonly file '" + status.Entry.RelativePath.generic_string() + "' (" +
                             (status.State == DB::WorkspaceEntryState::Missing ? "missing" : status.State == DB::WorkspaceEntryState::Modified ? "modified" : "replaced") +
                             "); use repair or explicit checkout/checkin flow");
  }

  for (const auto &entry : fs::recursive_directory_iterator(m_LocalRoot))
  {
    if (entry.is_directory())
      continue;
    if (!ShouldImportPath(m_LocalRoot, entry.path(), options))
      continue;

    const auto identity = CAS::Identify(entry.path());
    const auto import = m_Database->Import(entry.path(), identity);
    const auto blob = m_Database->GetBlob(import.Version->BlobId);
    if (blob->Status == DB::BlobStatus::Pending)
    {
      static_cast<void>(CAS::Store(m_Database->DatabaseFile().parent_path(), entry.path()));
      m_Database->UpdateBlobStatus(blob, DB::BlobStatus::Ready);
    }
    if (!import.CreatedNewVersion)
      continue;

    const auto file = m_Database->GetFileById(import.Version->FileId);
    m_Extensions.Run(Extensions::ImportedVersionContext{
        .Database = *m_Database,
        .File = file,
        .Version = import.Version,
        .AbsolutePath = entry.path(),
        .RelativePath = m_Database->BuildRelativePath(file)});
  }
}

void Vault::MaterializeFiles(const std::vector<DB::MaterializedFile> &files, DB::MaterializationKind kind)
{
  for (const auto &entry : files)
  {
    if (entry.BlobRef->Status != DB::BlobStatus::Ready)
      continue;

    const auto relative = Common::WorkspacePathFromVaultPath(entry.RelativePath);
    const auto outPath = m_LocalRoot / relative;
    fs::create_directories(outPath.parent_path());
    RemoveExistingPath(outPath);

    if (kind == DB::MaterializationKind::ReadOnlySymlink)
    {
      const auto target = CAS::BlobPath(m_ArchiveRoot, entry.BlobRef->Hash);
      std::error_code ec;
      fs::create_symlink(target, outPath, ec);
      if (ec)
        throw std::runtime_error("failed to create symlink materialization for '" + relative.generic_string() + "': " + ec.message());
    }
    else
    {
      CAS::Retrieve(m_ArchiveRoot, entry.BlobRef->Hash, outPath);
      if (kind == DB::MaterializationKind::ReadOnlyCopy)
        SetReadOnly(outPath);
      else
        SetWritable(outPath);
    }

    m_Database->UpsertWorkspaceEntry(m_LocalRoot, entry.LogicalFile, entry.Version, relative, kind);
  }
}

void Vault::MaterializeFolderTree(const DB::Folder &folder, const fs::path &localFolder, DB::MaterializationKind kind)
{
  fs::create_directories(localFolder);
  const auto folderRef = std::make_shared<DB::Folder>(folder);
  MaterializeFiles(m_Database->GetMaterializedFiles(folderRef), kind);
  for (const auto &subfolder : m_Database->GetFolders(folderRef))
    MaterializeFolderTree(*subfolder, localFolder / subfolder->Name, kind);
}

void Vault::Pop()
{
  for (const auto &rootFolder : m_Database->GetFolders(nullptr))
    if (rootFolder->Name == "ROOT")
      MaterializeFolderTree(*rootFolder, m_LocalRoot, DB::MaterializationKind::ReadOnlyCopy);
}

void Vault::Pop(const MaterializationOptions &options)
{
  const auto relative = Common::RequireRootedVaultPath(options.RelativeFilePath);

  const auto file = m_Database->GetFileByRelativePath(relative);
  const auto version = m_Database->GetFileVersion(file, options.VersionNumber);
  MaterializeFiles(m_Database->ResolveMaterialization(version, options.RelationScope), options.Kind);
}

void Vault::Checkout(const CheckoutOptions &options)
{
  if (options.User.empty())
    throw std::runtime_error("checkout requires user");
  if (options.Environment.empty())
    throw std::runtime_error("checkout requires environment");

  const auto relative = Common::RequireRootedVaultPath(options.RelativeFilePath);

  const auto file = m_Database->GetFileByRelativePath(relative);
  const auto version = m_Database->GetFileVersion(file, options.VersionNumber);
  m_Database->AcquireCheckoutLock(file, version, options.User, options.Environment, m_LocalRoot);
  MaterializeFiles(m_Database->ResolveMaterialization(version, options.RelationScope), DB::MaterializationKind::CheckoutCopy);
}

std::vector<DB::WorkspaceEntryStatus> Vault::Status() const
{
  return m_Database->GetWorkspaceStatus(m_LocalRoot);
}

void Vault::Repair()
{
  for (const auto &status : Status())
  {
    if (status.State == DB::WorkspaceEntryState::Ok)
      continue;
    if (status.Entry.Kind == DB::MaterializationKind::CheckoutCopy)
      continue;

    MaterializeFiles({DB::MaterializedFile{
        .LogicalFile = status.Entry.LogicalFile,
        .Version = status.Entry.Version,
        .BlobRef = m_Database->GetBlob(status.Entry.Version->BlobId),
        .RelativePath = Common::EnsureRootedVaultPath(status.Entry.RelativePath)}},
        status.Entry.Kind);
  }
}

void Vault::Checkin(const CheckinOptions &options)
{
  if (options.User.empty())
    throw std::runtime_error("checkin requires user");
  if (options.Environment.empty())
    throw std::runtime_error("checkin requires environment");

  const auto relative = Common::RequireRootedVaultPath(options.RelativeFilePath);

  const auto file = m_Database->GetFileByRelativePath(relative);
  const auto entry = m_Database->GetWorkspaceEntry(m_LocalRoot, file);
  if (!entry)
    throw std::runtime_error("file is not materialized in this workspace");
  if (entry->Kind != DB::MaterializationKind::CheckoutCopy)
    throw std::runtime_error("file is not checked out in this workspace");

  const auto lock = m_Database->GetCheckoutLock(file);
  if (!lock)
    throw std::runtime_error("file is not locked for checkout");
  if (lock->User != options.User || lock->Environment != options.Environment || fs::weakly_canonical(lock->WorkspaceRoot) != fs::weakly_canonical(m_LocalRoot))
    throw std::runtime_error("checkout lock is owned by a different user/environment/workspace");

  const auto statuses = Status();
  for (const auto &status : statuses)
  {
    if (status.Entry.LogicalFile->Id == file->Id)
    {
      if (status.State == DB::WorkspaceEntryState::Missing)
        throw std::runtime_error("checked out file is missing from workspace");
      if (status.State == DB::WorkspaceEntryState::Replaced)
        throw std::runtime_error("checked out file was replaced unexpectedly");
      break;
    }
  }

  const auto fullPath = m_LocalRoot / Common::WorkspacePathFromVaultPath(relative);
  const auto identity = CAS::Identify(fullPath);
  const auto import = m_Database->Import(fullPath, identity);
  const auto blob = m_Database->GetBlob(import.Version->BlobId);
  if (blob->Status == DB::BlobStatus::Pending)
  {
    static_cast<void>(CAS::Store(m_Database->DatabaseFile().parent_path(), fullPath));
    m_Database->UpdateBlobStatus(blob, DB::BlobStatus::Ready);
  }

  auto currentVersion = m_Database->GetFileVersion(file, std::nullopt);
  m_Database->UpsertWorkspaceEntry(m_LocalRoot, file, currentVersion, Common::WorkspacePathFromVaultPath(relative), DB::MaterializationKind::CheckoutCopy);
  if (options.ReleaseLock)
    m_Database->ReleaseCheckoutLock(file, options.User, options.Environment, m_LocalRoot);
}

void Vault::Unlock(const fs::path &relativeFilePath)
{
  const auto relative = Common::RequireRootedVaultPath(relativeFilePath);
  const auto file = m_Database->GetFileByRelativePath(relative);
  if (!m_Database->ForceReleaseCheckoutLock(file))
    throw std::runtime_error("file was not locked");
}
