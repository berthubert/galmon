#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include "ephemeris.hh"
#include "bits.hh"

bool getTOWFromInav(std::basic_string_view<uint8_t> inav, uint32_t *satTOW, uint16_t *wn);

struct GalileoMessage : GPSLikeEphemeris
{
  uint8_t wtype;

  typedef void (GalileoMessage::*func_t)(std::basic_string_view<uint8_t> page);
  std::vector<func_t> parsers{
    &GalileoMessage::parse0,
    &GalileoMessage::parse1,
    &GalileoMessage::parse2,
    &GalileoMessage::parse3,
    &GalileoMessage::parse4,
    &GalileoMessage::parse5,
    &GalileoMessage::parse6,
    &GalileoMessage::parse7,
    &GalileoMessage::parse8,
    &GalileoMessage::parse9,
    &GalileoMessage::parse10
      };

  
  int parse(std::basic_string_view<uint8_t> page)
  {
    wtype = getbitu(&page[0], 0, 6);
    if(wtype >= parsers.size()) {
      //      std::cerr<<"Asked for impossible galileo type "<<(int)wtype<<std::endl;
      return wtype;
    }
      
    std::invoke(parsers.at(wtype), this, page);
    return wtype;
  }

  int parseFnav(std::basic_string_view<uint8_t> page);
  
  uint8_t sparetime{0};
  uint16_t wn{0};  
  uint32_t tow{0}; 
  int iodalmanac;
  int wnalmanac;
  int t0almanac;
  int svid1, svid2, svid3;

  
  // spare word, only contains a WN and a TOW, but only if the 'time' field is set to 2
  void parse0(std::basic_string_view<uint8_t> page)
  {
    sparetime = getbitu(&page[0], 6, 2);
    if(sparetime == 2) {
      wn = getbitu(&page[0], 96, 12);
      tow = getbitu(&page[0], 108, 20);
    }
  }

  GalileoMessage()
  {
  }

  uint8_t e5ahs{0}, e5bhs{0}, e1bhs{0};
  uint8_t gpshealth{0};
  uint16_t ai0{0};
  int16_t ai1{0}, ai2{0};
  bool sf1{0}, sf2{0}, sf3{0}, sf4{0}, sf5{0};
  int BGDE1E5a{0}, BGDE1E5b{0};
  bool e5advs{false}, e5bdvs{false}, e1bdvs{false};
  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  
  //                     
  //      2^-30   2^-50  3600   8-bit week
  int32_t a0{0}, a1{0}, t0t{0}, wn0t{0};
  //      2^-35   2^-51   3600    8-bit week
  int32_t a0g{0}, a1g{0}, t0g{0}, wn0g{0};
  int8_t dtLS{0}, dtLSF{0};
  uint16_t wnLSF{0};
  uint8_t dn; // leap second day number


  uint32_t t0e; 
  uint32_t e, sqrtA;
  int32_t m0, omega0, i0, omega, idot, omegadot, deltan;
  
  int16_t cuc{0}, cus{0}, crc{0}, crs{0}, cic{0}, cis{0};
  //        60 seconds
  uint16_t t0c; 

  //      2^-34     2^-46
  int32_t af0{0}   ,   af1{0};
  //     2^-59
  int8_t af2{0};
  
  uint8_t sisa;

  uint16_t iodnav;

  int getIOD() const
  {
    return iodnav;
  }
  
  struct Almanac
  {
    int svid{-1};
    int t0almanac, wnalmanac;
    int af0, af1;
    int e1bhs, e5bhs;

    uint32_t e, deltaSqrtA;
    int32_t M0, Omega0, deltai, omega, Omegadot;

    double getMu() const { return 3.986005    * pow(10.0, 14.0); }
    double getOmegaE()    const { return 7.2921151467 * pow(10.0, -5.0);} // rad/s

    uint32_t getT0e() const { return 600 * t0almanac; }
    double getSqrtA() const { return sqrt(29600000) + ldexp(deltaSqrtA,     -9);   }
    double getE()     const { return ldexp(e,         -16);   }


