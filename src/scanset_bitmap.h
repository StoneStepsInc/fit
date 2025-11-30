#ifndef SCANSET_BITMAP_H
#define SCANSET_BITMAP_H

#include <vector>
#include <tuple>
#include <cstdint>

namespace fit {

//
// Provides a container for tracking scanset identifiers, SQLite
// rowid values, for each file version included into the scanset
// that is being represented by a single bit.
// 
// When the bitmap is created, all of the bits are set. During
// a verification scan, bits file version bits are cleared for
// each scanned file that was found in the database. After the
// verification scan is complete, the bits that are remaining
// identify the files that existed at the time of the base
// scan and are not present in the verification scan (i.e.
// either removed or cannot be accessed anymore).
// 
// Multiple scanset bitmaps are expected to be created for the
// same scanset, processed by different threads, and then merged,
// via the `scanset_bitmap_t::update` call, into a single scanset
// bitmap that can be used for reporting removed files.
//
class scanset_bitmap_t {
   public:
      //
      // A scanset bitmap iterator that returns rowid values
      // corresponding to non-zero bits in the scanset bitmap.
      //
      class const_iterator {
         private:
            const std::vector<uint64_t>& scanset_bitmap;

            uint64_t first_rowid;      // a copy of scanset_bitmap_t::first_rowid

            uint64_t last_rowid;       // a copy of scanset_bitmap_t::last_rowid

            size_t elem_offset;        // a zero-based offset of the current uint64_t element

            size_t bit_offset;         // a zero-based bit offset from the most significant bit at elem_offset (i.e. 64th bit is zero)

            uint64_t bit_mask;         // a bit mask for the bit identified by bit_offset

         public:
            explicit const_iterator(uint64_t first_rowid, uint64_t last_rowid, const std::vector<uint64_t>& scanset_bitmap, uint64_t current_rowid);

            uint64_t operator*(void) const;

            friend bool operator == (const const_iterator& a, const const_iterator& b);
            friend bool operator != (const const_iterator& a, const const_iterator& b);

            const_iterator& operator++(void);
            const_iterator operator++(int);
      };

   private:
      uint64_t first_rowid;      // the first rowid (invalid if zero)

      uint64_t last_rowid;       // the last rowid (invalid if zero, inclusive otherwise)

      size_t rowid_count;        // number of existing rowid values (i.e. those that have been cleared)

      std::vector<uint64_t> scanset_bitmap;

   public:
      scanset_bitmap_t(void);

      scanset_bitmap_t(std::tuple<uint64_t, uint64_t> first_last_rowid);

      scanset_bitmap_t(uint64_t first_rowid, uint64_t last_rowid);

      scanset_bitmap_t(scanset_bitmap_t&& other);

      scanset_bitmap_t& operator = (scanset_bitmap_t&& other);

      bool empty(void) const;

      size_t size(void) const;

      size_t count(void) const;

      void clear_rowid(size_t pos);

      bool test_rowid(size_t pos) const;

      void update(const scanset_bitmap_t& other);

      const_iterator begin(void) const;

      const_iterator end(void) const;
};

}

#endif // SCANSET_BITMAP_H
