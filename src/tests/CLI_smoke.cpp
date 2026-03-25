#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

struct TempDir
{
  fs::path dir;
  TempDir()
  {
    dir = fs::temp_directory_path() / fs::path("docmasys_cli_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

static int RunCommand(const std::string &cmd)
{
  return std::system(cmd.c_str());
}

TEST(CLI, HelpAndVerbFlow)
{
  const char *bin = std::getenv("DOCMASYS_BIN");
  ASSERT_NE(bin, nullptr);

  TempDir td;
  auto root = td.dir / "root";
  auto archive = td.dir / "archive";
  auto out = td.dir / "out";
  fs::create_directories(root);
  fs::create_directories(archive);

  {
    std::ofstream(root / "alpha.txt") << "v1";
  }

  EXPECT_EQ(RunCommand(std::string(bin) + " help > /dev/null"), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " import --archive " + archive.string() + " --root " + root.string()), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " versions --archive " + archive.string() + " --path alpha.txt > /dev/null"), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " get --archive " + archive.string() + " --ref alpha.txt --out " + out.string()), 0);
  EXPECT_TRUE(fs::exists(out / "alpha.txt"));
}