    double getI0()        const { return M_PI*56.0/180.0 + ldexp(deltai * M_PI,       -14);   } // radians
    double getOmega0()    const { return ldexp(Omega0 * M_PI,   -15);   } // radians
    double getOmegadot()  const { return ldexp(Omegadot * M_PI, -33);   } // radians/s
    double getOmega()     const { return ldexp(omega * M_PI,    -15);   } // radians
    double getM0()    const { return ldexp(M0 * M_PI, -15);   } // radians    

    double getIdot()      const { return 0;   } // radians/s
    double getCic()       const { return 0;   } // radians
    double getCis()       const { return 0;   } // radians
    double getCuc()   const { return 0;   } // radians
    double getCus()   const { return 0;   } // radians
    double getCrc()   const { return 0;   } // meters
    double getCrs()   const { return 0;   } // meters
    double getDeltan()const { return 0; } //radians/s
    int getIOD() const
    {
      return -1;
    }
  } alma1, alma2, alma3;

  
  // an ephemeris word
  void parse1(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    t0e = getbitu(&page[0],   16, 14); 
    m0 = getbits(&page[0],    30, 32);
    e = getbitu(&page[0],     62, 32);
    sqrtA = getbitu(&page[0], 94, 32);
  }

  // another ephemeris word
  void parse2(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    omega0 = getbits(&page[0], 16, 32);
    i0 = getbits(&page[0],     48, 32);
    omega = getbits(&page[0],  80, 32);
    idot = getbits(&page[0],   112, 14);
  }

  // yet another ephemeris word
  void parse3(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    omegadot = getbits(&page[0], 16, 24);
    deltan = getbits(&page[0],   40, 16);
    cuc = getbits(&page[0],      56, 16);
    cus = getbits(&page[0],      72, 16);
    crc = getbits(&page[0],      88, 16);
    crs = getbits(&page[0],     104, 16);
    sisa = getbitu(&page[0],    120, 8);
  }

  int getT0c() const
  {
    return t0c * 60;
  }
  int getT0t() const
  {
    return t0t * 3600;
  }
  int getT0g() const
  {
    return t0g * 3600;
  }

  // pair of nanosecond, nanosecond/s 
  std::pair<double, double> getAtomicOffset(int tow) const
  {
    int delta = ephAge(tow, getT0c());
    //           2^-34      2^-46                            2^-59
    double cur = af0  + ldexp(1.0*delta*af1, -12) + ldexp(1.0*delta*delta*af2, -25);
    double trend = ldexp(af1, -12) + ldexp(2.0*delta*af2, -25);

    // now in units of 2^-34 seconds, which are ~0.058 nanoseconds each
    double factor = ldexp(1000000000.0, -34);
    return {factor * cur, factor * trend};
  }

  std::pair<double, double> getUTCOffset(int tow, int wn) const
  {
    int dw = (int)(uint8_t)wn - (int)(uint8_t) wn0t;
    int delta = dw*7*86400  + tow - getT0t(); // NOT ephemeris age tricks

    // 2^-30  2^-50   3600
    // a0     a1      t0t 
    double cur = a0  + ldexp(1.0*delta*a1, -20);
    double trend = ldexp(a1, -20);

    // now in units of 2^-30 seconds, which are ~1.1 nanoseconds each
    
    double factor = ldexp(1000000000, -30);
    //    std::cout<<"dw: "<<dw<<" ds "<<tow-getT0t()<<" delta " << delta << " a0 " <<a0<<" a1 " << a1 <<" factor " << factor << std::endl;
    
    return {factor * cur, factor * trend};
  }
  // pair of nanosecond, nanosecond/s 
  std::pair<double, double> getGPSOffset(int tow, int wn) const
  {
    int dw = (int)(wn%64) - (int)(wn0g%64);
    if(dw > 31)
      dw = 31- dw;
    int delta = dw*7*86400  + tow - getT0g(); // NOT ephemeris age tricks

    // 2^-35  2^-51   3600
    // a0g     a1g      t0g 
    double cur = a0g  + ldexp(1.0*delta*a1g, -16);
    double trend = ldexp(1.0*a1g, -16);

    // now in units of 2^-35 seconds
    
    double factor = ldexp(1000000000, -35); // turn into nanoseconds
    //    std::cout<<"gps dw: "<<dw<<" ds "<<tow-getT0g()<<" delta " << delta << " a0 " <<a0g<<" a1 " << a1g <<" factor " << factor << std::endl;
    
    return {factor * cur, factor * trend};
  }

  

  
  // can't get enough of that ephemeris
  void parse4(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    cic = getbits(&page[0], 22, 16);
    cis = getbits(&page[0], 38, 16);

    t0c = getbitu(&page[0], 54, 14);  // 60
    af0 = getbits(&page[0], 68, 31);  // 2^-34
    af1 = getbits(&page[0], 99, 21);  // 2^-46
    af2 = getbits(&page[0], 120, 6);  // 2^-59
  }

