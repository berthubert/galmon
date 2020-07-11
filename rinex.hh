#pragma once
#include <string>
#include <time.h>
#include <stdio.h>
#include <zlib.h>
#include <fstream>
#include "navmon.hh"
#include <time.h>
#include "fmt/format.h"
#include "fmt/printf.h"

struct RINEXEntry
{
  time_t t;
  int gnss;
  int sv;
  double sisa;
  int health;
  int toe;
  int tow;
};

class RINEXReader
{
public:
  RINEXReader(std::string_view fname);
  bool get(RINEXEntry& rinex);
  ~RINEXReader();
private:
  void skipFileHeader();
  std::string d_fname;
  gzFile d_fp{0};
  time_t d_time{0};
};

class RINEXNavWriter
{
public:
  explicit RINEXNavWriter(std::string_view fname);
  template<typename T>
  void emitEphemeris(const SatID& sid, const T& t);
private:
  std::ofstream d_ofs;
  void emit(double n)
  {
    if(n >= 0)
      d_ofs << fmt::format(" {:18.12E}", n);
    else
      d_ofs << fmt::format("{:019.12E}", n);
  }
};

template<typename T>
void RINEXNavWriter::emitEphemeris(const SatID& sid, const T& e)
{

  /*
E01 2019 09 21 23 30 00-6.949011585675E-04-7.943867785798E-12 0.000000000000E+00
     1.090000000000E+02 9.746875000000E+01 2.820474626946E-09 2.393449606726E+00
     4.611909389496E-06 2.816439373419E-04 7.767230272293E-06 5.440614154816E+03
     6.030000000000E+05 1.862645149231E-09-1.121206798215E+00-4.656612873077E-08
     9.859399710315E-01 1.802500000000E+02-2.137974852171E+00-5.485228481783E-09
    -1.789360248322E-10 5.170000000000E+02 2.071000000000E+03
     3.120000000000E+00 0.000000000000E+00-1.862645149231E-09-2.095475792885E-09
     6.036660000000E+05
  */

  using std::endl;
  time_t then = utcFromGST(e.wn, (int)e.tow);
  struct tm tm;
  gmtime_r(&then, &tm);
  
  d_ofs << makeSatPartialName(sid)<<" " << fmt::sprintf("%04d %02d %02d %02d %02d %02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

  emit(ldexp(e.af0, -34));
  emit(ldexp(e.af1, -46));
  emit(ldexp(e.af2, -59));
  d_ofs<<"\n    ";

  emit(e.iodnav);
  emit(e.getCrs());
  emit(e.getDeltan());
  emit(e.getM0());
  d_ofs<<"\n    ";


  emit(e.getCuc());
  emit(e.getE());
  emit(e.getCus());
  emit(e.getSqrtA());
  d_ofs<<"\n    ";

  emit(e.getT0e());
  emit(e.getCic());
  emit(e.getOmega0());
  emit(e.getCis());

  d_ofs<<"\n    ";
  emit(e.getI0());
  emit(e.getCrc());
  emit(e.getOmega());
  emit(e.getOmegadot());

  d_ofs<<"\n    ";
  emit(e.getIdot());
  emit(257);
  emit(e.wn + 1024); // so it aligns with GPS


  d_ofs<<"\n    ";
  emit(numSisa(e.sisa));
  int health=0; // there are more bits in here, it is not just health, also signal
  // bits 8/9 encode the signal, so I/NAV, or F/NAV or equivalent
  health |= e.e1bdvs;
  health |= (e.e1bhs << 2);
  // don't have e5advs
  // don't have e5ahs
  health |= (e.e5bdvs << 6);
  health |= (e.e5bhs << 8);
  emit(health); // HEALTH
  emit(ldexp(e.BGDE1E5a, -32));
  emit(ldexp(e.BGDE1E5b, -32));

  d_ofs<<"\n    ";
  emit(e.tow); // XXX

  d_ofs<<std::endl;
  
}
