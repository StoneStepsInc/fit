#ifndef FIT_FILE_TRACKER_H
#define FIT_FILE_TRACKER_H

#include "print_stream.h"
#include "exif_reader.h"

#include "fit.h"

#include "sqlite.h"

#include <string>
#include <string_view>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <vector>
#include <optional>
#include <filesystem>

#include <cstdlib>
#include <cstdint>

#ifndef NO_SSE_AVX
#include "mb_hasher.h"
#include "mb_sha256_traits.h"
#endif

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
#ifndef NO_SSE_AVX
      typedef mb_hasher_t<mb_sha256_traits, std::unique_ptr<FILE, file_handle_deleter_t>, uint64_t&> mb_file_hasher_t;

      static constexpr const size_t HASH_BIN_SIZE = mb_file_hasher_t::traits::HASH_SIZE;
      static constexpr const size_t HASH_HEX_SIZE = HASH_BIN_SIZE * 2;

      static constexpr const std::string_view HASH_TYPE = mb_file_hasher_t::traits::HASH_TYPE;
#endif

      static constexpr const int DB_BUSY_TIMEOUT = 1000;

      // same field order as in the select statement (stmt_find_file)
      typedef std::tuple<int64_t, int64_t, std::string, std::optional<std::string>, int64_t, int64_t, int64_t> version_record_t;

      //
      // A wrapper for the file version select statement result tuple,
      // which provides meaningful names to numbered column identifiers.
      //
      struct find_file_result_t {
         const std::optional<version_record_t> version_record;

         bool has_value(void) const {return version_record.has_value();}

         int64_t version(void) const {return std::get<0>(version_record.value());}

         int64_t mod_time(void) const {return std::get<1>(version_record.value());}

         const std::string& hash_type(void) const {return std::get<2>(version_record.value());}

         const std::optional<std::string>& hexhash(void) const {return std::get<3>(version_record.value());}

         int64_t version_id(void) const {return std::get<4>(version_record.value());}

         int64_t file_id(void) const {return std::get<5>(version_record.value());}

         int64_t scanset_scan_id(void) const {return std::get<6>(version_record.value());}
      };

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

#ifndef NO_SSE_AVX
      mb_file_hasher_t mb_hasher;
#endif
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

      find_file_result_t select_version_record(const std::string& filepath);

      int64_t insert_version_record(const std::string& filepath, int64_t file_id, int64_t version, int64_t filesize, const std::filesystem::directory_entry& dir_entry, unsigned char hexhash_file[], std::optional<int64_t> exif_id);

      void insert_scanset_record(const std::string& filepath, int64_t version_id);

      void begin_transaction(const std::string& filepath);

      void commit_transaction(const std::string& filepath);

      void rollback_transaction(const std::string& filepath);

      void hash_file(const std::filesystem::path& filepath, uint64_t& filesize, unsigned char hexhash[]);

      void run(void);

#ifndef NO_SSE_AVX
      static std::tuple<std::unique_ptr<FILE, file_handle_deleter_t>, uint64_t&> open_file(const std::filesystem::path& filepath, uint64_t& filesize);
      static bool read_file(unsigned char *file_buffer, size_t buf_size, size_t& data_size, std::tuple<std::unique_ptr<FILE, file_handle_deleter_t>, uint64_t&>& args);
#endif

      static int sqlite_busy_handler_cb(void*, int count);

      static std::vector<std::string> parse_EXIF_exts(const options_t& options);

      static bool set_sqlite_journal_mode(sqlite3 *file_scan_db, print_stream_t& print_stream);

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

#endif // FIT_FILE_TRACKER_H
