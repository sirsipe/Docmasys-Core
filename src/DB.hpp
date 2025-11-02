#pragma once
#include <memory>
#include <filesystem>

class DB
{

public:
  [[nodiscard]] static std::unique_ptr<DB> Open(const std::filesystem::path &path)
  {
    return std::unique_ptr<DB>(new DB(path));
  }

public:
  [[nodiscard]] inline const std::filesystem::path &Path() const noexcept
  {
    return m_Path;
  }

  ~DB();

private:
  DB(const std::filesystem::path &path);

  DB(const DB &) = delete;
  DB &operator=(const DB &) = delete;
  DB(DB &&) = delete;
  DB &operator=(DB &&) = delete;

  void ExecSQL(const char *sql);

private:
  const std::filesystem::path m_Path;
  struct Impl;
  std::unique_ptr<Impl> m_Database;
};