  // ionospheric disturbance, health, group delay, time
  void parse5(std::basic_string_view<uint8_t> page)
  {
    ai0 = getbitu(&page[0], 6, 11);
    ai1 = getbits(&page[0], 17, 11); // ai1 & 2 are signed, 0 not
    ai2 = getbits(&page[0], 28, 14);
    
    sf1 = getbitu(&page[0], 42, 1);
    sf2 = getbitu(&page[0], 43, 1);
    sf3 = getbitu(&page[0], 44, 1);
    sf4 = getbitu(&page[0], 45, 1);
    sf5 = getbitu(&page[0], 46, 1);
    BGDE1E5a = getbits(&page[0], 47, 10); // 2^-32 s
    BGDE1E5b = getbits(&page[0], 57, 10); // 2^-32 s
    
    e5bhs = getbitu(&page[0], 67, 2);
    e1bhs = getbitu(&page[0], 69, 2);
    e5bdvs = getbitu(&page[0], 71, 1);
    e1bdvs = getbitu(&page[0], 72, 1);
    wn = getbitu(&page[0], 73, 12);
    tow = getbitu(&page[0], 85, 20);
  }

  // time stuff
  void parse6(std::basic_string_view<uint8_t> page)
  {
    a0 = getbits(&page[0], 6, 32);
    a1 = getbits(&page[0], 38, 24);
    dtLS = getbits(&page[0], 62, 8);
    t0t = getbitu(&page[0], 70, 8);
    wn0t = getbitu(&page[0], 78, 8);
    wnLSF = getbitu(&page[0], 86, 8);
    dn = getbitu(&page[0], 94, 3);
    dtLSF = getbits(&page[0], 97, 8);

    tow = getbitu(&page[0], 105, 20);
  }

  // almanac
  void parse7(std::basic_string_view<uint8_t> page)
  {
    iodalmanac = getbitu(&page[0], 6, 4);
    alma1.wnalmanac = wnalmanac = getbitu(&page[0], 10, 2);
    alma1.t0almanac = t0almanac = getbitu(&page[0], 12, 10);
    alma1.svid     = getbitu(&page[0], 22, 6);
    alma1.deltaSqrtA = getbitu(&page[0], 28, 13);
    alma1.e = getbitu(&page[0], 41, 11);
    alma1.omega = getbits(&page[0], 52, 16);
    alma1.deltai = getbits(&page[0], 68, 11);
    alma1.Omega0 = getbits(&page[0], 79, 16);
    alma1.Omegadot = getbits(&page[0], 95, 11);
    alma1.M0 = getbits(&page[0], 106, 16);
    
  }
  // almanac
  void parse8(std::basic_string_view<uint8_t> page)
  {
    iodalmanac = getbitu(&page[0], 6, 4);
    alma1.af0 = getbits(&page[0], 10, 16);
    alma1.af1 = getbits(&page[0], 26, 13);
    alma1.e5bhs = getbitu(&page[0], 39, 2);
    alma1.e1bhs = getbitu(&page[0], 41, 2);

    alma2.svid     = getbitu(&page[0], 43, 6);
    
    alma2.deltaSqrtA = getbitu(&page[0], 49, 13);
    alma2.e = getbitu(&page[0], 62, 11);
    alma2.omega = getbits(&page[0], 73, 16);
    alma2.deltai = getbits(&page[0], 89, 11);
    alma2.Omega0 = getbits(&page[0], 100, 16);
    alma2.Omegadot = getbits(&page[0], 116, 11);
  }

