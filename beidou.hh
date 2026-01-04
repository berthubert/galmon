#pragma once
#include <string>
#include <stdint.h>
#include "bits.hh"
#include <math.h>
#include <stdexcept>
#include <vector>
#include "ephemeris.hh"

std::vector<uint8_t> getCondensedBeidouMessage(const std::vector<uint8_t>& payload);
int beidouBitconv(int their);

/* Geostationary, so D2, so not to be parsed by this parser:
   C01 (140E)
   C02 (80E)
   C03 (110.5E)
   C04 (160E)
   C05 (58.75E)
*/

struct BeidouMessage : GPSLikeEphemeris
{
  uint8_t strtype;

  std::vector<uint8_t> g_cond;
  int bbitu(int bit, int len)
  {
    return getbitu(&g_cond[0], beidouBitconv(bit), len);
  };
  int bbits(int bit, int len)
  {
    return getbits(&g_cond[0], beidouBitconv(bit), len);
  };

  
  int fraid{-1}, sow{-1}; // part of every message (thanks!)
  
  int parse(const std::vector<uint8_t>& cond, uint8_t* pageno)
  {
    g_cond = cond;
    if(pageno)
      *pageno=0;
    fraid = bbitu(16, 3);

    sow = bbitu(19, 20);
    if(bbitu(1, 11) != 1810)
      throw std::runtime_error("BeiDou preamble not 1810: "+std::to_string(bbitu(1,11)));
    
    if(fraid == 1) {
      parse1(cond);
    }
    else if(fraid == 2) {
      parse2(cond);
    }
    else if(fraid == 3) {
      parse3(cond);
    }
    else if(fraid == 4) {
      int tmp = parse4(cond);
      if(pageno) *pageno=tmp;
    }
    else if(fraid == 5) {
      int tmp=parse5(cond);
      if(pageno) *pageno = tmp;
    }

    return fraid;
  }

  uint8_t sath1, aodc, urai, aode;
  uint16_t t0c;
  //          2^-33   2^-50   2^-66
  int wn{-1}, a0,     a1,     a2;
  //                  2^17    2^30

  uint32_t getT0c() const
  {
    return 8 * t0c;
  }
  std::pair<double, double> getAtomicOffset(int Sow = -1)
  {
    if(Sow == -1)
      Sow = sow;
    int delta = ephAge(Sow, getT0c());
    double cur = a0  + ldexp(delta*a1, -17) + ldexp(delta*delta*a2, -30);
    double trend = ldexp(a1, -17) + ldexp(2*delta*a2, -30);

    // now in units of 2^-33 seconds, which are ~0.116 nanoseconds each
    double factor = ldexp(1000000000, -33);
    return {factor * cur, factor * trend};
  }
  
  void parse1(const std::vector<uint8_t>& cond)
  {
    sath1 = bbitu(43,1);
    aodc = bbitu(31+13, 5);
    urai = bbitu(31+12+1+5, 4);
    wn = bbitu(61, 13);
    t0c = bbitu(74, 17);
    a0 = bbits(226, 24); // this should be af0, af1, af2
    a1 = bbits(258, 22);
    a2 = bbits(215, 11);
    aode = bbitu(288, 5);
  }

  int t0eMSB{-1};
  uint32_t sqrtA{0}, e;
  int32_t deltan;
  int32_t cuc, cus, crc, crs;
  int32_t m0;
  

  uint32_t getT0e() const { return 8*((t0eMSB << 15) + t0eLSB);  }  // seconds
  double getDeltan()const { return ldexp(deltan *M_PI, -43); } //radians/s
  double getSqrtA() const { return ldexp(sqrtA,     -19);     }
  double getE()     const { return ldexp(e,         -33);     }
  double getCuc()   const { return ldexp(cuc,       -31);     } // radians
  double getCus()   const { return ldexp(cus,       -31);     } // radians
  double getCrc()   const { return ldexp(crc,        -6);     } // meters
  double getCrs()   const { return ldexp(crs,        -6);     } // meters
  double getM0()    const { return ldexp(m0 * M_PI, -31);     } // radians

  int getIOD() const
  {
    return -1;
  }
  
  void parse2(const std::vector<uint8_t>& cond)
  {
    deltan = bbits(43, 16);
    cuc = bbits(67, 18);
    m0 = bbits(93, 32);
    e = bbitu(133,32);
    cus = bbits(181, 18);
    crc = bbits(199, 18);
    crs = bbits(225, 18);
    sqrtA = bbitu(251, 32);
    t0eMSB = bbitu(291, 2);
  }

  int t0eLSB{-1};
  int i0;
  int32_t cic, cis;
  int32_t omegadot, Omega0, idot, omega;

  double getMu()        const { return 3.986004418 * pow(10.0, 14.0);      } // m^3/s^2
  double getOmegaE()    const { return 7.2921150 * pow(10, -5);            } // rad/s
  double getI0()        const { return ldexp(i0 * M_PI,       -31);   } // radians
  double getCic()       const { return ldexp(cic,             -31);   } // radians
  double getCis()       const { return ldexp(cis,             -31);   } // radians
  double getOmegadot()  const { return ldexp(omegadot * M_PI, -43);   } // radians/s
  double getOmega0()    const { return ldexp(Omega0 * M_PI,   -31);   } // radians
  double getIdot()      const { return ldexp(idot * M_PI,     -43);   } // radians/s
  double getOmega()     const { return ldexp(omega * M_PI,    -31);   } // radians
  void parse3(const std::vector<uint8_t>& cond)
  {
    t0eLSB = bbitu(43, 15);
    i0 = bbits(66, 32);
    cic = bbits(106, 18);
    omegadot = bbits(132, 24);
    cis = bbits(164, 18);
    idot = bbits(190, 14);
    Omega0 = bbits(212, 32);
    omega = bbits(252, 32);
  }

