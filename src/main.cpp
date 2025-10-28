#include "Cache.h"
#include <iostream>
#include <unordered_map>


std::unordered_map<std::string_view, std::string_view> parseArgs(int argc, char* argv[]) {
    std::unordered_map<std::string_view, std::string_view> opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--")) {
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
}

int main(int argc, char** argv)
{

    auto args = parseArgs(argc, argv);

    if (!args.contains("cache"))
    { 
        Usage(argv[0]);
        return 1;
    }
        
    Cache oCache(args["cache"]);
    
    if (args.contains("upload"))
    {
        auto sha256 = oCache.Store(args["upload"] );
        std::cout << sha256 <<  std::endl;
        return 0;
    }
    else if (args.contains("download") && args.contains("hash"))
    {
        bool bOk = oCache.Retrieve(std::string(args["hash"]), args["download"]);
        std::cout << (bOk ? "Ok!" : "Failed!") <<  std::endl;
        return 0;
    }
    else
    {
        Usage(argv[0]);
        return 1;
    }
    return 0;
}