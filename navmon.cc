#include "navmon.hh"
#include <errno.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <stdexcept>
#include "fmt/format.h"
#include "fmt/printf.h"

using namespace std;


size_t readn2(int fd, void* buffer, size_t len)
{
  size_t pos=0;
  ssize_t res;
  for(;;) {
    res = read(fd, (char*)buffer + pos, len - pos);
    if(res == 0)
      throw EofException();
    if(res < 0) {
      throw runtime_error("failed in readn2: "+string(strerror(errno)));
    }

    pos+=(size_t)res;
    if(pos == len)
      break;
  }
  return len;
}

std::string humanTime(time_t t)
{
  struct tm tm={0};
  gmtime_r(&t, &tm);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &tm);
  return buffer;
}

std::string humanTime(time_t t, uint32_t nanoseconds)
{
  struct tm tm={0};
  gmtime_r(&t, &tm);

  char buffer[80];
  std::string fmt = "%a, %d %b %Y %H:%M:"+fmt::sprintf("%06.04f", tm.tm_sec + nanoseconds/1000000000.0) +" %z";
  
  strftime(buffer, sizeof(buffer), fmt.c_str(), &tm);
  return buffer;
}
