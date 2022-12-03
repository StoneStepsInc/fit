#include "file_tracker.h"
#include "exif_reader.h"

#include "fit.h"

extern "C" {
#include "sha256/sha256.h"
}

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
#include <algorithm>
#include <optional>

#include <cstring>

#ifndef _MSC_VER
#include <strings.h>    // for strcasecmp
#endif

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

namespace fit {

struct less_ci {
   bool operator () (const std::string& s1, const std::string& s2)
   {
      if(s1.empty() && s2.empty())
         return false;

      if(s1.length() != s2.length())
         return s1.length() < s2.length();

      return strcasecmp(s1.c_str(), s2.c_str()) < 0;
   }
};

static constexpr const size_t SHA256_HEX_SIZE = SHA256_DIGEST_SIZE * 2;

file_tracker_t::file_tracker_t(const options_t& options, int64_t scan_id, std::queue<std::filesystem::directory_entry>& files, std::mutex& files_mtx, progress_info_t& progress_info, print_stream_t& print_stream) :
      options(options),
      print_stream(print_stream),
      scan_id(scan_id),
      hash_type("SHA256"sv),
      file_buffer(new unsigned char[options.buffer_size]),
      files(files),
      files_mtx(files_mtx),
      progress_info(progress_info),
      EXIF_exts(parse_EXIF_exts(options))
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
   
   //
   // insert statement for new file records                  1    2     3
   //
   std::string_view sql_insert_file = "insert into files (name, ext, path) values (?, ?, ?)"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_file.data(), (int) sql_insert_file.length()+1, &stmt_insert_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert a file (%s)", sqlite3_errstr(errcode));

   //
   // insert statement for new file version records                   1        2         3           4          5          6     7        8
   //
   std::string_view sql_insert_version = "insert into versions (file_id, version, mod_time, entry_size, read_size, hash_type, hash, exif_id) values (?, ?, ?, ?, ?, ?, ?, ?)"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_version.data(), (int) sql_insert_version.length()+1, &stmt_insert_version, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert a file version (%s)", sqlite3_errstr(errcode));

