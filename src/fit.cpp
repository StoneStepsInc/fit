//
// File Integrity Tracker (fit)
// 
// Copyright (c) 2022, Stone Steps Inc.
//
#include "file_tree_walker.h"
#include "print_stream.h"

#include "fit.h"

#include <sqlite3.h>

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cinttypes>
#include <csignal>
#include <cstdarg>

#include <string>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <vector>
#include <queue>
#include <chrono>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

//
// Build number is used only when the project is being built in a
// CI pipeline, where build numbers are maintained.
// 
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

#define STR_BUILD_NUMBER2(v) #v
#define STR_BUILD_NUMBER(v) STR_BUILD_NUMBER2(v)

namespace fit {

static const char *title = "File Integrity Tracker";
static const char *version = "1.1.0+" STR_BUILD_NUMBER(BUILD_NUMBER);
static const char *copyright = "Copyright (c) 2022 Stone Steps Inc.";

std::atomic<bool> abort_scan = false;

extern "C" void console_ctrl_c_handler(int sig)
{
   if(sig == SIGINT || sig == SIGTERM)
      abort_scan = true;
}

void print_usage(void)
{
   printf("%s (%s) -- %s\n", title, version, copyright);

   fputs("\nUsage: fit [options]\n\n", stdout);

   fputs("    -b path      - scan database\n", stdout);
   fputs("    -p path      - base path\n", stdout);
   fputs("    -d path      - directory to scan\n", stdout);
   fputs("    -m message   - optional scan description\n", stdout);
   fputs("    -r           - recursive scan\n", stdout);
   fputs("    -v           - verify scanned files against database entries\n", stdout);
   fputs("    -t           - file hasher thread count (default: 4, min: 1, max: 64)\n", stdout);
   fputs("    -s size      - file buffer size (default: 65536, min: 512, max: 1048576)\n", stdout);
   fputs("    -i seconds   - progress reporting interval (default: 10, min: 1)\n", stdout);
   fputs("    -w           - skip hashing for files with same last-modified time\n", stdout);
   fputs("    -l path      - log file path\n", stdout);
   fputs("    -a           - skip restricted access directories\n", stdout);
   fputs("    -?           - this help\n", stdout);

   fputc('\n', stdout);
}

options_t parse_options(int argc, char *argv[])
{
   options_t options;

   for(size_t i = 1; i < argc; i++) {
      if(!argv[i])
         throw std::runtime_error("A null argument is not valid: " + std::to_string(i));

      if(*argv[i] != '-')
         throw std::runtime_error("Invalid option: " + std::string(argv[i]));

      switch(*(argv[i]+1)) {
         case 'b':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing database file path value");

            options.db_path = argv[++i];
            break;
         case 'p':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing base path value");

            options.base_path = argv[++i];
            break;
         case 'd':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing directory to scan value");

            options.scan_path = argv[++i];
            break;
         case 'm':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing scan message value");

            options.scan_message = argv[++i];
            break;
         case 'r':
            options.recursive_scan = true;
            break;
         case 'v':
            options.verify_files = true;
            break;
         case 't':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing thread count value");

            options.thread_count = atoi(argv[++i]);
            break;
         case 's':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing buffer size value");

            options.buffer_size = atoi(argv[++i]);
            break;
         case 'i':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing progress reporting interval value");

            options.progress_interval = atoi(argv[++i]);
            break;
         case 'w':
            options.skip_hash_mod_time = true;
            break;
         case 'l':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing log file path value");

            options.log_file = argv[++i];
            break;
         case 'a':
            options.skip_no_access_paths = true;
            break;
         case 'h':
         case '?':
            options.print_usage = true;
            break;
         default:
            throw std::runtime_error("Unknown option: " + std::string(argv[i]));
      }
   }

   return options;
}

void verify_options(options_t& options)
{
   if(options.thread_count == 0 || options.thread_count > 64)
      throw std::runtime_error("Invalid thread count");

   if(options.buffer_size < 512 || options.buffer_size > 1048576)
      throw std::runtime_error("Invalid file buffer size");

   // round buffer size up to the nearest 512 or 4096 boundary, if it's not already there
   size_t block_size = options.buffer_size < 4096 ? 512 : 4096;
   options.buffer_size += (block_size - options.buffer_size % block_size) % block_size;

   if(options.progress_interval < 0)
      throw std::runtime_error("Invalid progress reporting interval");

   // db_path
   if(options.db_path.empty())
      throw std::runtime_error("Database file path must be specified");

   if(std::filesystem::is_directory(options.db_path))
      throw std::runtime_error(options.db_path.u8string() + " cannot be a directory");

   if(!options.db_path.has_filename())
      throw std::runtime_error(options.db_path.u8string() + " must point to a file");

   if(!std::filesystem::is_directory(std::filesystem::absolute(options.db_path).remove_filename()))
      throw std::runtime_error(std::filesystem::absolute(options.db_path).remove_filename().u8string() + " must be an existing directory");

   // scan_path
   if(options.scan_path.empty())
      options.scan_path = std::filesystem::current_path();

   if(!std::filesystem::exists(options.scan_path))
      throw std::runtime_error(options.scan_path.u8string() + " does not exist");

   if(!std::filesystem::is_directory(options.scan_path))
      throw std::runtime_error(options.scan_path.u8string() + " is not a directory");

   //
   // Windows file paths are case-insensitive, but character case is
   // preserved in all file/directory operations, which is propagated
   // into the std::filesystem implementation, making it tricky to
   // work with mixed character case. That is, whether an instance of
   // directory_entry is constructed with `C:\TEMP` or `c:\temp`, each
   // will return the original string, even if the actual path is
   // `C:\Temp`. Transform each path into canonical form, which makes
   // it absolute and uses actual characters for each path element.
   //
   options.scan_path = std::filesystem::canonical(options.scan_path);

   if(!options.base_path.empty()) {
      if(!std::filesystem::is_directory(options.base_path))
         throw std::runtime_error(options.base_path.u8string() + " is not a directory");

      options.base_path = std::filesystem::canonical(options.base_path);

      // make sure that the scan path is under the base path
      std::filesystem::path::iterator bpi = options.base_path.begin();

      for(std::filesystem::path::iterator spi = options.scan_path.begin(); bpi != options.base_path.end() && spi != options.scan_path.end(); ++bpi, ++spi) {
         if(*bpi != *spi)
            throw std::runtime_error("Scan path must be under the base path");
      }

      // check if the loop ended because the scan path was shorter
      if(bpi != options.base_path.end())
         throw std::runtime_error("Scan path must be under the base path");
   }
}

sqlite3 *open_sqlite_database(const options_t& options, print_stream_t& print_stream)
{
   sqlite3 *file_scan_db = nullptr;

   if(SQLITE_VERSION_NUMBER != sqlite3_libversion_number())
      throw std::runtime_error("SQLite header and library versions don't match");

   // 1 - serialized, 2 - multi-threaded
   if(sqlite3_threadsafe() != 2 && sqlite3_threadsafe() != 1)
      throw std::runtime_error("SQLite library must be multi-threaded");

   if(sqlite3_config(SQLITE_CONFIG_MULTITHREAD) != SQLITE_OK)
      throw std::runtime_error("SQLite cannot be configured to operate multi-threaded");

   if(sqlite3_initialize() != SQLITE_OK)
      throw std::runtime_error("SQLite cannot be initialized");

   char *errmsg = nullptr;
   int errcode = SQLITE_OK;
   int sqlite_flags = options.verify_files ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;

   if((errcode = sqlite3_open_v2(options.db_path.u8string().c_str(), &file_scan_db, sqlite_flags, nullptr)) != SQLITE_OK) {

      if(options.verify_files)
         throw std::runtime_error("Cannot open a SQLite database in "s + options.db_path.generic_u8string());

      if((errcode = sqlite3_open_v2(options.db_path.u8string().c_str(), &file_scan_db, sqlite_flags | SQLITE_OPEN_CREATE, nullptr)) != SQLITE_OK)
         throw std::runtime_error(sqlite3_errstr(errcode));

      print_stream.info("Creating a new SQLite database %s", options.db_path.generic_u8string().c_str());

      // files table
      if(sqlite3_exec(file_scan_db, "create table files (scan_id INTEGER NOT NULL, version INTEGER NOT NULL, name TEXT NOT NULL, path TEXT NOT NULL, mod_time INTEGER NOT NULL, entry_size INTEGER NOT NULL, read_size INTEGER NOT NULL, hash_type VARCHAR(32) NOT NULL, hash TEXT NOT NULL);", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error("Cannot create table 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter<char>>(errmsg).get() + ")");

      if(sqlite3_exec(file_scan_db, "create unique index ix_files_path on files (path, version);", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error("Cannot create path index for 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter<char>>(errmsg).get());

      if(sqlite3_exec(file_scan_db, "create index ix_files_hash on files (hash, hash_type);", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error("Cannot create a hash index for 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter<char>>(errmsg).get() + ")");

      // scans table
      if(sqlite3_exec(file_scan_db, "create table scans (app_version TEXT NOT NULL, scan_time INTEGER NOT NULL, scan_path TEXT NOT NULL, base_path TEXT NOT NULL, current_path TEXT NOT NULL, message TEXT);", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error("Cannot create table 'scans' ("s + std::unique_ptr<char, sqlite_malloc_deleter<char>>(errmsg).get() + ")");

      if(sqlite3_exec(file_scan_db, "create index ix_scans_timestamp on scans (scan_time);", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error("Cannot create time stamp index for 'scans' ("s + std::unique_ptr<char, sqlite_malloc_deleter<char>>(errmsg).get() + ")");
   }
   
   return file_scan_db;
}

void close_sqlite_database(sqlite3 *file_scan_db)
{
   if(sqlite3_close(file_scan_db) != SQLITE_OK)
      fprintf(stderr, "Failed to close the SQLite database\n");

   if(sqlite3_shutdown())
      fprintf(stderr, "Failed to shut down SQLite\n");
}

int64_t insert_scan_record(const options_t& options, sqlite3 *file_scan_db)
{
   int errcode = SQLITE_OK;

   sqlite3_stmt *stmt_insert_scan = nullptr;

   std::string_view sql_insert_scan = "insert into scans (app_version, scan_time, scan_path, base_path, current_path, message) values (?, ?, ?, ?, ?, ?)"sv;

   // SQLite docs say there's a small performance gain if the null terminator is included in length
   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_scan.data(), (int) sql_insert_scan.length()+1, &stmt_insert_scan, nullptr)) != SQLITE_OK)
      throw std::runtime_error("Cannot prepare a SQLite statement to insert a scan ("s + sqlite3_errstr(errcode) + ")");

   errcode = sqlite3_bind_text(stmt_insert_scan, 1, version, -1, SQLITE_STATIC);
   errcode = sqlite3_bind_int64(stmt_insert_scan, 2, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
   errcode = sqlite3_bind_text(stmt_insert_scan, 3, options.scan_path.u8string().c_str(), static_cast<int>(options.scan_path.u8string().size()), SQLITE_TRANSIENT);
   errcode = sqlite3_bind_text(stmt_insert_scan, 4, options.base_path.u8string().c_str(), static_cast<int>(options.base_path.u8string().size()), SQLITE_TRANSIENT);
   errcode = sqlite3_bind_text(stmt_insert_scan, 5, std::filesystem::current_path().u8string().c_str(), static_cast<int>(std::filesystem::current_path().u8string().size()), SQLITE_TRANSIENT);

   if(options.scan_message.empty())
      errcode = sqlite3_bind_null(stmt_insert_scan, 6);
   else
      errcode = sqlite3_bind_text(stmt_insert_scan, 6, options.scan_message.c_str(), static_cast<int>(options.scan_message.size()), SQLITE_TRANSIENT);

   int64_t scan_id = 0;

   if((errcode = sqlite3_step(stmt_insert_scan)) == SQLITE_DONE)
      scan_id = sqlite3_last_insert_rowid(file_scan_db);
   else
      throw std::runtime_error("Cannot insert a scan record ("s + sqlite3_errstr(errcode) + ")");

   if((errcode = sqlite3_finalize(stmt_insert_scan)) != SQLITE_OK)
      throw std::runtime_error("Cannot finalize SQLite statment to insert a scan record ("s + sqlite3_errstr(errcode) + ")");

   return scan_id;
}

}

int main(int argc, char *argv[])
{
   try {
      struct sqlite3_deleter_t {
         void operator () (sqlite3 *sqlite_handle)
         {
            fit::close_sqlite_database(sqlite_handle);
         }
      };

      fit::options_t options = fit::parse_options(argc, argv);

      if(options.print_usage) {
         fit::print_usage();
         return EXIT_SUCCESS;
      }

      fit::verify_options(options);

      printf("%s (%s) -- %s\n\n", fit::title, fit::version, fit::copyright);

      signal(SIGINT, fit::console_ctrl_c_handler);
      signal(SIGTERM, fit::console_ctrl_c_handler);

      //
      // Open a print stream
      //
      std::unique_ptr<FILE, fit::file_handle_deleter_t> log_file;

      if(!options.log_file.empty()) {
         log_file.reset(fopen(options.log_file.c_str(), "ab"));

         if(!log_file)
            throw std::runtime_error("Cannot open log file "s + options.log_file);
      }

      fit::print_stream_t print_stream(log_file.get());

      //
      // Open a SQLite database
      //
      std::unique_ptr<sqlite3, sqlite3_deleter_t> file_scan_db(fit::open_sqlite_database(options, print_stream));

      int64_t scan_id = 0;
      
      if(!options.verify_files)
         scan_id = fit::insert_scan_record(options, file_scan_db.get());

      std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

      //
      // Walk the file tree
      //
      if(!options.scan_message.empty())
         print_stream.info("%s %s (%s)", options.verify_files ? "Verifying" : "Scanning", options.scan_path.u8string().c_str(), options.scan_message.c_str());
      else
         print_stream.info("%s %s", options.verify_files ? "Verifying" : "Scanning", options.scan_path.u8string().c_str());

      fit::file_tree_walker_t file_tree_walker(options, scan_id, print_stream);

      if(options.recursive_scan)
         file_tree_walker.walk_tree<std::filesystem::recursive_directory_iterator>();
      else
         file_tree_walker.walk_tree<std::filesystem::directory_iterator>();

      std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();

      fit::close_sqlite_database(file_scan_db.release());

      //
      // Print scan results and timing
      //

      // for quick scans, just report processed files and elapsed time
      if(std::chrono::duration_cast<std::chrono::seconds>(end_time-start_time).count() == 0) {
         print_stream.info("Processed %.1f GB in %" PRIu64 " files in %.1f min",
                           file_tree_walker.get_processed_size()/1'000'000'000., file_tree_walker.get_processed_files(),
                           std::chrono::duration_cast<std::chrono::milliseconds>(end_time-start_time).count()/60000.);
      }
      else {
         print_stream.info("Processed %.1f GB in %" PRIu64 " files in %.1f min (%.1f files/sec, %.1f MB/sec)",
                           file_tree_walker.get_processed_size()/1'000'000'000., file_tree_walker.get_processed_files(),
                           std::chrono::duration_cast<std::chrono::milliseconds>(end_time-start_time).count()/60000.,
                           file_tree_walker.get_processed_files()/(std::chrono::duration_cast<std::chrono::milliseconds>(end_time-start_time).count()/1000.),
                           file_tree_walker.get_processed_size()/1'000'000./(std::chrono::duration_cast<std::chrono::milliseconds>(end_time-start_time).count()/1000.));
      }

      if(options.verify_files) {
         print_stream.info("Found %" PRIu64 " modified, %" PRIu64 " new and %" PRIu64 " changed files",
                           file_tree_walker.get_modified_files(), file_tree_walker.get_new_files(), file_tree_walker.get_changed_files());
      }

      return EXIT_SUCCESS;
   }
   catch(const std::exception& error) {
      fprintf(stderr, "%s\n", error.what());
   }

   return EXIT_FAILURE;
}
