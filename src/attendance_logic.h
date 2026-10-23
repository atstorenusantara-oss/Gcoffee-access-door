#pragma once
#include <stdint.h>

namespace attendance {
constexpr uint32_t DAY = 86400;
constexpr uint32_t WIB = 7 * 3600;
constexpr uint32_t MIN_EPOCH = 1704067200; // 2024-01-01 UTC.
constexpr uint32_t MAX_EPOCH = 2145916799; // Batas aman time_t core Arduino 2.x.
enum Kind : uint8_t { SCAN = 1, REPEAT = 2, MANUAL = 3, SYNC = 4, DENIED = 5 };
constexpr int32_t localDay(uint32_t epoch) { return (epoch + WIB) / DAY; }
constexpr bool validEpoch(uint64_t epoch) { return epoch >= MIN_EPOCH && epoch <= MAX_EPOCH; }
constexpr bool inWeek(uint32_t epoch, uint32_t now) {
  return epoch != 0 && localDay(epoch) <= localDay(now) && localDay(epoch) >= localDay(now) - 6;
}
struct Summary { uint32_t first = 0, last = 0, count = 0; };
constexpr Summary include(Summary current, uint32_t epoch, uint8_t kind) {
  if (!epoch || kind != SCAN) return current;
  return {current.first == 0 || epoch < current.first ? epoch : current.first,
          epoch > current.last ? epoch : current.last, current.count + 1};
}
constexpr uint32_t fnvByte(uint32_t hash, uint8_t value) { return (hash ^ value) * 16777619u; }
}
