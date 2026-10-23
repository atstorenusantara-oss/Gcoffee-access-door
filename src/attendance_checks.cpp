// Pemeriksaan logika saat build; tidak mengakses relay atau mengubah log.
#include "attendance_logic.h"
using namespace attendance;
constexpr uint32_t first = 1792717200; // 2026-10-23 08:00 WIB.
constexpr auto one = include({}, first, SCAN);
static_assert(one.first == first && one.last == first && one.count == 1, "Single scan");
constexpr auto two = include(one, first + 9 * 3600, SCAN);
static_assert(two.first == first && two.last == first + 9 * 3600 && two.count == 2, "First and last");
constexpr auto correction = include(two, first - 60, SCAN);
static_assert(correction.first == first - 60 && correction.last == two.last, "Clock moved backwards");
static_assert(include(one, first + 1, REPEAT).count == 1, "Repeated reads excluded");
static_assert(include(one, first + 1, MANUAL).count == 1, "Manual door excluded");
static_assert(include(one, first + 1, SYNC).count == 1, "Clock sync excluded");
static_assert(include(one, first + 1, DENIED).count == 1, "Unknown card excluded");
static_assert(include(one, 0, SCAN).count == 1, "Unsynchronized scan excluded");
static_assert(inWeek(first - 6 * DAY, first), "Seven calendar days inclusive");
static_assert(!inWeek(first - 7 * DAY, first), "Eighth day excluded");
static_assert(!inWeek(first + DAY, first) && !inWeek(0, first), "Future and unknown excluded");
static_assert(localDay(1767286799) + 1 == localDay(1767286800), "WIB midnight boundary");
static_assert(!validEpoch(0) && validEpoch(MIN_EPOCH) && !validEpoch(uint64_t(MAX_EPOCH) + 1), "Epoch validation");
