#pragma once
#include <string>
#include <time.h>
#include <stdio.h>

struct SP3Entry
{
  int gnss;
  int sv;
  time_t t;
  double x, y, z; // meters
  double clockBias; // nanoseconds
};

class SP3Reader
{
public:
  SP3Reader(std::string_view fname);
  bool get(SP3Entry& sp3);
  ~SP3Reader();
private:
  std::string fname;
  FILE* d_fp{0};
  time_t d_time{0};
};
