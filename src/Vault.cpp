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
      CAS::Store(m_Database->DatabaseFile().parent_path(), entry.path());
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
    CAS::Retrieve(m_ArchiveRoot, entry.BlobRef->Hash, m_LocalRoot / entry.RelativePath.lexically_relative("ROOT"));
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
