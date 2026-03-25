#include "Extension.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace Docmasys;
using namespace Docmasys::Extensions;

namespace
{
class FileFactsExtension final : public ImportExtension
{
public:
  std::string Name() const override { return "file-facts"; }
  bool Supports(const ImportedVersionContext &) const override { return true; }
  void OnImported(const ImportedVersionContext &context) const override
  {
    const auto extension = context.AbsolutePath.extension().string();
    context.SetProperty("file.extension", extension);
    context.SetProperty("file.filename", context.AbsolutePath.filename().string());
  }
};

class RelationManifestExtension final : public ImportExtension
{
public:
  std::string Name() const override { return "relation-manifest"; }
  bool Supports(const ImportedVersionContext &context) const override
  {
    return context.AbsolutePath.extension() == ".dmsrel";
  }

  void OnImported(const ImportedVersionContext &context) const override
  {
    std::ifstream input(context.AbsolutePath);
    if (!input)
      throw std::runtime_error("failed to read relation manifest: " + context.AbsolutePath.string());

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line))
    {
      ++lineNumber;
      if (line.empty() || line[0] == '#')
        continue;

      std::istringstream iss(line);
      std::string relationType;
      std::string targetRef;
      if (!(iss >> relationType >> targetRef))
        throw std::runtime_error("invalid .dmsrel line " + std::to_string(lineNumber) + ": expected `<type> <path@version>`");

      const auto at = targetRef.rfind('@');
      if (at == std::string::npos)
        throw std::runtime_error("invalid .dmsrel line " + std::to_string(lineNumber) + ": missing @version");

      DB::RelationType type;
      if (relationType == "strong") type = DB::RelationType::Strong;
      else if (relationType == "weak") type = DB::RelationType::Weak;
      else if (relationType == "optional") type = DB::RelationType::Optional;
      else throw std::runtime_error("invalid .dmsrel relation type on line " + std::to_string(lineNumber));

      context.AddRelationTo(targetRef.substr(0, at), std::stoll(targetRef.substr(at + 1)), type);
    }
  }
};
} // namespace

void ImportedVersionContext::AddRelationTo(const fs::path &targetPath, std::int64_t versionNumber, DB::RelationType type) const
{
  auto file = Database.GetFileByRelativePath(targetPath.is_absolute() ? targetPath : fs::path("ROOT") / targetPath);
  Database.AddRelation(Version, Database.GetFileVersion(file, versionNumber), type);
}

void ImportedVersionContext::SetProperty(const std::string &name, const PropertyValue &value) const
{
  Database.SetVersionProperty(Version, name, value);
}

ImportExtensionRegistry::ImportExtensionRegistry(std::vector<std::shared_ptr<ImportExtension>> extensions)
    : m_Extensions(std::move(extensions))
{
}

ImportExtensionRegistry ImportExtensionRegistry::BuiltIn()
{
  return ImportExtensionRegistry({std::make_shared<FileFactsExtension>(), std::make_shared<RelationManifestExtension>()});
}

void ImportExtensionRegistry::Run(const ImportedVersionContext &context) const
{
  for (const auto &extension : m_Extensions)
    if (extension->Supports(context))
      extension->OnImported(context);
}
