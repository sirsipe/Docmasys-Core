#include "CAS.hpp"
#include <iostream>
#include <unordered_map>

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
  std::cout << " - Upload:   " << programName << " --cache <path_to_cache_folder> --upload <local_source>" << std::endl;
  std::cout << " - Download: " << programName << " --cache <path_to_cache_folder> --download <local_destination> --hash <hash>" << std::endl;
  std::cout << " - Identify: " << programName << " --identify <local_source>" << std::endl;
}

int main(int argc, char *argv[])
{

  auto args = parseArgs(argc, argv);

  if (args.contains("identify"))
  {
    std::cout << Docmasys::CAS::Identify(args["identify"]) << std::endl;
    return 0;
  }
  else if (args.contains("cache") && args.contains("upload"))
  {
    auto sha256 = Docmasys::CAS::Store(args["cache"], args["upload"]);
    std::cout << sha256 << std::endl;
    return 0;
  }
  else if (args.contains("cache") && args.contains("download") && args.contains("hash"))
  {
    Docmasys::CAS::Retrieve(args["cache"], std::string(args["hash"]), args["download"]);
    return 0;
  }
  else
  {
    Usage(argv[0]);
    return 1;
  }

  return 0;
}