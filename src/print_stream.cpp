#include "print_stream.h"

#include <ctime>
#include <iterator>

namespace fit {

print_stream_t::print_stream_t(std::ofstream&& print_stream) :
      print_stream(std::move(print_stream))
{
}

print_stream_t::print_stream_t(print_stream_t&& other) :
      print_stream(std::move(other.print_stream))
{
}

print_stream_t::~print_stream_t(void)
{
}

void print_stream_t::print(std::basic_ostream<char>& stream, const char* prefix, const FMTNS::string_view& fmt, FMTNS::format_args args)
{
   std::unique_lock lock(print_mtx);

   FMTNS::vformat_to(std::ostreambuf_iterator<char>(stream), fmt, args);
   stream.put('\n');

   if(print_stream) {
      char tstamp[32];
      time_t now = time(nullptr);
      strftime(tstamp, sizeof(tstamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

      // 1234567890123456789
      // 2025-11-29 12:34:56
      print_stream.write(tstamp, 19);
      print_stream.write(" [", 2);
      print_stream.write(prefix, 3);
      print_stream.write("] ", 2);

      FMTNS::vformat_to(std::ostreambuf_iterator<char>(print_stream), fmt, args);
      print_stream.put('\n');
   }

   lock.unlock();

   // flush log file on one thread to allow tailing it from another session
   if(print_stream && lock.try_lock())
      print_stream.flush();
}

}
