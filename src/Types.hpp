#pragma once
#include <array>
#include <cstdint>

namespace Docmasys
{
  /// @brief Type used for hash-based CAS identification
  using Identity = std::array<uint8_t, 32>;

  /// @brief Type of database IDs.
  using ID = std::int64_t;
};