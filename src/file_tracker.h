#ifndef FIT_FILE_TRACKER_H
#define FIT_FILE_TRACKER_H

#include "print_stream.h"
#include "exif_reader.h"
#include "scanset_bitmap.h"

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
// consistently represent values in relation with each other
// while the file tree is being scanned.
// 
// For example, it is possible that the number of processed
// files will not accurately reflect processed size because
// active threads may be updating some of these values while
// others were being printed.
//
struct progress_info_t {
   std::atomic<uint64_t> processed_files = 0;
   std::atomic<uint64_t> processed_size = 0;

   std::atomic<uint64_t> failed_files = 0;

   std::atomic<uint64_t> unmatched_files = 0;
   std::atomic<uint64_t> unmatched_size = 0;

   std::atomic<uint64_t> modified_files = 0;
   std::atomic<uint64_t> changed_files = 0;
   std::atomic<uint64_t> new_files = 0;

   std::atomic<uint64_t> removed_files = 0;
   std::atomic<uint64_t> removed_size = 0;
};

//
// A threaded file hasher.
//
class file_tracker_t {
   private:
      static constexpr const int DB_BUSY_TIMEOUT = 1000;

      // same field order as in the select statement (stmt_find_version)
      // version, mod_time, hash_type, hash, versions.rowid, file_id, scan_id
      typedef std::tuple<int64_t, int64_t, std::string, std::optional<std::string>, int64_t, int64_t, int64_t, int64_t> version_record_t;

      //
      // A wrapper for the file version select statement result tuple,
      // which provides meaningful names to numbered column identifiers.
      //
      struct version_record_result_t {
         std::optional<version_record_t> version_record;

         version_record_result_t(void) : version_record(std::nullopt) {}
         version_record_result_t(std::optional<version_record_t>&& other) : version_record(std::move(other)) {}
         version_record_result_t(version_record_result_t&& other) noexcept : version_record(std::move(other.version_record)) {}

         version_record_result_t& operator = (version_record_result_t&& other) noexcept {version_record = std::move(other.version_record); return *this;}

         bool has_value(void) const {return version_record.has_value();}

         int64_t version(void) const {return std::get<0>(version_record.value());}

         int64_t mod_time(void) const {return std::get<1>(version_record.value());}

         const std::string& hash_type(void) const {return std::get<2>(version_record.value());}

         const std::optional<std::string>& hexhash(void) const {return std::get<3>(version_record.value());}

         int64_t version_id(void) const {return std::get<4>(version_record.value());}

         int64_t file_id(void) const {return std::get<5>(version_record.value());}

         int64_t scanset_scan_id(void) const {return std::get<6>(version_record.value());}

         int64_t scanset_rowid(void) const {return std::get<7>(version_record.value());}
      };

      //
      // A wrapper class for an error captured while reading data
      // from a file being hashed. The reader function cannot throw
      // exceptions and this class serves as an error text holder
      // within hash job context parameters, so when a hash result
      // is obtained, and this tuple member is not empty, the hash
      // is considered invalid and the error within this class is
      // reported.
      // 
      // The reason it is a class and not just a string within the
      // tuple, is to allow adding additional error details in the
      // future.
      //
      struct file_read_error_t {
         std::string    error;
      };

#ifndef NO_SSE_AVX
      typedef mb_hasher_t<mb_sha256_traits, file_tracker_t,
                           // param_tuple_t
                           std::unique_ptr<FILE, file_handle_deleter_t>,
                           uint64_t,
                           version_record_result_t,
                           std::filesystem::directory_entry,
                           std::optional<file_read_error_t>> mb_file_hasher_t;
#endif

      static const size_t HASH_BIN_SIZE;
      static const size_t HASH_HEX_SIZE;

      static const std::string_view HASH_TYPE;

   private:
      const options_t& options;

      print_stream_t& print_stream;

      // the current scan ID (empty for verification scans)
      std::optional<int64_t> scan_id;

