//
// File Integrity Tracker (fit)
// 
// Copyright (c) 2023, Stone Steps Inc.
//
#include "file_tree_walker.h"
#include "print_stream.h"
#include "sqlite.h"
#include "unicode.h"

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

#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

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
static const char *version = "3.3.0+" STR_BUILD_NUMBER(BUILD_NUMBER);
static const char *copyright = "Copyright (c) 2025 Stone Steps Inc.";

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
//   v6.0   File time stamps are in time_t now (no schema changes)
// 
//   v7.0   Added scans.completed_time
// 
//   v8.0   Added scans.last_update_time, scans.cumulative_duration, scans.times_updated
//
static const int DB_SCHEMA_VERSION = 80;

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
   fputs("    -v [scan]    - verify scanned files against last or specified scan (default: last)\n", stdout);
#ifndef NO_SSE_AVX
   fputs("    -H number    - multi-buffer hash maximum (default: 8, min: 1, max: 32)\n", stdout);
#endif
   fputs("    -t number    - file hasher thread count (default: 4, min: 1, max: 64)\n", stdout);
   fputs("    -s size      - file buffer size (default: 524288, min: 512, max: 16777216)\n", stdout);
   fputs("    -i seconds   - progress reporting interval (default: 10, min: 1)\n", stdout);
   fputs("    -u           - continue last scan (update last scanset)\n", stdout);
   fputs("    -l path      - log file path\n", stdout);
   fputs("    -a           - skip restricted access directories\n", stdout);
   fputs("    -X [ext,...] - EXIF file extensions (default: .jpg.cr2.dng.nef.tif.heif.webp, none: no EXIF)\n", stdout);
   fputs("    -J           - store EXIF obtained from Exiv2 as JSON\n", stdout);
   fputs("    -S kind      - path separator kind for querying the database (default: none, Windows, POSIX)\n", stdout);
   fputs("    -?           - this help\n", stdout);

   fputc('\n', stdout);
}

