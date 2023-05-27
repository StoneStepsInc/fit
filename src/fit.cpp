//
// File Integrity Tracker (fit)
// 
// Copyright (c) 2023, Stone Steps Inc.
//
#include "file_tree_walker.h"
#include "print_stream.h"
#include "sqlite.h"

#include "fit.h"

#include <sqlite3.h>

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cinttypes>
#include <csignal>
#include <cstdarg>
#include <cstring>

#include <string>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <vector>
#include <queue>
#include <chrono>
#include <optional>

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
static const char *version = "2.1.0+" STR_BUILD_NUMBER(BUILD_NUMBER);
static const char *copyright = "Copyright (c) 2023 Stone Steps Inc.";

//
// A two-component database schema version is stored in the SQLite
// database header under user_version, which is an `int` field,
// and can be accessed via `PRAGMA user_version;`. The database
// version is not related to the application version and is
// incremented when the database schema is modified in any way.
// 
// The most significant component of the database schema version
// is incremented when the schema is changed in the trunk (e.g.
// 1.0, 2.0, 3.0). The least significant component is incremented
// when database schema changes in patch-level branches.
//
//   v0.0   Database version is not maintained
// 
//   v1.0   Introduced PRAGMA user_version
// 
//   v2.0   Added a file extension column
// 
//   v3.0   Added table scansets
//          Renamed table files to versions
//          Moved path, name, ext from the versions table to files
//          Added column options to scans
//          Added table exif
// 
//   v4.0   Reworked ix_scansets_scan_version
// 
//   v5.0   Added Exiv2Json to exif
//
static const int DB_SCHEMA_VERSION = 50;

std::atomic<bool> abort_scan = false;

void close_sqlite_database(sqlite3 *file_scan_db);

extern "C" void console_ctrl_c_handler(int sig)
{
   if(sig == SIGINT || sig == SIGTERM)
      abort_scan = true;
}

void print_usage(void)
{
   printf("%s (fit) %s -- %s\n", title, version, copyright);

   fputs("\nUsage: fit [options]\n\n", stdout);

   fputs("    -b path      - scan database\n", stdout);
   fputs("    -p path      - base path\n", stdout);
   fputs("    -d path      - directory to scan (may be used multiple times)\n", stdout);
   fputs("    -m message   - optional scan description\n", stdout);
   fputs("    -r           - recursive scan\n", stdout);
   fputs("    -v           - verify scanned files against database entries\n", stdout);
#ifndef NO_SSE_AVX
   fputs("    -H           - multi-buffer hash maximum (default: 8, min: 1, max: 32)\n", stdout);
#endif
   fputs("    -t           - file hasher thread count (default: 4, min: 1, max: 64)\n", stdout);
   fputs("    -s size      - file buffer size (default: 65536, min: 512, max: 1048576)\n", stdout);
   fputs("    -i seconds   - progress reporting interval (default: 10, min: 1)\n", stdout);
   fputs("    -w           - skip hashing for files with same last-modified time\n", stdout);
   fputs("    -u           - continue last scan (update last scanset)\n", stdout);
   fputs("    -l path      - log file path\n", stdout);
   fputs("    -a           - skip restricted access directories\n", stdout);
   fputs("    -X           - list of EXIF file extensions (default: .jpg.cr2.dng.nef.tif.heif.webp)\n", stdout);
   fputs("    -J           - store EXIF obtained from Exiv2 as JSON\n", stdout);
   fputs("    -?           - this help\n", stdout);

   fputc('\n', stdout);
}

