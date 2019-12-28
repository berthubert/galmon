#include "navmon.hh"
#include <errno.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <stdexcept>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <signal.h>
#include <sys/poll.h>
#include <iostream>

using namespace std;

using Clock = std::chrono::steady_clock; 

static double passedMsec(const Clock::time_point& then, const Clock::time_point& now)
{
  return std::chrono::duration_cast<std::chrono::microseconds>(now - then).count()/1000.0;
}


static double passedMsec(const Clock::time_point& then)
{
  return passedMsec(then, Clock::now());
}



size_t readn2Timeout(int fd, void* buffer, size_t len, double* timeout)
{
  size_t pos=0;
  ssize_t res;

  /* The plan.
     Calculate the 'end time', which is now + timeout
     At beginning of loop, calculate how many milliseconds this is in the future
     If <= 0, set remaining *timeout to 0, throw timeout exception
     Then wait that many milliseconds, if timeout, set remaining *timeout to zero, throw timeout
     Otherwise only adjust *timeout
  */

  auto limit = Clock::now();
  if(timeout) {
    //    cerr<<"Called with timeout "<<*timeout<<", "<<(*timeout*1000)<<"msec for "<<len<< " bytes"<<endl;

      
    if(*timeout < 0)
      throw TimeoutError();
    limit += std::chrono::milliseconds((int)(*timeout*1000));
  }

  for(;;) {
    if(timeout) {
      double msec = passedMsec(Clock::now(), limit);
      //      cerr<<"Need to wait "<<msec<<" msec for at most "<<(len-pos)<<" bytes"<<endl;
      if(msec < 0) {
        *timeout=0;

        throw TimeoutError();
      }
      
      struct pollfd pfd;
      memset(&pfd, 0, sizeof(pfd));
      pfd.fd = fd;
      pfd.events=POLLIN;
      
      res = poll(&pfd, 1, msec);
      if(res < 0)
        throw runtime_error("failed in poll: "+string(strerror(errno)));
      if(!res) {
        *timeout=0;
        throw TimeoutError();
      }
      // we have data!
    }
    res = read(fd, (char*)buffer + pos, len - pos);
    if(res == 0)
      throw EofException();
    if(res < 0) {
      if(errno == EAGAIN)
        continue;
      throw runtime_error("failed in readn2: "+string(strerror(errno)));
    }

    pos+=(size_t)res;
    if(pos == len)
      break;
  }
  if(timeout) {
    //    cerr<<"Spent "<<*timeout*1000 + passedMsec(limit);
    *timeout -= (*timeout*1000.0 + passedMsec(limit))/1000.0;
    if(*timeout < 0)
      *timeout=0;
    //    cerr<<", "<<(*timeout*1000)<<" left " <<endl;
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
  std::string fmt = "%a, %d %b %Y %H:%M:"+fmt::sprintf("%07.04f", tm.tm_sec + nanoseconds/1000000000.0) +" %z";
  
  strftime(buffer, sizeof(buffer), fmt.c_str(), &tm);
  return buffer;
}