options_t parse_options(int argc, char *argv[])
{
   options_t options;

   // start with 1 to skip the command program name
   for(size_t i = 1; i < argc; i++) {
      if(!argv[i])
         throw std::runtime_error(FMTNS::format("A null argument is not valid: {:d}", i));

      if(*argv[i] != '-')
         throw std::runtime_error(FMTNS::format("Invalid option: {:s}", argv[i]));

      // hold onto the option index to detect option values
      size_t opt_i = i;

      // conditions should match the loop with same option conditions following the switch for options with values
      if(*(argv[i]+1) != 'm' && *(argv[i]+1) != 'd' && *(argv[i]+1) != 'a') {
         if(i > 1)
            options.all += ' ';

         options.all += reinterpret_cast<char8_t*>(argv[i]);
      }

      if(*(argv[i]+1) == '-') {
         if(!strcmp(argv[i]+2, "upgrade-schema=6.0"))
            options.upgrade_schema_to_v60 = true;
         else if(!strcmp(argv[i]+2, "help"))
            options.print_usage = true;
         else
            throw std::runtime_error(FMTNS::format("Invalid long option {:s}", argv[i]));
      }
      else {
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

               //
               // Passing UTF-8 into Windows programs is quite a bit of a
               // challenge. Some of the workarounds are described in this
               // GitHub issue.
               // 
               // https://github.com/PowerShell/PowerShell/issues/7233
               // 
               // The top line is taken from one of the comments in the
               // issue above. The following command purposfully encodes
               // UTF-8 bytes as malformed characters in the default
               // encoding, such as Win1252, and will end up here as bytes
               // representing UTF-8 characters.
               // 
               // $OutputEncoding=[console]::InputEncoding=[console]::OutputEncoding=New-Object System.Text.UTF8Encoding
               // fit -b xyz.db -m $([System.Text.Encoding]::Default.GetString([System.Text.Encoding]::UTF8.GetBytes("ABC £ DEF")))
               // 
               // In the example above, the `£` character is encoded as A3
               // in Win1252 and as C2 A3 in UTF-8. Similarly, Katakana
               // character \u30A9 is encoded as E3 82 A9 in UTF-8. When
               // re-encoded into the `Default` character set above, the
               // former may look like extra Win1252 characters in the
               // debugger (e.g. `Â£`), but will be interpreted as valid
               // UTF-8 in the code and later in the database.
               //
               if(!unicode::is_valid_utf8(argv[i+1]))
                  // don't echo the message because it may be mangled in the output, creating more confusion
                  throw std::runtime_error("Scan message contains invalid UTF-8 characters");

               options.scan_message = reinterpret_cast<const char8_t*>(argv[++i]);
               break;
            case 'r':
               options.recursive_scan = true;
               break;
            case 'v':
               options.verify_files = true;

               // empty string means the last scan will be verified
               if(i+1 < argc && *(argv[i+1]) != '-')
                  options.verify_scan_id = atoi(argv[++i]);

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
            case 'u':
               options.update_last_scanset = true;
               break;
            case 'l':
               if(i+1 == argc || *(argv[i+1]) == '-')
                  throw std::runtime_error("Missing log file path value");

               options.log_file = reinterpret_cast<const char8_t*>(argv[++i]);
               break;
            case 'a':
               options.skip_no_access_paths = true;
               break;
            case 'X':
               // empty string means that EXIF scanning is disabled
               if(i+1 < argc && *(argv[i+1]) != '-')
                  options.EXIF_exts = reinterpret_cast<const char8_t*>(argv[++i]);
               else
                  options.EXIF_exts.emplace();
               break;
            case 'J':
               options.exiv2_json = true;
               break;
   #ifndef NO_SSE_AVX
            case 'H':
               if(i+1 == argc || *(argv[i+1]) == '-')
                  throw std::runtime_error("Missing multi-buffer hash maximum value");

               options.mb_hash_max = atoi(argv[++i]);
               break;
   #endif
            case 'S':
               if(i+1 == argc || *(argv[i+1]) == '-')
                  throw std::runtime_error("Missing query path separator value");

               if(*reinterpret_cast<const char8_t*>(argv[++i]) == u8'W')
                  options.query_path_sep = u8'\\';
               else if(*reinterpret_cast<const char8_t*>(argv[i]) == u8'w')
                  options.query_path_sep = u8'\\';
               else if(*reinterpret_cast<const char8_t*>(argv[i]) == u8'P')
                  options.query_path_sep = u8'/';
               else if(*reinterpret_cast<const char8_t*>(argv[i]) == u8'p')
                  options.query_path_sep = u8'/';
               else
                  throw std::runtime_error("Query path separator kind must be Windows or POSIX");
               
               break;
            case 'h':
            case '?':
               options.print_usage = true;
               break;
            default:
               throw std::runtime_error(FMTNS::format("Unknown option: {:s}", argv[i]));
         }
      }

      //
      // If this option has a value, append it to the list of options,
      // wrapping values that may contain spaces in double quotes. Skip
      // `-d` to allow additional directories and `-m` because we want
      // to collect only the original message (it may get messy tracking
      // multiple messages).
      // 
      if(opt_i != i && *(argv[opt_i]+1) != 'm' && *(argv[opt_i]+1) != 'd') {
         // wrap arguments that may have spaces in quotes
         if(strchr("blp", *(argv[opt_i]+1)))
            options.all = options.all + u8" \"" + reinterpret_cast<const char8_t*>(argv[i]) + u8"\"";
         else
            options.all = options.all + u8" " + reinterpret_cast<const char8_t*>(argv[i]);
      }
   }

   return options;
}

