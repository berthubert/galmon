#pragma once
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include <tuple>
#include <mutex>

extern const char* g_gitHash;


struct EofException{};
struct TimeoutError{};

size_t readn2(int fd, void* buffer, size_t len);
size_t readn2Timeout(int fd, void* buffer, size_t len, double* timeout);
std::string humanTimeNow();
std::string humanTime(time_t t);
std::string humanTimeShort(time_t t);
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

template<typename T>
class GetterSetter
{
public:
  void set(const T& t)
  {
    std::lock_guard<std::mutex> mut(d_lock);
    d_t = t;
  }

  T get()
  {
    T ret;
    {
      std::lock_guard<std::mutex> mut(d_lock);
      ret = d_t;
    }
    return ret;
  }
private:
  T d_t;
  std::mutex d_lock;
};


double truncPrec(double in, unsigned int digits);
std::string humanFt(uint8_t ft);
std::string humanSisa(uint8_t sisa);
std::string humanUra(uint8_t ura);

double numFt(uint8_t ft);
double numSisa(uint8_t sisa);
double numUra(uint8_t ura);



char getGNSSChar(int id);
std::string makeSatIDName(const SatID& satid);
std::string makeSatPartialName(const SatID& satid);

std::string sbasName(int prn);

extern int g_dtLS, g_dtLSBeidou;
uint64_t utcFromGST(int wn, int tow);
double utcFromGST(int wn, double tow);
double utcFromGPS(int wn, double tow);

std::string makeHexDump(const std::string& str);
size_t writen2(int fd, const void *buf, size_t count);
void unixDie(const std::string& reason);
