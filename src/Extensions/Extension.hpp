#pragma once
#include "../DB/Database.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Docmasys::Extensions
{
  struct ImportedVersionContext
  {
    DB::Database &Database;
    std::shared_ptr<DB::File> File;
    std::shared_ptr<DB::FileVersion> Version;
    std::filesystem::path AbsolutePath;
    std::filesystem::path RelativePath;

    void AddRelationTo(const std::filesystem::path &targetPath, std::int64_t versionNumber, DB::RelationType type) const;
    void SetProperty(const std::string &name, const PropertyValue &value) const;
  };

  class ImportExtension
  {
  public:
    virtual ~ImportExtension() = default;
    virtual std::string Name() const = 0;
    virtual bool Supports(const ImportedVersionContext &context) const = 0;
    virtual void OnImported(const ImportedVersionContext &context) const = 0;
  };

  class ImportExtensionRegistry
  {
  public:
    static ImportExtensionRegistry BuiltIn();
    void Run(const ImportedVersionContext &context) const;

  private:
    std::vector<std::shared_ptr<ImportExtension>> m_Extensions;
    explicit ImportExtensionRegistry(std::vector<std::shared_ptr<ImportExtension>> extensions);
  };
}
