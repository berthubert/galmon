#include "navmon.hh"
#include <errno.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <stdexcept>
#include "fmt/format.h"
#include "fmt/printf.h"

using namespace std;

size_t writen2(int fd, const void *buf, size_t len)
{
  const char *ptr = (char*)buf;
  const char *eptr = ptr + len;

  ssize_t res;
  while(ptr != eptr) {
    res = ::write(fd, ptr, eptr - ptr);
    if(res < 0) {
      throw runtime_error("failed in writen2: "+string(strerror(errno)));
    }
    else if (res == 0)
      throw EofException();

    ptr += (size_t) res;
  }

  return len;
}

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

std::string humanTimeNow()
{
  time_t now = time(0);
  return humanTime(now);
}

std::string humanTime(time_t t)
{
  static bool set_tz = false;
  struct tm tm={0};
  gmtime_r(&t, &tm);

  if (!set_tz) {
    setenv("TZ", "UTC", 1); // We think in UTC.
    tzset();
    set_tz = true;
  }

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &tm);
  // strftime(buffer, sizeof(buffer), "%F %T ", &tm);
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
