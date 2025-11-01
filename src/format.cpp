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

   // print small numbers as-is, with "bytes" spelled out
   if(bytes < 1000)
      return FMTNS::format("{:d} {:s}"sv, bytes, bytes == 1 ? "byte" : "bytes");

   uint64_t divisor = 1;
   size_t prefix = 0;

   while (bytes / divisor >= 1000) {
      divisor *= 1000;
      prefix++;
   }

   // uint64_t is 18.5 EB, so this will never be triggered in the current setup
   if(prefix >= sizeof(si_unit_pfx)/sizeof(si_unit_pfx[0]))
      throw std::logic_error(FMTNS::format("Cannot format {:d} as a human-readable number"sv, bytes));

   uint64_t whole = bytes / divisor;
    
   uint64_t remainder = bytes % divisor;
   uint64_t decimals;

   if(prefix < 4) {
      // add the equivalent of .5 to round it up or down
      remainder += (divisor / 100) / 2;

      // compute GB numbers and below with 2 digits after the decimal point
      if((decimals = (remainder / (divisor / 100))) == 100) {
         // adjust the whole part if the remainder pushes decimals past 99 (e.g. 14,995)
         whole++;
         decimals = 0;
      }
   } else {
      remainder += (divisor / 1000) / 2;

      // otherwise, compute numbers with 3 digits after the decimal point
      if((decimals = (remainder / (divisor / 1000))) == 1000) {
         whole++;
         decimals = 0;
      }
   }

   // print numbers that round to .0 as integers, with the appropriate unit prefix
   if(decimals == 0)
      return FMTNS::format("{:d} {:s}B"sv, whole, si_unit_pfx[prefix]);

   // omit trailing zeros for all cases (e.g. 12.3 GB not 12.30 GB)
   if(prefix < 4) {
      if(decimals % 10 == 0)
         return FMTNS::format("{:d}.{:d} {:s}B"sv, whole, decimals/10, si_unit_pfx[prefix]);

      return FMTNS::format("{:d}.{:02d} {:s}B"sv, whole, decimals, si_unit_pfx[prefix]);
   }
   
   if(decimals % 100 == 0)
      return FMTNS::format("{:d}.{:d} {:s}B"sv, whole, decimals/100, si_unit_pfx[prefix]);

   if(decimals % 10 == 0)
      return FMTNS::format("{:d}.{:02d} {:s}B"sv, whole, decimals/10, si_unit_pfx[prefix]);

   return FMTNS::format("{:d}.{:03d} {:s}B"sv, whole, decimals, si_unit_pfx[prefix]);
}

std::string hr_time(std::chrono::steady_clock::duration elapsed)
{
   size_t hours = std::chrono::duration_cast<std::chrono::hours>(elapsed).count();
   size_t minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() % 60;
   size_t seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() % 60;

   // milliseconds are not rounded into seconds, etc (i.e. 5:12.999 is printed as 5:12)
   if(!hours && !minutes)
      return FMTNS::format("{:d}.{:03d} sec"sv, seconds, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() % 1000);

   if(!hours)
      return FMTNS::format("{:d}:{:02d} min"sv, minutes, seconds);

   return FMTNS::format("{:d}:{:02d}:{:02d} hours"sv, hours, minutes, seconds);
}

}
