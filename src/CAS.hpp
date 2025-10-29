#pragma once
#include <filesystem>
#include <string>

/// @brief Content Addressable Storage that indentifies files by SHA256 and uses zlib to compress the files when stored.
namespace Docmasys::CAS
{
  /// @brief Calculate hash identity for give file
  /// @param file Full path to local file which content to read and calculate hash for.
  /// @return Hexadecimal string (SHA256)
  [[nodiscard]] std::string Identify(
      const std::filesystem::path &file);

  /// @brief Store the given files to CAS vault.
  /// @param root Full path to the CAS vault root.
  /// @param file Full path to the file to store.
  /// @return Hexadecimal string (SHA256) of the file that represents its identity.
  [[nodiscard]] std::string Store(
      const std::filesystem::path &root,
      const std::filesystem::path &file);

  /// @brief Retrieve stored file from CAS with given identity.
  /// @param root Full path to the CAS vault root.
  /// @param identity Hexadecimal string (SHA256) that identifies the file.
  /// @param outFile Full path to the target where the file should be retrieved to.
  void Retrieve(
      const std::filesystem::path &root,
      const std::string &identity,
      const std::filesystem::path &outFile);

  /// @brief Deletes a file from CAS with given identity.
  /// @param root Full path to the CAS vault root.
  /// @param identity Hexadecimal string (SHA256) that identifies the file to delete from CAS.
  void Delete(
      const std::filesystem::path &root,
      const std::string &identity);
}