  // almanac
  void parse9(std::basic_string_view<uint8_t> page)
  {
    iodalmanac = getbitu(&page[0], 6, 4);
    alma2.wnalmanac = wnalmanac = getbitu(&page[0], 10, 2);
    alma3.t0almanac = alma2.t0almanac = t0almanac = getbitu(&page[0], 12, 10);

    alma2.M0 = getbits(&page[0], 22, 16);
    alma2.af0 = getbits(&page[0], 38, 16);
    alma2.af1 = getbits(&page[0], 54, 13);
    alma2.e5bhs = getbitu(&page[0], 67, 2);
    alma2.e1bhs = getbitu(&page[0], 69, 2);

    alma3.svid = getbitu(&page[0], 71, 6);
    alma3.deltaSqrtA = getbitu(&page[0], 77, 13);
    alma3.e = getbitu(&page[0], 90, 11);
    alma3.omega = getbits(&page[0], 101, 16);
    alma3.deltai = getbits(&page[0], 117, 11);    
        
  }

  // almanac + more time stuff (GPS)
  void parse10(std::basic_string_view<uint8_t> page)
  {
    iodalmanac = getbitu(&page[0], 6, 4);
    alma3.Omega0 = getbits(&page[0], 10, 16);
    alma3.Omegadot = getbits(&page[0], 26, 11);
    alma3.M0 = getbits(&page[0], 37, 16);
    
    alma3.af0 = getbits(&page[0], 53, 16);
    alma3.af1 = getbits(&page[0], 69, 13);
    alma3.e5bhs = getbitu(&page[0], 82, 2);
    alma3.e1bhs = getbitu(&page[0], 84, 2);
    
    a0g = getbits(&page[0], 86, 16);
    a1g = getbits(&page[0], 102, 12);
    t0g = getbitu(&page[0], 114, 8);
    wn0g = getbitu(&page[0], 122, 6);
  }

  double getMu() const
  {
    return 3.986004418 * pow(10.0, 14.0);
  } // m^3/s^2
  // same for galileo & gps
  double getOmegaE()    const { return 7.2921151467 * pow(10.0, -5.0);} // rad/s

  uint32_t getT0e() const { return t0e * 60; }
  double getSqrtA() const { return ldexp(sqrtA,     -19);   }
  double getE()     const { return ldexp(e,         -33);   }
  double getCuc()   const { return ldexp(cuc,       -29);   } // radians
  double getCus()   const { return ldexp(cus,       -29);   } // radians
  double getCrc()   const { return ldexp(crc,        -5);   } // meters
  double getCrs()   const { return ldexp(crs,        -5);   } // meters
  double getM0()    const { return ldexp(m0 * M_PI, -31);   } // radians
  double getDeltan()const { return ldexp(deltan *M_PI, -43); } //radians/s
  double getI0()        const { return ldexp(i0 * M_PI,       -31);   } // radians
  double getCic()       const { return ldexp(cic,             -29);   } // radians
  double getCis()       const { return ldexp(cis,             -29);   } // radians
  double getOmegadot()  const { return ldexp(omegadot * M_PI, -43);   } // radians/s
  double getOmega0()    const { return ldexp(omega0 * M_PI,   -31);   } // radians
  double getIdot()      const { return ldexp(idot * M_PI,     -43);   } // radians/s
  double getOmega()     const { return ldexp(omega * M_PI,    -31);   } // radians

  
};
