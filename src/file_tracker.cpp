#include "file_tracker.h"

#include "fit.h"

#ifdef NO_SSE_AVX
// this is intended for older Linux machines, but if used on Windows, add sha256/sha256.c into fit.vcxproj
extern "C" {
#include "sha256/sha256.h"
}
#endif

#ifdef _WIN32
#include <cwchar>    // for _wfopen
#endif

#include <stdexcept>
#include <chrono>
#include <algorithm>

#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

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
   bool operator () (const std::u8string& s1, const std::u8string& s2)
   {
      if(s1.empty() && s2.empty())
         return false;

      if(s1.length() != s2.length())
         return s1.length() < s2.length();

      // use simplified byte comparison because it is only used for comparing extensions of picture files
      return strcasecmp(reinterpret_cast<const char*>(s1.c_str()), reinterpret_cast<const char*>(s2.c_str())) < 0;
   }
};

#ifdef NO_SSE_AVX
static constexpr const size_t HASH_BIN_SIZE = SHA256_DIGEST_SIZE;
static constexpr const size_t HASH_HEX_SIZE = HASH_BIN_SIZE * 2;
static constexpr std::string_view HASH_TYPE = "SHA256";
#endif

file_tracker_t::file_tracker_t(const options_t& options, int64_t scan_id, std::queue<std::filesystem::directory_entry>& files, std::mutex& files_mtx, progress_info_t& progress_info, print_stream_t& print_stream) :
      options(options),
      print_stream(print_stream),
      scan_id(scan_id),
      hash_type(HASH_TYPE),
      file_buffer(new unsigned char[options.buffer_size]),
      files(files),
      files_mtx(files_mtx),
      progress_info(progress_info),
      EXIF_exts(parse_EXIF_exts(options)),
      exif_reader(options)
#ifndef NO_SSE_AVX
      , mb_hasher(*this, options.buffer_size, options.mb_hash_max)
