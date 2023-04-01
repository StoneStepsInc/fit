#ifndef FIT_UNICODE_H
#define FIT_UNICODE_H

#include <string>
#include <string_view>

namespace fit {
namespace unicode {

bool is_valid_utf8(const std::string& str);
bool is_valid_utf8(const std::string_view& str);

// Checks if a string of `slen` characters contains invalid UTF-8 sequences. May contain null characters within `slen`.
bool is_valid_utf8(const char *str, size_t slen);

// Checks if a null-terminated string contains invalid UTF-8 sequences
bool is_valid_utf8(const char *str);

}
}

#endif // FIT_UNICODE_H