   if((errcode = sqlite3_bind_text(stmt_insert_version, 6, hash_type.data(), static_cast<int>(hash_type.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      print_stream.error("Cannot bind a hash type for a SQLite statement to insert a file version (%s)", sqlite3_errstr(errcode));

   //
   // select statement to look-up files by their path
   // parameters:                                                                                                                                                                                                            1
   // columns:                                    0         1          2     3               4        5        6
   std::string_view sql_find_file = "SELECT version, mod_time, hash_type, hash, versions.rowid, file_id, scan_id FROM versions JOIN files ON file_id = files.rowid JOIN scansets ON version_id = versions.rowid WHERE path = ? ORDER BY scan_id DESC, version DESC LIMIT 1"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_find_file.data(), (int) sql_find_file.length()+1, &stmt_find_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to find a file version (%s)", sqlite3_errstr(errcode));

   //
   // insert statement for scanset file records                            1           2
   //
   std::string_view sql_insert_scanset_file = "insert into scansets (scan_id, version_id) values (?, ?)"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_scanset_file.data(), (int) sql_insert_scanset_file.length()+1, &stmt_insert_scanset_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert a scanset file (%s)", sqlite3_errstr(errcode));

   if((errcode = sqlite3_bind_int64(stmt_insert_scanset_file, 1, scan_id)) != SQLITE_OK)
      print_stream.error("Cannot bind a scan ID for a SQLite statement to insert a scanset file (%s)", sqlite3_errstr(errcode));

   //
   // insert statement for EXIF records
   //
   std::string sql_insert_exif = "INSERT INTO exif ("
                                      "BitsPerSample,Compression,DocumentName,ImageDescription,Make,Model,Orientation,SamplesPerPixel,"
                                      "Software,DateTime,Artist,Copyright,ExposureTime,FNumber,ExposureProgram,ISOSpeedRatings,"
                                      "TimeZoneOffset,SensitivityType,ISOSpeed,DateTimeOriginal,DateTimeDigitized,OffsetTime,OffsetTimeOriginal,OffsetTimeDigitized,"
                                      "ShutterSpeedValue,ApertureValue,SubjectDistance,BrightnessValue,ExposureBiasValue,MaxApertureValue,MeteringMode,LightSource,"
                                      "Flash,FocalLength,UserComment,SubsecTime,SubSecTimeOriginal,SubSecTimeDigitized,FlashpixVersion,FlashEnergy,"
                                      "SubjectLocation,ExposureIndex,SensingMethod,SceneType,ExposureMode,WhiteBalance,DigitalZoomRatio,FocalLengthIn35mmFilm,"
                                      "SceneCaptureType,SubjectDistanceRange,ImageUniqueID,CameraOwnerName,BodySerialNumber,LensSpecification,LensMake,LensModel,"
                                      "LensSerialNumber,GPSLatitudeRef,GPSLatitude,GPSLongitudeRef,GPSLongitude,GPSAltitudeRef,GPSAltitude,GPSTimeStamp,"
                                      "GPSSpeedRef,GPSSpeed,GPSDateStamp,XMPxmpRating) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?)";

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_exif.c_str(), (int) sql_insert_exif.length()+1, &stmt_insert_exif, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert an EXIF record (%s)", sqlite3_errstr(errcode));

   //
   // SQLite transaction statements
   //
   std::string_view sql_begin_txn = "BEGIN DEFERRED TRANSACTION"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_begin_txn.data(), (int) sql_begin_txn.length()+1, &stmt_begin_txn, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to begin a transaction (%s)", sqlite3_errstr(errcode));

   std::string_view sql_commit_txn = "COMMIT TRANSACTION"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_commit_txn.data(), (int) sql_commit_txn.length()+1, &stmt_commit_txn, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to commit a transaction (%s)", sqlite3_errstr(errcode));

   std::string_view sql_rollback_txn = "ROLLBACK TRANSACTION"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_rollback_txn.data(), (int) sql_rollback_txn.length()+1, &stmt_rollback_txn, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to roll back a transaction (%s)", sqlite3_errstr(errcode));
}

file_tracker_t::file_tracker_t(file_tracker_t&& other) :
      options(other.options),
      print_stream(other.print_stream),
      scan_id(other.scan_id),
      hash_type(other.hash_type),
      file_buffer(std::move(other.file_buffer)),
      files(other.files),
      files_mtx(other.files_mtx),
      progress_info(other.progress_info),
      file_tracker_thread(std::move(other.file_tracker_thread)),
      file_scan_db(other.file_scan_db),
      stmt_insert_file(other.stmt_insert_file),
      stmt_insert_version(other.stmt_insert_version),
      stmt_insert_scanset_file(other.stmt_insert_scanset_file),
      stmt_insert_exif(other.stmt_insert_exif),
      stmt_find_file(other.stmt_find_file),
      stmt_begin_txn(other.stmt_begin_txn),
      stmt_commit_txn(other.stmt_commit_txn),
      stmt_rollback_txn(other.stmt_rollback_txn),
      EXIF_exts(std::move(other.EXIF_exts)),
      exif_reader(std::move(other.exif_reader))
{
   other.file_scan_db = nullptr;

   other.stmt_insert_version = nullptr;
   other.stmt_insert_file = nullptr;
   other.stmt_insert_scanset_file = nullptr;
   other.stmt_insert_exif = nullptr;

   other.stmt_find_file = nullptr;

   other.stmt_begin_txn = nullptr;
   other.stmt_commit_txn = nullptr;
   other.stmt_rollback_txn = nullptr;
}

file_tracker_t::~file_tracker_t(void)
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

   if(stmt_insert_version) {
      if((errcode = sqlite3_finalize(stmt_insert_version)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to insert a version (%s)", sqlite3_errstr(errcode));
   }

   if(stmt_insert_scanset_file) {
      if((errcode = sqlite3_finalize(stmt_insert_scanset_file)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to insert a scanset file (%s)", sqlite3_errstr(errcode));
   }

   if(stmt_insert_exif) {
      if((errcode = sqlite3_finalize(stmt_insert_exif)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to insert an EXIF record (%s)", sqlite3_errstr(errcode));
   }

   if(stmt_begin_txn) {
      if((errcode = sqlite3_finalize(stmt_begin_txn)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to begin a transaction (%s)", sqlite3_errstr(errcode));
   }

   if(stmt_commit_txn) {
      if((errcode = sqlite3_finalize(stmt_commit_txn)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to commit a transaction (%s)", sqlite3_errstr(errcode));
   }

   if(stmt_rollback_txn) {
      if((errcode = sqlite3_finalize(stmt_rollback_txn)) != SQLITE_OK)
         print_stream.error("Cannot finalize SQLite statment to rollback a transaction (%s)", sqlite3_errstr(errcode));
   }

   if(file_scan_db) {
      if((errcode = sqlite3_close(file_scan_db)) != SQLITE_OK)
         print_stream.error("Failed to close the SQLite database (%s)", sqlite3_errstr(errcode));
   }
}

std::vector<std::string> file_tracker_t::parse_EXIF_exts(const options_t& options)
{
   std::vector<std::string> EXIF_exts;

   if(options.EXIF_exts.has_value() && !options.EXIF_exts.value().empty()) {
      size_t start = 0, pos;

      EXIF_exts.reserve(16);

      // hop dot separators and add each extension to the vector
      while(start < options.EXIF_exts.value().length()-1 &&
               *(options.EXIF_exts.value().data()+start) == '.' &&
               (pos = options.EXIF_exts.value().find_first_of('.', start+1)) != std::string::npos) {
         EXIF_exts.emplace_back(options.EXIF_exts.value().data()+start, pos-start);
         start = pos;
      }

      if(start < options.EXIF_exts.value().length() && *(options.EXIF_exts.value().data()+start) == '.')
         EXIF_exts.emplace_back(options.EXIF_exts.value().data()+start, options.EXIF_exts.value().length()-start);
   }

   if(!EXIF_exts.empty())
      std::sort(EXIF_exts.begin(), EXIF_exts.end(), less_ci());

   return EXIF_exts;
}

void file_tracker_t::hash_file(const std::filesystem::path& filepath, uint64_t& filesize, uint8_t hexhash[])
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

   sha256_t ctx;
   sha256_init(&ctx);

   size_t lastread = 0;

   filesize = 0;

   while((lastread = std::fread(file_buffer.get(), 1, options.buffer_size, file.get())) != 0) {
      sha256_update(&ctx, file_buffer.get(), lastread);

      filesize += lastread;
   }

   if(std::ferror(file.get()))
      throw std::runtime_error("Cannot read file (" + std::string(strerror(errno)) + ") " + filepath.u8string());

   // hash for zero-length files should not be evaluated
   if(filesize) {
      uint8_t filehash[SHA256_DIGEST_SIZE];

      sha256_final(&ctx, filehash);

      static const char hex[] = "0123456789abcdef";

      for(size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
         hexhash[i*2] = hex[(*(filehash+i) & 0xF0) >> 4];
         hexhash[i*2+1] = hex[*(filehash+i) & 0x0F];
      }
   }
}

int64_t file_tracker_t::insert_file_record(const std::string& filepath, const std::filesystem::directory_entry& dir_entry)
{
   int64_t file_id = 0;
   int errcode = SQLITE_OK;

   sqlite_stmt_binder_t insert_file_stmt(stmt_insert_file, "insert file"sv);

   insert_file_stmt.bind_param(dir_entry.path().filename().u8string());

   if(dir_entry.path().extension().empty())
      insert_file_stmt.bind_param(nullptr);
   else
      insert_file_stmt.bind_param(dir_entry.path().extension().u8string());

   insert_file_stmt.bind_param(filepath);
            
   if((errcode = sqlite3_step(stmt_insert_file)) != SQLITE_DONE)
      throw std::runtime_error("Cannot insert a file record for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

   file_id = sqlite3_last_insert_rowid(file_scan_db);

   insert_file_stmt.reset();

   return file_id;
}

int64_t file_tracker_t::insert_exif_record(const std::string& filepath, const std::vector<exif::field_value_t>& exif_fields, const exif::field_bitset_t& field_bitset)
{
   int64_t exif_id = 0;

   int errcode = SQLITE_OK;

   sqlite_stmt_binder_t insert_exif_stmt(stmt_insert_exif, "insert exif"sv);

   for(size_t i = 0; i < exif_fields.size(); i++) {
      if(!field_bitset.test(i))
         insert_exif_stmt.bind_param(nullptr);
      else {
         if(exif_fields[i].index() == 1)
            insert_exif_stmt.bind_param(std::get<1>(exif_fields[i]));
         else if(exif_fields[i].index() == 2)
            insert_exif_stmt.bind_param(std::get<2>(exif_fields[i]));
         else
            throw std::runtime_error("Bad field value in EXIF record for "s + filepath + " (" + std::to_string(i) + ")");
      }
   }

   if((errcode = sqlite3_step(stmt_insert_exif)) != SQLITE_DONE)
      throw std::runtime_error("Cannot insert an EXIF record for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

   // get the row ID for the new EXIF record
   exif_id = sqlite3_last_insert_rowid(file_scan_db);

   insert_exif_stmt.reset();

   return exif_id;
}

int64_t file_tracker_t::insert_version_record(const std::string& filepath, int64_t file_id, int64_t version, int64_t filesize, const std::filesystem::directory_entry& dir_entry, uint8_t hexhash_file[SHA256_HEX_SIZE], std::optional<int64_t> exif_id)
{
   int64_t version_id = 0;

   int errcode = SQLITE_OK;

   sqlite_stmt_binder_t insert_version_stmt(stmt_insert_version, "insert version"sv);

   insert_version_stmt.bind_param(file_id);

   insert_version_stmt.bind_param(version);

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
   insert_version_stmt.bind_param(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(dir_entry.last_write_time().time_since_epoch()).count()));

   insert_version_stmt.bind_param(static_cast<int64_t>(dir_entry.file_size()));
   insert_version_stmt.bind_param(static_cast<int64_t>(filesize));

   // hash_type
   insert_version_stmt.skip_param();

   // insert a NULL for a zero-length file
   if(!filesize)
      insert_version_stmt.bind_param(nullptr);
   else
      insert_version_stmt.bind_param(std::string_view(reinterpret_cast<const char*>(hexhash_file), SHA256_HEX_SIZE));

   if(exif_id.has_value())
      insert_version_stmt.bind_param(exif_id.value());
   else
      insert_version_stmt.bind_param(nullptr);

   //
   // If we get SQLITE_BUSY after the busy handler runs for allowed
   // amount of time, report this file as failed. The most likely
   // cause for this would be that one of statements wasn't reset
   // and left SQLite record storage locked.
   //
   if((errcode = sqlite3_step(stmt_insert_version)) != SQLITE_DONE)
      throw std::runtime_error("Cannot insert a version record for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

   // get the row ID for the new version record
   version_id = sqlite3_last_insert_rowid(file_scan_db);

   insert_version_stmt.reset();

   return version_id;
}

void file_tracker_t::insert_scanset_record(const std::string& filepath, int64_t version_id)
{
   int errcode = SQLITE_OK;

   sqlite_stmt_binder_t insert_scanset_file_stmt(stmt_insert_scanset_file, "insert scanset file"sv);

   // scan_id
   insert_scanset_file_stmt.skip_param();

   insert_scanset_file_stmt.bind_param(version_id);

   if((errcode = sqlite3_step(stmt_insert_scanset_file)) != SQLITE_DONE)
      throw std::runtime_error("Cannot insert a scanset record for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

   insert_scanset_file_stmt.reset();
}

void file_tracker_t::run(void)
{
   int errcode = SQLITE_OK;

   // a file path string buffer
   std::string filepath;

   filepath.reserve(1024);
               
   uint8_t hexhash_file[SHA256_HEX_SIZE + 1] = {};       // file hash; should not be accessed if filesize == 0
   uint8_t hexhash_field[SHA256_HEX_SIZE + 1] = {};      // database hash; should no be accessed if hash_field_is_null is false

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

         // these variables should be evaluated only if version_id has value
         int64_t version = 0;
         int64_t mod_time = 0;
         int64_t scanset_scan_id = 0;           
         bool hash_field_is_null = false;

         bool hash_match = false;

         std::optional<int64_t> version_id;
         std::optional<int64_t> file_id;
         std::optional<int64_t> exif_id;

         errcode = sqlite3_step(stmt_find_file);

         if(errcode != SQLITE_DONE && errcode != SQLITE_ROW)
            throw std::runtime_error("SQLite select failed for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

         // if we found a file record, get the columns we need
         if(errcode == SQLITE_ROW) {
            version = sqlite3_column_int64(stmt_find_file, 0);

            mod_time = sqlite3_column_int64(stmt_find_file, 1);

            version_id = sqlite3_column_int64(stmt_find_file, 4);

            file_id = sqlite3_column_int64(stmt_find_file, 5);

            scanset_scan_id = sqlite3_column_int64(stmt_find_file, 6);

            hash_field_is_null = sqlite3_column_type(stmt_find_file, 3) == SQLITE_NULL;

            if(!hash_field_is_null) {
               // hold onto the column hash value
               if(std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 2)), sqlite3_column_bytes(stmt_find_file, 2)) == "SHA256"sv) {
                  if(sqlite3_column_bytes(stmt_find_file, 3) != SHA256_HEX_SIZE)
                     throw std::runtime_error("Bad hash size for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");

                  memcpy(hexhash_field, reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 3)), SHA256_HEX_SIZE);
               }
               else
                  throw std::runtime_error("Unknown hash type: "s + reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 2)));
            }
         }

         //
         // Reset the select statement to release read locks acquired above
         // before starting a transaction. Without this step, some insert
         // statements will fail with SQLITE_BUSY because of possible
         // resource deadlocks, which are reported immediately and without
         // calling the busy handler.
         //
         find_file_stmt.reset();

         //
         // Skip all file processing if we are updating the last scanset and
         // found a file version record with the same scan number as we have
         // in this file hasher instance, which is the last scan number.
         //
         if(!options.update_last_scanset || !version_id.has_value() || scanset_scan_id != scan_id) {
            //
            // Check if we were asked to skip hashing based on the file
            // modification time being unchanged from what was recorded
            // in the database.
            // 
            if(!options.verify_files && options.skip_hash_mod_time && version_id.has_value()
                  && mod_time == std::chrono::duration_cast<std::chrono::seconds>(dir_entry.last_write_time().time_since_epoch()).count())
               hash_match = true;
            else {
               // hash the file if we didn't find its version record or the last-modified time changed
               hash_file(dir_entry.path(), filesize, hexhash_file);

               // consider a NULL hash field as a match for zero-length files
               hash_match = version_id.has_value() && ((filesize == 0 && hash_field_is_null) || memcmp(hexhash_file, hexhash_field, SHA256_HEX_SIZE) == 0);
            }

            //
            // We may insert a few records after this point and need to make
            // sure they are inserted within a transaction to avoid having a
            // file without versions, which will trigger primary key violation
            // next time the same file is being processed.
            // 
            // Worth noting that the transaction is deferred, so in case we
            // don't insert any records, there should be no locks placed on
            // the database.
            //
            if(!options.verify_files) {
               errcode = sqlite3_step(stmt_begin_txn);

               if(errcode != SQLITE_DONE)
                  throw std::runtime_error("Cannot start a SQLite transaction for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");
            }

            // if there is no file record for this path, insert one to get a file ID
            if(!options.verify_files && !file_id.has_value())
               file_id = insert_file_record(filepath, dir_entry);

            // if there's no version record or the hash didn't match, we need to insert a new one
            if(!hash_match) {
               // handle the mismatched hash based on whether we are verifying or scanning
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
                  // Check if this file is a picture and we need to check for EXIF
                  // data. We don't try to figure out if EXIF changed and store an
                  // EXIF record for every version of the picture file.
                  //
                  if(!EXIF_exts.empty() && !dir_entry.path().extension().empty()) {
                     if(binary_search(EXIF_exts.begin(), EXIF_exts.end(), dir_entry.path().extension().u8string(), less_ci())) {
                        // filepath will contain a relative path if base path was specified
                        exif::field_bitset_t field_bitset = exif_reader.read_file_exif(dir_entry.path(), print_stream);

                        // ignore EXIF unless Make or Model were specified (some image editors add meaningless default values).
                        if(field_bitset.test(exif::EXIF_FIELD_Make) || field_bitset.test(exif::EXIF_FIELD_Model))
                           exif_id = insert_exif_record(filepath, exif_reader.get_exif_fields(), field_bitset);
                     }
                  }

                  //
                  // Insert a new version record for this file. There is no
                  // concurrency for this record because each file path is
                  // unique in the queue, so there is no danger of a version
                  // conflict.
                  //
                  version_id = insert_version_record(filepath, file_id.value(), version+1, filesize, dir_entry, hexhash_file, exif_id);
               }

               // reuse for updated files during scans and for mismatched files during verification
               progress_info.updated_files++;
               progress_info.updated_size += options.skip_hash_mod_time ? dir_entry.file_size() : filesize;
            }

            if(!options.verify_files) {
               // insert a scanset record with the new or existing version ID
               insert_scanset_record(filepath, version_id.value());

               errcode = sqlite3_step(stmt_commit_txn);

               if(errcode != SQLITE_DONE)
                  throw std::runtime_error("Cannot commit a SQLite transaction for "s + filepath + " (" + sqlite3_errstr(errcode) + ")");
            }
         }

         //
         // Update stats for processed files
         //
         progress_info.processed_files++;
         progress_info.processed_size += options.skip_hash_mod_time ? dir_entry.file_size() : filesize;
      }
      catch (const std::exception& error) {
         progress_info.failed_files++;

         print_stream.error("%s", error.what());

         // if we started a transaction, roll it back
         if(!sqlite3_get_autocommit(file_scan_db)) {
            errcode = sqlite3_step(stmt_rollback_txn);

            if(errcode != SQLITE_DONE)
               print_stream.error("Cannot rollback a SQLite transaction for %s (%s)", filepath.c_str(), sqlite3_errstr(errcode));
         }
      }

      // need to lock to access the queue
      lock.lock();
   }
}

int file_tracker_t::sqlite_busy_handler_cb(void*, int count)
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

void file_tracker_t::initialize(print_stream_t& print_stream)
{
   exif::exif_reader_t::initialize(print_stream);
}

void file_tracker_t::cleanup(print_stream_t& print_stream) noexcept
{
   exif::exif_reader_t::cleanup(print_stream);
}

void file_tracker_t::start(void)
{
   file_tracker_thread = std::thread(&file_tracker_t::run, this);
}

void file_tracker_t::stop(void)
{
   stop_request = true;
}

void file_tracker_t::join(void)
{
   if(file_tracker_thread.joinable())
      file_tracker_thread.join();
}

}
