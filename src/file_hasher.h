#ifndef FIT_FILE_HASHER_H
#define FIT_FILE_HASHER_H

#include "print_stream.h"

#include "fit.h"

#include "sqlite.h"

#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>

#include <cstdlib>
#include <cstdint>

namespace fit {

//
// This progress tracker is intended to be fast and will not
// consistently represent values in relation with each other.
// For example, it is possible that the number of processed
// files will not accurately reflect processed size because
// active threads may be updating some of these values while
// others were being printed.
//
struct progress_info_t {
   std::atomic<uint64_t> processed_files = 0;

   std::atomic<uint64_t> processed_size = 0;

   std::atomic<uint64_t> failed_files = 0;

   std::atomic<uint64_t> updated_files = 0;

   std::atomic<uint64_t> updated_size = 0;

   std::atomic<uint64_t> modified_files = 0;
   std::atomic<uint64_t> changed_files = 0;
   std::atomic<uint64_t> new_files = 0;
};

//
// A threaded file hasher.
//
class file_hasher_t {
   private:
      static constexpr const int DB_BUSY_TIMEOUT = 1000;

   private:
      const options_t& options;

      print_stream_t& print_stream;

      int64_t scan_id;

      const std::string_view hash_type;

      std::unique_ptr<unsigned char[]> file_buffer;

      std::queue<std::filesystem::directory_entry>& files;
      std::mutex& files_mtx;

      std::thread file_hasher_thread;

      std::atomic<bool> stop_request = 0;

      progress_info_t& progress_info;

      sqlite3 *file_scan_db = nullptr;

      sqlite3_stmt *stmt_insert_file = nullptr;
      sqlite3_stmt *stmt_find_file = nullptr;

   private:
      void hash_file(const std::filesystem::path& filepath, uint64_t& filesize, uint8_t hexhash[]);

      void run(void);
      
      static int sqlite_busy_handler_cb(void*, int count);

   public:
      file_hasher_t(const options_t& options, int64_t scan_id, std::queue<std::filesystem::directory_entry>& files, std::mutex& files_mtx, progress_info_t& progress_info, print_stream_t& print_stream);

      file_hasher_t(file_hasher_t&& other);

      ~file_hasher_t(void);

      void start(void);

      void stop(void);

      void join(void);
};

}

#endif // FIT_FILE_HASHER_H
