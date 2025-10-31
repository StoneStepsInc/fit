#include <gtest/gtest.h>

#include "../format.h"

#include <string>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

namespace fit {
namespace test {

TEST(hr_bytes, max_uint64_test)
{
   // 18,446,744,073,709,551,615
   ASSERT_EQ("18.447 EB"s, fit::hr_bytes(std::numeric_limits<uint64_t>::max()));
}

TEST(hr_bytes, zero_bytes_test)
{
   ASSERT_EQ("0 bytes"s, fit::hr_bytes(UINT64_C(0)));
}

TEST(hr_bytes, one_byte_test)
{
   ASSERT_EQ("1 byte"s, fit::hr_bytes(UINT64_C(1)));
}

TEST(hr_bytes, hundreds_bytes_test)
{
   ASSERT_EQ("321 bytes"s, fit::hr_bytes(UINT64_C(321)));
}

TEST(hr_bytes, kb_no_decimals_test)
{
   ASSERT_EQ("12 KB"s, fit::hr_bytes(UINT64_C(12'000)));
}

TEST(hr_bytes, kb_one_decimal_test)
{
   ASSERT_EQ("12.1 KB"s, fit::hr_bytes(UINT64_C(12'102)));
}

TEST(hr_bytes, kb_two_decimals_test)
{
   ASSERT_EQ("12.01 KB"s, fit::hr_bytes(UINT64_C(12'012)));
   ASSERT_EQ("12.12 KB"s, fit::hr_bytes(UINT64_C(12'123)));
   ASSERT_EQ("12.13 KB"s, fit::hr_bytes(UINT64_C(12'125)));
}

TEST(hr_bytes, mb_no_decimals_test)
{
   ASSERT_EQ("12 MB"s, fit::hr_bytes(UINT64_C(12'000'123)));
}

TEST(hr_bytes, mb_one_decimal_test)
{
   ASSERT_EQ("12.1 MB"s, fit::hr_bytes(UINT64_C(12'102'123)));
}

TEST(hr_bytes, mb_two_decimals_test)
{
   ASSERT_EQ("12.01 MB"s, fit::hr_bytes(UINT64_C(12'012'123)));
   ASSERT_EQ("12.12 MB"s, fit::hr_bytes(UINT64_C(12'123'123)));
   ASSERT_EQ("12.13 MB"s, fit::hr_bytes(UINT64_C(12'125'123)));
}

TEST(hr_bytes, gb_no_decimals_test)
{
   ASSERT_EQ("12 GB"s, fit::hr_bytes(UINT64_C(12'001'234'567)));
}


TEST(hr_bytes, gb_one_decimal_test)
{
   ASSERT_EQ("12.1 GB"s, fit::hr_bytes(UINT64_C(12'103'456'789)));
}

TEST(hr_bytes, gb_two_decimals_test)
{
   ASSERT_EQ("12.01 GB"s, fit::hr_bytes(UINT64_C(12'012'345'678)));
   ASSERT_EQ("12.12 GB"s, fit::hr_bytes(UINT64_C(12'123'345'678)));
   ASSERT_EQ("12.13 GB"s, fit::hr_bytes(UINT64_C(12'125'345'678)));
}

TEST(hr_bytes, tb_no_decimals)
{
   ASSERT_EQ("34 TB"s, fit::hr_bytes(UINT64_C(34'000'123'456'789)));
}

TEST(hr_bytes, tb_one_decimal_test)
{
   ASSERT_EQ("34.2 TB"s, fit::hr_bytes(UINT64_C(34'200'123'456'789)));
}

TEST(hr_bytes, tb_two_decimals_test)
{
   ASSERT_EQ("34.03 TB"s, fit::hr_bytes(UINT64_C(34'030'123'456'789)));
   ASSERT_EQ("34.56 TB"s, fit::hr_bytes(UINT64_C(34'560'123'456'789)));
}

TEST(hr_bytes, tb_three_decimals_test)
{
   ASSERT_EQ("34.001 TB"s, fit::hr_bytes(UINT64_C(34'001'123'456'789)));
   ASSERT_EQ("34.031 TB"s, fit::hr_bytes(UINT64_C(34'031'123'456'789)));
   ASSERT_EQ("34.232 TB"s, fit::hr_bytes(UINT64_C(34'231'523'456'789)));
}

}
}
