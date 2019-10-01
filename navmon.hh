#pragma once
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include <tuple>

struct EofException{};
size_t readn2(int fd, void* buffer, size_t len);
std::string humanTime(time_t t);
std::string humanTime(time_t t, uint32_t nanoseconds);
struct SatID
{
  uint32_t gnss{255}; // these could all be 'int16_t' but leads to howling numbers of warnings with protobuf
  uint32_t sv{0};
  uint32_t sigid{0};
  bool operator<(const SatID& rhs) const
  {
    return std::tie(gnss, sv, sigid) < std::tie(rhs.gnss, rhs.sv, rhs.sigid);
  }
};
