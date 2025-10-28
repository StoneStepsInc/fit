#include <gtest/gtest.h>

#include "../scanset_bitmap.h"

#include <set>
#include <ranges>
#include <algorithm>

namespace fit {
namespace test {

class scanset_bitmap_test_t : public ::testing::Test {
   protected:
      const uint64_t first_rowid;
      const uint64_t last_rowid;

      const size_t scanset_size;

      fit::scanset_bitmap_t scanset_bitmap;

      std::set<uint64_t> expected;

      std::set<uint64_t> result;

   protected:
      scanset_bitmap_test_t(uint64_t first_rowid, uint64_t last_rowid, size_t scanset_size) :
            first_rowid(first_rowid),
            last_rowid(last_rowid),
            scanset_size(scanset_size),
            scanset_bitmap(first_rowid, last_rowid)
      {
      }
};

class small_scanset_bitmap_test_t : public scanset_bitmap_test_t {
   protected:
      small_scanset_bitmap_test_t(void) :
         scanset_bitmap_test_t(1000, 1009, 10)
      {
      }
};

class large_scanset_bitmap_test_t : public scanset_bitmap_test_t {
   protected:
      large_scanset_bitmap_test_t(void) :
         scanset_bitmap_test_t(1000, 1'000'999, 1'000'000)
      {
      }
};

TEST_F(small_scanset_bitmap_test_t, small_bitmap_middle_bit_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   // keep a single bit in the middle of the bitmap and clear all other bits
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i != scanset_size/2+1000) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   // all bits but one have been cleared
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-1, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(small_scanset_bitmap_test_t, small_bitmap_first_bit_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   // keep the first bit and clear all other bits
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i != first_rowid) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-1, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(small_scanset_bitmap_test_t, small_bitmap_last_bit_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   // keep the last bit and clear all other bits
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i != last_rowid) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-1, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(small_scanset_bitmap_test_t, small_bitmap_no_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   // clear all bits
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      scanset_bitmap.clear_rowid(i);
      ASSERT_FALSE(scanset_bitmap.test_rowid(i));
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(small_scanset_bitmap_test_t, small_bitmap_odd_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   // clear even bits and keep odd bits
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i % 2 == 0) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size/2, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(small_scanset_bitmap_test_t, small_bitmap_even_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   // clear odd bits and keep even bits
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i % 2 != 0) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         expected.insert(i);
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size/2, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(small_scanset_bitmap_test_t, small_bitmap_merge_first_last_bits_test)
{
   expected = {1001, 1003, 1004, 1005};

   // keep bits corresponding to rowid values in `expected`
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(!expected.contains(i))
         scanset_bitmap.clear_rowid(i);
   }

   fit::scanset_bitmap_t scanset_bitmap2(first_rowid, last_rowid);

   std::set<uint64_t> expected2 = {1003, 1005, 1006, 1009};

   // keep bits corresponding to rowid values in `expected2`
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(!expected2.contains(i))
         scanset_bitmap2.clear_rowid(i);
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-expected.size(), scanset_bitmap.count());

   ASSERT_EQ(scanset_size, scanset_bitmap2.size());
   ASSERT_EQ(scanset_size-expected2.size(), scanset_bitmap2.count());

   scanset_bitmap.update(scanset_bitmap2);

   std::set<uint64_t> combined_expected;

   // keep rowid values that exist in both sets (1003, 1005) - those are the bits that haven't been cleared in ether bitmap
   std::ranges::set_intersection(expected, expected2, std::inserter(combined_expected, combined_expected.begin()));

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-combined_expected.size(), scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(combined_expected, result);
}


TEST_F(small_scanset_bitmap_test_t, small_bitmap_merge_odd_even_bits_test)
{
   // clear odd bits and track even rowid values in `expected`
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i % 2 != 0)
         scanset_bitmap.clear_rowid(i);
      else
         expected.insert(i);
   }

   fit::scanset_bitmap_t scanset_bitmap2(first_rowid, last_rowid);

   std::set<uint64_t> expected2;

   // clear even bits and track odd rowid values in `expected2`
   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i % 2 == 0)
         scanset_bitmap2.clear_rowid(i);
      else
         expected2.insert(i);
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size/2, scanset_bitmap.count());

   ASSERT_EQ(scanset_size, scanset_bitmap2.size());
   ASSERT_EQ(scanset_size/2, scanset_bitmap2.count());

   scanset_bitmap.update(scanset_bitmap2);

   std::set<uint64_t> combined_expected;

   // the intersection should produce an empty combined set, which is what we exepct in the combined bitmap
   std::ranges::set_intersection(expected, expected2, std::inserter(combined_expected, combined_expected.begin()));

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-combined_expected.size(), scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(combined_expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_middle_bit_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i != scanset_size/2+1000) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-1, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap) {
      result.insert(rowid);
   }

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_first_bit_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i != first_rowid) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-1, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap) {
      result.insert(rowid);
   }

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_last_bit_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i != last_rowid) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-1, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap) {
      result.insert(rowid);
   }

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_no_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      scanset_bitmap.clear_rowid(i);
      ASSERT_FALSE(scanset_bitmap.test_rowid(i));
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap) {
      result.insert(rowid);
   }

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_odd_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      // clear even bits and leave odd bits standing
      if(i % 2 == 0) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
         expected.insert(i);
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size/2, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap) {
      result.insert(rowid);
   }

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_even_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      // clear odd bits and leave even bits standing
      if(i % 2 != 0) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         expected.insert(i);
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size/2, scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap) {
      result.insert(rowid);
   }

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_first_last_bits_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   std::set<uint64_t> set_bits = {1005, 1006, 1007, 1008, 1009, 1'000'991, 1'000'992, 1'000'993, 1'000'994, 1'000'995};

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(!set_bits.contains(i)) {
         scanset_bitmap.clear_rowid(i);
         ASSERT_FALSE(scanset_bitmap.test_rowid(i));
      }
      else {
         expected.insert(i);
         ASSERT_TRUE(scanset_bitmap.test_rowid(i));
      }
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-set_bits.size(), scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(expected, result);
}

TEST_F(large_scanset_bitmap_test_t, large_bitmap_modulo_2_3_div_overlap_test)
{
   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(0, scanset_bitmap.count());

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i % 2 != 0)
         scanset_bitmap.clear_rowid(i);
      else
         expected.insert(i);
   }

   fit::scanset_bitmap_t scanset_bitmap2(first_rowid, last_rowid);
   std::set<uint64_t> expected2;

   for(size_t i = first_rowid; i <= last_rowid; i++) {
      if(i % 3 != 0)
         scanset_bitmap2.clear_rowid(i);
      else
         expected2.insert(i);
   }

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-expected.size(), scanset_bitmap.count());

   ASSERT_EQ(scanset_size, scanset_bitmap2.size());
   ASSERT_EQ(scanset_size-expected2.size(), scanset_bitmap2.count());

   std::set<uint64_t> combined_expected;

   std::ranges::set_intersection(expected, expected2, std::inserter(combined_expected, combined_expected.begin()));

   scanset_bitmap.update(scanset_bitmap2);

   ASSERT_EQ(scanset_size, scanset_bitmap.size());
   ASSERT_EQ(scanset_size-combined_expected.size(), scanset_bitmap.count());

   for(uint64_t rowid : scanset_bitmap)
      result.insert(rowid);

   ASSERT_EQ(combined_expected, result);
}

}
}
