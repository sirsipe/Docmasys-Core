#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Vault.hpp"
#include "DB/Database.hpp"

namespace fs = std::filesystem;
using namespace Docmasys;

namespace
{
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

std::string ToString(DB::RelationType type)
{
  switch (type)
  {
  case DB::RelationType::Strong:
    return "strong";
  case DB::RelationType::Weak:
    return "weak";
  case DB::RelationType::Optional:
    return "optional";
  }
  throw std::runtime_error("unknown relation type");
}

std::string ToString(PropertyValueType type)
{
  switch (type)
  {
  case PropertyValueType::String:
    return "string";
  case PropertyValueType::Int:
    return "int";
  case PropertyValueType::Bool:
    return "bool";
  }
  throw std::runtime_error("unknown property type");
}

std::string ToString(const PropertyValue &value)
{
  if (const auto *s = std::get_if<std::string>(&value)) return *s;
  if (const auto *i = std::get_if<std::int64_t>(&value)) return std::to_string(*i);
  return std::get<bool>(value) ? "true" : "false";
}

DB::RelationType ParseRelationType(const std::string &value)
{
  if (value == "strong") return DB::RelationType::Strong;
  if (value == "weak") return DB::RelationType::Weak;
  if (value == "optional") return DB::RelationType::Optional;
  throw std::runtime_error("invalid relation type: " + value);
}

DB::RelationScope ParseScope(const std::string &value)
{
  if (value == "none") return DB::RelationScope::None;
  if (value == "strong") return DB::RelationScope::Strong;
  if (value == "strong+weak") return DB::RelationScope::StrongAndWeak;
  if (value == "all") return DB::RelationScope::All;
  throw std::runtime_error("invalid scope: " + value);
}

PropertyValue ParsePropertyValue(const std::string &type, const std::string &value)
{
  if (type == "string") return value;
  if (type == "int") return static_cast<std::int64_t>(std::stoll(value));
  if (type == "bool")
  {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    throw std::runtime_error("invalid bool value: " + value);
  }
  throw std::runtime_error("invalid property type: " + type);
}

ParsedRef ParseRef(const std::string &value)
{
  const auto at = value.rfind('@');
  if (at == std::string::npos)
    return {fs::path(value), std::nullopt};
  ParsedRef ref{fs::path(value.substr(0, at)), std::nullopt};
  ref.Version = std::stoll(value.substr(at + 1));
  return ref;
}

fs::path NormalizeVaultPath(const fs::path &path)
{
  auto normalized = path.lexically_normal();
  if (!normalized.empty() && normalized.begin()->string() == "ROOT")
    return normalized;
  return fs::path("ROOT") / normalized;
}

Options ParseOptions(int argc, char *argv[], int start)
{
  Options options;
  for (int i = start; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (!arg.starts_with("--"))
      throw std::runtime_error("unexpected argument: " + arg);
    const auto key = arg.substr(2);
    if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--"))
      throw std::runtime_error("missing value for --" + key);
    options[key].push_back(argv[++i]);
  }
  return options;
}

std::vector<std::string> ReadManifestLines(const fs::path &file)
{
  std::ifstream input(file);
  if (!input)
    throw std::runtime_error("failed to open manifest: " + file.string());

  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);)
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#')
      continue;
    lines.push_back(line.substr(first));
  }
  return lines;
}

std::vector<std::string> ValuesOf(const Options &options, const std::string &key)
{
  auto it = options.find(key);
  if (it == options.end())
    return {};
  return it->second;
}

const std::string &Require(const Options &options, const std::string &key)
{
  auto it = options.find(key);
  if (it == options.end() || it->second.empty() || it->second.front().empty())
    throw std::runtime_error("missing required option --" + key);
  return it->second.front();
}

std::optional<std::string> OptionalValue(const Options &options, const std::string &key)
{
  auto it = options.find(key);
  if (it == options.end() || it->second.empty())
    return std::nullopt;
  return it->second.front();
}

std::vector<std::string> CollectBatchValues(const Options &options, const std::string &valueKey, const std::string &fileKey)
{
  auto values = ValuesOf(options, valueKey);
  for (const auto &manifest : ValuesOf(options, fileKey))
  {
    auto lines = ReadManifestLines(manifest);
    values.insert(values.end(), lines.begin(), lines.end());
  }
  return values;
}

