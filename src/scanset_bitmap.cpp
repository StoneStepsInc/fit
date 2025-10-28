#include "scanset_bitmap.h"

#include <climits>
#include <cstdint>
#include <stdexcept>
#include <bit>
#include <functional>

#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

namespace fit {

scanset_bitmap_t::const_iterator::const_iterator(uint64_t first_rowid, uint64_t last_rowid, const std::vector<uint64_t>& scanset_bitmap, uint64_t current_rowid) :
      scanset_bitmap(scanset_bitmap),
      first_rowid(first_rowid),
      last_rowid(last_rowid),
      elem_offset((current_rowid - first_rowid) / (sizeof(uint64_t) * CHAR_BIT)),
      bit_offset((current_rowid - first_rowid) % (sizeof(uint64_t) * CHAR_BIT)),
      bit_mask(UINT64_C(1) << ((sizeof(uint64_t) * CHAR_BIT) - 1 - bit_offset))
{
   if(current_rowid <= last_rowid) {
      if(!(scanset_bitmap[elem_offset] & bit_mask))
         ++(*this);
   }
}

uint64_t scanset_bitmap_t::const_iterator::operator*() const
{
   uint64_t rowid = (elem_offset * CHAR_BIT * sizeof(uint64_t)) + bit_offset + first_rowid;

   // check if the iterator has been exhausted
   if(rowid > last_rowid)
      throw std::range_error(FMTNS::format("No more scanset elements"));

   // make sure it is a valid rowid bit that is still standing
   if(!(scanset_bitmap[elem_offset] & bit_mask))
      throw std::logic_error(FMTNS::format("Invalid scanset iterator"));
   
   return rowid;
}

bool operator == (const scanset_bitmap_t::const_iterator& a, const scanset_bitmap_t::const_iterator& b)
{
   return a.elem_offset == b.elem_offset && a.bit_offset == b.bit_offset;
}

bool operator != (const scanset_bitmap_t::const_iterator& a, const scanset_bitmap_t::const_iterator& b)
{
   return !(a == b);
}

scanset_bitmap_t::const_iterator& scanset_bitmap_t::const_iterator::operator++()
{
   // move to the next bit, whether within the current element or in the next one (may move past the last valid bit)
   if(bit_offset < sizeof(uint64_t) * CHAR_BIT - 1) {
      bit_offset++;
      bit_mask >>= 1;
   }
   else {
      elem_offset++;
      bit_offset = 0;
      bit_mask = UINT64_C(1) << ((sizeof(uint64_t) * CHAR_BIT) - 1);
   }

   // look for the next set bit through the entire bitmap vector
   while((elem_offset * CHAR_BIT * sizeof(uint64_t)) + bit_offset + first_rowid <= last_rowid) {
      uint64_t elem_value = scanset_bitmap[elem_offset];

      if(elem_value) {
         if(bit_offset == 0) {
            // if this is the first iteration for this element, skip the left-most zeros in one jump
            bit_offset = std::countl_zero(elem_value);
            bit_mask = UINT64_C(1) << (sizeof(uint64_t) * CHAR_BIT - bit_offset - 1);
         }

         // look for the next set bit within the current element
         while(bit_offset < sizeof(uint64_t) * CHAR_BIT) {
            if(elem_value & bit_mask)
               return *this;

            // clear all bits to the left of the current mask bit
            uint64_t tail_bits = elem_value & ((bit_mask - 1) | bit_mask);

            bit_offset = std::countl_zero(tail_bits);
            bit_mask = UINT64_C(1) << (sizeof(uint64_t) * CHAR_BIT - bit_offset - 1);
         }
      }

      // we exhausted the current element and need to move to the next one
      if(elem_offset < scanset_bitmap.size()-1) {
         elem_offset++;
         bit_offset = 0;
      }
      else {
         // count the number of bits in the last element (elem_offset now is size()-1)
         bit_offset = last_rowid - first_rowid + 1 - elem_offset * sizeof(uint64_t) * CHAR_BIT;

         // if we landed on the bit past the last one in this element, move to the next element
         if(bit_offset == sizeof(uint64_t) * CHAR_BIT) {
            bit_offset = 0;
            elem_offset++;
         }
      }

      // set the bit mask to the next bit (may be set or unset or even the one be past the last valid one)
      bit_mask = UINT64_C(1) << (sizeof(uint64_t) * CHAR_BIT - bit_offset - 1);
   }

   return *this;
}

scanset_bitmap_t::const_iterator scanset_bitmap_t::const_iterator::operator++(int)
{
   const_iterator tmp = *this;
   ++(*this);
   return tmp;
}

scanset_bitmap_t::scanset_bitmap_t(void) :
      first_rowid(0),
      last_rowid(0),
      rowid_count(0)
{
}

scanset_bitmap_t::scanset_bitmap_t(std::tuple<uint64_t, uint64_t> first_last_rowid) :
      scanset_bitmap_t(std::get<0>(first_last_rowid), std::get<1>(first_last_rowid))
{
}

scanset_bitmap_t::scanset_bitmap_t(uint64_t first_rowid, uint64_t last_rowid) :
      first_rowid(first_rowid),
      last_rowid(last_rowid),
      rowid_count(0),
      // set all bits to one (last-first yields one less than the number of rowid's, plus 64 rounds up the integer division to handle multiples of 64)
      scanset_bitmap((last_rowid - first_rowid + sizeof(uint64_t) * CHAR_BIT) / (sizeof(uint64_t) * CHAR_BIT), ~UINT64_C(0))
{
   // this constructor is expected to be called with specific values and a scanset rowid cannot be a zero
   if(!last_rowid || !first_rowid)
      throw std::logic_error("A scanset bitmap cannot be constructed with a zero first or last rowid");

   // compute the last valid bit in the bitmap
   uint64_t bit_mask = UINT64_C(1) << ((sizeof(uint64_t) * CHAR_BIT) - (last_rowid + 1 - first_rowid) % (sizeof(uint64_t) * CHAR_BIT));

   // clear bits to the right from the last valid bit
   scanset_bitmap.back() &= ~(bit_mask-1);
}

scanset_bitmap_t::scanset_bitmap_t(scanset_bitmap_t&& other) :
      first_rowid(other.first_rowid),
      last_rowid(other.last_rowid),
      rowid_count(other.rowid_count),
      scanset_bitmap(std::move(other.scanset_bitmap))
{
   other.first_rowid = 0;
   other.last_rowid = 0;
   other.rowid_count = 0;
}

scanset_bitmap_t& scanset_bitmap_t::operator = (scanset_bitmap_t&& other)
{
   first_rowid = other.first_rowid;
   last_rowid = other.last_rowid;
   rowid_count = other.rowid_count;

   scanset_bitmap = std::move(other.scanset_bitmap);

   other.first_rowid = 0;
   other.last_rowid = 0;
   other.rowid_count = 0;

   return *this;
}

bool scanset_bitmap_t::empty(void) const
{
   return !first_rowid && !last_rowid;
}

size_t scanset_bitmap_t::size(void) const
{
   // both ends are inclusive, so [0, 0] would mean we have 1 entry, but because zero is not allowed, we can interpret it as empty
   return (first_rowid && last_rowid) ? (last_rowid - first_rowid + 1) : 0;
}

size_t scanset_bitmap_t::count(void) const
{
   return rowid_count;
}

void scanset_bitmap_t::clear_rowid(size_t rowid)
{
   if(rowid < first_rowid || rowid > last_rowid)
      throw std::logic_error(FMTNS::format("Invalid rowid {:d}", rowid));

   size_t elem_offset = (rowid - first_rowid) / (sizeof(uint64_t) * CHAR_BIT);
   size_t bit_offset = (rowid - first_rowid) % (sizeof(uint64_t) * CHAR_BIT);

   // compute the bit mask, counting rows from the most significant bit (i.e. 1st rowid is bit 63)
   uint64_t bit_mask = UINT64_C(1) << ((sizeof(uint64_t) * CHAR_BIT) - 1 - bit_offset);

   // set and count the new bit only if it is not already set
   if(scanset_bitmap[elem_offset] & bit_mask) {
      rowid_count++;
      scanset_bitmap[elem_offset] &= ~bit_mask;
   }
}

bool scanset_bitmap_t::test_rowid(size_t rowid) const
{
   if(rowid < first_rowid || rowid > last_rowid)
      throw std::logic_error(FMTNS::format("Invalid rowid {:d}", rowid));

   size_t elem_offset = (rowid - first_rowid) / (sizeof(uint64_t) * CHAR_BIT);
   size_t bit_offset = (rowid - first_rowid) % (sizeof(uint64_t) * CHAR_BIT);

   // test the computed element against the bit mask for the specified row ID
   return (scanset_bitmap[elem_offset] & UINT64_C(1) << ((sizeof(uint64_t) * CHAR_BIT) - 1 - bit_offset)) != 0;
}

void scanset_bitmap_t::update(const scanset_bitmap_t& other)
{
   if(other.last_rowid != last_rowid || other.first_rowid != first_rowid)
      throw std::logic_error(FMTNS::format("Both bitmaps must have the same rowid range [{:d}, {:d}]/[{:d}, {:d}]", first_rowid, last_rowid, other.first_rowid, other.last_rowid));

   for(size_t i = 0; i < scanset_bitmap.size(); i++) {
      // extract the new 0s as 1s from the incoming bitset (a new 0 is counted where this bitmap has 1 and the other one has a zero)
      uint64_t new_holes = scanset_bitmap[i] & ~other.scanset_bitmap[i];

      // clear the bits to the right of the last valid bit in the last element
      if(i == scanset_bitmap.size()-1) {
         uint64_t bit_mask = UINT64_C(1) << ((sizeof(uint64_t) * CHAR_BIT) - (last_rowid + 1 - first_rowid) % (sizeof(uint64_t) * CHAR_BIT));
         new_holes &= ~(bit_mask-1);
      }

      // update the rowid count with the number of new cleared bits in the other bitmap
      rowid_count += std::popcount(new_holes);

      // transfer the cleared bits from the other bitmap into this one
      scanset_bitmap[i] &= other.scanset_bitmap[i];
   }
}

scanset_bitmap_t::const_iterator scanset_bitmap_t::begin(void) const
{
   return const_iterator(first_rowid, last_rowid, scanset_bitmap, first_rowid);
}

scanset_bitmap_t::const_iterator scanset_bitmap_t::end(void) const
{
   return const_iterator(first_rowid, last_rowid, scanset_bitmap, last_rowid+1);
}

}
