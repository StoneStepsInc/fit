#include "file_hasher.h"

#include "fit.h"

#include "sha256/sha256.h"

#ifdef _WIN32
#include <cwchar>    // for _wfopen
#endif

#include <string>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <chrono>

#include <cstring>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

namespace fit {

static constexpr const size_t SHA256_HEX_SIZE = SHA256_SIZE_BYTES * 2;

file_hasher_t::file_hasher_t(const options_t& options, int64_t scan_id, std::queue<std::filesystem::directory_entry>& files, std::mutex& files_mtx, progress_info_t& progress_info, print_stream_t& print_stream) :
      options(options),
      print_stream(print_stream),
      scan_id(scan_id),
      hash_type("SHA256"sv),
      file_buffer(new unsigned char[options.buffer_size]),
      files(files),
      files_mtx(files_mtx),
      progress_info(progress_info)
{
   int errcode = SQLITE_OK;

   if((errcode = sqlite3_open_v2(options.db_path.u8string().c_str(), &file_scan_db, SQLITE_OPEN_READWRITE, nullptr)) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errstr(errcode));

   //
   // SQLite keeps calling sqlite_busy_handler_cb for this amount
   // of time while record storage is locked and after SQLITE_BUSY
   // is returned.
   //
   if((errcode = sqlite3_busy_timeout(file_scan_db, DB_BUSY_TIMEOUT)) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errstr(errcode));

   if((errcode = sqlite3_busy_handler(file_scan_db, sqlite_busy_handler_cb, nullptr)) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errstr(errcode));
   
   // insert statement for new file records                     1     2     3        3         5           5          6          8     9
   std::string_view sql_insert_file = "insert into files (scan_id, name, path, version, mod_time, entry_size, read_size, hash_type, hash) values (?, ?, ?, ?, ?, ?, ?, ?, ?)"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_file.data(), (int) sql_insert_file.length()+1, &stmt_insert_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert a file (%s)", sqlite3_errstr(errcode));

   if((errcode = sqlite3_bind_int64(stmt_insert_file, 1, scan_id)) != SQLITE_OK)
      print_stream.error("Cannot bind a scan ID for a SQLite statement to insert a file (%s)", sqlite3_errstr(errcode));

   if((errcode = sqlite3_bind_text(stmt_insert_file, 8, hash_type.data(), static_cast<int>(hash_type.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      print_stream.error("Cannot bind a scan ID for a SQLite statement to insert a file (%s)", sqlite3_errstr(errcode));

   // select statement to look-up files by their path
   // parameters:                                                                                  1
   // columns:                                    0         1          2     3
   std::string_view sql_find_file = "select version, mod_time, hash_type, hash from files where path = ? order by version desc limit 1"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_find_file.data(), (int) sql_find_file.length()+1, &stmt_find_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to find a file (%s)", sqlite3_errstr(errcode));
}

file_hasher_t::file_hasher_t(file_hasher_t&& other) :
      options(other.options),
      print_stream(other.print_stream),
      scan_id(other.scan_id),
      hash_type(other.hash_type),
      file_buffer(std::move(other.file_buffer)),
      files(other.files),
      files_mtx(other.files_mtx),
      progress_info(other.progress_info),
      file_hasher_thread(std::move(other.file_hasher_thread)),
      file_scan_db(other.file_scan_db),
      stmt_insert_file(other.stmt_insert_file),
      stmt_find_file(other.stmt_find_file)
{
   other.file_scan_db = nullptr;

   other.stmt_find_file = nullptr;
   other.stmt_insert_file = nullptr;
}

file_hasher_t::~file_hasher_t(void)
{
   int errcode = SQLITE_OK;

   if(stmt_find_file) {
      if((errcode = sqlite3_finalize(stmt_find_file)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to find a file (%s)", sqlite3_errstr(errcode));
   }

   if(stmt_insert_file) {
      if((errcode = sqlite3_finalize(stmt_insert_file)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to insert a file (%s)", sqlite3_errstr(errcode));
   }

   if(file_scan_db) {
      if((errcode = sqlite3_close(file_scan_db)) != SQLITE_OK)
         print_stream.error("Failed to close the SQLite database (%s)", sqlite3_errstr(errcode));
   }
}

void file_hasher_t::hash_file(const std::filesystem::path& filepath, uint64_t& filesize, uint8_t hexhash[])
{
   //
   // The narrow character version of fopen will fail to open files
   // with names containing characters that cannot be converted to
   // the current Windows character set. Use the non-standard
   // _wfopen to work around this problem.
   //
   #ifdef _WIN32
   std::unique_ptr<FILE, file_handle_deleter_t> file(_wfopen(filepath.wstring().c_str(), L"rb"));
   #else
   std::unique_ptr<FILE, file_handle_deleter_t> file(fopen(filepath.u8string().c_str(), "rb"));
   #endif

   if(!file)
      throw std::runtime_error("Cannot open file " + filepath.u8string());

   sha256_context ctx;
   sha256_init(&ctx);

   size_t lastread = 0;

   filesize = 0;

   while((lastread = std::fread(file_buffer.get(), 1, options.buffer_size, file.get())) != 0) {
      sha256_hash(&ctx, file_buffer.get(), lastread);

      filesize += lastread;
   }

   if(std::ferror(file.get()))
      throw std::runtime_error("Cannot read file (" + std::string(strerror(errno)) + ") " + filepath.u8string());

   // hash for zero-length files should not be evaluated
   if(!filesize)
      memset(hexhash, 0, SHA256_SIZE_BYTES);
   else {
      uint8_t filehash[SHA256_SIZE_BYTES];

      sha256_done(&ctx, filehash);

      static const char hex[] = "0123456789abcdef";

      for(size_t i = 0; i < SHA256_SIZE_BYTES; i++) {
         hexhash[i*2] = hex[(*(filehash+i) & 0xF0) >> 4];
         hexhash[i*2+1] = hex[*(filehash+i) & 0x0F];
      }
   }
}

void file_hasher_t::run(void)
{
   int errcode = SQLITE_OK;

   // a file path string buffer
   std::string filepath;

   filepath.reserve(1024);
               
   uint8_t hexhash_file[SHA256_HEX_SIZE + 1] = {}, hexhash_field[SHA256_HEX_SIZE + 1] = {};

   std::unique_lock<std::mutex> lock(files_mtx);

   while(!stop_request) {
      if(files.empty()) {
         lock.unlock();

         //
         // We don't expect this thread to idle much and don't need
         // a condition variable here. It will reach this code only
         // after it starts and right before the final few files
         // are being processed. At all other times it will find
         // files in the queue.
         //
         std::this_thread::sleep_for(std::chrono::milliseconds(300));

         lock.lock();
         continue;
      }

      std::filesystem::directory_entry dir_entry = std::move(files.front());
      files.pop();

      lock.unlock();

      try {
         uint64_t filesize = 0;

         //
         // Check if we were asked to skip hashing based on the file
         // modification time being unchanged from what was recorded
         // in the database, in which case we need to query the
         // database first.
         // 
         // Note that only files intended for hashing are queued, so
         // there is no need to check directory entry flags.
         //
         if(options.verify_files || !options.skip_hash_mod_time)
            hash_file(dir_entry.path(), filesize, hexhash_file);

         //
         // If we have a base path, remove it from the full path, so files
         // can be verified with a different base path.
         // 
         // lexically_relative is case sensitive and will be confused if
         // mixed-case directory names are compared on Windows. See a
         // comment in fit::verify_options where base path and scan path
         // are compared. The net effect here will be that if mixed
         // characters reach this point, `C:\Users\X` and `C:\Users\x`
         // will be tracked as two different paths.
         //
         if(options.base_path.empty())
            filepath = dir_entry.path().u8string();
         else
            filepath = dir_entry.path().lexically_relative(options.base_path).u8string();

         //
         // Attempt to find the file by its relative path first. For
         // files with multiple versions, always select the latest
         // version.
         //
         sqlite_stmt_binder_t find_file_stmt(stmt_find_file, "find file"sv);

         find_file_stmt.bind_param(filepath);

         int64_t version = 0;
         bool hash_match = false;
         int64_t mod_time = 0;

         errcode = sqlite3_step(stmt_find_file);

         if(errcode != SQLITE_DONE && errcode != SQLITE_ROW)
            throw std::runtime_error("SQLite select failed for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

         if(errcode == SQLITE_ROW) {
            version = sqlite3_column_int64(stmt_find_file, 0);

            mod_time = sqlite3_column_int64(stmt_find_file, 1);

            // compare the stored hash against the one we just computed
            if(std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 2)), sqlite3_column_bytes(stmt_find_file, 2)) == "SHA256"sv) {
               // consider a NULL hash as a match for zero-length files
               if(sqlite3_column_type(stmt_find_file, 3) == SQLITE_NULL)
                  hash_match = filesize == 0;
               else {
                  if(sqlite3_column_bytes(stmt_find_file, 3) != SHA256_HEX_SIZE)
                     throw std::runtime_error("Bad hash size for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

                  // compare in-place, unless scanning with delayed hashing was requested
                  if(options.verify_files || !options.skip_hash_mod_time)
                     hash_match = memcmp(hexhash_file, reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 3)), SHA256_HEX_SIZE) == 0;
                  else
                     memcpy(hexhash_field, reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 3)), SHA256_HEX_SIZE);
               }
            }
            else
               throw std::runtime_error("Unknown hash type: "s + reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 2)));
         }

         // reset early to release read locks the select acquired
         find_file_stmt.reset();

         // check if we still need to hash the file
         if(!options.verify_files && options.skip_hash_mod_time) {
            // if the file modification time didn't change, assume it has the same hash, as requested
            if(mod_time && mod_time == std::chrono::duration_cast<std::chrono::seconds>(dir_entry.last_write_time().time_since_epoch()).count())
               hash_match = true;
            else {
               // hash the file if we didn't find its record or the last-modified time changed
               hash_file(dir_entry.path(), filesize, hexhash_file);
               hash_match = memcmp(hexhash_file, hexhash_field, SHA256_HEX_SIZE) == 0;
            }
         }

         // handle the mismatched hash based on whether we are verifying files or not
         if(!hash_match) {
            if(options.verify_files) {
               // differentiate between new, modified and changed files
               if(!mod_time) {
                  progress_info.new_files++;
                  print_stream.info(   "new file: %s", filepath.c_str());
               }
               else {
                  if(mod_time != std::chrono::duration_cast<std::chrono::seconds>(dir_entry.last_write_time().time_since_epoch()).count()) {
                     progress_info.modified_files++;
                     print_stream.info("modified: %s", filepath.c_str());
                  }
                  else {
                     progress_info.changed_files++;
                     print_stream.info("changed : %s", filepath.c_str());
                  }
               }
            }
            else {
               //
               // Insert a new version record for this file. There is no
               // concurrency for this record because each file path is
               // unique in the queue, so there is no danger of a version
               // conflict.
               //
               sqlite_stmt_binder_t insert_file_stmt(stmt_insert_file, "insert file"sv);

               // scan_id
               insert_file_stmt.skip_param();

               insert_file_stmt.bind_param(dir_entry.path().filename().u8string());
               insert_file_stmt.bind_param(filepath);
               insert_file_stmt.bind_param(version+1);

               //
               // File system time is implementation specific and for Windows
               // is the FILETIME value, which can be translated to Unix epoch
               // time with this SQLite statement:
               // 
               //     datetime(mod_time-11644473600, 'unixepoch')
               // 
               // We don't need to evaluate it as time, just whether it's the
               // same or not, so there is no need for a platform-specific way
               // to interpret it as time.
               //
               insert_file_stmt.bind_param(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(dir_entry.last_write_time().time_since_epoch()).count()));

               insert_file_stmt.bind_param(static_cast<int64_t>(dir_entry.file_size()));
               insert_file_stmt.bind_param(static_cast<int64_t>(filesize));

               // hash_type
               insert_file_stmt.skip_param();

               // insert a NULL for a zero-length file
               if(!filesize)
                  insert_file_stmt.bind_param(nullptr);
               else
                  insert_file_stmt.bind_param(std::string_view(reinterpret_cast<const char*>(hexhash_file), SHA256_HEX_SIZE));

               //
               // If we get SQLITE_BUSY after the busy handler runs for allowed
               // amount of time, report this file as failed. The most likely
               // cause for this would be that one of statements wasn't reset
               // and left SQLite record storage locked.
               //
               if((errcode = sqlite3_step(stmt_insert_file)) != SQLITE_DONE)
                  throw std::runtime_error("Cannot insert a file record for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

               insert_file_stmt.reset();
            }

            // reuse for updated files during scans and for mismatched files during verification
            progress_info.updated_files++;
            progress_info.updated_size += options.skip_hash_mod_time ? dir_entry.file_size() : filesize;
         }

         progress_info.processed_files++;
         progress_info.processed_size += options.skip_hash_mod_time ? dir_entry.file_size() : filesize;
      }
      catch (const std::exception& error) {
         progress_info.failed_files++;

         print_stream.error("%s", error.what());
      }

      // need to lock to access the queue
      lock.lock();
   }
}

int file_hasher_t::sqlite_busy_handler_cb(void*, int count)
{
   //
   // Try immediately for the first 10 attempts, then speel for
   // 1 ms between 10 and 100 attempts and then give it 10 ms
   // between tries for all subsequent attempts.
   //
   if(count > 100)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   else if(count > 10)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

   //
   // Keep returning that we want another attempt and it will
   // fail after the busy timeout expires, regardless of this
   // return value.
   //
   return 1;
}

void file_hasher_t::start(void)
{
   file_hasher_thread = std::thread(&file_hasher_t::run, this);
}

void file_hasher_t::stop(void)
{
   stop_request = true;
}

void file_hasher_t::join(void)
{
   if(file_hasher_thread.joinable())
      file_hasher_thread.join();
}

}
