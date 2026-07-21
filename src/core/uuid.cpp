// SPDX-License-Identifier: Apache-2.0

#include <graphscore/core/uuid.hpp>

#include <random>
#include <sstream>
#include <iomanip>

namespace graphscore {

Uuid Uuid::generate() {
  static thread_local std::random_device rd;
  static thread_local std::mt19937_64 gen(rd());
  static thread_local std::uniform_int_distribution<std::uint64_t> dis;

  std::array<std::uint8_t, kSize> bytes{};
  std::uint64_t hi = dis(gen);
  std::uint64_t lo = dis(gen);
  for (std::size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<std::uint8_t>((hi >> (i * 8)) & 0xFF);
    bytes[i + 8] = static_cast<std::uint8_t>((lo >> (i * 8)) & 0xFF);
  }
  bytes[6] = (bytes[6] & 0x0F) | 0x40;
  bytes[8] = (bytes[8] & 0x3F) | 0x80;
  return Uuid(bytes);
}

std::string Uuid::to_string() const {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < kSize; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
    oss << std::setw(2) << static_cast<int>(bytes_[i]);
  }
  return oss.str();
}

}  // namespace graphscore
