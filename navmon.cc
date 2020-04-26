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

std::string humanTimeShort(time_t t)
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
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
  // strftime(buffer, sizeof(buffer), "%F %T ", &tm);
  return buffer;
}


std::string humanTime(time_t t, uint32_t nanoseconds)
{
  struct tm tm={0};
  gmtime_r(&t, &tm);

  char buffer[80];
  std::string fmt = "%a, %d %b %Y %H:%M:"+fmt::sprintf("%07.4f", tm.tm_sec + nanoseconds/1000000000.0) +" %z";
  
  strftime(buffer, sizeof(buffer), fmt.c_str(), &tm);
  return buffer;
}

// truncate to x digits precision, see testrunner.cc for details
double truncPrec(double in, unsigned int digits)
{
  double partial = in - trunc(in);
  int factor=1;
  for(; digits ; --digits)
    factor *= 10;
  return trunc(in) + round(partial * factor) / factor;
}


// GLONASS URA/SISA
string humanFt(uint8_t ft)
{
  static const char* ret[]={"100 cm", "200 cm", "250 cm", "400 cm", "500 cm", "7 m", "10 m", "12 m", "14 m", "16 m", "32 m", "64 m", "128 m", "256 m", "512 m", "NONE"};
  if(ft < 16)
    return ret[ft];
  return "???";
}

// GLONASS URA/SISA
double numFt(uint8_t ft)
{
  static const double ret[]={1, 2, 2.5, 4, 5, 7, 10,12,14,16,32,64,128,256,512,-1};
  if(ft < 16)
    return ret[ft];
  return -1;
}


// Galileo SISA
string humanSisa(uint8_t sisa)
{
  unsigned int sval = sisa;
  if(sisa < 50)
    return std::to_string(sval)+" cm";
  if(sisa < 75)
    return std::to_string(50 + 2* (sval-50))+" cm";
  if(sisa < 100)
    return std::to_string(100 + 4*(sval-75))+" cm";
  if(sisa < 125)
    return std::to_string(200 + 16*(sval-100))+" cm";
  if(sisa < 255)
    return "SPARE";
  return "NO SISA AVAILABLE";
}

// Galileo SISA
double numSisa(uint8_t sisa)
{
  unsigned int sval = sisa;
  if(sisa < 50)
    return sisa/100.0;
  if(sisa < 75)
    return (50 + 2* (sval-50))/100.0;
  if(sisa < 100)
    return (100 + 4*(sval-75))/100.0;
  if(sisa < 125)
    return (200 + 16*(sval-100))/100.0;
  return -1;
}


// GPS/BeiDou URA
string humanUra(uint8_t ura)
{
  if(ura < 6)
    return fmt::sprintf("%d cm", (int)(100*pow(2.0, 1.0+1.0*ura/2.0)));
  else if(ura < 15)
    return fmt::sprintf("%d m", (int)(pow(2, ura-2)));
  return "NO URA AVAILABLE";
}

// GPS/BeiDou URA
double numUra(uint8_t ura)
{
  if(ura < 6)
    return pow(2.0, 1.0+1.0*ura/2.0);
  else if(ura < 15)
    return pow(2, ura-2);
  return -1;
}

char getGNSSChar(int id)
{
  if(id==0)
    return 'G';
  else if(id==2)
    return 'E';
  else if(id==3)
    return 'C';
  else if(id==6)
    return 'R';
  else if(id==255)
    return '?';
  else
    return '0'+id;
}

std::string makeSatIDName(const SatID& satid)
{
  return fmt::sprintf("%c%02d@%d", getGNSSChar(satid.gnss), satid.sv, satid.sigid);
}
std::string makeSatPartialName(const SatID& satid)
{
  return fmt::sprintf("%c%02d", getGNSSChar(satid.gnss), satid.sv);
}


int g_dtLS{18}, g_dtLSBeidou{4};
uint64_t utcFromGST(int wn, int tow)
{
  return (935280000 + wn * 7*86400 + tow - g_dtLS); 
}

double utcFromGST(int wn, double tow)
{
  return (935280000.0 + wn * 7*86400 + tow - g_dtLS); 
}

double utcFromGPS(int wn, double tow)
{
  return (315964800 + wn * 7*86400 + tow - g_dtLS); 
}

string makeHexDump(const string& str)
{
  char tmp[5];
  string ret;
  ret.reserve((int)(str.size()*2.2));

  for(string::size_type n=0;n<str.size();++n) {
    snprintf(tmp, sizeof(tmp), "%02x ", (unsigned char)str[n]);
    ret+=tmp;
  }
  return ret;
}

std::string sbasName(int prn)
{
  string sbas;
  if(prn == 138 || prn == 131 || prn == 133) {
    sbas = "WAAS";
  }
  else if(prn== 126 || prn == 136 || prn == 123 ) {
    sbas = "EGNOS";
  }
  else if(prn == 140 || prn == 125 || prn == 141) {
    sbas = "SDCM";
  }
  else if(prn == 127 || prn == 128 || prn == 138) {
    sbas ="GAGAN";
  }
  else
    sbas ="SBAS?";

  sbas+=" " + std::to_string(prn);
  return sbas;
}

size_t writen2(int fd, const void *buf, size_t count)
{
  const char *ptr = (char*)buf;
  const char *eptr = ptr + count;

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

  return count;
}

void unixDie(const std::string& reason)
{
  throw std::runtime_error(reason+": "+strerror(errno));
}
