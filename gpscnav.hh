#pragma once
#include <bitset>
#include <string>
#include <vector>
#include <map>
#include "bits.hh"
#include <iostream>
#include <math.h>

struct GPSCNavAlmanac
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
  double getT0e() const
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

    
};


struct GPSCNavState
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
    uint32_t wn{0}, tow{0};

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

  
  uint8_t gpshealth{0};
  uint16_t ai0{0};
  int16_t ai1{0}, ai2{0};

  int BGDE1E5a{0}, BGDE1E5b{0};

  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  //                     
  //      2^-30   2^-50  1      8-bit week
  int32_t a0{0}, a1{0}, t0t{0}, wn0t{0};  
  int32_t a0g{0}, a1g{0}, t0g{0}, wn0g{0};
  int8_t dtLS{0}, dtLSF{0};
  uint16_t wnLSF{0};
  uint8_t dn; // leap second day number


  int gpsiod{-1};

  int teop;
  int deltaUT1;
  int deltaUT1dot;

  // milliseconds
  std::pair<double, double> getUT1OffsetMS(int tow)
  {
    int delta = ephAge(tow, 16* teop);
    std::cout<<" tow "<<tow<<" teop "<< 16*teop <<" delta "<<delta<<" " << 1000.0*ldexp(deltaUT1, -24)<<"ms ";
    double cur = ldexp(deltaUT1 + ldexp(delta * deltaUT1dot / 86400.0, -1), -24);
    double trend = ldexp(deltaUT1dot, -25);
    return {1000.0* cur, 1000.0*trend};
  }
  
};

template<typename T>
std::pair<double, double> getGPSCNavAtomicOffset(int tow, const T& eph)
{
  int delta = ephAge(tow, getT0c(eph));
  double cur = eph.af0  + ldexp(delta*eph.af1, -12) + ldexp(delta*delta*eph.af2, -24);
  double trend = ldexp(eph.af1, -12) + ldexp(2*delta*eph.af2, -24);
  
  // now in units of 2^-31 seconds, which are 0.5 nanoseconds each
  double factor = ldexp(1000000000, -31);
  return {factor * cur, factor * trend};
}

template<typename T>
std::pair<double, double> getGPSCNavUTCOffset(int tow, int wn, const T& eph) 
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


    


template<typename T>
int parseGPSCNavMessage(const std::vector<uint8_t>& msg, T& out)
{
  using namespace std;
  int type = getbitu(&msg[0], 14, 6);
  out.tow = 6 * getbitu(&msg[0], 20, 17) - 12;

  if(type == 10) {
    out.wn = getbitu(&msg[0], 38, 13);
    //    out.ura = getbitu(&msg[0], 65, 5);
    out.t0e = getbitu(&msg[0], 70, 11); // XXX scale?

    //    int32_t deltaSqrtA = getbits(&msg[0], 81, 26);
    //    int32_t SqrtADot = getbits(&msg[0], 107, 25);
    out.deltan = getbitu(&msg[0], 132, 17); // XXX scale??
    //    int deltandot = getbitu(&msg[0], 149, 23); // XXX scale??
    //    int64_t M0n = getbitu(&msg[0], 172, 33); // XXX scale??

    out.e = getbitu(&msg[0], 205, 33); // XXX scale??
    out.omega = getbitu(&msg[0], 238, 33); // XXX scale?
  }
  else if(type == 11) {
    out.t0e = getbitu(&msg[0], 38, 11); // XXX scale?
    out.omega0 = getbitu(&msg[0], 49, 33); // XXX scale?
    out.i0 = getbitu(&msg[0], 82, 33); // XXX scale?
    //    int64_t deltaOmegaDot = getbitu(&msg[0], 115, 17); // XXX scale?
    out.idot = getbitu(&msg[0], 132, 15); // XXX scale?
    out.cis = getbits(&msg[0], 147, 16);// XXX scale?
    out.cic = getbits(&msg[0], 163, 16);// XXX scale?
    out.crs = getbits(&msg[0], 179, 24);// XXX scale?
    out.crc = getbits(&msg[0], 203, 24);// XXX scale?
    out.cus = getbits(&msg[0], 227, 21);// XXX scale?
    out.cuc = getbits(&msg[0], 248, 21); // XXX scale?
  }
  else if(type == 32) {
    out.deltaUT1 = getbits(&msg[0], 215, 31);
    out.deltaUT1dot = getbits(&msg[0], 246, 19);
    out.teop = getbitu(&msg[0], 127, 16);
  }
  return type;
}
