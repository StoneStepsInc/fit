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
   bool skip_hash_mod_time = false;
   bool skip_no_access_paths = false;
   bool update_last_scanset = false;
   bool exiv2_json = false;

   std::string scan_message;
   std::string log_file;
   std::optional<std::string> EXIF_exts;

#ifndef NO_SSE_AVX
   std::optional<size_t> mb_hash_max;
#endif

   size_t thread_count = 4;
   size_t buffer_size = 65536;

   int progress_interval = 10;

   std::string all;
};

}

#endif // FIT_H

