#include "file_tree_walker.h"

#include "fit.h"

#include <string>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <chrono>

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cinttypes>

namespace fit {

file_tree_walker_t::file_tree_walker_t(const options_t& options, int64_t scan_id, print_stream_t& print_stream) :
      options(options),
      print_stream(print_stream),
      scan_id(scan_id)
{
   for(size_t i = 0; i < options.thread_count; i++)
      file_hashers.emplace_back(options, scan_id, files, files_mtx, progress_info, print_stream);
}

void file_tree_walker_t::report_progress(void)
{
   if(!progress_info.updated_files) {
      print_stream.info("Processed %.1f GB in %" PRIu64 " files\n",
                        progress_info.processed_size.load()/1'000'000'000., progress_info.processed_files.load());
   }
   else {
      print_stream.info("Processed %.1f GB in %" PRIu64 " files (%" PRIu64 "/%.1f GB %s)\n",
                        progress_info.processed_size.load()/1'000'000'000., progress_info.processed_files.load(),
                        progress_info.updated_files.load(), progress_info.updated_size.load()/1'000'000'000.,
                        options.verify_files ? "changed" : "updated");
   }

   if(progress_info.failed_files)
      print_stream.info("Failed to process %" PRIu64 " files\n", progress_info.failed_files.load());
}

template <typename dir_iter_t>
void file_tree_walker_t::walk_tree(void)
{
   static const char *abort_message = "Aborting... Ctrl-C to kill (may render database unusable)\n";

   // start hasher threads
   for(size_t i = 0; i < file_hashers.size(); i++)
      file_hashers[i].start();

   uint64_t queued_files = 0;

   // set the report time a few seconds into the fiture
   std::chrono::steady_clock::time_point report_time = std::chrono::steady_clock::now() + std::chrono::seconds(options.progress_interval);

   for(const std::filesystem::directory_entry& dir_entry : dir_iter_t(options.scan_path)) {

      //
      // A symlink is also presented as a regular file and we want to
      // skip symbolic links because if their targets are under the
      // same base path, we will pick them up in the appropriate
      // file type and if they are outside of the base path, then
      // they should not be included at all (e.g. if a target is
      // on a different volume in a recursive scan or in a different
      // directory in a non-recursive scan).
      //
      if(!dir_entry.is_symlink() && dir_entry.is_regular_file()) {
         std::unique_lock<std::mutex> lock(files_mtx);

         files.push(dir_entry);

         queued_files++;

         //
         // If we reached the maximum queue size, let it process some of
         // the queue before piling up more files.
         //
         if(files.size() > MAX_FILE_QUEUE_SIZE) {
            while(!abort_scan && files.size() > (MAX_FILE_QUEUE_SIZE*3)/4) {
               lock.unlock();

               std::this_thread::sleep_for(std::chrono::milliseconds(100));

               // if we are past the report time, print current stats and compute the next report time
               if(options.progress_interval && std::chrono::steady_clock::now() > report_time) {
                  report_progress();
                  report_time = std::chrono::steady_clock::now() + std::chrono::seconds(options.progress_interval);
               }

               lock.lock();
            }
         }

         if(abort_scan) {
            print_stream.info(abort_message);
            break;
         }
      }
   }

   // wait for all file hasher threads to process all queued files
   while(!abort_scan && (progress_info.processed_files + progress_info.failed_files) != queued_files) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if(options.progress_interval && std::chrono::steady_clock::now() > report_time) {
         report_progress();
         report_time = std::chrono::steady_clock::now() + std::chrono::seconds(options.progress_interval);
      }

      if(abort_scan) {
         print_stream.info(abort_message);
         break;
      }
   }

   // tell all file hasher threads to stop
   for(size_t i = 0; i < file_hashers.size(); i++)
      file_hashers[i].stop();

   // and wait until they actually stop
   for(size_t i = 0; i < file_hashers.size(); i++)
      file_hashers[i].join();
}

uint64_t file_tree_walker_t::get_processed_files(void) const
{
   return progress_info.processed_files.load();
}

uint64_t file_tree_walker_t::get_processed_size(void) const
{
   return progress_info.processed_size.load();
}

uint64_t file_tree_walker_t::get_modified_files(void) const
{
   return progress_info.modified_files.load();
}

uint64_t file_tree_walker_t::get_new_files(void) const
{
   return progress_info.new_files.load();
}

uint64_t file_tree_walker_t::get_changed_files(void) const
{
   return progress_info.changed_files.load();
}

}

template void fit::file_tree_walker_t::walk_tree<std::filesystem::directory_iterator>(void);
template void fit::file_tree_walker_t::walk_tree<std::filesystem::recursive_directory_iterator>(void);
