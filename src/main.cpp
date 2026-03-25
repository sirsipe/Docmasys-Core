#include <iostream>
#include <unordered_map>
#include "Vault.hpp"

std::unordered_map<std::string_view, std::string_view> parseArgs(int argc, char *argv[])
{
  std::unordered_map<std::string_view, std::string_view> opts;
  for (int i = 1; i < argc; ++i)
  {
    std::string_view arg = argv[i];
    if (arg.starts_with("--"))
    {
      if (i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--"))
        opts[arg.substr(2)] = argv[++i];
      else
        opts[arg.substr(2)] = "true";
    }
  }
  return opts;
}

void Usage(std::string programName)
{
  std::cout << "USAGE:\n";
  std::cout << " - Push <local_folder> to archive:   " << programName << " --archive <path_to_archive> --push <local_folder>\n";
  std::cout << " - Pop archive to <local_folder>:    " << programName << " --archive <path_to_archive> --pop <local_folder>\n";
}

int main(int argc, char *argv[])
{
  auto args = parseArgs(argc, argv);
  if (!args.contains("archive"))
  {
    Usage(argv[0]);
    return 1;
  }

  if (args.contains("push"))
  {
    Docmasys::Vault vault(args["push"], args["archive"]);
    vault.Push();
  }

  if (args.contains("pop"))
  {
    Docmasys::Vault vault(args["pop"], args["archive"]);
    vault.Pop();
  }

  return 0;
}
