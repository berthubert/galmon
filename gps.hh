#pragma once
#include <bitset>
#include <string>
#include <map>
#include "bits.hh"
#include <iostream>
#include <math.h>
#include "ephemeris.hh"

std::basic_string<uint8_t> getCondensedGPSMessage(std::basic_string_view<uint8_t> payload);


struct GPSAlmanac :  GPSLikeEphemeris
{
  int dataid{-1};
  int sv;
  uint32_t t0a{0}; 
  uint32_t e{0}, sqrtA{0};
  int32_t M0, Omega0, deltai, omega, omegadot;
  int health;
  int af0, af1;

  double getMu() const
  {
    return 3.986005    * pow(10.0, 14.0);
  } // m^3/s^2
    // same for galileo & gps
  double getOmegaE()    const { return 7.2921151467 * pow(10.0, -5.0);} // rad/s

    
  double getE() const
  {
    return ldexp(e, -21);
  }
  uint32_t getT0e() const
  {
    return ldexp(t0a, 12);
  }

  double getI0() const
  {
    return M_PI*0.3 + ldexp(M_PI*deltai, -19);
  }

  double getOmegadot() const
  {
    return ldexp(M_PI * omegadot, -38);
  }

  double getSqrtA() const
  {
    return ldexp(sqrtA, -11);
  }

  double getOmega0() const
  {
    return ldexp(M_PI * Omega0, -23);
  }
  double getOmega() const
  {
    return ldexp(M_PI * omega, -23);
  }

  double getM0() const
  {
    return ldexp(M_PI * M0, -23);
  }
  double getIdot()      const { return 0;   } // radians/s
  double getCic()       const { return 0;   } // radians
  double getCis()       const { return 0;   } // radians
  double getCuc()   const { return 0;   } // radians
  double getCus()   const { return 0;   } // radians
  double getCrc()   const { return 0;   } // meters
  double getCrs()   const { return 0;   } // meters
  double getDeltan()const { return 0; } //radians/s

  int getIOD() const { return 0; } // XXX ioda?
};


struct GPSState :  GPSLikeEphemeris
{
  uint32_t t0e; 
  uint32_t e, sqrtA;
  int32_t m0, omega0, i0, omega, idot, omegadot, deltan;
  int16_t cuc{0}, cus{0}, crc{0}, crs{0}, cic{0}, cis{0};
  //        16 seconds
  uint16_t t0c; 
    
  //      2^-31     2^-43
  int32_t af0   ,   af1;
  //     ???
  int8_t af2;

  double getMu() const
  {
    return 3.986005    * pow(10.0, 14.0);
  } // m^3/s^2
  // same for galileo & gps
  double getOmegaE()    const { return 7.2921151467 * pow(10.0, -5.0);} // rad/s

  uint32_t getT0e() const { return t0e; }
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

  GPSAlmanac gpsalma;
  
  uint8_t gpshealth{0};
  uint16_t ai0{0};
  int16_t ai1{0}, ai2{0};

  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  uint16_t wn{0};  // we put the "unrolled" week number here!
  uint32_t tow{0}; // "last seen"
  //                     
  //      2^-30   2^-50  1      8-bit week
  int32_t a0{0}, a1{0}, t0t{0}, wn0t{0};  
  int32_t a0g{0}, a1g{0}, t0g{0}, wn0g{0};
  int8_t dtLS{0}, dtLSF{0};
  uint16_t wnLSF{0};
  uint8_t dn; // leap second day number
  //   1  2^-31 2^-43 2^-55   16 second
  int ura;

  int gpsiod{-1};

  int getIOD() const
  {
    return gpsiod;
  }
  int parseGPSMessage(std::basic_string_view<uint8_t> cond, uint8_t* pageptr=0);
};

template<typename T>
int getT0c(const T& eph)
{
  return eph.t0c * 16;
}

template<typename T>
std::pair<double, double> getGPSAtomicOffset(int tow, const T& eph)
{
  int delta = ephAge(tow, getT0c(eph));
  double cur = eph.af0  + ldexp(delta*eph.af1, -12) + ldexp(delta*delta*eph.af2, -24);
  double trend = ldexp(eph.af1, -12) + ldexp(2*delta*eph.af2, -24);
  
  // now in units of 2^-31 seconds, which are 0.5 nanoseconds each
  double factor = ldexp(1000000000, -31);
  return {factor * cur, factor * trend};
}

template<typename T>
std::pair<double, double> getGPSUTCOffset(int tow, int wn, const T& eph) 
{
  // 2^-30  2^-50   3600
  // a0     a1      t0t 

  int dw = (int)(uint8_t)wn - (int)(uint8_t) eph.wn0t;
  int delta = dw*7*86400  + tow - eph.t0t; // this is pre-scaled for GPS..

  double cur = eph.a0  + ldexp(1.0*delta*eph.a1, -20);
  double trend = ldexp(eph.a1, -20);
  
  // now in units of 2^-30 seconds, which are ~1.1 nanoseconds each
  
  double factor = ldexp(1000000000, -30);
  return {factor * cur, factor * trend};
}



