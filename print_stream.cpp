#include "print_stream.h"

#include <ctime>

namespace fit {

print_stream_t::print_stream_t(FILE *print_stream) :
      print_stream(print_stream)
{
}

print_stream_t::print_stream_t(print_stream_t&& other) :
      print_stream(other.print_stream)
{
   other.print_stream = nullptr;
}

print_stream_t::~print_stream_t(void)
{
}

void print_stream_t::print(FILE *stream, const char *prefix, const char *fmt, va_list valist)
{
   std::lock_guard<std::mutex> lock(print_mtx);

   va_list ap;

   va_copy(ap, valist);
   vfprintf(stream, fmt, valist);
   va_end(ap);

   fputc('\n', stream);

   if(print_stream) {
      char tstamp[] = "2022-04-15 00:00:00";
      time_t now = time(nullptr);
      strftime(tstamp, sizeof(tstamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

      fputs(tstamp, print_stream);
      fputs(" [", print_stream);
      fputs(prefix, print_stream);
      fputs("] ", print_stream);

      va_copy(ap, valist);
      vfprintf(print_stream, fmt, valist);
      va_end(ap);

      fputc('\n', print_stream);
   }
}

void print_stream_t::info(const char *fmt, ...)
{
   va_list valist;

   va_start(valist, fmt);
   print(stdout, "inf", fmt, valist);
   va_end(valist);
}

void print_stream_t::warning(const char *fmt, ...)
{
   va_list valist;

   va_start(valist, fmt);
   print(stdout, "wrn", fmt, valist);
   va_end(valist);
}

void print_stream_t::error(const char *fmt, ...)
{
   va_list valist;

   va_start(valist, fmt);
   print(stderr, "err", fmt, valist);
   va_end(valist);
}

}
