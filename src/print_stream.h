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

template <>
struct FMTNS::formatter<std::u8string_view, char> : FMTNS::formatter<string_view, char> {
      template<class format_context_t>
      auto format(const std::u8string_view& str, format_context_t& fmtctx) const
      {
         return FMTNS::formatter<string_view, char>::format(std::string_view(reinterpret_cast<const char*>(str.data()), str.length()), fmtctx);
      }
};

template <>
struct FMTNS::formatter<std::u8string, char> : FMTNS::formatter<string_view, char> {
      template<class format_context_t>
      auto format(const std::u8string& str, format_context_t& fmtctx) const
      {
         return FMTNS::formatter<string_view, char>::format(std::string_view(reinterpret_cast<const char*>(str.c_str()), str.length()), fmtctx);
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
