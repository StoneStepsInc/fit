#include "format.h"

#include <chrono>

#if defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 13)
#include <format>
#define FMTNS std
#else
#include <fmt/format.h>
#define FMTNS fmt
#endif

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

namespace fit {

std::string hr_bytes(uint64_t bytes)
{
   const char *si_unit_pfx[] = {"", "K", "M", "G", "T", "P", "E", "Z"};

   size_t prefix = 0;
   double result = static_cast<double>(bytes);

   while (result >= 1000.) {
      result /= 1000.;
      prefix++;
   }

   // print small numbers as-is, with "bytes" spelled out
   if(prefix == 0)
      return FMTNS::format("{:d} {:s}"sv, bytes, bytes == 1 ? "byte" : "bytes");

   // print numbers that round to .0 as integers, with the appropriate unit prefix
   if(static_cast<uint64_t>(result + .5)  == 0)
      return FMTNS::format("{:d} {:s}B"sv, static_cast<uint64_t>(result + .5), si_unit_pfx[prefix]);
   
   // otherwise, print numbers with 2 digits after the decimal point
   return FMTNS::format("{:.2f} {:s}B"sv, result, si_unit_pfx[prefix]);
}

std::string hr_time(std::chrono::steady_clock::duration elapsed)
{
   size_t hours = std::chrono::duration_cast<std::chrono::hours>(elapsed).count();
   size_t minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() % 60;
   size_t seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() % 60;

   if(!hours && !minutes)
      return FMTNS::format("{:d}.{:03d} sec"sv, seconds, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() % 1000);

   if(!hours)
      return FMTNS::format("{:d}:{:02d} min"sv, minutes, seconds);

   return FMTNS::format("{:d}:{:02d}:{:02d} hours"sv, hours, minutes, seconds);
}

}
