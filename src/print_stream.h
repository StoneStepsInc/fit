#ifndef FIT_PRINT_STREAM_H
#define FIT_PRINT_STREAM_H

#include "format.h"

#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <iostream>

#include <string>

namespace fit {

class print_stream_t {
   private:
      std::mutex     print_mtx;
      std::ofstream  print_stream;

   private:
      void print(std::basic_ostream<char>& stream, const char* prefix, const std::string_view& fmt, std::format_args args);

   public:
      print_stream_t(std::ofstream&& print_stream);

      print_stream_t(print_stream_t&& other);

      ~print_stream_t(void);

      template <typename... Args>
      void info(const std::format_string<Args...>& fmt, Args&&... args)
      {
         print(std::cout, "inf", fmt.get(), std::make_format_args(args...));
      }

      template <typename... Args>
      void warning(const std::format_string<Args...>& fmt, Args&&... args)
      {
         print(std::cout, "wrn", fmt.get(), std::make_format_args(args...));
      }

      template <typename... Args>
      void error(const std::format_string<Args...>& fmt, Args&&... args)
      {
         print(std::cout, "err", fmt.get(), std::make_format_args(args...));
      }
};

}

#endif // FIT_PRINT_STREAM_H
