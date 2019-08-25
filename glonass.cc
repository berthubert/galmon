#include "gps.hh"

// this strips out spare bits + parity, and leaves 10 clean 24 bit words
std::basic_string<uint8_t> getGlonassMessage(std::basic_string_view<uint8_t> payload)
{
  uint8_t buffer[4*4];

  for(int w = 0 ; w < 4; ++w) {
    setbitu(buffer, 32*w, 32, getbitu(&payload[0],  w*32, 32));
  }

  return std::basic_string<uint8_t>(buffer, 16);
  
}


