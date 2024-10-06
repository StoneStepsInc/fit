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

// defined in fit.cpp
extern std::atomic<bool> abort_scan;

file_tree_walker_t::file_tree_walker_t(const options_t& options, std::optional<int64_t>& scan_id, std::optional<int64_t>& base_scan_id, print_stream_t& print_stream) :
      options(options),
      print_stream(print_stream),
      scan_id(scan_id),
      base_scan_id(base_scan_id)
{
   for(size_t i = 0; i < options.thread_count; i++)
      file_trackers.emplace_back(options, scan_id, base_scan_id, files, files_mtx, progress_info, print_stream);
}

void file_tree_walker_t::initialize(print_stream_t& print_stream)
{
   file_tracker_t::initialize(print_stream);
}

void file_tree_walker_t::cleanup(print_stream_t& print_stream) noexcept
{
   file_tracker_t::cleanup(print_stream);
}

void file_tree_walker_t::report_progress(void)
{
   if(!progress_info.updated_files) {
      print_stream.info("Processed %.1f GB in %" PRIu64 " files",
                        progress_info.processed_size.load()/1'000'000'000., progress_info.processed_files.load());
   }
   else {
      print_stream.info("Processed %.1f GB in %" PRIu64 " files (%" PRIu64 "/%.1f GB %s)",
                        progress_info.processed_size.load()/1'000'000'000., progress_info.processed_files.load(),
                        progress_info.updated_files.load(), progress_info.updated_size.load()/1'000'000'000.,
                        options.verify_files ? "changed" : "updated");
   }

   if(progress_info.failed_files)
      print_stream.info("Failed to process %" PRIu64 " files", progress_info.failed_files.load());
}

template <typename dir_iter_t>
void file_tree_walker_t::walk_tree(void)
{
   static const char *abort_message = "Aborting... Ctrl-C to kill (may render database unusable)";
   static const char *enum_files_error_msg = "Cannot enumerate files";

   // start hasher threads
   for(size_t i = 0; i < file_trackers.size(); i++)
      file_trackers[i].start();

   uint64_t queued_files = 0;

   // set the report time a few seconds into the fiture
   std::chrono::steady_clock::time_point report_time = std::chrono::steady_clock::now() + std::chrono::seconds(options.progress_interval);

   try {
      std::filesystem::directory_options dir_it_opts = options.skip_no_access_paths ? std::filesystem::directory_options::skip_permission_denied : std::filesystem::directory_options::none;

      for(const std::filesystem::path& scan_path : options.scan_paths) {
         print_stream.info("%s \"%s\"", options.verify_files ? "Verifying" : "Scanning", scan_path.u8string().c_str());

         for(const std::filesystem::directory_entry& dir_entry : dir_iter_t(scan_path, dir_it_opts)) {
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
               std::unique_lock<std::mutex> files_lock(files_mtx);

               files.push(dir_entry);

               queued_files++;

               //
               // If we reached the maximum queue size, let it process some of
               // the queue before piling up more files.
               //
               if(files.size() > MAX_FILE_QUEUE_SIZE) {
                  while(!abort_scan && files.size() > (MAX_FILE_QUEUE_SIZE*3)/4) {
                     files_lock.unlock();

                     std::this_thread::sleep_for(std::chrono::milliseconds(100));

                     // if we are past the report time, print current stats and compute the next report time
                     if(options.progress_interval && std::chrono::steady_clock::now() > report_time) {
                        report_progress();
                        report_time = std::chrono::steady_clock::now() + std::chrono::seconds(options.progress_interval);
                     }

                     files_lock.lock();
                  }
               }

               if(abort_scan) {
                  interrupted_scan = true;
                  print_stream.info(abort_message);
                  break;
               }
            }
         }
      }
   }
   catch (const std::filesystem::filesystem_error& error) {
      //
      // The message returned from what() is not very informative
      // on Windows and currently looks like this:
      // 
      //    recursive_directory_iterator::operator++: Access is denied.
      // 
      // Both error paths are populated for two-argument commands,
      // such as `rename`, and for iterating directories none of them
      // is populated currently, but in case if it changes, print the
      // first one that is not empty.
      // 
      // For access-denied errors, there may be nothing helpful we can
      // report because the iterator contains no value after a failed
      // increment call (even if the overload that takes error_code is
      // called - all we get back is the end iterator).
      //
      if(error.path1().empty() && error.path2().empty())
         print_stream.error("Cannot queue a file, %s (%s)", enum_files_error_msg, error.code().message().c_str());
      else if(!error.path1().empty())
         print_stream.error("Cannot queue a file, %s (%s) for %s", enum_files_error_msg, error.code().message().c_str(), error.path1().u8string().c_str());
      else
         print_stream.error("Cannot queue a file, %s (%s) for %s", enum_files_error_msg, error.code().message().c_str(), error.path2().u8string().c_str());
   }
   catch (const std::exception& error) {
      print_stream.error("Cannot queue a file, %s (%s)\n", enum_files_error_msg, error.what());
   }

   // wait for all file hasher threads to process all queued files
   while(!abort_scan && (progress_info.processed_files + progress_info.failed_files) != queued_files) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if(options.progress_interval && std::chrono::steady_clock::now() > report_time) {
         report_progress();
         report_time = std::chrono::steady_clock::now() + std::chrono::seconds(options.progress_interval);
      }

      if(abort_scan) {
         interrupted_scan = true;
         print_stream.info(abort_message);
         break;
      }
   }

   // tell all file hasher threads to stop
   for(size_t i = 0; i < file_trackers.size(); i++)
      file_trackers[i].stop();

   // and wait until they actually stop
   for(size_t i = 0; i < file_trackers.size(); i++)
      file_trackers[i].join();
}

bool file_tree_walker_t::was_scan_completed(void) const
{
   return !interrupted_scan;
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
