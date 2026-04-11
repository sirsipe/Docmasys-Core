#pragma once

#include "../DB/Database.hpp"
#include "../Types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Docmasys::CLI
{
  namespace fs = std::filesystem;

  struct ParsedRef
  {
    fs::path Path;
    std::optional<std::int64_t> Version;
  };

  struct RelationSpec
  {
    ParsedRef From;
    ParsedRef To;
    DB::RelationType Type;
  };

  using Options = std::unordered_map<std::string, std::vector<std::string>>;

  std::string ToString(DB::RelationType type);
  std::string ToString(DB::BlobStatus status);
  std::string ToString(DB::MaterializationKind kind);
  std::string ToString(DB::WorkspaceEntryState state);
  std::string ToString(PropertyValueType type);
  std::string ToString(const PropertyValue &value);

  DB::RelationType ParseRelationType(const std::string &value);
  DB::RelationScope ParseScope(const std::string &value);
  DB::MaterializationKind ParseMaterializationKind(const std::string &value);
  PropertyValue ParsePropertyValue(const std::string &type, const std::string &value);
  ParsedRef ParseRef(const std::string &value);
  Options ParseOptions(int argc, char *argv[], int start);
  std::vector<std::string> ReadManifestLines(const fs::path &file);
  std::vector<std::string> ValuesOf(const Options &options, const std::string &key);
  const std::string &Require(const Options &options, const std::string &key);
  std::optional<std::string> OptionalValue(const Options &options, const std::string &key);
  std::vector<std::string> CollectBatchValues(const Options &options, const std::string &valueKey, const std::string &fileKey);
  RelationSpec ParseRelationLine(const std::string &line);
  std::vector<RelationSpec> CollectRelationSpecs(const Options &options);
  void PrintUsage(const std::string &programName);
}
