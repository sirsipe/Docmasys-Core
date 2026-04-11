#pragma once

#include "../Types.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace Docmasys::Tests
{
  namespace fs = std::filesystem;

  struct TempDir
  {
    fs::path dir;

    TempDir()
    {
      auto base = fs::temp_directory_path();
      for (int i = 0; i < 1000; ++i)
      {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto candidate = base / ("docmasys_test_" + unique + "_" + std::to_string(i));
        if (fs::create_directory(candidate))
        {
          dir = candidate;
          break;
        }
      }
      if (dir.empty())
        throw std::runtime_error("TempDir: failed to create");
    }

    ~TempDir()
    {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }
  };

  inline void WriteFile(const fs::path &path, std::string_view content)
  {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << content;
  }

  inline Identity MakeIdentity(std::uint8_t seed)
  {
    Identity id{};
    for (size_t i = 0; i < id.size(); ++i)
      id[i] = static_cast<std::uint8_t>(seed + i);
    return id;
  }
}
