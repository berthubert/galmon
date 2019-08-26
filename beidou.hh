#pragma once
#include <string>
#include <stdint.h>
#include "bits.hh"
#include <math.h>
std::basic_string<uint8_t> getCondensedBeidouMessage(std::basic_string_view<uint8_t> payload);
int beidouBitconv(int their);


struct BeidouMessage
{
  uint8_t strtype;

  std::basic_string_view<uint8_t> g_cond;
  int bbitu(int bit, int len)
  {
    return getbitu(&g_cond[0], beidouBitconv(bit), len);
  };
  int bbits(int bit, int len)
  {
    return getbits(&g_cond[0], beidouBitconv(bit), len);
  };

  
  int fraid{-1}, sow{-1}; // part of every message (thanks!)
  
  int parse(std::basic_string_view<uint8_t> cond, uint8_t* pageno)
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
  int wn{-1}, a0, a1, a2;
  
  void parse1(std::basic_string_view<uint8_t> cond)
  {
    sath1 = bbitu(43,1 );
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
  uint32_t sqrtA, e;
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
  
  
  void parse2(std::basic_string_view<uint8_t> cond)
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
  void parse3(std::basic_string_view<uint8_t> cond)
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

  // 4 is all almanac
  int parse4(std::basic_string_view<uint8_t> cond)
  {
    return bbitu(44, 7);
  }

  int a0gps, a1gps, a0gal, a1gal, a0glo, a1glo, a0utc, a1utc;
  int8_t deltaTLS;

  int parse5(std::basic_string_view<uint8_t> cond)
  {
    int pageno = bbitu(44, 7);
    if(pageno == 9) {
      a0gps = bbits(97, 14);
      a1gps = bbits(111, 16);
      a0gal = bbits(135, 14);
      a1gal = bbits(157, 16);
      a0glo = bbits(181, 14);
      a1glo = bbits(195, 16);
    }
    if(pageno == 10) {
      a0utc = bbits(91, 32);
      a1utc = bbits(131, 24);
      deltaTLS = bbits(31+12+1+7, 8);
    }
    return pageno;
    
  }  
};

