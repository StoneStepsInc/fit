#ifndef FIT_FORMAT_H
#define FIT_FORMAT_H

#include <string>
#include <cstdint>
#include <chrono>

namespace fit {

std::string hr_bytes(uint64_t num);

std::string hr_time(std::chrono::steady_clock::duration elapsed);

}

#endif // FIT_FORMAT_H