  struct Almanac
  {
    bool geo{false};
    uint32_t sqrtA;
    int a0, a1;
    int Omega0;
    uint32_t e;
    int deltai;
    uint8_t t0a;
    int Omegadot;
    int omega;
    int M0;
    int AmEpID;
    int AmID;
    int pageno;
    
    double getMu()        const { return 3.986004418 * pow(10.0, 14.0);      } // m^3/s^2
    double getOmegaE()    const { return 7.2921150 * pow(10, -5);            } // rad/s

    uint32_t getT0e() const { return ldexp((int)t0a, 12); }
    double getSqrtA() const { return ldexp(sqrtA,     -11);   }
    double getE()     const { return ldexp(e,         -21);   }
    double getOmega()     const { return ldexp(omega * M_PI,    -23);   } // radians
    double getM0()    const { return ldexp(M0 * M_PI, -23);   } // radians
    double getOmega0()    const { return ldexp(Omega0 * M_PI,   -23);   } // radians
    double getOmegadot()  const { return ldexp(Omegadot * M_PI, -38);   } // radians/s
    // XXX for GEO, i0 = 0!! 
    double getI0()        const {
      if(geo)
        return ldexp(M_PI*deltai, -19);
      return M_PI*0.3 + ldexp(M_PI*deltai, -19);
    } // radians

    double getIdot()      const { return 0;   } // radians/s
    double getCic()       const { return 0;   } // radians
    double getCis()       const { return 0;   } // radians
    double getCuc()   const { return 0;   } // radians
    double getCus()   const { return 0;   } // radians
    double getCrc()   const { return 0;   } // meters
    double getCrs()   const { return 0;   } // meters
    double getDeltan()const { return 0; } //radians/s
  } alma;
  
  // 4 is all almanac
  int parse4(const std::vector<uint8_t>& cond)
  {
    alma.sqrtA = bbitu(51, 24);
    alma.a1 = bbits(91, 11);
    alma.a0 = bbits(102, 11);
    alma.Omega0 = bbits(121, 24);
    
    alma.e = bbitu(153, 17);
    alma.deltai = bbits(170, 16);
    alma.t0a = bbitu(194, 8);
    alma.Omegadot = bbits(202, 17);

    alma.omega = bbits(227, 24);
    alma.M0 = bbits(259, 24);
    alma.AmEpID = bbitu(291, 2);
    alma.pageno = bbitu(44, 7);
    return  alma.pageno;
  }

  //                                            2^-30  2^-50
  int a0gps, a1gps, a0gal, a1gal, a0glo, a1glo, a0utc, a1utc;
  int8_t deltaTLS, deltaTLSF;
  uint8_t wnLSF, dn;

  // in Beidou the offset is a0utc + SOW * a1utc
  std::pair<double, double> getUTCOffset(int tow) const
  {
    // 2^-30  2^-50   
    // a0utc  a1utc       
    double cur = a0utc  + ldexp(1.0*tow*a1utc, -20);
    double trend = ldexp(a1utc, -20);

    // now in units of 2^-30 seconds, which are ~1.1 nanoseconds each
    
    double factor = ldexp(1000000000, -30);
    return {factor * cur, factor * trend};
  }

  // in Beidou the offset is a0GPS + SOW * a1GPS
  std::pair<double, double> getGPSOffset(int tow) const
  {
    double cur = a0gps/10.0  + tow*a1gps/10.0;
    double trend = a1gps/1.0;
    return {cur, trend};
  }

  
  int parse5(const std::vector<uint8_t>& cond)
  {
    alma.pageno = bbitu(44, 7);
    if(alma.pageno == 9) {
      a0gps = bbits(97, 14);
      a1gps = bbits(111, 16);
      a0gal = bbits(135, 14);
      a1gal = bbits(157, 16);
      a0glo = bbits(181, 14);
      a1glo = bbits(195, 16);
    }
    if(alma.pageno == 10) {
      a0utc = bbits(91, 32);
      a1utc = bbits(131, 24);
      deltaTLS = bbits(31+12+1+7, 8);
      deltaTLSF = bbits(61+6, 8);
      wnLSF = bbits(61+6+8, 8);
      dn = bbits(151+12, 8);
    }
    else {
      alma.sqrtA = bbitu(51, 24);
      alma.a1 = bbits(91, 11);
      alma.a0 = bbits(102, 11);
      alma.Omega0 = bbits(121, 24);
      
      alma.e = bbitu(153, 17);
      alma.deltai = bbits(170, 16);
      alma.t0a = bbitu(194, 8);
      alma.Omegadot = bbits(202, 17);
      
      alma.omega = bbits(227, 24);
      alma.M0 = bbits(259, 24);
      if(alma.pageno <=6)
        alma.AmEpID = bbitu(291, 2);
      else if(alma.pageno >= 11 && alma.pageno <= 23)
        alma.AmID = bbitu(291, 2);
    }

    return alma.pageno;
    
  }  
};

struct BeidouAlmanacEntry
{
  int sv;
  BeidouMessage::Almanac alma;
};

bool processBeidouAlmanac(const BeidouMessage& bm, struct BeidouAlmanacEntry& bae);
