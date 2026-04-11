#include <gtest/gtest.h>

#include "TestSupport.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using Docmasys::Tests::TempDir;

namespace
{
  int RunCommand(const std::string &cmd)
  {
    return std::system(cmd.c_str());
  }

  std::string ShellQuote(const fs::path &path)
  {
    return '"' + path.string() + '"';
  }

  std::string NullRedirect()
  {
#ifdef _WIN32
    return " > NUL";
#else
    return " > /dev/null";
#endif
  }

  std::string NullRedirectBoth()
  {
#ifdef _WIN32
    return " > NUL 2>&1";
#else
    return " > /dev/null 2>&1";
#endif
  }

  std::string RunAndCapture(const fs::path &outputFile, const std::string &cmd)
  {
    EXPECT_EQ(RunCommand(cmd + " > " + ShellQuote(outputFile)), 0);
    std::ifstream input(outputFile);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
  }
}

TEST(CLI, HelpAndVerbFlow)
{
  const char *bin = std::getenv("DOCMASYS_BIN");
  ASSERT_NE(bin, nullptr);

  TempDir td;
  auto helpOut = td.dir / "help.txt";
  const auto helpText = RunAndCapture(helpOut, std::string(bin) + " help");
  EXPECT_NE(helpText.find("Common flows:"), std::string::npos);
  EXPECT_NE(helpText.find("status states: ok, missing, modified, replaced"), std::string::npos);
  auto root = td.dir / "root";
  auto archive = td.dir / "archive";
  auto out = td.dir / "out";
  fs::create_directories(root);
  fs::create_directories(archive);

  Docmasys::Tests::WriteFile(root / "alpha.txt", "v1");

  EXPECT_EQ(RunCommand(std::string(bin) + " help" + NullRedirect()), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " import --archive " + archive.string() + " --root " + root.string()), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " versions --archive " + archive.string() + " --path alpha.txt" + NullRedirect()), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " props set --archive " + archive.string() + " --ref alpha.txt@1 --name answer --type int --value 42"), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " props get --archive " + archive.string() + " --ref alpha.txt@1 --name ANSWER" + NullRedirect()), 0);
  EXPECT_EQ(RunCommand(std::string(bin) + " get --archive " + archive.string() + " --ref alpha.txt --out " + out.string() + " --mode readonly-copy"), 0);
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

  Docmasys::Tests::WriteFile(root / "docs" / "alpha.txt", "alpha");
  Docmasys::Tests::WriteFile(root / "refs" / "beta.txt", "beta");
  Docmasys::Tests::WriteFile(pathsFile, "docs/alpha.txt\nrefs/beta.txt\n");
  Docmasys::Tests::WriteFile(refsFile, "docs/alpha.txt@1\nrefs/beta.txt@1\n");

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

  Docmasys::Tests::WriteFile(edgesFile, "docs/alpha.txt@1 refs/beta.txt@1 strong\n");
  ASSERT_EQ(RunCommand(std::string(bin) + " relate --archive " + archive.string() +
                       " --edges-file " + edgesFile.string()), 0);

  const auto relationsOutput = RunAndCapture(capture,
      std::string(bin) + " relations --archive " + archive.string() +
      " --refs-file " + refsFile.string());
  EXPECT_NE(relationsOutput.find("ROOT/docs/alpha.txt@1 --strong--> ROOT/refs/beta.txt@1"), std::string::npos);

  ASSERT_EQ(RunCommand(std::string(bin) + " checkout --archive " + archive.string() +
                       " --refs-file " + refsFile.string() +
                       " --out " + out.string() +
                       " --user simo --environment ws1"), 0);
  EXPECT_TRUE(fs::exists(out / "docs" / "alpha.txt"));
  EXPECT_TRUE(fs::exists(out / "refs" / "beta.txt"));

  const auto locksOutput = RunAndCapture(capture,
      std::string(bin) + " locks list --archive " + archive.string());
  EXPECT_NE(locksOutput.find("ROOT/docs/alpha.txt\tsimo\tws1"), std::string::npos);
  EXPECT_NE(locksOutput.find("ROOT/refs/beta.txt\tsimo\tws1"), std::string::npos);

  Docmasys::Tests::WriteFile(out / "docs" / "alpha.txt", "alpha-v2");

  ASSERT_EQ(RunCommand(std::string(bin) + " checkin --archive " + archive.string() +
                       " --root " + out.string() +
                       " --ref docs/alpha.txt --user simo --environment ws1"), 0);

  const auto statusOutput = RunAndCapture(capture,
      std::string(bin) + " status --archive " + archive.string() +
      " --root " + out.string());
  EXPECT_NE(statusOutput.find("docs/alpha.txt\tcheckout-copy\tok\t2"), std::string::npos);

  ASSERT_EQ(RunCommand(std::string(bin) + " props remove --archive " + archive.string() +
                       " --refs-file " + refsFile.string() +
                       " --name reviewed"), 0);
}

TEST(CLI, ImportRejectsTamperedReadonlyAndUnlockClearsLock)
{
  const char *bin = std::getenv("DOCMASYS_BIN");
  ASSERT_NE(bin, nullptr);

  TempDir td;
  auto root = td.dir / "root";
  auto archive = td.dir / "archive";
  auto readonlyWs = td.dir / "readonly";
  auto editWs = td.dir / "edit";
  auto otherWs = td.dir / "other";
  fs::create_directories(root);
  fs::create_directories(archive);
  fs::create_directories(readonlyWs);
  fs::create_directories(editWs);
  fs::create_directories(otherWs);

  Docmasys::Tests::WriteFile(root / "alpha.txt", "v1");
  ASSERT_EQ(RunCommand(std::string(bin) + " import --archive " + archive.string() + " --root " + root.string()), 0);
  ASSERT_EQ(RunCommand(std::string(bin) + " get --archive " + archive.string() + " --ref alpha.txt --out " + readonlyWs.string() + " --mode readonly-copy"), 0);

  fs::permissions(readonlyWs / "alpha.txt", fs::perms::owner_write, fs::perm_options::add);
  Docmasys::Tests::WriteFile(readonlyWs / "alpha.txt", "bad");
  EXPECT_NE(RunCommand(std::string(bin) + " import --archive " + archive.string() + " --root " + readonlyWs.string() + NullRedirectBoth()), 0);

  ASSERT_EQ(RunCommand(std::string(bin) + " checkout --archive " + archive.string() + " --ref alpha.txt --out " + editWs.string() + " --user simo --environment ws1"), 0);
  EXPECT_NE(RunCommand(std::string(bin) + " checkout --archive " + archive.string() + " --ref alpha.txt --out " + otherWs.string() + " --user other --environment ws2" + NullRedirectBoth()), 0);
  ASSERT_EQ(RunCommand(std::string(bin) + " unlock --archive " + archive.string() + " --ref alpha.txt"), 0);
  ASSERT_EQ(RunCommand(std::string(bin) + " checkout --archive " + archive.string() + " --ref alpha.txt --out " + otherWs.string() + " --user other --environment ws2"), 0);
}
