#ifndef FIT_PRINT_STREAM_H
#define FIT_PRINT_STREAM_H

#include <mutex>
#include <cstdio>
#include <cstdarg>

#include <string>

#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

namespace fit {
//
// Within this project std::string and std::string_view always
// carry UTF-8 strings, so we can just cast the character pointer,
// without applying any character encoding conversions.
//
struct u8tosv_t {
   std::string_view str;

   u8tosv_t(const std::u8string_view& str) : str(reinterpret_cast<const char*>(str.data()), str.length()) {}
   u8tosv_t(const std::u8string& str) : str(reinterpret_cast<const char*>(str.c_str()), str.length()) {}
};
}

//
// Define a formatter specialization using a lightweight string
// view wrapper that merely casts char8_t pointer to a char pointer.
// 
// Worth noting that VC++ 19.37 compiles if a formatter is
// specialized for std::u8string_view, but errors are reported
// for fmt and std in GCC v13, probably because a generic narrow
// character string cannot be interpreted without knowing the
// character encoding.
//
template <>
struct FMTNS::formatter<fit::u8tosv_t, char> : FMTNS::formatter<std::string_view, char> {
      template<class format_context_t>
      auto format(const fit::u8tosv_t& u8tosv, format_context_t& fmtctx) const
      {
         return FMTNS::formatter<std::string_view, char>::format(u8tosv.str, fmtctx);
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
