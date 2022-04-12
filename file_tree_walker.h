#ifndef FIT_FILE_TREE_WALKER_H
#define FIT_FILE_TREE_WALKER_H

#include "file_hasher.h"

#include "fit.h"

#include <string>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>

#include <cstdlib>
#include <cstdint>

namespace fit {

//
// A class that traverses a file tree and orchestrates threaded file
// hashers to compute and store file checksums.
//
class file_tree_walker_t {
   private:
      static constexpr const size_t MAX_FILE_QUEUE_SIZE = 1000;

   private:
      const options_t& options;
      int64_t scan_id;

      std::vector<file_hasher_t>   file_hashers;

      std::queue<std::filesystem::directory_entry> files;
      std::mutex files_mtx;
      std::condition_variable queue_ready;

      progress_info_t progress_info;

   public:
      file_tree_walker_t(const options_t& options, int64_t scan_id);

      void report_progress(void);

      template <typename dir_iter_t>
      void walk_tree(void);

      uint64_t get_processed_files(void) const;
      uint64_t get_processed_size(void) const;

      uint64_t get_modified_files(void) const;
      uint64_t get_new_files(void) const;
      uint64_t get_changed_files(void) const;
};

}

#endif // FIT_FILE_TREE_WALKER_H
