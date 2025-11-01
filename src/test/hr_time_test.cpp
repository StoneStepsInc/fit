#include <gtest/gtest.h>

#include "../format.h"

#include <string>
#include <chrono>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

using namespace std::literals::chrono_literals;

namespace fit {
namespace test {

TEST(hr_time_suite, seconds_milliseconds_test)
{
   ASSERT_EQ("0.001 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(1ms)));
   ASSERT_EQ("0.012 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(12ms)));
   ASSERT_EQ("0.123 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(123ms)));

   ASSERT_EQ("55.001 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(55'001ms)));
   ASSERT_EQ("55.020 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(55'020ms)));
   ASSERT_EQ("55.300 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(55'300ms)));
   ASSERT_EQ("55.123 sec"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(55'123ms)));
}

TEST(hr_time_suite, minutes_seconds_test)
{
   ASSERT_EQ("2:03 min"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(2min + 3s + 456ms)));
   ASSERT_EQ("2:30 min"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(2min + 30s + 456ms)));

   // no rounding for time values
   ASSERT_EQ("10:12 min"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(10min + 12s + 953ms)));
}

TEST(hr_time_suite, hours_minutes_seconds_test)
{
   ASSERT_EQ("2:03:04 hours"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(2h + 3min + 4s + 123ms)));
   ASSERT_EQ("14:05:43 hours"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(14h + 5min + 43s + 123ms)));
   ASSERT_EQ("14:12:43 hours"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(14h + 12min + 43s + 123ms)));
   ASSERT_EQ("75:55:03 hours"s, hr_time(std::chrono::duration_cast<std::chrono::steady_clock::duration>(75h + 55min + 3s + 123ms)));
}

}
}