void verify_options(options_t& options)
{
   if(options.thread_count == 0 || options.thread_count > 64)
      throw std::runtime_error("Invalid thread count");

   if(options.buffer_size < 512 || options.buffer_size > 16*1024*1024)
      throw std::runtime_error("Invalid file buffer size");

   // round buffer size up to the nearest 512 or 4096 boundary, if it's not already there
   size_t block_size = options.buffer_size < 4096 ? 512 : 4096;
   options.buffer_size += (block_size - options.buffer_size % block_size) % block_size;

   if(options.progress_interval < 0)
      throw std::runtime_error("Invalid progress reporting interval");

   if(options.query_path_sep.has_value() && !options.verify_files)
      throw std::runtime_error("A query path separator may only be specified when verifying files");

   // ignore the native path separator for the current platform
   if(options.query_path_sep.has_value() && options.query_path_sep.value() == std::filesystem::path::preferred_separator)
      options.query_path_sep.reset();

   // db_path
   if(options.db_path.empty())
      throw std::runtime_error("Database file path must be specified");

   if(std::filesystem::is_directory(options.db_path))
      throw std::runtime_error(FMTNS::format("{:s} cannot be a directory", u8tosv_t(options.db_path.u8string())));

   if(!options.db_path.has_filename())
      throw std::runtime_error(FMTNS::format("{:s} must point to a file", u8tosv_t(options.db_path.u8string())));

   if(!std::filesystem::is_directory(std::filesystem::absolute(options.db_path).remove_filename()))
      throw std::runtime_error(FMTNS::format("{:s} must be an existing directory", u8tosv_t(std::filesystem::absolute(options.db_path).remove_filename().u8string())));

   // scan_path
   if(options.scan_paths.empty())
      options.scan_paths.emplace_back(std::filesystem::current_path());

   for(const std::filesystem::path& scan_path : options.scan_paths) {
      if(!std::filesystem::exists(scan_path))
         throw std::runtime_error(FMTNS::format("{:s} does not exist", u8tosv_t(scan_path.u8string())));
   }

   for(const std::filesystem::path& scan_path : options.scan_paths) {
      if(!std::filesystem::is_directory(scan_path))
         throw std::runtime_error(FMTNS::format("{:s} is not a directory", u8tosv_t(scan_path.u8string())));
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
         throw std::runtime_error(FMTNS::format("{:s} is not a directory", u8tosv_t(options.base_path.u8string())));

      options.base_path = std::filesystem::canonical(options.base_path);

      // make sure that all scan paths are under the base path
      for(const std::filesystem::path& scan_path : options.scan_paths) {
         std::filesystem::path::iterator bpi = options.base_path.begin();

         for(std::filesystem::path::iterator spi = scan_path.begin(); bpi != options.base_path.end() && spi != scan_path.end(); ++bpi, ++spi) {
            if(*bpi != *spi)
               break;
         }

         // check if the entire base path was consumed (i.e. there's no mismatch and the scan path isn't shorter)
         if(bpi != options.base_path.end())
            throw std::runtime_error(FMTNS::format("Scan path must be under the base path ({:s})", u8tosv_t(scan_path.u8string())));
      }
   }

   // if there are no EXIF extensions, use the default list
   if(!options.EXIF_exts.has_value())
      options.EXIF_exts = u8".jpg.jpeg.cr2.dng.nef.tiff.tif.heif.webp"sv;
}

std::string schema_version_string(int schema_version)
{
   return std::to_string(schema_version/10) + "." + std::to_string(schema_version%10);
}

