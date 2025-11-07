#include "Vault.hpp"
#include "CAS/CAS.hpp"
#include <iostream>

using namespace Docmasys;
namespace fs = std::filesystem;

Vault::Vault(const fs::path &root, const fs::path &archive)
    : m_LocalRoot(root),
      m_ArchiveRoot(archive)
{
  m_Database = DB::Database::Open(archive / "content.db", m_LocalRoot);
  std::cout << "Vault: " << m_LocalRoot << std::endl;
}

void Vault::Push()
{
  for (const auto &entry : fs::recursive_directory_iterator(m_LocalRoot))
  {

    if (entry.is_directory())
      continue;

    const auto identity = CAS::Identify(entry.path());
    const auto blob = m_Database->Import(entry.path(), identity);
    if (blob->Status == DB::BlobStatus::Pending)
    {
      std::cout << "Uploading '" << entry.path() << "'..." << std::endl;
      const auto identity2 = CAS::Store(m_Database->DatabaseFile().parent_path(), entry.path());
      m_Database->UpdateBlobStatus(blob, DB::BlobStatus::Ready);
    }
  }
}

void Vault::Materialize(const std::shared_ptr<DB::Folder> &folder, const fs::path &localFolder)
{

  auto files = m_Database->GetFilesAndBlobs(folder);

  for (const auto &file : files)
  {
    if (file.second->Status == DB::BlobStatus::Ready)
    {
      fs::path target = localFolder / file.first->Name;
      CAS::Retrieve(m_ArchiveRoot, file.second->Hash, target);
    }
  }

  auto folders = m_Database->GetFolders(folder);
  for (const auto &subfolder : folders)
  {
    fs::path oPath = localFolder / subfolder->Name;
    fs::create_directory(oPath);
    Materialize(subfolder, oPath);
  }
}

void Vault::Pop()
{
  auto rootFolders = m_Database->GetFolders(nullptr);
  for (const auto &rootFolder : rootFolders)
  {
    if (rootFolder->Name == "ROOT")
    {
      Materialize(rootFolder, m_LocalRoot);
    }
  }
}