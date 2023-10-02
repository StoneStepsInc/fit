#ifndef FIT_PRINT_STREAM_H
#define FIT_PRINT_STREAM_H

#include <mutex>
#include <cstdio>
#include <cstdarg>

#include <string>

#ifdef _MSC_VER
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

namespace fit {
struct u8tosv_t {
   std::string_view str;

   u8tosv_t(const std::u8string_view& str) : str(reinterpret_cast<const char*>(str.data()), str.length()) {}
   u8tosv_t(const std::u8string& str) : str(reinterpret_cast<const char*>(str.c_str()), str.length()) {}

   operator const std::string_view& (void) const {return str;}
};
}

template <>
struct FMTNS::formatter<fit::u8tosv_t, char> : FMTNS::formatter<std::string_view, char> {
      template<class format_context_t>
      auto format(const fit::u8tosv_t& u8tosv, format_context_t& fmtctx) const
      {
         return FMTNS::formatter<std::string_view, char>::format(u8tosv, fmtctx);
      }
};

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