sqlite3 *open_sqlite_database(const options_t& options, int& schema_version, print_stream_t& print_stream)
{
   sqlite3 *file_scan_db = nullptr;

   if(SQLITE_VERSION_NUMBER != sqlite3_libversion_number()) {
      // check if the major version component of SQLite used in a build is the same as in the shared library
      if(SQLITE_VERSION_NUMBER/1'000'000 != sqlite3_libversion_number()/1'000'000)
         throw std::runtime_error(FMTNS::format("Major version of SQLite used in a build ({:d}) must be the same as in the runtime version ({:d})"sv, SQLITE_VERSION_NUMBER, sqlite3_libversion_number()));

      // check that the runtime version is older than the one used in a build (on a chance that it may misinterpret arguments from newer source)
      if(SQLITE_VERSION_NUMBER/1'000 > sqlite3_libversion_number()/1'000)
         throw std::runtime_error(FMTNS::format("SQLite version used in a build ({:d}) cannot be greater than the runtime version ({:d})"sv, SQLITE_VERSION_NUMBER, sqlite3_libversion_number()));
   }

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
      if((errcode = sqlite3_open_v2(reinterpret_cast<const char*>(options.db_path.u8string().c_str()), &file_scan_db, sqlite_flags, nullptr)) == SQLITE_OK) {
         sqlite_stmt_t user_version_stmt("PRAGMA user_version"sv);

         if((errcode = user_version_stmt.prepare(file_scan_db, "PRAGMA user_version;"sv)) != SQLITE_OK)
            throw std::runtime_error("Cannot prepare a SQLite statement for a database schema version ("s + sqlite3_errstr(errcode) + ")");
      
         errcode = sqlite3_step(user_version_stmt);

         if(errcode != SQLITE_ROW)
            schema_version = 0;
         else
            schema_version = sqlite3_column_int(user_version_stmt, 0);

         if((errcode = user_version_stmt.finalize()) != SQLITE_OK)
            fprintf(stderr, "Cannot finalize SQLite statement for a database schema version (%s)", sqlite3_errstr(errcode));
      }
      else {
         if(options.verify_files)
            throw std::runtime_error(FMTNS::format("Cannot open a SQLite database in {:s}", u8tosv_t(options.db_path.generic_u8string())));

         // attempt to create a new database
         if((errcode = sqlite3_open_v2(reinterpret_cast<const char*>(options.db_path.u8string().c_str()), &file_scan_db, sqlite_flags | SQLITE_OPEN_CREATE, nullptr)) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errstr(errcode));

         print_stream.info("Creating a new SQLite database %s", options.db_path.generic_u8string().c_str());

         // files table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE files ("
                                          "id INTEGER NOT NULL PRIMARY KEY,"
                                          "name TEXT NOT NULL,"
                                          "ext TEXT NULL,"
                                          "path TEXT NOT NULL);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         if(sqlite3_exec(file_scan_db, "CREATE UNIQUE INDEX ix_files_path ON files (path);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create path index for 'files' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get());

         // versions table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE versions ("
                                          "id INTEGER NOT NULL PRIMARY KEY,"
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
                                          "id INTEGER NOT NULL PRIMARY KEY,"
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
                                          "id INTEGER NOT NULL PRIMARY KEY,"
                                          "app_version TEXT NOT NULL,"
                                          "scan_time INTEGER NOT NULL,"
                                          "completed_time INTEGER NULL,"
                                          "last_update_time INTEGER NULL,"
                                          "times_updated INTEGER NULL,"
                                          "cumulative_duration INTEGER NULL,"
                                          "base_path TEXT,"
                                          "options TEXT NOT NULL,"
                                          "message TEXT);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create table 'scans' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         if(sqlite3_exec(file_scan_db, "CREATE INDEX ix_scans_timestamp ON scans (scan_time);", nullptr, nullptr, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Cannot create time stamp index for 'scans' ("s + std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get() + ")");

         // scansets table
         if(sqlite3_exec(file_scan_db, "CREATE TABLE scansets ("
                                          "id INTEGER NOT NULL PRIMARY KEY,"
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
   int errcode = SQLITE_OK;

   if((errcode = sqlite3_close(file_scan_db)) != SQLITE_OK)
      fprintf(stderr, "Failed to close the SQLite database (%s)\n", sqlite3_errstr(errcode));

   if((errcode = sqlite3_shutdown()) != SQLITE_OK)
      fprintf(stderr, "Failed to shut down SQLite (%s)\n", sqlite3_errstr(errcode));
}

int64_t insert_scan_record(const options_t& options, sqlite3 *file_scan_db)
{
   int64_t scan_id = 0;

   int errcode = SQLITE_OK;

   sqlite3_stmt *stmt_insert_scan = nullptr;

   //                                                               1          2          4        6        7
   std::string_view sql_insert_scan = "INSERT INTO scans (app_version, scan_time, base_path, options, message) VALUES (?, ?, ?, ?, ?)"sv;

   // SQLite docs say there's a small performance gain if the null terminator is included in length
   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_scan.data(), (int) sql_insert_scan.length()+1, &stmt_insert_scan, nullptr)) != SQLITE_OK)
      throw std::runtime_error("Cannot prepare a SQLite statement to insert a scan ("s + sqlite3_errstr(errcode) + ")");

   {
      // need a statement block here to make sure statement is reset before it is finalized
      sqlite_stmt_binder_t insert_scan_stmt(stmt_insert_scan, "insert scan"sv);

      insert_scan_stmt.bind_param(std::u8string_view(reinterpret_cast<const char8_t*>(version)));
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
      throw std::runtime_error("Cannot finalize SQLite statement to insert a scan record ("s + sqlite3_errstr(errcode) + ")");

   return scan_id;
}

