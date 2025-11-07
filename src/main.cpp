#include <iostream>
#include <unordered_map>
#include "Vault.hpp"

/// @brief Parse command line arguments to easy-to-use map.
/// @param argc count of the arguments
/// @param argv argument array.
/// @return
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
        opts[arg.substr(2)] = "true"; // flag
    }
  }
  return opts;
}

void Usage(std::string programName)
{
  std::cout << "USAGE:" << std::endl;
  std::cout << " - Push <local_folder> to archive:   " << programName << " --archive <path_to_archive> --push <local_folder>" << std::endl;
  std::cout << " - Pop archive to <local_folder>:   " << programName << " --archive <path_to_archive> --pop <local_folder>" << std::endl;
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
    auto vault = std::make_unique<Docmasys::Vault>(
        args["push"],
        args["archive"]);

    vault->Push();
  }

  if (args.contains("pop"))
  {
    auto vault = std::make_unique<Docmasys::Vault>(
        args["pop"],
        args["archive"]);

    vault->Pop();
  }
  return 0;
}