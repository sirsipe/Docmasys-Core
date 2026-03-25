#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <sstream>
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

static std::string ShellQuote(const fs::path &path)
{
  return '"' + path.string() + '"';
}

static std::string RunAndCapture(const fs::path &outputFile, const std::string &cmd)
{
  EXPECT_EQ(RunCommand(cmd + " > " + ShellQuote(outputFile)), 0);
  std::ifstream input(outputFile);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
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
  EXPECT_EQ(RunCommand(std::string(bin) + " props set --archive " + archive.string() + " --ref alpha.txt@1 --name answer --type int --value 42"), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " props get --archive " + archive.string() + " --ref alpha.txt@1 --name ANSWER > /dev/null"), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " get --archive " + archive.string() + " --ref alpha.txt --out " + out.string()), 0);
  EXPECT_TRUE(fs::exists(out / "alpha.txt"));
}

TEST(CLI, BatchOperations)
{
  const char *bin = std::getenv("DOCMASYS_BIN");
  ASSERT_NE(bin, nullptr);

  TempDir td;
  auto root = td.dir / "root";
  auto archive = td.dir / "archive";
  auto out = td.dir / "out";
  auto capture = td.dir / "capture.txt";
  auto refsFile = td.dir / "refs.txt";
  auto pathsFile = td.dir / "paths.txt";
  auto edgesFile = td.dir / "edges.txt";

  fs::create_directories(root / "docs");
  fs::create_directories(root / "refs");
  fs::create_directories(archive);

  {
    std::ofstream(root / "docs" / "alpha.txt") << "alpha";
    std::ofstream(root / "refs" / "beta.txt") << "beta";
    std::ofstream(pathsFile) << "docs/alpha.txt\nrefs/beta.txt\n";
    std::ofstream(refsFile) << "docs/alpha.txt@1\nrefs/beta.txt@1\n";
  }

  ASSERT_EQ(RunCommand(std::string(bin) + " import --archive " + archive.string() + " --root " + root.string()), 0);

  ASSERT_EQ(RunCommand(std::string(bin) + " props set --archive " + archive.string() +
                       " --ref docs/alpha.txt@1 --ref refs/beta.txt@1 --name reviewed --type bool --value true"), 0);

  const auto versionsOutput = RunAndCapture(capture,
      std::string(bin) + " versions --archive " + archive.string() +
      " --paths-file " + pathsFile.string());
  EXPECT_NE(versionsOutput.find("ROOT/docs/alpha.txt@1"), std::string::npos);
  EXPECT_NE(versionsOutput.find("ROOT/refs/beta.txt@1"), std::string::npos);

  const auto propsOutput = RunAndCapture(capture,
      std::string(bin) + " props get --archive " + archive.string() +
      " --refs-file " + refsFile.string() +
      " --name reviewed");
  EXPECT_NE(propsOutput.find("ROOT/docs/alpha.txt@1\treviewed\tbool\ttrue"), std::string::npos);
  EXPECT_NE(propsOutput.find("ROOT/refs/beta.txt@1\treviewed\tbool\ttrue"), std::string::npos);

  {
    std::ofstream(edgesFile) << "docs/alpha.txt@1 refs/beta.txt@1 strong\n";
  }
  ASSERT_EQ(RunCommand(std::string(bin) + " relate --archive " + archive.string() +
                       " --edges-file " + edgesFile.string()), 0);

  const auto relationsOutput = RunAndCapture(capture,
      std::string(bin) + " relations --archive " + archive.string() +
      " --refs-file " + refsFile.string());
  EXPECT_NE(relationsOutput.find("ROOT/docs/alpha.txt@1 --strong--> ROOT/refs/beta.txt@1"), std::string::npos);

  ASSERT_EQ(RunCommand(std::string(bin) + " get --archive " + archive.string() +
                       " --refs-file " + refsFile.string() +
                       " --out " + out.string()), 0);
  EXPECT_TRUE(fs::exists(out / "docs" / "alpha.txt"));
  EXPECT_TRUE(fs::exists(out / "refs" / "beta.txt"));

  ASSERT_EQ(RunCommand(std::string(bin) + " props remove --archive " + archive.string() +
                       " --refs-file " + refsFile.string() +
                       " --name reviewed"), 0);
}