#endif
{
   int errcode = SQLITE_OK;

   if((errcode = sqlite3_open_v2(reinterpret_cast<const char*>(options.db_path.u8string().c_str()), &file_scan_db, SQLITE_OPEN_READWRITE, nullptr)) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errstr(errcode));

   if(!set_sqlite_journal_mode(file_scan_db, print_stream))
      print_stream.warning("Cannot set SQlite journal mode to WAL (will run slower)");

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
   std::string_view sql_insert_file = "INSERT INTO files (name, ext, path) VALUES (?, ?, ?)"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_file.data(), (int) sql_insert_file.length()+1, &stmt_insert_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert a file (%s)", sqlite3_errstr(errcode));

   //
   // insert statement for new file version records                   1        2         3           4          5          6     7        8
   //
   std::string_view sql_insert_version = "INSERT INTO versions (file_id, version, mod_time, entry_size, read_size, hash_type, hash, exif_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_insert_version.data(), (int) sql_insert_version.length()+1, &stmt_insert_version, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to insert a file version (%s)", sqlite3_errstr(errcode));

   if((errcode = sqlite3_bind_text(stmt_insert_version, 6, hash_type.data(), static_cast<int>(hash_type.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      print_stream.error("Cannot bind a hash type for a SQLite statement to insert a file version (%s)", sqlite3_errstr(errcode));

   //
   // select statement to look-up files by their path
   // 
   // The results are sorted by version and then by scan_id to take
   // advantage of the (version_id, scan_id) index, which offers
   // better performance than (scan_id, version_id). Versions may
   // stay the same between subsequent scans, but a version cannot
   // increase without scan_id increasing as well (i.e. different
   // versions cannot have the same scan_id), so we always end up
   // with the latest version and scan_id at the top.
   // 
   // parameters:                                                                                                                                                                                                            1
   // columns:                                    0         1          2     3               4        5        6
   std::string_view sql_find_file = "SELECT version, mod_time, hash_type, hash, versions.rowid, file_id, scan_id FROM versions JOIN files ON file_id = files.rowid JOIN scansets ON version_id = versions.rowid WHERE path = ? ORDER BY version DESC, scan_id DESC LIMIT 1"sv;

   if((errcode = sqlite3_prepare_v2(file_scan_db, sql_find_file.data(), (int) sql_find_file.length()+1, &stmt_find_file, nullptr)) != SQLITE_OK)
      print_stream.error("Cannot prepare a SQLite statement to find a file version (%s)", sqlite3_errstr(errcode));

   //
   // insert statement for scanset file records                            1           2
   //
   std::string_view sql_insert_scanset_file = "INSERT INTO scansets (scan_id, version_id) VALUES (?, ?)"sv;

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
                                      "GPSSpeedRef,GPSSpeed,GPSDateStamp,XMPxmpRating,Exiv2Json) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?, ?, ?, ?, "
                                         "?, ?, ?, ?, ?)";

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
#ifndef NO_SSE_AVX
      , mb_hasher(*this, options.buffer_size, other.mb_hasher.max_jobs())
#endif
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

time_t file_tracker_t::file_time_to_time_t(const std::chrono::file_clock::time_point& file_time)
{
#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
   // works only in VC++ 19.37, GCC 13 and CLang 17
   // utc_clock epoch is 1970, even though the official UTC epoch is 1972
   // this might be more concise - std::chrono::file_clock::to_utc(mod_time_fc), but GCC 13 doesn't implement to_utc (plus use of generic clock_cast is encouraged)
   return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::utc_clock>(file_time).time_since_epoch()).count();
#else
   // VC++ 19.37 does not implement to_sys and GCC 13 does not implement to_utc
   // note that system_clock epoch is not required to be 1970, even though many implementations do so, so must be explicitly converted to time_t
   return std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(file_time));
#endif
}

std::vector<std::u8string> file_tracker_t::parse_EXIF_exts(const options_t& options)
{
   std::vector<std::u8string> EXIF_exts;

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

bool file_tracker_t::set_sqlite_journal_mode(sqlite3 *file_scan_db, print_stream_t& print_stream)
{
   int errcode = SQLITE_OK;

   sqlite_stmt_t journal_mode_stmt("PRAGMA journal_mode=WAL"sv);

   if((errcode = journal_mode_stmt.prepare(file_scan_db, "PRAGMA journal_mode=WAL;"sv)) != SQLITE_OK) {
      print_stream.warning("Cannot prepare a SQLite statement for setting journal mode to WAL (%s)", sqlite3_errstr(errcode));
      return false;
   }
      
   errcode = sqlite3_step(journal_mode_stmt);

   const char *journal_mode = nullptr;

   if(errcode == SQLITE_ROW)
      journal_mode = reinterpret_cast<const char*>(sqlite3_column_text(journal_mode_stmt, 0));

   bool have_wal = journal_mode && !strcmp(journal_mode, "wal");

   if((errcode = journal_mode_stmt.finalize()) != SQLITE_OK)
      print_stream.warning("Cannot finalize SQLite statment for setting journal mode to WAL (%s)", sqlite3_errstr(errcode));

   return have_wal;
}

#ifdef NO_SSE_AVX
void file_tracker_t::hash_file(const std::filesystem::path& filepath, uint64_t& filesize, unsigned char hexhash[])
{
   #ifdef _WIN32
   std::unique_ptr<FILE, file_handle_deleter_t> file(_wfopen(filepath.wstring().c_str(), L"rb"));
   #else
   std::unique_ptr<FILE, file_handle_deleter_t> file(fopen(reinterpret_cast<const char*>(filepath.u8string().c_str()), "rb"));
   #endif

   if(!file)
      throw std::runtime_error("Cannot open file (" + std::string(strerror(errno)) + ") " + filepath.u8string());

   sha256_t ctx;
   sha256_init(&ctx);

   filesize = 0;

   size_t lastread = 0;

   while((lastread = std::fread(file_buffer.get(), 1, options.buffer_size, file.get())) != 0) {
      sha256_update(&ctx, file_buffer.get(), lastread);
      filesize += lastread;
   }

   if(std::ferror(file.get()))
      throw std::runtime_error("Cannot read file (" + std::string(strerror(errno)) + ") " + filepath.u8string());

   // hash for zero-length files should not be evaluated
   if(filesize) {
      unsigned char filehash[HASH_BIN_SIZE];

      sha256_final(&ctx, filehash);

      static const char hex[] = "0123456789abcdef";

      for(size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
         hexhash[i*2] = hex[(*(filehash+i) & 0xF0) >> 4];
         hexhash[i*2+1] = hex[*(filehash+i) & 0x0F];
      }
   }
}
#else
file_tracker_t::mb_file_hasher_t::param_tuple_t file_tracker_t::open_file(find_file_result_t&& version_record, std::filesystem::directory_entry&& dir_entry) const
{
   //
   // The narrow character version of fopen will fail to open files
   // with names containing characters that cannot be converted to
   // the current Windows character set. Use the non-standard
   // _wfopen to work around this problem.
   //
   #ifdef _WIN32
   std::unique_ptr<FILE, file_handle_deleter_t> file(_wfopen(dir_entry.path().wstring().c_str(), L"rb"));
   #else
   std::unique_ptr<FILE, file_handle_deleter_t> file(fopen(reinterpret_cast<const char*>(dir_entry.path().u8string().c_str()), "rb"));
   #endif

   if(!file)
      throw std::runtime_error(FMTNS::format("Cannot open file ({:s}) {:s}", strerror(errno), u8tosv_t(dir_entry.path().u8string())));

   //
   // Need to make sure the reference is preserved, so we update the
   // actual file size value passed into this function.
   // 
   return std::make_tuple(std::move(file), 0, std::move(version_record), std::move(dir_entry), std::nullopt);
}

bool file_tracker_t::read_file(unsigned char *file_buffer, size_t buf_size, size_t& data_size, mb_file_hasher_t::param_tuple_t& args) const noexcept
{
   try {
      std::unique_ptr<FILE, file_handle_deleter_t>& file = std::get<0>(args);
      uint64_t& file_size = std::get<1>(args);

      data_size = std::fread(file_buffer, 1, buf_size, file.get());

      if(std::ferror(file.get()))
         throw std::runtime_error(FMTNS::format("Cannot read file ({:s}) {:s} ", strerror(errno), u8tosv_t(std::get<3>(args).path().u8string())));

      file_size += data_size;

      return feof(file.get()) == 0;
   }
   catch (const std::exception& error) {
      // std::optional<file_read_error_t>
      std::get<4>(args).emplace(error.what());
   }
   catch (...) {
      std::get<4>(args).emplace(FMTNS::format("Unexpected error caught while reading {:s}", u8tosv_t(std::get<3>(args).path().u8string())));
   }

   // indicate that we didn't read anything
   data_size = 0;

   // reset the number of bytes read so far because it is irrelevant and misleading in this case
   std::get<1>(args) = 0;

   return false;
}
#endif      

int64_t file_tracker_t::insert_file_record(const std::u8string& filepath, const std::filesystem::directory_entry& dir_entry)
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
      throw std::runtime_error(FMTNS::format("Cannot insert a file record for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));

   file_id = sqlite3_last_insert_rowid(file_scan_db);

   insert_file_stmt.reset();

   return file_id;
}

int64_t file_tracker_t::insert_exif_record(const std::u8string& filepath, const std::vector<exif::field_value_t>& exif_fields, const exif::field_bitset_t& field_bitset)
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
            throw std::runtime_error(FMTNS::format("Bad field value in EXIF record for {:s} ({:d})", u8tosv_t(filepath), i));
      }
   }

   if((errcode = sqlite3_step(stmt_insert_exif)) != SQLITE_DONE)
      throw std::runtime_error(FMTNS::format("Cannot insert an EXIF record for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));

   // get the row ID for the new EXIF record
   exif_id = sqlite3_last_insert_rowid(file_scan_db);

   insert_exif_stmt.reset();

   return exif_id;
}

file_tracker_t::find_file_result_t file_tracker_t::select_version_record(const std::u8string& filepath) const
{
   int errcode = SQLITE_OK;

   unsigned char hexhash_field[HASH_HEX_SIZE + 1] = {};      // database hash; should no be accessed if hash_field_is_null is false

   //
   // Attempt to find the file by its relative path first. For
   // files with multiple versions, always select the latest
   // version.
   //
   sqlite_stmt_binder_t find_file_stmt(stmt_find_file, "find file"sv);

   find_file_stmt.bind_param(filepath);

   errcode = sqlite3_step(stmt_find_file);

   if(errcode != SQLITE_DONE && errcode != SQLITE_ROW)
      throw std::runtime_error(FMTNS::format("SQLite select failed for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));

   // if we found a file record, get the columns we need
   if(errcode != SQLITE_ROW)
      return find_file_result_t{std::nullopt};

   int64_t version = sqlite3_column_int64(stmt_find_file, 0);

   int64_t mod_time = sqlite3_column_int64(stmt_find_file, 1);

   std::string hash_type(reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 2)), sqlite3_column_bytes(stmt_find_file, 2));

   bool hash_field_is_null = sqlite3_column_type(stmt_find_file, 3) == SQLITE_NULL;

   int64_t version_id = sqlite3_column_int64(stmt_find_file, 4);

   int64_t file_id = sqlite3_column_int64(stmt_find_file, 5);

   int64_t scanset_scan_id = sqlite3_column_int64(stmt_find_file, 6);

   if(!hash_field_is_null) {
      // hold onto the column hash value
      if(hash_type == HASH_TYPE) {
         if(sqlite3_column_bytes(stmt_find_file, 3) != HASH_HEX_SIZE)
            throw std::runtime_error(FMTNS::format("Bad hash size for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));

         memcpy(hexhash_field, reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 3)), HASH_HEX_SIZE);
      }
      else
         throw std::runtime_error("Unknown hash type: "s + reinterpret_cast<const char*>(sqlite3_column_text(stmt_find_file, 2)));
   }

   //
   // Reset the select statement to release read locks acquired above
   // before starting a transaction. Without this step, some insert
   // statements will fail with SQLITE_BUSY because of possible
   // resource deadlocks, which are reported immediately and without
   // calling the busy handler.
   //
   find_file_stmt.reset();

   return find_file_result_t{std::make_tuple(
            version,
            mod_time,
            hash_type,
            (!hash_field_is_null ? std::make_optional<std::string>(reinterpret_cast<char*>(hexhash_field)) : std::nullopt),
            version_id,
            file_id,
            scanset_scan_id)};
}

int64_t file_tracker_t::insert_version_record(const std::u8string& filepath, int64_t file_id, int64_t version, int64_t filesize, const std::filesystem::directory_entry& dir_entry, unsigned char hexhash_file[], std::optional<int64_t> exif_id)
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
   insert_version_stmt.bind_param(static_cast<int64_t>(file_time_to_time_t(dir_entry.last_write_time())));

   insert_version_stmt.bind_param(static_cast<int64_t>(dir_entry.file_size()));
   insert_version_stmt.bind_param(static_cast<int64_t>(filesize));

   // hash_type
   insert_version_stmt.skip_param();

   // insert a NULL for a zero-length file
   if(!filesize)
      insert_version_stmt.bind_param(nullptr);
   else
      insert_version_stmt.bind_param(std::u8string_view(reinterpret_cast<const char8_t*>(hexhash_file), HASH_HEX_SIZE));

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
      throw std::runtime_error(FMTNS::format("Cannot insert a version record for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));

   // get the row ID for the new version record
   version_id = sqlite3_last_insert_rowid(file_scan_db);

   insert_version_stmt.reset();

   return version_id;
}

void file_tracker_t::insert_scanset_record(const std::u8string& filepath, int64_t version_id)
{
   int errcode = SQLITE_OK;

   sqlite_stmt_binder_t insert_scanset_file_stmt(stmt_insert_scanset_file, "insert scanset file"sv);

   // scan_id
   insert_scanset_file_stmt.skip_param();

   insert_scanset_file_stmt.bind_param(version_id);

   if((errcode = sqlite3_step(stmt_insert_scanset_file)) != SQLITE_DONE)
      throw std::runtime_error(FMTNS::format("Cannot insert a scanset record for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));

   insert_scanset_file_stmt.reset();
}

void file_tracker_t::begin_transaction(const std::u8string& filepath)
{
   int errcode = sqlite3_step(stmt_begin_txn);

   if(errcode != SQLITE_DONE)
      throw std::runtime_error(FMTNS::format("Cannot start a SQLite transaction for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));
}

void file_tracker_t::commit_transaction(const std::u8string& filepath)
{
   int errcode = sqlite3_step(stmt_commit_txn);

   if(errcode != SQLITE_DONE)
      throw std::runtime_error(FMTNS::format("Cannot commit a SQLite transaction for {:s} ({:s})", u8tosv_t(filepath), sqlite3_errstr(errcode)));
}

void file_tracker_t::rollback_transaction(const std::u8string& filepath)
{
   int errcode = sqlite3_step(stmt_rollback_txn);

   if(errcode != SQLITE_DONE)
      print_stream.error("Cannot rollback a SQLite transaction for %s (%s)", filepath.c_str(), sqlite3_errstr(errcode));
}

void file_tracker_t::run(void)
{
   int errcode = SQLITE_OK;

   // a file path string buffer
   std::u8string filepath;

   filepath.reserve(1024);

   // a file path for querying with an alternative path separator character
   std::u8string filepath_query;

   if(options.query_path_sep.has_value())
      filepath_query.reserve(1024);
               
   unsigned char hexhash_file[HASH_HEX_SIZE + 1] = {};      // file hash; should not be accessed if filesize == 0

   std::unique_lock<std::mutex> files_lock(files_mtx);

   while(!stop_request) {
      bool hash_match = false;                              // if true, the file didn't change; if false, a new version will be created

      std::optional<std::filesystem::directory_entry> dir_entry;

      find_file_result_t version_record;

      if(files.empty()) {
#ifdef NO_SSE_AVX
         if(true) {
#else
         if(!mb_hasher.active_jobs()) {
#endif
            files_lock.unlock();

            //
            // We don't expect this thread to idle much and don't need
            // a condition variable here. It will reach this code only
            // after it starts and right before the final few files
            // are being processed. At all other times it will find
            // files in the queue.
            //
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            files_lock.lock();
            continue;
         }
      }
      else {
         dir_entry = std::move(files.front());
         files.pop();
      }

      files_lock.unlock();

      try {
         uint64_t filesize = 0;

         // dir_entry will be empty when we are finalizing last few hash jobs
         if(dir_entry.has_value()) {
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
            try {
            if(options.base_path.empty())
               filepath = dir_entry.value().path().u8string();
            else
               filepath = dir_entry.value().path().lexically_relative(options.base_path).u8string();
            }
            catch (const std::exception& error) {
               progress_info.failed_files++;

               //
               // Windows may store file paths with invalid UTF-16 characters,
               // which fail to convert to UTF-8 paths. For example, a file
               // path may have a UCS-2 code that represents and incomplete
               // surrogate pair, such as `\xD83D`, which cannot be converted
               // to a UTF-8 string.
               //
               // Use a crude ASCII conversion and replace all non-ASCII
               // characters with `?`, just to identify which file we
               // couldn't process.
               //
               filepath.clear();

               std::u16string filepath_u16 = dir_entry.value().path().u16string();
               std::transform(filepath_u16.begin(), filepath_u16.end(),
                                 std::back_inserter(filepath),
                                 [] (char16_t chr) -> char {return (chr >= u' ' && chr < u'\x7f' ? static_cast<char>(chr) : '?');});

               print_stream.error("Cannot convert path to UTF-8 (%s) %s", error.what(), filepath.c_str());
               files_lock.lock();
               continue;
            }

            if(options.query_path_sep.has_value() && options.query_path_sep.value() != std::filesystem::path::preferred_separator) {
               // tests show that it's ~15% faster to copy a string and then replace a few characters vs. copying a character at a time (i.e. copy is done a few words at a time)
               filepath_query = filepath;
               std::for_each(filepath_query.begin(), filepath_query.end(), [this](char8_t& pc) {if(pc == std::filesystem::path::preferred_separator) pc = options.query_path_sep.value();});
            }

            version_record = select_version_record(!options.query_path_sep.has_value() ? filepath : filepath_query);
         }

         //
         // Skip all file processing if we are updating the last scanset and
         // found a file version record with the same scan number as we have
         // in this file tracker instance, which is set to the last scan
         // number if options.update_last_scanset is true.
         //
         if(!options.update_last_scanset || !version_record.has_value() || version_record.scanset_scan_id() != scan_id) {
            //
            // Check if we were asked to skip hashing based on the file
            // modification time being unchanged from what was recorded
            // in the database.
            // 
            if(dir_entry.has_value()) {
               if(!options.verify_files && options.skip_hash_mod_time && version_record.has_value()
                     && version_record.mod_time() == static_cast<int64_t>(file_time_to_time_t(dir_entry.value().last_write_time()))) {
                  hash_match = true;
               }
            }

            // a non-optional to allow version+1 whether version_record has a value or not
            int64_t version = 0;

            std::optional<int64_t> version_id;        // version record identifier (either from the database or a newly inserted one)
            std::optional<int64_t> file_id;           // a file identifier (same as version_id)
            std::optional<int64_t> exif_id;           // an EXIF data identifier (same as version_id)

            //
            // Need to set these for options.skip_hash_mod_time==true, when
            // we avoid hashing, so they are never restored after multi-buffer
            // hashing nor inserted because hash_match==true.
            //
            if(version_record.has_value()) {
               version = version_record.version();
               version_id = version_record.version_id();
               file_id = version_record.file_id();
            }

            if(!hash_match) {
#ifdef NO_SSE_AVX
               hash_file(dir_entry.value().path(), filesize, hexhash_file);
#else
               // check if we have a new file to submit for hashing (otherwise we are finalizing last few hash jobs)
               if(dir_entry.has_value()) {
                  //
                  // Submit a parallel hash job for this file and pack all data we
                  // gathered for this file as job arguments, so we can restore them
                  // when we get hashes in a different order. Note that version_record
                  // will be empty for new files, which is expected.
                  //
                  try {
                     mb_hasher.submit_job(&file_tracker_t::open_file, &file_tracker_t::read_file, std::move(version_record), std::move(dir_entry).value());
                  }
                  catch (const std::exception& error) {
                     progress_info.failed_files++;

                     //
                     // If we failed to open a file, the hash job slot remains available
                     // and we can just continue with the next file. The error is expected
                     // to be self-descriptive, so we don't have to provide more context
                     // here.
                     //
                     print_stream.error("%s", error.what());
                     files_lock.lock();
                     continue;
                  }

                  // if the multi-buffer hasher can accept more parallel jobs, get another file
                  if(mb_hasher.available_jobs() > 0) {
                     files_lock.lock();
                     continue;
                  }
               }

               uint32_t isa_mb_hash[mb_file_hasher_t::traits::HASH_UINT32_SIZE];

               std::optional<mb_file_hasher_t::param_tuple_t> args = mb_hasher.get_hash(isa_mb_hash);

               // close the file handle explicitly to avoid keeping it open while handling hashing results
               std::get<0>(args.value()).reset();

               // if we got an error while reading this file, report the error, discard results and continue to the next file
               if(std::get<4>(args.value()).has_value()) {
                  progress_info.failed_files++;

                  // same as when calling mb_hasher.submit_job
                  print_stream.error("%s", std::get<4>(args.value()).value().error.c_str());
                  files_lock.lock();
                  continue;
               }

               filesize = std::get<1>(args.value());

               // hash for zero-length files should not be evaluated
               if(filesize)
                  mb_file_hasher_t::isa_mb_hash_to_hex(isa_mb_hash, hexhash_file);

               // restore the version record and directory entry to continue the loop interrupted by queuing hash jobs
               version_record = std::move(std::get<2>(args.value()));

               if(version_record.has_value()) {
                  version = version_record.version();

                  version_id = version_record.version_id();

                  file_id = version_record.file_id();
               }

               dir_entry = std::move(std::get<3>(args.value()));

               // invalid UCS-2 code points have been filtered out above
               if(options.base_path.empty())
                  filepath = dir_entry.value().path().u8string();
               else
                  filepath = dir_entry.value().path().lexically_relative(options.base_path).u8string();
#endif

               // consider a NULL hash field as a match for zero-length files
               hash_match = version_record.has_value() && ((filesize == 0 && !version_record.hexhash().has_value()) || memcmp(hexhash_file, version_record.hexhash().value().data(), HASH_HEX_SIZE) == 0);
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
               begin_transaction(filepath);
            }

            // if there is no file record for this path, insert one to get a file ID
            if(!options.verify_files && !file_id.has_value())
               file_id = insert_file_record(filepath, dir_entry.value());

            // if there's no version record or the hash didn't match, we need to insert a new one
            if(!hash_match) {
               // handle the mismatched hash based on whether we are verifying or scanning
               if(options.verify_files) {
                  // differentiate between new, modified and changed files
                  if(!version_record.has_value()) {
                     progress_info.new_files++;
                     print_stream.info(   "new file: %s", filepath.c_str());
                  }
                  else {
                     if(version_record.mod_time() != static_cast<int64_t>(file_time_to_time_t(dir_entry.value().last_write_time()))) {
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
                  if(!EXIF_exts.empty() && !dir_entry.value().path().extension().empty()) {
                     if(std::binary_search(EXIF_exts.begin(), EXIF_exts.end(), dir_entry.value().path().extension().u8string(), less_ci())) {
                        // filepath will contain a relative path if base path was specified
                        exif::field_bitset_t field_bitset = exif_reader.read_file_exif(dir_entry.value().path(), print_stream);

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
                  version_id = insert_version_record(filepath, file_id.value(), version+1, filesize, dir_entry.value(), hexhash_file, exif_id);
               }

               // reuse for updated files during scans and for mismatched files during verification
               progress_info.updated_files++;
               progress_info.updated_size += options.skip_hash_mod_time ? dir_entry.value().file_size() : filesize;
            }

            if(!options.verify_files) {
               // insert a scanset record with the new or existing version ID
               insert_scanset_record(filepath, version_id.value());

               commit_transaction(filepath);
            }
         }

         //
         // Update stats for processed files
         //
         progress_info.processed_files++;
         progress_info.processed_size += options.skip_hash_mod_time ? dir_entry.value().file_size() : filesize;
      }
      catch (const std::exception& error) {
         progress_info.failed_files++;

         //
         // filepath is empty when the directory entry has a path that
         // cannot be converted to UTF-8. Use a crude ASCII conversion
         // and replace all non-ASCII characters with `?`, just to
         // identify which file we couldn't process.
         //
         if(filepath.empty()) {
            std::u16string filepath_u16 = dir_entry.value().path().u16string();
            std::transform(filepath_u16.begin(), filepath_u16.end(),
                              std::back_inserter(filepath),
                              [] (char16_t chr) -> char {return (chr >= u' ' && chr < u'\x7f' ? static_cast<char>(chr) : '?');});
         }

         print_stream.error("Cannot process file %s (%s)", filepath.c_str(), error.what());

         // if we started a transaction, roll it back
         if(!sqlite3_get_autocommit(file_scan_db)) {
            rollback_transaction(filepath);
         }
      }

      // need to lock to access the queue
      files_lock.lock();
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

#ifndef NO_SSE_AVX
#include "mb_hasher_tmpl.cpp"
#endif
