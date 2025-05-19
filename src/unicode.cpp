#include "unicode.h"

namespace fit {
namespace unicode {

template <char lo, char hi>
inline bool in_range(char ch) 
{
   return static_cast<unsigned char>(ch) >= static_cast<unsigned char>(lo) &&
            static_cast<unsigned char>(ch) <= static_cast<unsigned char>(hi);
}

//
// [Unicode ch.3 table 3-7](https://www.unicode.org/versions/latest/ch03.pdf)
// 
// Code Points          First Byte  Second Byte Third Byte  Fourth Byte
// U+0000   .. U+007F   00..7F
// U+0080   .. U+07FF   C2..DF      80..BF
// U+0800   .. U+0FFF   E0          A0..BF      80..BF
// U+1000   .. U+CFFF   E1..EC      80..BF      80..BF
// U+D000   .. U+D7FF   ED          80..9F      80..BF
// U+E000   .. U+FFFF   EE..EF      80..BF      80..BF
// U+10000  .. U+3FFFF  F0          90..BF      80..BF      80..BF
// U+40000  .. U+FFFFF  F1..F3      80..BF      80..BF      80..BF
// U+100000 .. U+10FFFF F4          80..8F      80..BF      80..BF
// 
// slen is the remaining string length, if available, and may be greater
// than the character size pointer to by str.
// 
// if slen is greater than zero, it may point to a null character, which
// is considered as a valid UTF-8 character within the string (i.e. if
// str = "\x0""ABC" and slen == 4, value 1 is returned).
// 
// If slen is zero, str is assumed to be pointing to a null-terminated
// string and code units will be evaluated such that a null character
// anywhere in the sequence will invalidate it. This mode allows the
// caller to avoid having to count characters before calling this
// function.
// 
// The function returns a value between 1 and 4 if str points to a valid
// UTF-8 character and a zero otherwise.
//
static inline size_t utf8_size(const char *str, size_t slen)
{
   if(!str)
      return 0;

   //
   // IMPORTANT: All range comparisons for slen == 0 must allow the case
   // in which the null character is at the right edge of a memory block
   // and the following byte is inaccessible. That is, for "\xF0\x0"
   // followed by inaccesible memory, range comparisons must not attempt
   // to evaluate bytes beyond the null character, even though the first
   // byte suggests a 4-byte sequence.
   //

   // must not be called for the null terminator character
   if(slen == 0 && *str == '\x0')
      return 0;

   // 1-byte character (including null terminator)
   if(in_range<'\x00', '\x7F'>(*str))
      return 1;
   
   if(slen && slen < 2)
      return 0;

   // 2-byte sequences
   if(in_range<'\xC2', '\xDF'>(*str))
      return in_range<'\x80', '\xBF'>(*(str+1)) ? 2 : 0;
   
   if(slen && slen < 3)
      return 0;

   // 3-byte sequences
   if(*str == '\xE0')
      return in_range<'\xA0', '\xBF'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) ? 3 : 0;
      
   if(in_range<'\xE1', '\xEC'>(*str))
      return in_range<'\x80', '\xBF'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) ? 3 : 0;
      
   if(*str == '\xED')
      return in_range<'\x80', '\x9F'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) ? 3 : 0;
      
   if(in_range<'\xEE', '\xEF'>(*str))
      return in_range<'\x80', '\xBF'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) ? 3 : 0;

   if(slen && slen < 4)
      return 0;

   // 4-byte sequences
   if(*str == '\xF0')
      return in_range<'\x90', '\xBF'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) &&  in_range<'\x80', '\xBF'>(*(str+3)) ? 4 : 0;

   if(in_range<'\xF1', '\xF3'>(*str))
      return in_range<'\x80', '\xBF'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) &&  in_range<'\x80', '\xBF'>(*(str+3)) ? 4 : 0;
      
   if(*str == '\xF4')
      return in_range<'\x80', '\x8F'>(*(str+1)) && in_range<'\x80', '\xBF'>(*(str+2)) &&  in_range<'\x80', '\xBF'>(*(str+3)) ? 4 : 0;
   
   return 0;
}

bool is_valid_utf8(const std::string& str)
{
   return is_valid_utf8(str.c_str(), str.length());
}

bool is_valid_utf8(const std::string_view& str)
{
   return is_valid_utf8(str.data(), str.length());
}

bool is_valid_utf8(const char *str, size_t slen)
{
   const char *cp = str;

   size_t cusz;   // size in code units

   while(cp < str+slen && (cusz = utf8_size(cp, slen-(cp-str))) != 0)
      cp += cusz;

   return cp == str+slen;
}

bool is_valid_utf8(const char *str)
{
   const char *cp = str;

   size_t cusz;

   while(*cp && (cusz = utf8_size(cp, 0)) != 0)
      cp += cusz;

   return *cp == '\x0';
}

}
}
