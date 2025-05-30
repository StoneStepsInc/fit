#ifndef FIT_H
#define FIT_H

#include <string>
#include <filesystem>
#include <optional>
#include <vector>

#include <cstdlib>
#include <cstdint>

namespace fit {

struct file_handle_deleter_t {
   void operator () (FILE *file)
   {
      std::fclose(file);
   }
};

//
// Command line options and their values.
//
struct options_t {
   std::filesystem::path base_path;
   std::filesystem::path db_path;

   std::vector<std::filesystem::path> scan_paths;

   bool recursive_scan = false;
   bool verify_files = false;
   bool print_usage = false;
   bool skip_no_access_paths = false;
   bool update_last_scanset = false;
   bool exiv2_json = false;
   bool upgrade_schema_to_v60 = false;

   std::optional<int> verify_scan_id;
   std::optional<char8_t> query_path_sep;

   std::u8string scan_message;
   std::u8string log_file;
   std::optional<std::u8string> EXIF_exts;

#ifndef NO_SSE_AVX
   size_t mb_hash_max = 8;
#endif

   size_t thread_count = 4;
   size_t buffer_size = 512*1024;

   int progress_interval = 10;

   std::u8string all;
};

}

#endif // FIT_H

