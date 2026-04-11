#include "CommandHelpers.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace Docmasys::CLI
{
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

  std::string ToString(DB::BlobStatus status)
  {
    switch (status)
    {
    case DB::BlobStatus::Pending:
      return "pending";
    case DB::BlobStatus::Ready:
      return "ready";
    }
    throw std::runtime_error("unknown blob status");
  }

  std::string ToString(DB::MaterializationKind kind)
  {
    switch (kind)
    {
    case DB::MaterializationKind::ReadOnlyCopy:
      return "readonly-copy";
    case DB::MaterializationKind::ReadOnlySymlink:
      return "readonly-symlink";
    case DB::MaterializationKind::CheckoutCopy:
      return "checkout-copy";
    }
    throw std::runtime_error("unknown materialization kind");
  }

  std::string ToString(DB::WorkspaceEntryState state)
  {
    switch (state)
    {
    case DB::WorkspaceEntryState::Ok:
      return "ok";
    case DB::WorkspaceEntryState::Missing:
      return "missing";
    case DB::WorkspaceEntryState::Modified:
      return "modified";
    case DB::WorkspaceEntryState::Replaced:
      return "replaced";
    }
    throw std::runtime_error("unknown workspace state");
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

  DB::MaterializationKind ParseMaterializationKind(const std::string &value)
  {
    if (value == "readonly-copy") return DB::MaterializationKind::ReadOnlyCopy;
    if (value == "readonly-symlink") return DB::MaterializationKind::ReadOnlySymlink;
    if (value == "checkout-copy") return DB::MaterializationKind::CheckoutCopy;
    throw std::runtime_error("invalid materialization kind: " + value);
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
      for (const auto &line : ReadManifestLines(manifest))
        specs.push_back(ParseRelationLine(line));

    if (specs.empty())
      throw std::runtime_error("relate requires at least one relation spec");

    return specs;
  }

  void PrintUsage(const std::string &programName)
  {
    std::cout << "Docmasys CLI\n\n";
    std::cout << "Archive / workspace engine with immutable versions, relations, properties, and explicit checkout flow.\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << programName << " help\n";
    std::cout << "  " << programName << " import --archive <archive> --root <folder> [--include <glob> | --includes-file <file>]... [--ignore <glob> | --ignores-file <file>]...\n";
    std::cout << "  " << programName << " get --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--out <folder>] [--scope none|strong|strong+weak|all] [--mode readonly-copy|readonly-symlink]\n";
    std::cout << "  " << programName << " checkout --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --out <folder> --user <user> --environment <environment> [--scope none|strong|strong+weak|all]\n";
    std::cout << "  " << programName << " checkin --archive <archive> (--ref <path> | --refs-file <file>)... --root <folder> --user <user> --environment <environment> [--keep-lock true|false]\n";
    std::cout << "  " << programName << " unlock --archive <archive> (--ref <path> | --refs-file <file>)...\n";
    std::cout << "  " << programName << " status --archive <archive> --root <folder>\n";
    std::cout << "  " << programName << " repair --archive <archive> --root <folder>\n";
    std::cout << "  " << programName << " versions --archive <archive> (--path <path> | --paths-file <file>)...\n";
    std::cout << "  " << programName << " relate --archive <archive> [--from <path[@version]> --to <path[@version]> --type strong|weak|optional]... [--edges-file <file>]\n";
    std::cout << "  " << programName << " relations --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--type strong|weak|optional|all]\n";
    std::cout << "  " << programName << " props list --archive <archive> (--ref <path[@version]> | --refs-file <file>)...\n";
    std::cout << "  " << programName << " props get --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>\n";
    std::cout << "  " << programName << " props set --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property> --type string|int|bool --value <value>\n";
    std::cout << "  " << programName << " props remove --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>\n";
    std::cout << "  " << programName << " locks list --archive <archive>\n";
    std::cout << "  " << programName << " inspect --archive <archive> [--root <folder>]\n\n";

    std::cout << "Common flows:\n";
    std::cout << "  Import folder into archive\n";
    std::cout << "    " << programName << " import --archive ./archive --root ./source\n";
    std::cout << "    " << programName << " import --archive ./archive --root ./source --include 'docs/**' --ignore '**/*.tmp'\n\n";
    std::cout << "  Materialize readonly view\n";
    std::cout << "    " << programName << " get --archive ./archive --ref docs/readme.txt --out ./ws --mode readonly-copy\n\n";
    std::cout << "  Checkout, edit, and check in\n";
    std::cout << "    " << programName << " checkout --archive ./archive --ref docs/readme.txt --out ./ws --user alice --environment laptop\n";
    std::cout << "    " << programName << " checkin  --archive ./archive --root ./ws --ref docs/readme.txt --user alice --environment laptop\n\n";
    std::cout << "Notes:\n";
    std::cout << "  - Paths are vault-relative; ROOT/ prefix is optional in input.\n";
    std::cout << "  - Omitting @version means latest version.\n";
    std::cout << "  - checkin/unlock accept logical paths only, not @version selectors.\n";
    std::cout << "  - status states: ok, missing, modified, replaced.\n";
    std::cout << "  - import include/ignore globs are matched against workspace-relative paths.\n";
  }
}
