#ifndef FIT_FORMAT_H
#define FIT_FORMAT_H

#include <string>
#include <cstdint>
#include <chrono>

// cppreference.com reports that GCC v13.3 has a bug in computing the result of std::format_to_n
#if defined(_MSC_VER) || (defined(__GNUC__) && (__GNUC__ >= 14 || (__GNUC__ == 13 && __GNUC_MINOR__ > 3)))
#include <format>
#define FMTNS std

// std::format_string -> std::string_view
#define FMTSV(fmt) fmt.get()

// std::format allows custom formatters to operate on std::u8string
#define u8sv(x) x

//
// Within this project std::string and std::string_view always
// carry UTF-8 strings, so we can just cast the character pointer,
// without applying any character encoding conversions.
//
template <>
struct std::formatter<std::u8string, char> : std::formatter<std::string_view, char> {
      std::format_context::iterator format(const std::u8string& u8str, std::format_context& fmtctx) const
      {
         return std::formatter<std::string_view, char>::format(std::string_view(reinterpret_cast<const char*>(u8str.data()), u8str.size()), fmtctx);
      }
};

#else
#include <fmt/format.h>
#define FMTNS fmt

// fmt::format_string -> fmt::string_view
#define FMTSV(fmt) fmt

//
// {fmt} explicitly blocks std::u8string from being used in custom
// formatters if it's used with the `const char*` format string,
// reporting that character types cannot be mismatched between the
// two. This class wraps a standard string view pointing to the
// original UTF-8 string, so we can format UTF-8 strings with a
// custom formatter. Also, see the note above std::formatter above.
//
struct u8sv {
    std::string_view sv;
    u8sv(const std::u8string& str) : sv(reinterpret_cast<const char*>(str.data()), str.size()) {}
};

template <>
struct fmt::formatter<u8sv, char> : fmt::formatter<std::string_view, char> {
      template<class format_context_t>
      auto format(const u8sv& u8str, format_context_t& fmtctx) const
      {
         return fmt::formatter<std::string_view, char>::format(u8str.sv, fmtctx);
      }
};
#endif

namespace fit {

std::string hr_bytes(uint64_t num);

std::string hr_time(std::chrono::steady_clock::duration elapsed);

}

#endif // FIT_FORMAT_H