int complete_scan_record(int64_t scan_id, sqlite3 *file_scan_db)
{
   int errcode = SQLITE_OK;

   sqlite_stmt_t stmt_complete_scan("complete scan"sv);

   //                                                                    1               2
   std::string_view sql_complete_scan = "UPDATE scans SET completed_time=? WHERE rowid = ?"sv;

   stmt_complete_scan.prepare(file_scan_db, sql_complete_scan);

   sqlite_stmt_binder_t complete_scan_stmt(stmt_complete_scan, "complete scan"sv);

   complete_scan_stmt.bind_param(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
   complete_scan_stmt.bind_param(scan_id);

   errcode = sqlite3_step(stmt_complete_scan);

   if(errcode != SQLITE_DONE)
      return 0;

   return sqlite3_changes(file_scan_db);
}

int update_scan_duration(const options_t& options, int64_t scan_id, int64_t scan_duration, sqlite3 *file_scan_db)
{
   int errcode = SQLITE_OK;

   sqlite_stmt_t stmt_update_scan_duration("update scan duration"sv);

   //                                                                    1               2
   std::string_view sql_update_scan_duration = options.update_last_scanset ?
                                       "UPDATE scans SET cumulative_duration=coalesce(cumulative_duration, 0)+?, times_updated=coalesce(times_updated, 0)+1 WHERE rowid = ?"sv :
                                       "UPDATE scans SET cumulative_duration=coalesce(cumulative_duration, 0)+? WHERE rowid = ?"sv;

   stmt_update_scan_duration.prepare(file_scan_db, sql_update_scan_duration);

   sqlite_stmt_binder_t update_scan_duration_stmt(stmt_update_scan_duration, "update scan duration"sv);

   update_scan_duration_stmt.bind_param(scan_duration);
   update_scan_duration_stmt.bind_param(scan_id);

   errcode = sqlite3_step(stmt_update_scan_duration);

   if(errcode != SQLITE_DONE)
      return 0;

   // return the number of records changed in this update
   return sqlite3_changes(file_scan_db);
}

void set_last_scan_update_time(int64_t scan_id, sqlite3 *file_scan_db)
{
   int errcode = SQLITE_OK;

   sqlite_stmt_t stmt_scan_update_time("last scan update time"sv);

   //                                                                    1               2
   std::string_view sql_complete_scan = "UPDATE scans SET last_update_time=? WHERE rowid = ?"sv;

   stmt_scan_update_time.prepare(file_scan_db, sql_complete_scan);

   sqlite_stmt_binder_t scan_update_time_stmt(stmt_scan_update_time, "last scan update time"sv);

   scan_update_time_stmt.bind_param(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
   scan_update_time_stmt.bind_param(scan_id);

   errcode = sqlite3_step(stmt_scan_update_time);

   if(errcode != SQLITE_DONE)
      throw std::runtime_error(FMTNS::format("Cannot set update time for scan {:d} ({:s})", scan_id, sqlite3_errstr(errcode)));

   int updated_scans = sqlite3_changes(file_scan_db);

   if(updated_scans != 1)
      throw std::runtime_error(FMTNS::format("Cannot set update time for scan {:d}", scan_id));
}

std::tuple<std::optional<int64_t>, bool> select_base_scan_id(const options_t& options, sqlite3 *file_scan_db)
{
   std::optional<int64_t> scan_id;
   bool completed_scan = false;

   int errcode = SQLITE_OK;

   sqlite_stmt_t stmt_base_scan("select base scan"sv);

   std::string_view sql_base_scan = options.verify_scan_id.has_value() ?
                                       "SELECT scans.rowid, completed_time, last_update_time FROM scans WHERE scans.rowid = ?"sv :
                                       "SELECT scans.rowid, completed_time, last_update_time FROM scans ORDER BY scans.rowid DESC LIMIT 1"sv;

   stmt_base_scan.prepare(file_scan_db, sql_base_scan);

   sqlite_stmt_binder_t base_scan_stmt(stmt_base_scan, "select base scan"sv);

   if(options.verify_scan_id.has_value())
      base_scan_stmt.bind_param(options.verify_scan_id.value());

   errcode = sqlite3_step(stmt_base_scan);

   if(errcode != SQLITE_DONE && errcode != SQLITE_ROW) {
      if(options.verify_scan_id.has_value())
         throw std::runtime_error(FMTNS::format("Cannot find scan {:d} in the database", options.verify_scan_id.value()));
      else
         throw std::runtime_error("Cannot find the last scan in the database");
   }

   if(errcode == SQLITE_ROW) {
      scan_id = sqlite3_column_int64(stmt_base_scan, 0);

      std::optional<int64_t> completed_time;
      std::optional<int64_t> last_update_time;

      if(sqlite3_column_type(stmt_base_scan, 1) != SQLITE_NULL)
         completed_time = sqlite3_column_int64(stmt_base_scan, 1);

      if(sqlite3_column_type(stmt_base_scan, 2) != SQLITE_NULL)
         last_update_time = sqlite3_column_int64(stmt_base_scan, 2);

      // a scan is completed if there is completed time in the record and either there is no last scan update time or it is in the past
      completed_scan = completed_time.has_value() && (!last_update_time.has_value() || completed_time.value() > last_update_time.value());
   }

   return std::make_tuple(scan_id, completed_scan);
}

std::u8string select_scan_options(int64_t scan_id, sqlite3 *file_scan_db)
{
   std::u8string scan_options;

   int errcode = SQLITE_OK;

   sqlite_stmt_t stmt_last_scan("scan options"sv);

   std::string_view sql_last_scan = "SELECT options FROM scans WHERE scans.rowid = ?"sv;

   stmt_last_scan.prepare(file_scan_db, sql_last_scan);

   sqlite_stmt_binder_t last_scan_stmt(stmt_last_scan, "scan options"sv);

   last_scan_stmt.bind_param(scan_id);

   errcode = sqlite3_step(stmt_last_scan);

   if(errcode != SQLITE_ROW)
      throw std::runtime_error(FMTNS::format("Cannot select scan {:d} options ({:s})", scan_id, sqlite3_errstr(errcode)));

   scan_options.assign(reinterpret_cast<const char8_t*>(sqlite3_column_text(stmt_last_scan, 0)), sqlite3_column_bytes(stmt_last_scan, 0));

   last_scan_stmt.release();

   if((errcode = stmt_last_scan.finalize()) != SQLITE_OK)
      throw std::runtime_error("Cannot finalize scan options statement ("s + sqlite3_errstr(errcode) + ")");

   return scan_options;
}

std::optional<int64_t> select_base_scan_for_update(int64_t scan_id, sqlite3 *file_scan_db)
{
   std::optional<int64_t> base_scan_id;

   int errcode = SQLITE_OK;

   sqlite_stmt_t stmt_base_scan("base scan for update"sv);

   // no need to check for a completed scan because we would keep trying to complete the last one until it is done (i.e. only the last one can be incomplete)
   std::string_view sql_last_scan = "SELECT rowid FROM scans WHERE scans.rowid < ? ORDER BY rowid DESC LIMIT 1"sv;

   stmt_base_scan.prepare(file_scan_db, sql_last_scan);

   sqlite_stmt_binder_t base_scan_stmt(stmt_base_scan, "base scan for update"sv);

   base_scan_stmt.bind_param(scan_id);

   errcode = sqlite3_step(stmt_base_scan);

   if(errcode == SQLITE_ROW)
      base_scan_id = sqlite3_column_int64(stmt_base_scan, 0);

   base_scan_stmt.release();

   if((errcode = stmt_base_scan.finalize()) != SQLITE_OK)
      throw std::runtime_error("Cannot finalize a base scan for update statement ("s + sqlite3_errstr(errcode) + ")");

   return base_scan_id;
}

std::optional<int64_t> select_scan_for_update(const options_t& options, int64_t scan_id, sqlite3 *file_scan_db)
{
   std::u8string scan_options;
   std::optional<int64_t> base_scan_id;

   base_scan_id = select_base_scan_for_update(scan_id, file_scan_db);
   scan_options = select_scan_options(scan_id, file_scan_db);

   //
   // Make sure last scan was created with exact same options, in the
   // same order as the current one, with an extra -u. Current scan
   // options should be validated at this point.
   // 

   // current options must be longer than original options because of the extra -u
   if(options.all.length() <= scan_options.length())
      throw std::runtime_error("Update scan cannot have fewer options than the last scan");
   
   const char8_t *optcp = options.all.c_str();
   const char8_t *scncp = scan_options.c_str();

   // both sets of options are normalized and have same spacing and quoting
   while(*optcp) {
      if(*optcp == *scncp)
         optcp++, scncp++;
      else {
         // if we ran out of scan options, we must have only -u at the end
         if(!*scncp) { 
            if(!strcmp(reinterpret_cast<const char*>(optcp), " -u"))
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

   return base_scan_id;
}

void update_schema_from_v50(sqlite3 *file_scan_db, print_stream_t& print_stream)
{
   char *errmsg = nullptr;
   int errcode = SQLITE_OK;

   std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
   uint64_t record_count = 0;

   sqlite3_stmt *stmt_select_mod_time = nullptr;
   std::string_view sql_select_mod_time = "select rowid, mod_time from versions"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_select_mod_time.data(), (int) sql_select_mod_time.length()+1, &stmt_select_mod_time, nullptr)) != SQLITE_OK)
      throw std::runtime_error(FMTNS::format("Cannot prepare a SQLite statement to select mod_time in versions ({:s})"sv, sqlite3_errstr(errcode)));

   sqlite3_stmt *stmt_update_mod_time = nullptr;
   std::string_view sql_update_mod_time = "update versions set mod_time = ? where rowid = ?"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_update_mod_time.data(), (int) sql_update_mod_time.length()+1, &stmt_update_mod_time, nullptr)) != SQLITE_OK) {
      sqlite3_finalize(stmt_select_mod_time);
      throw std::runtime_error(FMTNS::format("Cannot prepare a SQLite statement to update mod_time in versions ({:s})"sv, sqlite3_errstr(errcode)));
   }

   print_stream.info("Upgrading database schema to version 6.0");

   errcode = sqlite3_step(stmt_select_mod_time);

   if(errcode != SQLITE_DONE && errcode != SQLITE_ROW) {
      sqlite3_finalize(stmt_select_mod_time);
      sqlite3_finalize(stmt_update_mod_time);
      throw std::runtime_error(FMTNS::format("Cannot select version records to update mod_time ({:s})", sqlite3_errstr(errcode)));
   }

   //
   // Walk all version records and update mod_time from the
   // file_clock value to a time_t.
   // 
   // This loop should run on the same operating system type and
   // in the same time zone that were used to create initial time
   // stamps (e.g. updating Linux time stamps on Windows will
   // create a mess).
   //
   while(errcode == SQLITE_ROW) {
      int64_t rowid = sqlite3_column_int64(stmt_select_mod_time, 0);
      int64_t mod_time = sqlite3_column_int64(stmt_select_mod_time, 1);

      fit::sqlite_stmt_binder_t update_mod_time_stmt(stmt_update_mod_time, "update mod_time"sv);

      std::chrono::file_clock::time_point mod_time_fc{std::chrono::seconds(mod_time)};

      // see file_tracker_t::file_time_to_time_t
      #if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
      update_mod_time_stmt.bind_param(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::utc_clock>(mod_time_fc).time_since_epoch()).count());
      #else
      update_mod_time_stmt.bind_param(std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(mod_time_fc)));
      #endif

      update_mod_time_stmt.bind_param(rowid);

      if((errcode = sqlite3_step(stmt_update_mod_time)) != SQLITE_DONE) {
         sqlite3_finalize(stmt_select_mod_time);
         sqlite3_finalize(stmt_update_mod_time);
         throw std::runtime_error(FMTNS::format("Cannot update mod_time in a version record {:d} ({:d})"sv, rowid, errcode));
      }

      record_count++;

      if(record_count % 1'000 == 0)
         putc('.', stdout);

      if(record_count % 50'000 == 0)
         putc('\n', stdout);

      errcode = sqlite3_step(stmt_select_mod_time);
   }

   if(record_count && record_count % 50'000)
      putc('\n', stdout);

   if(errcode != SQLITE_DONE) {
      sqlite3_finalize(stmt_select_mod_time);
      sqlite3_finalize(stmt_update_mod_time);
      throw std::runtime_error(FMTNS::format("Failed to select all version records to update mod_time ({:s})", sqlite3_errstr(errcode)));
   }

   if((errcode = sqlite3_finalize(stmt_select_mod_time)) != SQLITE_OK) {
      sqlite3_finalize(stmt_update_mod_time);
      throw std::runtime_error("Cannot finalize a SQLite statement to select mod_time ("s + sqlite3_errstr(errcode) + ")");
   }

   if((errcode = sqlite3_finalize(stmt_update_mod_time)) != SQLITE_OK)
      throw std::runtime_error("Cannot finalize SQLite statement to update mod_time ("s + sqlite3_errstr(errcode) + ")");

   //
   // Update `upgrades` and `PRAGMA user_version`
   //
   if(sqlite3_exec(file_scan_db, "CREATE TABLE IF NOT EXISTS upgrades ("
                                 "  upgrade_from INTEGER NOT NULL PRIMARY KEY,"
                                 "  upgrade_to INTEGER NOT NULL,"
                                 "  upgrade_time INTEGER NOT NULL)", nullptr, nullptr, &errmsg) != SQLITE_OK)
      throw std::runtime_error(FMTNS::format("Cannot create table 'upgrades' ({:s})"sv, std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get()));

   if(sqlite3_exec(file_scan_db, "INSERT INTO upgrades ("
                                 "   upgrade_from,"
                                 "   upgrade_to,"
                                 "   upgrade_time"
                                 ") VALUES ("
                                 "   (select user_version from pragma_user_version()),"
                                 "   60,"
                                 "   CAST(strftime('%s', 'now') AS INTEGER))", nullptr, nullptr, &errmsg) != SQLITE_OK)
      throw std::runtime_error(FMTNS::format("Cannot update version to 6.0 in 'upgrades' ({:s})"sv, std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get()));

   if(sqlite3_exec(file_scan_db, "PRAGMA user_version=60", nullptr, nullptr, &errmsg) != SQLITE_OK)
      throw std::runtime_error(FMTNS::format("Cannot update version to 6.0 in 'user_version' ({:s})"sv, std::unique_ptr<char, sqlite_malloc_deleter_t<char>>(errmsg).get()));

   print_stream.info("Updated %" PRIu64 " records in %.3lf minutes",
                        record_count,
                        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_time).count()/1000./60.);
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
         // open the log file in binary mode, where supported, so character encoding is preserved
         log_file.reset(fopen(reinterpret_cast<const char*>(options.log_file.c_str()), "ab"));

         if(!log_file)
            throw std::runtime_error(FMTNS::format("Cannot open log file {:s}", fit::u8tosv_t(options.log_file)));
      }

      fit::print_stream_t print_stream(log_file.get());

      //
      // Open a SQLite database
      //
      int schema_version = 0;

      std::unique_ptr<sqlite3, sqlite3_database_deleter_t> file_scan_db(fit::open_sqlite_database(options, schema_version, print_stream));

      if(options.upgrade_schema_to_v60) {
         if(schema_version != 50)
            throw std::runtime_error(FMTNS::format("--upgrade-schema=6.0 cannot be used against database schema {:s}"sv, fit::schema_version_string(schema_version)));

         fit::update_schema_from_v50(file_scan_db.get(), print_stream);

         // finish the upgrade process without doing anything else
         return EXIT_SUCCESS;
      }

      if(schema_version != fit::DB_SCHEMA_VERSION) {
         if(schema_version == 50)
            throw std::runtime_error("See section Upgrading Database v5.0 to v6.0 in README.md");

         throw std::runtime_error(FMTNS::format("Database must be upgraded from v{:s} to v{:s}"sv, fit::schema_version_string(schema_version), fit::schema_version_string(fit::DB_SCHEMA_VERSION)));
      }

      std::optional<int64_t> scan_id;
      std::optional<int64_t> base_scan_id;
      bool completed_scan = false;
      
      //
      // Get the base scan, against which current files will be
      // compared. For a regular scan, it will be the last scan,
      // and for a verification scan it can be any scan specified
      // on the command line, defaulting to the last one. The base
      // scan can change in case if the last scan is incomplete.
      //
      std::tie(base_scan_id, completed_scan) = fit::select_base_scan_id(options, file_scan_db.get());

      if(options.verify_files) {
         if(!base_scan_id.has_value()) {
            if(options.verify_scan_id.has_value())
               throw std::runtime_error(FMTNS::format("Cannot verify files without a base scan (scan {:d} is not found)", options.verify_scan_id.value()));
            else
               throw std::runtime_error("Cannot verify files without a base scan (none is found)");
         }

         if(!completed_scan)
            throw std::runtime_error(FMTNS::format("Cannot verify files against an incomplete scan {:d}", base_scan_id.value()));
      }
      else {
         if(!options.update_last_scanset && base_scan_id.has_value() && !completed_scan) {
            options.update_last_scanset = true;
            options.all += u8" -u";

            print_stream.warning("Continuing interrupted scan %" PRId64 " (forcing -u)", base_scan_id.value());
         }

         if(!options.update_last_scanset)
            scan_id = fit::insert_scan_record(options, file_scan_db.get());
         else {
            // in this context base_scan_id is expected to contain the last scan (reassigned below)
            if(!base_scan_id.has_value())
               throw std::runtime_error("There is no last scan to update");

            if(completed_scan)
               print_stream.warning("Updating a completed scan %" PRId64 " (completed time will change)", base_scan_id.value());

            // set the current scan to the one we found (last scan at this point) and find a previous scan to use as a base scan
            scan_id = base_scan_id.value();
            base_scan_id = fit::select_scan_for_update(options, base_scan_id.value(), file_scan_db.get());

            fit::set_last_scan_update_time(scan_id.value(), file_scan_db.get());
         }
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
         fit::file_tree_walker_t file_tree_walker(options, scan_id, base_scan_id, print_stream);

         if(options.recursive_scan)
            file_tree_walker.walk_tree<std::filesystem::recursive_directory_iterator>();
         else
            file_tree_walker.walk_tree<std::filesystem::directory_iterator>();

         if(!options.verify_files) {
            if(file_tree_walker.was_scan_completed()) {
               if(fit::complete_scan_record(scan_id.value(), file_scan_db.get()) != 1)
                  print_stream.warning("Cannot update completed time for scan %d", scan_id);
            }

            // at this point cumulative time will not reflect any activities performed while closing the database
            if(fit::update_scan_duration(options, scan_id.value(), std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start_time).count(), file_scan_db.get()) != 1)
               print_stream.warning("Cannot update cumulative duration for scan %d", scan_id);

            fit::close_sqlite_database(file_scan_db.release());
         }

         std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();

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
