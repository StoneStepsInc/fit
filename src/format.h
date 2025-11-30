#ifndef FIT_FORMAT_H
#define FIT_FORMAT_H

#include <string>
#include <cstdint>
#include <chrono>

#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

//
// Within this project std::string and std::string_view always
// carry UTF-8 strings, so we can just cast the character pointer,
// without applying any character encoding conversions.
//
template <>
struct FMTNS::formatter<std::u8string, char> : FMTNS::formatter<std::string_view, char> {
      template<class format_context_t>
      auto format(const std::u8string& u8str, format_context_t& fmtctx) const
      {
         return FMTNS::formatter<std::string_view, char>::format(std::string_view(reinterpret_cast<const char*>(u8str.data()), u8str.size()), fmtctx);
      }
};

namespace fit {

std::string hr_bytes(uint64_t num);

std::string hr_time(std::chrono::steady_clock::duration elapsed);

}

#endif // FIT_FORMAT_H
