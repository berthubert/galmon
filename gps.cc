#include "gps.hh"

// this strips out spare bits + parity, and leaves 10 clean 24 bit words
std::basic_string<uint8_t> getCondensedGPSMessage(std::basic_string_view<uint8_t> payload)
{
  uint8_t buffer[10*24/8];

  for(int w = 0 ; w < 10; ++w) {
    setbitu(buffer, 24*w, 24, getbitu(&payload[0], 2 + w*32, 24));
  }

  return std::basic_string<uint8_t>(buffer, 30);
  
}

