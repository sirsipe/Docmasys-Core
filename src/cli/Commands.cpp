#include "Commands.hpp"
#include "CommandHelpers.hpp"

#include "../Common/PathUtils.hpp"
#include "../DB/Database.hpp"
#include "../Vault.hpp"

#include <iostream>
#include <stdexcept>

using namespace Docmasys;

namespace Docmasys::CLI
{
  namespace
  {
    int RunImport(const Options &options)
    {
      Vault(Require(options, "root"), Require(options, "archive")).Push(ImportOptions{
          .IncludePatterns = CollectBatchValues(options, "include", "includes-file"),
          .IgnorePatterns = CollectBatchValues(options, "ignore", "ignores-file")});
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
      const auto kind = ParseMaterializationKind(OptionalValue(options, "mode").value_or("readonly-copy"));
      if (kind == DB::MaterializationKind::CheckoutCopy)
        throw std::runtime_error("get does not accept checkout-copy mode; use checkout verb");

      for (const auto &rawRef : refs)
      {
        const auto ref = ParseRef(rawRef);
        vault.Pop(MaterializationOptions{
            .RelativeFilePath = Common::EnsureRootedVaultPath(ref.Path),
            .VersionNumber = ref.Version,
            .RelationScope = scope,
            .Kind = kind});
      }
      return 0;
    }

    int RunCheckout(const Options &options)
    {
      const auto archive = fs::path(Require(options, "archive"));
      const auto out = fs::path(Require(options, "out"));
      const auto refs = CollectBatchValues(options, "ref", "refs-file");
      if (refs.empty())
        throw std::runtime_error("checkout requires at least one --ref or --refs-file");

      Vault vault(out, archive);
      const auto scope = ParseScope(OptionalValue(options, "scope").value_or("none"));
      const auto user = Require(options, "user");
      const auto environment = Require(options, "environment");
      for (const auto &rawRef : refs)
      {
        const auto ref = ParseRef(rawRef);
        vault.Checkout(CheckoutOptions{
            .RelativeFilePath = Common::EnsureRootedVaultPath(ref.Path),
            .VersionNumber = ref.Version,
            .RelationScope = scope,
            .User = user,
            .Environment = environment});
      }
      return 0;
    }

    int RunCheckin(const Options &options)
    {
      const auto archive = fs::path(Require(options, "archive"));
      const auto root = fs::path(Require(options, "root"));
      const auto refs = CollectBatchValues(options, "ref", "refs-file");
      if (refs.empty())
        throw std::runtime_error("checkin requires at least one --ref or --refs-file");

      Vault vault(root, archive);
      const auto user = Require(options, "user");
      const auto environment = Require(options, "environment");
      const bool releaseLock = OptionalValue(options, "keep-lock").value_or("false") != "true";
      for (const auto &rawRef : refs)
      {
        const auto ref = ParseRef(rawRef);
        if (ref.Version)
          throw std::runtime_error("checkin does not accept @version selectors");
        vault.Checkin(CheckinOptions{
            .RelativeFilePath = ref.Path,
            .User = user,
            .Environment = environment,
            .ReleaseLock = releaseLock});
      }
      return 0;
    }

    int RunUnlock(const Options &options)
    {
      const auto archive = fs::path(Require(options, "archive"));
      const auto refs = CollectBatchValues(options, "ref", "refs-file");
      if (refs.empty())
        throw std::runtime_error("unlock requires at least one --ref or --refs-file");

      Vault vault(".", archive);
      for (const auto &rawRef : refs)
      {
        const auto ref = ParseRef(rawRef);
        if (ref.Version)
          throw std::runtime_error("unlock does not accept @version selectors");
        vault.Unlock(ref.Path);
      }
      return 0;
    }

    int RunStatus(const Options &options)
    {
      Vault vault(fs::path(Require(options, "root")), fs::path(Require(options, "archive")));
      for (const auto &status : vault.Status())
        std::cout << status.Entry.RelativePath.generic_string() << '\t'
                  << ToString(status.Entry.Kind) << '\t'
                  << ToString(status.State) << '\t'
                  << status.Entry.Version->VersionNumber << "\n";
      return 0;
    }

    int RunRepair(const Options &options)
    {
      Vault(fs::path(Require(options, "root")), fs::path(Require(options, "archive"))).Repair();
      return 0;
    }

