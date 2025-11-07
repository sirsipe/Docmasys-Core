#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <future>
#include <vector>
#include <atomic>

#include "../CAS/CAS.hpp" // your header

namespace fs = std::filesystem;

// ----------- Small helpers -----------

static std::string RandomBytes(size_t n)
{
  std::mt19937_64 rng{1234567};
  std::string s(n, '\0');
  for (size_t i = 0; i < n; ++i)
    s[i] = char(rng() & 0xFF);
  return s;
}

static fs::path MakeFile(const fs::path &p, std::string_view data)
{
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return p;
}

struct TempDir
{
  fs::path dir;
  TempDir()
  {
    auto base = fs::temp_directory_path();
    for (int i = 0; i < 1000; ++i)
    {
      auto cand = base / ("cas_test_" + std::to_string(::getpid()) + "_" + std::to_string(i));
      if (fs::create_directory(cand))
      {
        dir = cand;
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

// ------------- Tests -----------------

TEST(CAS, Identify_SameContentSameHash)
{
  TempDir td;
  auto f1 = td.dir / "a.bin";
  auto f2 = td.dir / "b.bin";
  auto data = RandomBytes(1024 * 32);
  MakeFile(f1, data);
  MakeFile(f2, data);

  auto h1 = Docmasys::CAS::Identify(f1);
  auto h2 = Docmasys::CAS::Identify(f2);
  EXPECT_EQ(h1, h2);
  EXPECT_EQ(h1.size(), 64u); // sha256 hex
}

TEST(CAS, Store_And_Retrieve_Roundtrip)
{
  TempDir td;
  auto root = td.dir / "vault";
  fs::create_directories(root);

  auto src = td.dir / "source.bin";
  auto out = td.dir / "out.bin";
  auto data = RandomBytes(1024 * 128);
  MakeFile(src, data);

  auto id = Docmasys::CAS::Store(root, src);
  ASSERT_FALSE(id.empty());

  Docmasys::CAS::Retrieve(root, id, out);

  // Verify content identical
  std::ifstream fi(out, std::ios::binary);
  std::string got((std::istreambuf_iterator<char>(fi)), {});
  EXPECT_EQ(got, data);

  // Verify object exists in vault
  auto idStr = Docmasys::CAS::ToHexString(id);
  fs::path obj = root / "Objects" / idStr.substr(0, 2) / idStr.substr(2, 2) / idStr;
  EXPECT_TRUE(fs::exists(obj));
}

TEST(CAS, Delete_RemovesObject_And_CleansEmptyParents)
{
  TempDir td;
  auto root = td.dir / "vault";
  fs::create_directories(root);

  auto src = td.dir / "f.dat";
  MakeFile(src, "hello world");

  auto id = Docmasys::CAS::Store(root, src);
  auto idStr = Docmasys::CAS::ToHexString(id);

  // paths
  auto objStore = root / "Objects";
  auto d1 = objStore / idStr.substr(0, 2);
  auto d2 = d1 / idStr.substr(2, 2);
  auto obj = d2 / idStr;

  ASSERT_TRUE(fs::exists(obj));

  Docmasys::CAS::Delete(root, id);

  EXPECT_FALSE(fs::exists(obj));
  // parents should be removed if empty
  EXPECT_FALSE(fs::exists(d2));
  // d1 might also be removed if empty; check emptiness:
  if (fs::exists(d1))
  {
    EXPECT_FALSE(fs::is_empty(d1)) << "Parent wasn't cleaned though empty";
  }
}

TEST(CAS, Store_SameIdentity_FromManyThreads_IsIdempotent)
{
  TempDir td;
  auto root = td.dir / "vault";
  fs::create_directories(root);

  auto src = td.dir / "data.bin";
  auto data = RandomBytes(1024 * 1024);
  MakeFile(src, data);

  const int N = 16;
  std::vector<std::future<std::array<uint8_t, 32>>> futs;
  futs.reserve(N);
  for (int i = 0; i < N; ++i)
  {
    futs.emplace_back(std::async(std::launch::async, [&]
                                 { return Docmasys::CAS::Store(root, src); }));
  }

  std::vector<std::array<uint8_t, 32>> ids;
  ids.reserve(N);
  for (auto &f : futs)
    ids.push_back(f.get());

  // all ids must match and object must exist exactly once
  for (int i = 1; i < N; ++i)
    EXPECT_EQ(ids[i], ids[0]);
  auto id = ids[0];
  auto idstr = Docmasys::CAS::ToHexString(id);
  auto obj = root / "Objects" / idstr.substr(0, 2) / idstr.substr(2, 2) / idstr;
  EXPECT_TRUE(fs::exists(obj));

  // Ensure only the final file exists (no temp leftovers under Objects)
  int fileCount = 0;
  for (auto &p : fs::recursive_directory_iterator(root / "Objects"))
    if (fs::is_regular_file(p))
      ++fileCount;
  EXPECT_EQ(fileCount, 1);
}

TEST(CAS, Retrieve_ToSamePath_FromManyThreads_NoCorruption)
{
  TempDir td;
  auto root = td.dir / "vault";
  fs::create_directories(root);

  // Put one object in CAS
  auto src = td.dir / "data.bin";
  auto data = RandomBytes(1024 * 256);
  MakeFile(src, data);
  auto id = Docmasys::CAS::Store(root, src);

  // Retrieve concurrently to SAME out path
  auto out = td.dir / "joined.bin";
  const int N = 12;
  std::vector<std::future<void>> futs;
  for (int i = 0; i < N; ++i)
  {
    futs.emplace_back(std::async(std::launch::async, [&, i]
                                 { Docmasys::CAS::Retrieve(root, id, out); }));
  }
  for (auto &f : futs)
    f.get();

  // Verify content intact
  std::ifstream fi(out, std::ios::binary);
  std::string got((std::istreambuf_iterator<char>(fi)), {});
  EXPECT_EQ(got, data);
}

TEST(CAS, Delete_While_Retrieve_Race_Tolerant)
{
  TempDir td;
  auto root = td.dir / "vault";
  fs::create_directories(root);

  // Store object
  auto src = td.dir / "race.dat";
  auto data = RandomBytes(256 * 1024);
  MakeFile(src, data);
  auto id = Docmasys::CAS::Store(root, src);

  // Try to retrieve while deleting.
  // Depending on timing, either retrieve succeeds, or delete wins and retrieve throws.
  auto out = td.dir / "race_out.dat";
  std::exception_ptr retrieve_ex;

  std::thread t1([&]
                 {
    try { Docmasys::CAS::Retrieve(root, id, out); }
    catch (...) { retrieve_ex = std::current_exception(); } });

  std::thread t2([&]
                 {
    // small delay to increase overlap
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    try { Docmasys::CAS::Delete(root, id); } catch(...) {} });

  t1.join();
  t2.join();

  // Both outcomes are acceptable:
  // 1) retrieve succeeded -> out exists and matches data
  // 2) delete won -> retrieve likely threw; out may not exist
  if (fs::exists(out))
  {
    std::ifstream fi(out, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(fi)), {});
    EXPECT_EQ(got, data);
  }
  // If retrieve threw, ensure it's specifically because object went missing
  // (we don't assert the exception message to keep this test portable)
}

// Optionally: stress test (disabled by default)
// Define -DCAS_ENABLE_STRESS to enable.
#ifdef CAS_ENABLE_STRESS
TEST(CAS, STRESS_Store_Retrieve_Delete)
{
  TempDir td;
  auto root = td.dir / "vault";
  fs::create_directories(root);

  const int rounds = 50;
  const int threads = 16;

  for (int r = 0; r < rounds; ++r)
  {
    auto src = td.dir / ("s" + std::to_string(r) + ".bin");
    auto data = RandomBytes(64 * 1024 + r);
    MakeFile(src, data);
    auto id = Docmasys::CAS::Store(root, src);

    // mix of retrieves/deletes
    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i)
    {
      ts.emplace_back([&, i]
                      {
        auto out = td.dir / ("o" + std::to_string(r) + "_" + std::to_string(i) + ".bin");
        try { Docmasys::CAS::Retrieve(root, id, out); } catch(...) {} });
    }
    // Some deletes racing
    std::thread del([&]
                    { try { Docmasys::CAS::Delete(root, id); } catch(...) {} });

    for (auto &t : ts)
      t.join();
    del.join();
  }
  SUCCEED();
}
#endif
