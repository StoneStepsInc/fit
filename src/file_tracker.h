#ifndef FIT_FILE_HASHER_H
#define FIT_FILE_HASHER_H

#include "print_stream.h"
#include "exif_reader.h"

#include "fit.h"

#include "sqlite.h"

#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <vector>
#include <optional>

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
class file_tracker_t {
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

      std::thread file_tracker_thread;

      std::atomic<bool> stop_request = 0;

      progress_info_t& progress_info;

      std::vector<std::string> EXIF_exts;

      exif::exif_reader_t exif_reader;

      sqlite3 *file_scan_db = nullptr;

      sqlite3_stmt *stmt_insert_file = nullptr;
      sqlite3_stmt *stmt_insert_version = nullptr;
      sqlite3_stmt *stmt_insert_scanset_file = nullptr;
      sqlite3_stmt *stmt_insert_exif = nullptr;

      sqlite3_stmt *stmt_find_file = nullptr;

      sqlite3_stmt *stmt_begin_txn = nullptr;
      sqlite3_stmt *stmt_commit_txn = nullptr;
      sqlite3_stmt *stmt_rollback_txn = nullptr;

   private:
      int64_t insert_file_record(const std::string& filepath, const std::filesystem::directory_entry& dir_entry);

      int64_t insert_exif_record(const std::string& filepath, const std::vector<exif::field_value_t>& exif_fields, const exif::field_bitset_t& field_bitset);

      int64_t insert_version_record(const std::string& filepath, int64_t file_id, int64_t version, int64_t filesize, const std::filesystem::directory_entry& dir_entry, uint8_t hexhash_file[], std::optional<int64_t> exif_id);

      void insert_scanset_record(const std::string& filepath, int64_t version_id);

      void hash_file(const std::filesystem::path& filepath, uint64_t& filesize, uint8_t hexhash[]);

      void run(void);
      
      static int sqlite_busy_handler_cb(void*, int count);

      static std::vector<std::string> parse_EXIF_exts(const options_t& options);

   public:
      file_tracker_t(const options_t& options, int64_t scan_id, std::queue<std::filesystem::directory_entry>& files, std::mutex& files_mtx, progress_info_t& progress_info, print_stream_t& print_stream);

      file_tracker_t(file_tracker_t&& other);

      ~file_tracker_t(void);

      static void initialize(print_stream_t& print_stream);

      static void cleanup(print_stream_t& print_stream) noexcept;

      void start(void);

      void stop(void);

      void join(void);
};

}

#endif // FIT_FILE_HASHER_H