options_t parse_options(int argc, char *argv[])
{
   options_t options;

   // start with 1 to skip the command program name
   for(size_t i = 1; i < argc; i++) {
      if(!argv[i])
         throw std::runtime_error("A null argument is not valid: " + std::to_string(i));

      if(*argv[i] != '-')
         throw std::runtime_error("Invalid option: " + std::string(argv[i]));

      // hold onto the option index to detect option values
      size_t opt_i = i;

      // see below
      if(*(argv[i]+1) != 'm' && *(argv[i]+1) != 'd') {
         if(i > 1)
            options.all += ' ';

         options.all += argv[i];
      }

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

            options.scan_paths.emplace_back(argv[++i]);
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
         case 'u':
            options.update_last_scanset = true;
            break;
         case 'l':
            if(i+1 == argc || *(argv[i+1]) == '-')
               throw std::runtime_error("Missing log file path value");

            options.log_file = argv[++i];
            break;
         case 'a':
            options.skip_no_access_paths = true;
            break;
         case 'X':
            // empty string means that EXIF scanning is disabled
            if(i+1 < argc && *(argv[i+1]) != '-')
               options.EXIF_exts = argv[++i];
            else
               options.EXIF_exts.emplace();
            break;
         case 'J':
            options.exiv2_json = true;
            break;
#ifndef NO_SSE_AVX
         case 'H':
            options.mb_hash_max = atoi(argv[++i]);
            break;
#endif
         case 'h':
         case '?':
            options.print_usage = true;
            break;
         default:
            throw std::runtime_error("Unknown option: " + std::string(argv[i]));
      }

      //
      // For options with values, append it to the option line, unless it
      // is a message or a scan directory. We also don't want to enforce
      // either for scanset updates, so multiple directories under the
      // same base path can be scanned within one scanset. The message is
      // stored in its own column and both are reported in the output.
      // 
      if(opt_i != i && *(argv[opt_i]+1) != 'm' && *(argv[opt_i]+1) != 'd') {
         // wrap arguments that may have spaces in quotes
         if(strchr("blp", *(argv[opt_i]+1)))
            options.all = options.all + " \"" + argv[i] + "\"";
         else
            options.all = options.all + " " + argv[i];
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
   if(options.scan_paths.empty())
      options.scan_paths.emplace_back(std::filesystem::current_path());

   for(const std::filesystem::path& scan_path : options.scan_paths) {
      if(!std::filesystem::exists(scan_path))
         throw std::runtime_error(scan_path.u8string() + " does not exist");
   }

   for(const std::filesystem::path& scan_path : options.scan_paths) {
      if(!std::filesystem::is_directory(scan_path))
         throw std::runtime_error(scan_path.u8string() + " is not a directory");
   }

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
   for(std::filesystem::path& scan_path : options.scan_paths)
      scan_path = std::filesystem::canonical(scan_path);

   if(!options.base_path.empty()) {
      if(!std::filesystem::is_directory(options.base_path))
         throw std::runtime_error(options.base_path.u8string() + " is not a directory");

      options.base_path = std::filesystem::canonical(options.base_path);

      // make sure that the scan path is under the base path
      std::filesystem::path::iterator bpi = options.base_path.begin();

      for(const std::filesystem::path& scan_path : options.scan_paths) {
         for(std::filesystem::path::iterator spi = scan_path.begin(); bpi != options.base_path.end() && spi != scan_path.end(); ++bpi, ++spi) {
            if(*bpi != *spi)
               throw std::runtime_error("Scan path must be under the base path");
         }
      }

      // check if the loop ended because the scan path was shorter
      if(bpi != options.base_path.end())
         throw std::runtime_error("Scan path must be under the base path");
   }

   // if there are no EXIF extensions, use the default list
   if(!options.EXIF_exts.has_value())
      options.EXIF_exts = ".jpg.jpeg.cr2.dng.nef.tiff.tif.heif.webp"sv;
}

std::string schema_version_string(int schema_version)
{
   return std::to_string(schema_version/10) + "." + std::to_string(schema_version%10);
}

sqlite3 *open_sqlite_database(const options_t& options, int& schema_version, print_stream_t& print_stream)
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

   try {
      // attempt to open an existing database first
      if((errcode = sqlite3_open_v2(options.db_path.u8string().c_str(), &file_scan_db, sqlite_flags, nullptr)) == SQLITE_OK) {
         sqlite_stmt_t user_version_stmt("PRAGMA user_version"sv);

         if((errcode = user_version_stmt.prepare(file_scan_db, "PRAGMA user_version;"sv)) != SQLITE_OK)
            throw std::runtime_error("Cannot prepare a SQLite statement for a database schema version ("s + sqlite3_errstr(errcode) + ")");
      
         errcode = sqlite3_step(user_version_stmt);

         if(errcode != SQLITE_ROW)
            schema_version = 0;
         else
            schema_version = sqlite3_column_int(user_version_stmt, 0);

         if((errcode = user_version_stmt.finalize()) != SQLITE_OK)
            fprintf(stderr, "Cannot finalize SQLite statment for a database schema version (%s)", sqlite3_errstr(errcode));
      }
      else {
         if(options.verify_files)
            throw std::runtime_error("Cannot open a SQLite database in "s + options.db_path.generic_u8string());

         // attempt to create a new database
         if((errcode = sqlite3_open_v2(options.db_path.u8string().c_str(), &file_scan_db, sqlite_flags | SQLITE_OPEN_CREATE, nullptr)) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errstr(errcode));

         print_stream.info("Creating a new SQLite database %s", options.db_path.generic_u8string().c_str());

         // files table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE files ("
                                          "name TEXT NOT NULL,"
                                          "ext TEXT NULL,"
                                          "path TEXT NOT NULL);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         if(sqlite3_exec(file_scan_db, "CREATE UNIQUE INDEX ix_files_path ON files (path);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create path index for 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get());

         // versions table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE versions ("
                                          "file_id INTEGER NOT NULL,"
                                          "version INTEGER NOT NULL,"
                                          "mod_time INTEGER NOT NULL,"
                                          "entry_size INTEGER NOT NULL,"
                                          "read_size INTEGER NOT NULL, "
                                          "exif_id INTEGER NULL, "
                                          "hash_type VARCHAR(32) NOT NULL,"
                                          "hash TEXT);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         if(sqlite3_exec(file_scan_db, "CREATE UNIQUE INDEX ix_versions_file ON versions (file_id, version);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create path index for 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get());

         if(sqlite3_exec(file_scan_db, "CREATE INDEX ix_versions_hash ON versions (hash, hash_type);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create a hash index for 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         // exif table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE exif ("
                                          "BitsPerSample TEXT NULL,Compression INTEGER NULL,DocumentName TEXT NULL,ImageDescription TEXT NULL,"
                                          "Make TEXT NULL,Model TEXT NULL,Orientation INTEGER NULL,SamplesPerPixel TEXT NULL,"
                                          "Software TEXT NULL,DateTime TEXT NULL,Artist TEXT NULL,Copyright TEXT NULL,"
                                          "ExposureTime TEXT NULL,FNumber TEXT NULL,ExposureProgram INTEGER NULL,ISOSpeedRatings INTEGER NULL,"
                                          "SensitivityType INTEGER NULL,ISOSpeed INTEGER NULL,TimeZoneOffset TEXT NULL,DateTimeOriginal TEXT NULL,"
                                          "DateTimeDigitized TEXT NULL,OffsetTime TEXT NULL,OffsetTimeOriginal TEXT NULL,OffsetTimeDigitized TEXT NULL,"
                                          "ShutterSpeedValue TEXT NULL,ApertureValue TEXT NULL,SubjectDistance TEXT NULL,BrightnessValue TEXT NULL,"
                                          "ExposureBiasValue TEXT NULL,MaxApertureValue TEXT NULL,MeteringMode INTEGER NULL,LightSource INTEGER NULL,"
                                          "Flash INTEGER NULL,FocalLength TEXT NULL,UserComment TEXT NULL,SubsecTime TEXT NULL,"
                                          "SubSecTimeOriginal TEXT NULL,SubSecTimeDigitized TEXT NULL,FlashpixVersion TEXT NULL,FlashEnergy TEXT NULL,"
                                          "SubjectLocation TEXT NULL,ExposureIndex TEXT NULL,SensingMethod INTEGER NULL,SceneType INTEGER NULL,"
                                          "ExposureMode INTEGER NULL,WhiteBalance INTEGER NULL,DigitalZoomRatio TEXT NULL,FocalLengthIn35mmFilm TEXT NULL,"
                                          "SceneCaptureType INTEGER NULL,SubjectDistanceRange TEXT NULL,ImageUniqueID TEXT NULL,CameraOwnerName TEXT NULL,"
                                          "BodySerialNumber TEXT NULL,LensSpecification TEXT NULL,LensMake TEXT NULL,LensModel TEXT NULL,"
                                          "LensSerialNumber TEXT NULL,GPSLatitudeRef TEXT NULL,GPSLatitude TEXT NULL,GPSLongitudeRef TEXT NULL,"
                                          "GPSLongitude TEXT NULL,GPSAltitudeRef TEXT NULL,GPSAltitude TEXT NULL,GPSTimeStamp TEXT NULL,"
                                          "GPSSpeedRef TEXT NULL,GPSSpeed TEXT NULL,GPSDateStamp TEXT NULL, XMPxmpRating TEXT NULL, Exiv2Json TEXT NULL);",nullptr,nullptr,&errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'exif' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         // scans table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE scans ("
                                          "app_version TEXT NOT NULL,"
                                          "scan_time INTEGER NOT NULL,"
                                          "base_path TEXT,"
                                          "options TEXT NOT NULL,"
                                          "message TEXT);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'scans' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         if(sqlite3_exec(file_scan_db, "CREATE INDEX ix_scans_timestamp ON scans (scan_time);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create time stamp index for 'scans' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         // scansets table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE scansets ("
                                          "scan_id INTEGER NOT NULL,"
                                          "version_id INTEGER NOT NULL);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'scansets' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         if(sqlite3_exec(file_scan_db, "CREATE UNIQUE INDEX ix_scansets_version_scan ON scansets (version_id, scan_id);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create file/scan ID index for 'scansets' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         // set the current database schema version
         if(sqlite3_exec(file_scan_db, ("PRAGMA user_version="+std::to_string(DB_SCHEMA_VERSION)+";").c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot set a database schema version ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         schema_version = DB_SCHEMA_VERSION;
      }
   }
   catch (...) {
      if(file_scan_db)
         close_sqlite_database(file_scan_db);
      throw;
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
   int64_t scan_id = 0;

   int errcode = SQLITE_OK;

   sqlite3_stmt *stmt_insert_scan = nullptr;

   //                                                               1          2          4        6        7
   std::string_view sql_insert_scan = "insert into scans (app_version, scan_time, base_path, options, message) values (?, ?, ?, ?, ?)"sv;

   // SQLite docs say there's a small performance gain if the null terminator is included in length
   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_scan.data(), (int) sql_insert_scan.length()+1, &stmt_insert_scan, nullptr)) != SQLITE_OK)
      throw std::runtime_error("Cannot prepare a SQLite statement to insert a scan ("s + sqlite3_errstr(errcode) + ")");

   {
      // need a statement block here to make sure statement is reset before it is finalized
      sqlite_stmt_binder_t insert_scan_stmt(stmt_insert_scan, "insert scan"sv);

      insert_scan_stmt.bind_param(std::string_view(version));
      insert_scan_stmt.bind_param(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

      if(options.base_path.empty())
         insert_scan_stmt.bind_param(nullptr);
      else
         insert_scan_stmt.bind_param(options.base_path.u8string());

      insert_scan_stmt.bind_param(options.all);

      if(options.scan_message.empty())
         insert_scan_stmt.bind_param(nullptr);
      else
         insert_scan_stmt.bind_param(options.scan_message);

      if((errcode = sqlite3_step(stmt_insert_scan)) == SQLITE_DONE)
         scan_id = sqlite3_last_insert_rowid(file_scan_db);
      else
         throw std::runtime_error("Cannot insert a scan record ("s + sqlite3_errstr(errcode) + ")");
   }

   if((errcode = sqlite3_finalize(stmt_insert_scan)) != SQLITE_OK)
      throw std::runtime_error("Cannot finalize SQLite statment to insert a scan record ("s + sqlite3_errstr(errcode) + ")");

   return scan_id;
}

int64_t select_last_scan_id(const options_t& options, sqlite3 *file_scan_db)
{
   std::string scan_options;
   std::optional<int64_t> scan_id;
   std::optional<std::runtime_error> select_error;

   int errcode = SQLITE_OK;

   sqlite3_stmt *stmt_last_scan = nullptr;

   std::string_view sql_last_scan = "SELECT scans.rowid, options FROM scans ORDER BY scans.rowid DESC LIMIT 1"sv;

   // SQLite docs say there's a small performance gain if the null terminator is included in length
   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_last_scan.data(), (int) sql_last_scan.length()+1, &stmt_last_scan, nullptr)) != SQLITE_OK)
      throw std::runtime_error("Cannot prepare a last scan statement ("s + sqlite3_errstr(errcode) + ")");

   errcode = sqlite3_step(stmt_last_scan);

   // if there's any error, hold onto it, so we can finalize the statement
   if(errcode != SQLITE_DONE && errcode != SQLITE_ROW)
      select_error.emplace("SQLite select the last scan "s + " (" + sqlite3_errstr(errcode) + ")");
   else {
      if(errcode == SQLITE_ROW) {
         scan_id = sqlite3_column_int64(stmt_last_scan, 0);

         scan_options.assign(reinterpret_cast<const char*>(sqlite3_column_text(stmt_last_scan, 1)), sqlite3_column_bytes(stmt_last_scan, 1));
      }
   }

   if((errcode = sqlite3_reset(stmt_last_scan)) != SQLITE_OK)
      throw std::runtime_error("Cannot reset a last scan statement ("s + sqlite3_errstr(errcode) + ")");

   if((errcode = sqlite3_finalize(stmt_last_scan)) != SQLITE_OK)
      throw std::runtime_error("Cannot finalize a last scan statment ("s + sqlite3_errstr(errcode) + ")");

   // error out if we didn't find the last scan
   if(!scan_id.has_value()) {
      if(!select_error.has_value())
         throw std::runtime_error("Cannot find the last scan in the database");
      else
         throw select_error.value();
   }

   //
   // Make sure last scan was created with exact same options, in the
   // same order as the current one, with an extra -u. Current scan
   // options should be validated at this point.
   // 

   // current options must be longer than original options because of the extra -u
   if(options.all.length() <= scan_options.length())
      throw std::runtime_error("Update scan cannot have fewer options than the last scan");
   
   const char *optcp = options.all.c_str();
   const char *scncp = scan_options.c_str();

   // both sets of options are normalized and have same spacing and quoting
   while(*optcp) {
      if(*optcp == *scncp)
         optcp++, scncp++;
      else {
         // if we ran out of scan options, we must have only -u at the end
         if(!*scncp) { 
            if(!strcmp(optcp, " -u"))
               optcp += 3;
            break;
         }

         if(*optcp != 'u')
            break;

         //
         // We got -u verified, skip to the next one, if any (e.g. `-r -b "xyz.db" -r`
         // vs. `-r -b "xyz.db" -u -u` will not have space after the last -u).
         //
         if(*++optcp == ' ' && *++optcp == '-')
            optcp++;
      }
   }

   // error out if either of the pointers isn't at the null terminator
   if(*optcp || *scncp)
      throw std::runtime_error("Update scan must have the same options as the last scan");

   return scan_id.value();
}

}