RelationSpec ParseRelationLine(const std::string &line)
{
  std::istringstream input(line);
  std::string from;
  std::string to;
  std::string type;
  if (!(input >> from >> to >> type))
    throw std::runtime_error("invalid relation manifest line: " + line);
  std::string extra;
  if (input >> extra)
    throw std::runtime_error("invalid relation manifest line: " + line);
  return {ParseRef(from), ParseRef(to), ParseRelationType(type)};
}

std::vector<RelationSpec> CollectRelationSpecs(const Options &options)
{
  std::vector<RelationSpec> specs;

  auto fromRefs = ValuesOf(options, "from");
  auto toRefs = ValuesOf(options, "to");
  auto types = ValuesOf(options, "type");

  if (!fromRefs.empty() || !toRefs.empty() || !types.empty())
  {
    if (fromRefs.empty() || toRefs.empty() || types.empty())
      throw std::runtime_error("relate requires --from, --to, and --type");
    if (fromRefs.size() != toRefs.size())
      throw std::runtime_error("relate requires matching counts for --from and --to");
    if (types.size() != 1 && types.size() != fromRefs.size())
      throw std::runtime_error("relate requires either one --type for all pairs or one --type per pair");

    for (std::size_t i = 0; i < fromRefs.size(); ++i)
      specs.push_back({ParseRef(fromRefs[i]), ParseRef(toRefs[i]), ParseRelationType(types[types.size() == 1 ? 0 : i])});
  }

  for (const auto &manifest : ValuesOf(options, "edges-file"))
  {
    for (const auto &line : ReadManifestLines(manifest))
      specs.push_back(ParseRelationLine(line));
  }

  if (specs.empty())
    throw std::runtime_error("relate requires at least one relation spec");

  return specs;
}

void PrintUsage(const std::string &programName)
{
  std::cout << "Docmasys CLI\n\n";
  std::cout << "Usage:\n";
  std::cout << "  " << programName << " help\n";
  std::cout << "  " << programName << " import --archive <archive> --root <folder>\n";
  std::cout << "  " << programName << " get --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--out <folder>] [--scope none|strong|strong+weak|all]\n";
  std::cout << "  " << programName << " versions --archive <archive> (--path <path> | --paths-file <file>)...\n";
  std::cout << "  " << programName << " relate --archive <archive> [--from <path[@version]> --to <path[@version]> --type strong|weak|optional]... [--edges-file <file>]\n";
  std::cout << "  " << programName << " relations --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--type strong|weak|optional|all]\n";
  std::cout << "  " << programName << " props list --archive <archive> (--ref <path[@version]> | --refs-file <file>)...\n";
  std::cout << "  " << programName << " props get --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>\n";
  std::cout << "  " << programName << " props set --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property> --type string|int|bool --value <value>\n";
  std::cout << "  " << programName << " props remove --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>\n";
  std::cout << "  " << programName << " inspect --archive <archive> [--root <folder>]\n";
}

int RunImport(const Options &options)
{
  Vault(Require(options, "root"), Require(options, "archive")).Push();
  return 0;
}

int RunGet(const Options &options)
{
  const auto archive = fs::path(Require(options, "archive"));
  const auto out = fs::path(OptionalValue(options, "out").value_or("."));
  const auto refs = CollectBatchValues(options, "ref", "refs-file");
  if (refs.empty())
    throw std::runtime_error("get requires at least one --ref or --refs-file");

  Vault vault(out, archive);
  const auto scope = ParseScope(OptionalValue(options, "scope").value_or("none"));
  for (const auto &rawRef : refs)
  {
    const auto ref = ParseRef(rawRef);
    vault.Pop(MaterializationOptions{
        .RelativeFilePath = NormalizeVaultPath(ref.Path),
        .VersionNumber = ref.Version,
        .RelationScope = scope});
  }
  return 0;
}

int RunVersions(const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  const auto paths = CollectBatchValues(options, "path", "paths-file");
  if (paths.empty())
    throw std::runtime_error("versions requires at least one --path or --paths-file");

  for (const auto &rawPath : paths)
  {
    auto file = db->GetFileByRelativePath(NormalizeVaultPath(rawPath));
    for (const auto &version : db->GetFileVersions(file))
      std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber << "\n";
  }
  return 0;
}

