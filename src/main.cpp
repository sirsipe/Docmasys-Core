#include <filesystem>
#include <iostream>
#include <optional>
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

using Options = std::unordered_map<std::string, std::string>;

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
    options[key] = argv[++i];
  }
  return options;
}

const std::string &Require(const Options &options, const std::string &key)
{
  auto it = options.find(key);
  if (it == options.end() || it->second.empty())
    throw std::runtime_error("missing required option --" + key);
  return it->second;
}

std::optional<std::string> OptionalValue(const Options &options, const std::string &key)
{
  auto it = options.find(key);
  if (it == options.end())
    return std::nullopt;
  return it->second;
}

void PrintUsage(const std::string &programName)
{
  std::cout << "Docmasys CLI\n\n";
  std::cout << "Usage:\n";
  std::cout << "  " << programName << " help\n";
  std::cout << "  " << programName << " import --archive <archive> --root <folder>\n";
  std::cout << "  " << programName << " get --archive <archive> --ref <path[@version]> [--out <folder>] [--scope none|strong|strong+weak|all]\n";
  std::cout << "  " << programName << " versions --archive <archive> --path <path>\n";
  std::cout << "  " << programName << " relate --archive <archive> --from <path[@version]> --to <path[@version]> --type strong|weak|optional\n";
  std::cout << "  " << programName << " relations --archive <archive> --ref <path[@version]> [--type strong|weak|optional|all]\n";
  std::cout << "  " << programName << " props list --archive <archive> --ref <path[@version]>\n";
  std::cout << "  " << programName << " props get --archive <archive> --ref <path[@version]> --name <property>\n";
  std::cout << "  " << programName << " props set --archive <archive> --ref <path[@version]> --name <property> --type string|int|bool --value <value>\n";
  std::cout << "  " << programName << " props remove --archive <archive> --ref <path[@version]> --name <property>\n";
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
  const auto ref = ParseRef(Require(options, "ref"));
  Vault vault(out, archive);
  vault.Pop(MaterializationOptions{
      .RelativeFilePath = NormalizeVaultPath(ref.Path),
      .VersionNumber = ref.Version,
      .RelationScope = ParseScope(OptionalValue(options, "scope").value_or("none"))});
  return 0;
}

int RunVersions(const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  auto file = db->GetFileByRelativePath(NormalizeVaultPath(Require(options, "path")));
  for (const auto &version : db->GetFileVersions(file))
    std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber << "\n";
  return 0;
}

int RunRelate(const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  const auto fromRef = ParseRef(Require(options, "from"));
  const auto toRef = ParseRef(Require(options, "to"));
  if (!fromRef.Version || !toRef.Version)
    throw std::runtime_error("relate requires explicit @version in both --from and --to");
  auto fromFile = db->GetFileByRelativePath(NormalizeVaultPath(fromRef.Path));
  auto toFile = db->GetFileByRelativePath(NormalizeVaultPath(toRef.Path));
  db->AddRelation(db->GetFileVersion(fromFile, fromRef.Version), db->GetFileVersion(toFile, toRef.Version), ParseRelationType(Require(options, "type")));
  return 0;
}

int RunRelations(const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  const auto ref = ParseRef(Require(options, "ref"));
  auto file = db->GetFileByRelativePath(NormalizeVaultPath(ref.Path));
  auto version = db->GetFileVersion(file, ref.Version);
  std::optional<DB::RelationType> typeFilter;
  const auto filter = OptionalValue(options, "type").value_or("all");
  if (filter != "all")
    typeFilter = ParseRelationType(filter);
  for (const auto &rel : db->GetOutgoingRelations(version, typeFilter))
  {
    auto targetFile = db->GetFileById(rel.To->FileId);
    std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber
              << " --" << ToString(rel.Type) << "--> "
              << db->BuildRelativePath(targetFile).generic_string() << '@' << rel.To->VersionNumber << "\n";
  }
  return 0;
}

int RunProps(const std::string &subcommand, const Options &options)
{
  auto db = DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
  const auto ref = ParseRef(Require(options, "ref"));
  auto file = db->GetFileByRelativePath(NormalizeVaultPath(ref.Path));
  auto version = db->GetFileVersion(file, ref.Version);

  if (subcommand == "list")
  {
    for (const auto &property : db->ListVersionProperties(version))
      std::cout << property.Name << '\t' << ToString(property.Type) << '\t' << ToString(property.Value) << "\n";
    return 0;
  }

  if (subcommand == "get")
  {
    const auto property = db->GetVersionProperty(version, Require(options, "name"));
    if (!property)
      throw std::runtime_error("property not found");
    std::cout << property->Name << '\t' << ToString(property->Type) << '\t' << ToString(property->Value) << "\n";
    return 0;
  }

  if (subcommand == "set")
  {
    db->SetVersionProperty(version, Require(options, "name"), ParsePropertyValue(Require(options, "type"), Require(options, "value")));
    return 0;
  }

  if (subcommand == "remove")
  {
    if (!db->RemoveVersionProperty(version, Require(options, "name")))
      throw std::runtime_error("property not found");
    return 0;
  }

  throw std::runtime_error("unknown props subcommand: " + subcommand);
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
