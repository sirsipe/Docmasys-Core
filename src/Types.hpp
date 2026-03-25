#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace Docmasys
{
  /// @brief Type used for hash-based CAS identification
  using Identity = std::array<uint8_t, 32>;

  /// @brief Type of database IDs.
  using ID = std::int64_t;

  enum class PropertyValueType : std::uint8_t
  {
    String = 0,
    Int = 1,
    Bool = 2,
  };

  using PropertyValue = std::variant<std::string, std::int64_t, bool>;
}
