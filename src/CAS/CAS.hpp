#pragma once
#include <filesystem>
#include "../Types.hpp"

/// @brief Content Addressable Storage that indentifies files by SHA256 and uses zlib to compress the files when stored.
namespace Docmasys::CAS
{
  /// @brief Calculate hash identity for give file
  /// @param file Full path to local file which content to read and calculate hash for.
  /// @return SHA256 identity
  [[nodiscard]] Identity Identify(
      const std::filesystem::path &file);

  [[nodiscard]] std::string ToHexString(const Identity &identity);

  /// @brief Store the given files to CAS vault.
  /// @param root Full path to the CAS vault root.
  /// @param file Full path to the file to store.
  /// @return SHA256 identity
  [[nodiscard]] Identity Store(
      const std::filesystem::path &root,
      const std::filesystem::path &file);

  /// @brief Retrieve stored file from CAS with given identity.
  /// @param root Full path to the CAS vault root.
  /// @param identity Hexadecimal string (SHA256) that identifies the file.
  /// @param outFile Full path to the target where the file should be retrieved to.
  void Retrieve(
      const std::filesystem::path &root,
      const Identity &identity,
      const std::filesystem::path &outFile);

  /// @brief Deletes a file from CAS with given identity.
  /// @param root Full path to the CAS vault root.
  /// @param identity SHA256 identity
  void Delete(
      const std::filesystem::path &root,
      const Identity &identity);
}
