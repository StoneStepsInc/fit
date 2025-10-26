#ifndef SCANSET_BITMAP_H
#define SCANSET_BITMAP_H

#include <vector>
#include <tuple>
#include <cstdint>

namespace fit {

class scanset_bitmap_t {
   public:
      class const_iterator {
         private:
            const std::vector<uint64_t>& scanset_bitmap;

            uint64_t first_rowid;

            uint64_t last_rowid;

            size_t elem_offset;

            size_t bit_offset;

            uint64_t bit_mask;

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