int RunRelate(const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  for (const auto &spec : CollectRelationSpecs(options))
  {
    if (!spec.From.Version || !spec.To.Version)
      throw std::runtime_error("relate requires explicit @version in both endpoints");
    auto fromFile = db->GetFileByRelativePath(NormalizeVaultPath(spec.From.Path));
    auto toFile = db->GetFileByRelativePath(NormalizeVaultPath(spec.To.Path));
    db->AddRelation(db->GetFileVersion(fromFile, spec.From.Version), db->GetFileVersion(toFile, spec.To.Version), spec.Type);
  }
  return 0;
}

int RunRelations(const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  std::optional<DB::RelationType> typeFilter;
  const auto filter = OptionalValue(options, "type").value_or("all");
  if (filter != "all")
    typeFilter = ParseRelationType(filter);

  const auto refs = CollectBatchValues(options, "ref", "refs-file");
  if (refs.empty())
    throw std::runtime_error("relations requires at least one --ref or --refs-file");

  for (const auto &rawRef : refs)
  {
    const auto ref = ParseRef(rawRef);
    auto file = db->GetFileByRelativePath(NormalizeVaultPath(ref.Path));
    auto version = db->GetFileVersion(file, ref.Version);
    for (const auto &rel : db->GetOutgoingRelations(version, typeFilter))
    {
      auto targetFile = db->GetFileById(rel.To->FileId);
      std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber
                << " --" << ToString(rel.Type) << "--> "
                << db->BuildRelativePath(targetFile).generic_string() << '@' << rel.To->VersionNumber << "\n";
    }
  }
  return 0;
}

int RunProps(const std::string &subcommand, const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  const auto refs = CollectBatchValues(options, "ref", "refs-file");
  if (refs.empty())
    throw std::runtime_error("props requires at least one --ref or --refs-file");

  for (const auto &rawRef : refs)
  {
    const auto ref = ParseRef(rawRef);
    auto file = db->GetFileByRelativePath(NormalizeVaultPath(ref.Path));
    auto version = db->GetFileVersion(file, ref.Version);

    if (subcommand == "list")
    {
      for (const auto &property : db->ListVersionProperties(version))
        std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber << '\t'
                  << property.Name << '\t' << ToString(property.Type) << '\t' << ToString(property.Value) << "\n";
      continue;
    }

    if (subcommand == "get")
    {
      const auto property = db->GetVersionProperty(version, Require(options, "name"));
      if (!property)
        throw std::runtime_error("property not found");
      std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber << '\t'
                << property->Name << '\t' << ToString(property->Type) << '\t' << ToString(property->Value) << "\n";
      continue;
    }

    if (subcommand == "set")
    {
      db->SetVersionProperty(version, Require(options, "name"), ParsePropertyValue(Require(options, "type"), Require(options, "value")));
      continue;
    }

    if (subcommand == "remove")
    {
      if (!db->RemoveVersionProperty(version, Require(options, "name")))
        throw std::runtime_error("property not found");
      continue;
    }

    throw std::runtime_error("unknown props subcommand: " + subcommand);
  }

  return 0;
}

int RunInspect(const Options &options)
{
  const auto archive = fs::path(Require(options, "archive"));
  auto db = DB::Database::Open(archive / "content.db", OptionalValue(options, "root").value_or("."));
  for (const auto &item : db->InspectCurrentFiles())
    std::cout << item.RelativePath.generic_string() << " @" << item.Version->VersionNumber << "\n";
  return 0;
}
}

int main(int argc, char *argv[])
{
  try
  {
    const std::string programName = argc > 0 ? argv[0] : "Docmasys";
    if (argc < 2)
    {
      PrintUsage(programName);
      return 1;
    }

    const std::string command = argv[1];
    if (command == "help" || command == "--help" || command == "-h")
    {
      PrintUsage(programName);
      return 0;
    }

    if (command == "props")
    {
      if (argc < 3)
        throw std::runtime_error("props requires a subcommand");
      return RunProps(argv[2], ParseOptions(argc, argv, 3));
    }

    const auto options = ParseOptions(argc, argv, 2);
    if (command == "import") return RunImport(options);
    if (command == "get") return RunGet(options);
    if (command == "versions") return RunVersions(options);
    if (command == "relate") return RunRelate(options);
    if (command == "relations") return RunRelations(options);
    if (command == "inspect") return RunInspect(options);

    throw std::runtime_error("unknown command: " + command);
  }
  catch (const std::exception &ex)
  {
    std::cerr << "error: " << ex.what() << '\n';
    return 1;
  }
}
