#pragma once
#include <bitset>
#include <string>
#include "bits.hh"
#include <iostream>
#include <math.h>
std::basic_string<uint8_t> getGlonassessage(std::basic_string_view<uint8_t> payload);

struct GlonassMessage
{
  uint8_t strtype;

  int parse(std::basic_string_view<uint8_t> gstr)
  {
    strtype = getbitu(&gstr[0], 1, 4);
    if(strtype == 1) {
      parse1(gstr);
    }
    else if(strtype == 2) {
      parse2(gstr);
    }
    else if(strtype == 3) {
      parse3(gstr);
    }
    else if(strtype == 4) {
      parse4(gstr);
    }
    else if(strtype == 5) {
      parse5(gstr);
    }
    else if(strtype == 6 || strtype ==8 || strtype == 10 || strtype ==12 ||strtype ==14) {
      parse6_8_10_12_14(gstr);
    }
    else if(strtype == 7 || strtype == 9 || strtype == 11 || strtype ==13 ||strtype ==15) {
      parse7_9_11_13_15(gstr);
    }
    return strtype;
  }

  /* The GLONASS day starts at 00:00 Moscow time, which is on UTC+3 by definition.
     This means midnight is 21:00 UTC the previous day.
     Various GLONASS things relate to "the day", so it is important to note which day we are at
  */
  uint8_t hour, minute, seconds, P1;
  int32_t x{0}, dx, ddx; // 2^-11 km, 2^-20 km/s, 2^-30 km/s^2

  double getX() { return ldexp(x*1000.0, -11); }
  double getY() { return ldexp(y*1000.0, -11); }
  double getZ() { return ldexp(z*1000.0, -11); }

  double getdX() { return ldexp(dx*1000.0, -20); }
  double getdY() { return ldexp(dy*1000.0, -20); }
  double getdZ() { return ldexp(dz*1000.0, -20); }

  
  double getRadius() { return sqrt(getX()*getX() + getY()*getY() + getZ()*getZ()); }
   
  void parse1(std::basic_string_view<uint8_t> gstr)
  {
    hour = getbitu(&gstr[0], 9, 5);
    minute = getbitu(&gstr[0], 14, 6);
    seconds = 30*getbitu(&gstr[0], 20, 1);
    P1 = getbitu(&gstr[0], 85-78, 2);
    x=getbitsglonass(&gstr[0], 85-35, 27); // 2^-11
    dx=getbitsglonass(&gstr[0], 85-64, 24); // 2^-20
    ddx=getbitsglonass(&gstr[0], 85-40, 5); // 2^-30
  }

  uint8_t Bn, Tb, P2;
  int32_t y{0}, dy, ddy;

  /* The GLONASS ephemeris centered on the "Tb-th" interval, from the start of the Moscow day.
     An interval is 15 minutes long, plus a spacer of length described by P1. If P1 is zero, there is no spacer.
  */
  
  void parse2(std::basic_string_view<uint8_t> gstr)
  {
    Bn = getbitu(&gstr[0], 85-80, 3); // Health bit, only look at MSB, ignore the rest. 0 is ok.
    Tb = getbitu(&gstr[0], 85-76, 7); 
    P2 = getbitu(&gstr[0], 85-77, 1);
    
    y=getbitsglonass(&gstr[0], 85-35, 27); // 2^-11, in kilometers
    dy=getbitsglonass(&gstr[0], 85-64, 24); // 2^-20, in kilometers
    ddy=getbitsglonass(&gstr[0], 85-40, 5); // 2^-30, in kilometers
    
  }

