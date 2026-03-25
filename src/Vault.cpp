#include "Vault.hpp"
#include "CAS/CAS.hpp"

using namespace Docmasys;
namespace fs = std::filesystem;

Vault::Vault(const fs::path &root, const fs::path &archive)
    : m_Database(DB::Database::Open(archive / "content.db", root)),
      m_LocalRoot(root),
      m_ArchiveRoot(archive)
{
}

void Vault::Push()
{
  for (const auto &entry : fs::recursive_directory_iterator(m_LocalRoot))
  {
    if (entry.is_directory())
      continue;

    const auto identity = CAS::Identify(entry.path());
    const auto version = m_Database->Import(entry.path(), identity);
    const auto blob = m_Database->GetBlob(version->BlobId);
    if (blob->Status == DB::BlobStatus::Pending)
    {
      static_cast<void>(CAS::Store(m_Database->DatabaseFile().parent_path(), entry.path()));
      m_Database->UpdateBlobStatus(blob, DB::BlobStatus::Ready);
    }
  }
}

void Vault::MaterializeFiles(const std::vector<DB::MaterializedFile> &files)
{
  for (const auto &entry : files)
  {
    if (entry.BlobRef->Status != DB::BlobStatus::Ready)
      continue;

    const auto relative = entry.RelativePath.lexically_relative("ROOT");
    CAS::Retrieve(m_ArchiveRoot, entry.BlobRef->Hash, m_LocalRoot / relative);
  }
}

void Vault::MaterializeFolderTree(const std::shared_ptr<DB::Folder> &folder, const fs::path &localFolder)
{
  fs::create_directories(localFolder);
  MaterializeFiles(m_Database->GetMaterializedFiles(folder));
  for (const auto &subfolder : m_Database->GetFolders(folder))
    MaterializeFolderTree(subfolder, localFolder / subfolder->Name);
}

void Vault::Pop()
{
  for (const auto &rootFolder : m_Database->GetFolders(nullptr))
    if (rootFolder->Name == "ROOT")
      MaterializeFolderTree(rootFolder, m_LocalRoot);
}

void Vault::Pop(const MaterializationOptions &options)
{
  auto relative = options.RelativeFilePath.lexically_normal();
  if (relative.empty())
    throw std::runtime_error("relative file path is required");
  if (*relative.begin() != fs::path("ROOT"))
    relative = fs::path("ROOT") / relative;

  const auto file = m_Database->GetFileByRelativePath(relative);
  const auto version = m_Database->GetFileVersion(file, options.VersionNumber);
  MaterializeFiles(m_Database->ResolveMaterialization(version, options.RelationScope));
}