      // the scan ID against which files will be compared (empty for initial scans; cannot be empty for verification scans)
      std::optional<int64_t> base_scan_id;

      std::unique_ptr<unsigned char[]> file_buffer;

      std::queue<std::filesystem::directory_entry>& files;
      std::mutex& files_mtx;

      std::thread file_tracker_thread;

      std::atomic<bool> stop_request = 0;

      progress_info_t& progress_info;

      std::vector<std::u8string> EXIF_exts;

      exif::exif_reader_t exif_reader;

      scanset_bitmap_t scanset_bitmap;

#ifndef NO_SSE_AVX
      mb_file_hasher_t mb_hasher;
#endif
      sqlite3 *file_scan_db = nullptr;

      sqlite_stmt_t stmt_insert_file;
      sqlite_stmt_t stmt_insert_version;
      sqlite_stmt_t stmt_insert_scanset_entry;
      sqlite_stmt_t stmt_insert_exif;

      sqlite_stmt_t stmt_find_last_version;
      sqlite_stmt_t stmt_find_scan_version;

      sqlite_stmt_t stmt_begin_txn;
      sqlite_stmt_t stmt_commit_txn;
      sqlite_stmt_t stmt_rollback_txn;

   private:
      void init_scan_db_conn(void);

      void init_new_scan_stmts(void);

      void init_transaction_stmts(void);

      void init_base_scan_stmts(void);

      int64_t insert_file_record(const std::u8string& filepath, const std::filesystem::directory_entry& dir_entry);

      int64_t insert_exif_record(const std::u8string& filepath, const std::vector<exif::field_value_t>& exif_fields, const exif::field_bitset_t& field_bitset);

      version_record_result_t select_version_record(const std::u8string& filepath);

      int64_t insert_version_record(const std::u8string& filepath, int64_t file_id, int64_t version, int64_t filesize, const std::filesystem::directory_entry& dir_entry, unsigned char hexhash_file[], std::optional<int64_t> exif_id);

      void insert_scanset_record(const std::u8string& filepath, int64_t version_id);

      void begin_transaction(const std::u8string& filepath);

      void commit_transaction(const std::u8string& filepath);

      void rollback_transaction(const std::u8string& filepath);

      void hash_file(const std::filesystem::path& filepath, uint64_t& filesize, unsigned char hexhash[]);

      void run(void);

#ifndef NO_SSE_AVX
      mb_file_hasher_t::param_tuple_t open_file(version_record_result_t&& version_record, std::filesystem::directory_entry&& dir_entry) const;
      bool read_file(unsigned char *file_buffer, size_t buf_size, size_t& data_size, mb_file_hasher_t::param_tuple_t& args) const noexcept;
#endif

      static time_t file_time_to_time_t(const std::chrono::file_clock::time_point& file_time);

      static std::u8string to_ascii_path(const std::filesystem::path& fspath);

      static int sqlite_busy_handler_cb(void*, int count);

      static std::vector<std::u8string> parse_EXIF_exts(const options_t& options);

      static bool set_sqlite_journal_mode(sqlite3 *file_scan_db, print_stream_t& print_stream);

      static std::tuple<uint64_t, uint64_t> get_scanset_rowid_range(sqlite3 *file_scan_db, int64_t scan_id);

   public:
      file_tracker_t(const options_t& options, std::optional<int64_t>& scan_id, std::optional<int64_t>& base_scan_id, std::queue<std::filesystem::directory_entry>& files, std::mutex& files_mtx, progress_info_t& progress_info, print_stream_t& print_stream);

      file_tracker_t(file_tracker_t&& other);

      ~file_tracker_t(void);

      static void initialize(print_stream_t& print_stream);

      static void cleanup(print_stream_t& print_stream) noexcept;

      void start(void);

      void stop(void);

      void join(void);

      void update_file_removals(const file_tracker_t& other);

      void report_file_removals(void);
};

}

#endif // FIT_FILE_TRACKER_H