  int32_t z{0}, dz, ddz;
  bool l_n;
  bool P, P3;
  uint16_t gamman;
  void parse3(std::basic_string_view<uint8_t> gstr)
  {
    z   = getbitsglonass(&gstr[0], 85-35, 27); // 2^-11
    dz  = getbitsglonass(&gstr[0], 85-64, 24); // 2^-20
    ddz = getbitsglonass(&gstr[0], 85-40, 5); // 2^-30

    P = getbitu(&gstr[0], 85 - 66, 1);
    P3 = getbitu(&gstr[0], 85 - 80, 1);

    gamman = getbitu(&gstr[0], 85 - 79, 11);
    l_n = getbitu(&gstr[0], 85 - 65, 1);
  }


  /* NT is the 'day number' within the current four-year-plan, which run in blocks from 1996.
     Not yet sure if this starts from 0 or not (I guess 1)
  */
  
  uint16_t NT; 
  uint8_t FT{255}, En, deltaTaun, M;
  int32_t taun{0}; // 2^-30
  bool P4;

  double getTaunNS()
  {
    return 1000*ldexp(1000000.0*taun, -30); 
  }
  
  void parse4(std::basic_string_view<uint8_t> gstr)
  {
    NT = getbitu(&gstr[0], 85-26, 11);
    FT = getbitu(&gstr[0], 85-33, 4);
    M = getbitu(&gstr[0], 85-10, 2);
    taun = getbitsglonass(&gstr[0], 85-80, 22);
    En = getbitu(&gstr[0], 85-53, 5);
    P4 = getbitu(&gstr[0], 85-34, 1);
    deltaTaun = getbitsglonass(&gstr[0], 85 - 58, 4);
  }

  uint32_t getGloTime() const;
  
  uint8_t n4{0}; // counting from 1996 ('n4=1'), this is the 4-year plan index we are currently in
  int32_t taugps;
  int32_t tauc;
  
  void parse5(std::basic_string_view<uint8_t> gstr)
  {
    n4=getbitu(&gstr[0], 85-36, 5);
    taugps = getbitsglonass(&gstr[0], 85-31, 22);
    tauc = getbitsglonass(&gstr[0], 85-69, 32);
    l_n = getbitu(&gstr[0], 85 - 9, 1);
  }

  double omegana; // 2^-15 semi-circles, at instantt of tlambdana
  uint8_t hna;
  uint32_t tlambdana; // 2^-5s time of ascending node passage, also: t0a, relative to MT midnight
  int32_t deltatna; // 2^-9

  double gettLambdaNa() const
  {
    return ldexp(tlambdana, -5);
  }
  
  void parse7_9_11_13_15(std::basic_string_view<uint8_t> gstr)
  {
    l_n = getbitu(&gstr[0], 85 - 9, 1);
    omegana = getbitsglonass(&gstr[0], 85-80, 16);
    hna = getbitu(&gstr[0], 85 - 14, 5); // this is always positive, but there is a translation table
    tlambdana = getbitu(&gstr[0], 85 - 64, 21);
    deltatna = getbitsglonass(&gstr[0], 85 - 43, 22);
  }
  

  int nA;
  bool CnA;
  int32_t lambdana; // 2^-20 semi-circles
  int32_t deltaina; // 2^-20 semi-circles
  int32_t epsilonna; // 2^-20
  int32_t tauna; // 2^-18
  
  double getLambdaNaDeg() const
  {
    return ldexp(180.0*lambdana, -20);
  }

  double getE() const
  {
    return ldexp(epsilonna, -20);
  }

  double getI0() const
  {
    return M_PI*63.0/180  + ldexp(M_PI* deltaina, -20);
  }
  
  void parse6_8_10_12_14(std::basic_string_view<uint8_t> gstr)
  {
    CnA = getbitu(&gstr[0], 85-80, 1);
    nA = getbitu(&gstr[0], 85-77, 5);
    lambdana = getbitsglonass(&gstr[0], 85-62, 21);
    deltaina = getbitsglonass(&gstr[0], 85-41, 18);
    epsilonna = getbitu(&gstr[0], 85- 23, 15);
    tauna = getbitsglonass(&gstr[0], 85 - 72, 10);
  }
  
};