    std::unique_ptr<DB::Database> OpenArchiveDb(const Options &options)
    {
      return DB::Database::Open(fs::path(Require(options, "archive")) / "content.db", ".");
    }

    int RunVersions(const Options &options)
    {
      auto db = OpenArchiveDb(options);
      const auto paths = CollectBatchValues(options, "path", "paths-file");
      if (paths.empty())
        throw std::runtime_error("versions requires at least one --path or --paths-file");

      for (const auto &rawPath : paths)
      {
        auto file = db->GetFileByRelativePath(Common::EnsureRootedVaultPath(rawPath));
        for (const auto &version : db->GetFileVersions(file))
          std::cout << db->BuildRelativePath(file).generic_string() << '@' << version->VersionNumber << "\n";
      }
      return 0;
    }

    int RunRelate(const Options &options)
    {
      auto db = OpenArchiveDb(options);
      for (const auto &spec : CollectRelationSpecs(options))
      {
        if (!spec.From.Version || !spec.To.Version)
          throw std::runtime_error("relate requires explicit @version in both endpoints");
        auto fromFile = db->GetFileByRelativePath(Common::EnsureRootedVaultPath(spec.From.Path));
        auto toFile = db->GetFileByRelativePath(Common::EnsureRootedVaultPath(spec.To.Path));
        db->AddRelation(db->GetFileVersion(fromFile, spec.From.Version), db->GetFileVersion(toFile, spec.To.Version), spec.Type);
      }
      return 0;
    }

    int RunRelations(const Options &options)
    {
      auto db = OpenArchiveDb(options);
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
        auto file = db->GetFileByRelativePath(Common::EnsureRootedVaultPath(ref.Path));
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
      auto db = OpenArchiveDb(options);
      const auto refs = CollectBatchValues(options, "ref", "refs-file");
      if (refs.empty())
        throw std::runtime_error("props requires at least one --ref or --refs-file");

      for (const auto &rawRef : refs)
      {
        const auto ref = ParseRef(rawRef);
        auto file = db->GetFileByRelativePath(Common::EnsureRootedVaultPath(ref.Path));
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

    int RunLocks(const std::string &subcommand, const Options &options)
    {
      auto db = OpenArchiveDb(options);
      if (subcommand != "list")
        throw std::runtime_error("unknown locks subcommand: " + subcommand);
      for (const auto &lock : db->ListCheckoutLocks())
        std::cout << db->BuildRelativePath(lock.LogicalFile).generic_string() << '\t'
                  << lock.User << '\t' << lock.Environment << '\t'
                  << lock.WorkspaceRoot.generic_string() << "\n";
      return 0;
    }

    int RunInspect(const Options &options)
    {
      const auto archive = fs::path(Require(options, "archive"));
      auto db = DB::Database::Open(archive / "content.db", OptionalValue(options, "root").value_or("."));
      std::cout << "path\tversion\tblob\tproperties\toutgoing_relations\n";
      for (const auto &item : db->InspectCurrentFiles())
      {
        const auto propertyCount = db->ListVersionProperties(item.Version).size();
        const auto relationCount = db->GetOutgoingRelations(item.Version, std::nullopt).size();
        std::cout << item.RelativePath.generic_string() << '\t'
                  << item.Version->VersionNumber << '\t'
                  << ToString(item.BlobRef->Status) << '\t'
                  << propertyCount << '\t'
                  << relationCount << "\n";
      }
      return 0;
    }
  }

  int Dispatch(int argc, char *argv[])
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

    if (command == "locks")
    {
      if (argc < 3)
        throw std::runtime_error("locks requires a subcommand");
      return RunLocks(argv[2], ParseOptions(argc, argv, 3));
    }

    const auto options = ParseOptions(argc, argv, 2);
    if (command == "import") return RunImport(options);
    if (command == "get") return RunGet(options);
    if (command == "checkout") return RunCheckout(options);
    if (command == "checkin") return RunCheckin(options);
    if (command == "unlock") return RunUnlock(options);
    if (command == "status") return RunStatus(options);
    if (command == "repair") return RunRepair(options);
    if (command == "versions") return RunVersions(options);
    if (command == "relate") return RunRelate(options);
    if (command == "relations") return RunRelations(options);
    if (command == "inspect") return RunInspect(options);

    throw std::runtime_error("unknown command: " + command);
  }
}