int main(int argc, char *argv[])
{
   try {
      struct sqlite3_database_deleter_t {
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

      printf("%s (fit) %s -- %s\n\n", fit::title, fit::version, fit::copyright);

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
      int schema_version = 0;

      std::unique_ptr<sqlite3, sqlite3_database_deleter_t> file_scan_db(fit::open_sqlite_database(options, schema_version, print_stream));

      if(schema_version != fit::DB_SCHEMA_VERSION)
         throw std::runtime_error("Database must be upgraded from v"s + fit::schema_version_string(schema_version) + " to v"s + fit::schema_version_string(fit::DB_SCHEMA_VERSION));

      int64_t scan_id = 0;
      
      if(!options.verify_files) {
         if(!options.update_last_scanset)
            scan_id = fit::insert_scan_record(options, file_scan_db.get());
         else
            scan_id = fit::select_last_scan_id(options, file_scan_db.get());
      }

      std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

      //
      // Walk the file tree
      //
      if(!options.scan_message.empty())
         print_stream.info("%s (%s) with options %s", options.verify_files ? "Verifying" : "Scanning", options.scan_message.c_str(), options.all.c_str());
      else
         print_stream.info("%s with options %s", options.verify_files ? "Verifying" : "Scanning", options.all.c_str());

      //
      // Initialize underlying libraries before any of the components
      // are created and threads started.
      //
      fit::file_tree_walker_t::initialize(print_stream);

      try {
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
      }
      catch (...) {
         fit::file_tree_walker_t::cleanup(print_stream);
         throw;
      }

      fit::file_tree_walker_t::cleanup(print_stream);

      return EXIT_SUCCESS;
   }
   catch (const std::exception& error) {
      fprintf(stderr, "ERROR: %s\n", error.what());
   }

   return EXIT_FAILURE;
}
