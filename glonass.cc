#include "glonass.hh"
#include <math.h>
#include <string.h>

// this strips out spare bits + parity, and leaves 10 clean 24 bit words
std::basic_string<uint8_t> getGlonassMessage(std::basic_string_view<uint8_t> payload)
{
  uint8_t buffer[4*4];

  for(int w = 0 ; w < 4; ++w) {
    setbitu(buffer, 32*w, 32, getbitu(&payload[0],  w*32, 32));
  }

  return std::basic_string<uint8_t>(buffer, 16);
  
}

uint32_t GlonassMessage::getGloTime() const
{
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = 96+4*(n4 -1);
  tm.tm_mon = 0;
  tm.tm_mday = 1;
  tm.tm_hour = -3; 
  tm.tm_min = 0;
  tm.tm_sec = 0;
  time_t t = timegm(&tm);
  //  cout<<" n4 "<<(int)gm.n4<<" start-of-4y "<< humanTime(t) <<" NT "<<(int)gm.NT;
  
  t += 86400 * (NT-1);
        
  t += 3600 * (hour) + 60 * minute + seconds;
  return t - 820368000; // this starts GLONASS time at 31st of december 1995, 00:00 UTC
}
