#ifndef FIT_PRINT_STREAM_H
#define FIT_PRINT_STREAM_H

#include <mutex>
#include <cstdio>
#include <cstdarg>

namespace fit {

class print_stream_t {
   private:
      std::mutex     print_mtx;
      FILE           *print_stream;

   private:
      void print(FILE *strm, const char *prefix, const char *fmt, va_list valist);

   public:
      print_stream_t(FILE *print_stream);

      print_stream_t(print_stream_t&& other);

      ~print_stream_t(void);

      void info(const char *fmt, ...);

      void warning(const char *fmt, ...);

      void error(const char *fmt, ...);
};

}

#endif // FIT_PRINT_STREAM_H
