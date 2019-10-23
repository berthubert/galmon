#include "navmon.hh"
#include "galileo.hh"
#include "gps.hh"
#include "beidou.hh"
#include "glonass.hh"
#include <map>
#include "tle.hh"
using namespace std; // XXX

struct SVPerRecv
{
  int el{-1}, azi{-1}, db{-1};
  time_t deltaHzTime{-1};
  double deltaHz{-1};
  double prres{-1};
  time_t t; // last seen
};
  
struct SVIOD
{
  std::bitset<32> words;
  int gnssid;
  uint32_t t0e; 
  uint32_t e, sqrtA;
  int32_t m0, omega0, i0, omega, idot, omegadot, deltan;
  
  int16_t cuc{0}, cus{0}, crc{0}, crs{0}, cic{0}, cis{0};
  //        60 seconds
  uint16_t t0c; // clock epoch, stored UNSCALED, since it is not in the same place as GPS

  //      2^-34     2^-46
  int32_t af0{0}   ,   af1{0};
  //     2^-59
  int8_t af2{0};
  
  uint8_t sisa;

  uint32_t wn{0}, tow{0};
  bool complete() const
  {
    if(gnssid==2)
      return words[1] && words[2] && words[3] && words[4];
    else
      return words[2] && words[3];
  }
  void addGalileoWord(std::basic_string_view<uint8_t> page);

  double getMu() const
  {
    if(gnssid == 2) return 3.986004418 * pow(10.0, 14.0);
    if(gnssid == 0) return 3.986005    * pow(10.0, 14.0);
    throw std::runtime_error("getMu() called for unsupported gnssid "+to_string(gnssid));
  } // m^3/s^2
  // same for galileo & gps
  double getOmegaE()    const { return 7.2921151467 * pow(10.0, -5.0);} // rad/s

  uint32_t getT0e() const { return t0e; }
  uint32_t getT0c() const { return 60*t0c; }
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


/* Most of thes fields are raw, EXCEPT:
   t0t = seconds, raw fields are 3600 seconds (galileo), 4096 seconds (GPS)
*/

struct SVStat
{
  uint8_t e5bhs{0}, e1bhs{0};
  uint8_t gpshealth{0};
  uint16_t ai0{0};
  int16_t ai1{0}, ai2{0};
  bool sf1{0}, sf2{0}, sf3{0}, sf4{0}, sf5{0};
  int BGDE1E5a{0}, BGDE1E5b{0};
  bool e5bdvs{false}, e1bdvs{false};

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
  int ura, af0, af1,  af2,   t0c, gpsiod; // GPS parameters that should not be here XXX (or perhaps they should)

  GPSAlmanac gpsalma;
  
  // beidou:
  int t0eMSB{-1}, t0eLSB{-1}, aode{-1}, aodc{-1};
  BeidouMessage beidouMessage, oldBeidouMessage;
  BeidouMessage lastBeidouMessage1, lastBeidouMessage2;
  TLERepo::Match tleMatch;
  double lastTLELookupX{0};
  
  // new galileo
  GalileoMessage galmsg;
  map<int, GalileoMessage> galmsgTyped;
  
  // Glonass
  GlonassMessage glonassMessage;
  pair<uint32_t, GlonassMessage> glonassAlmaEven;
  
  map<uint64_t, SVPerRecv> perrecv;

  double latestDisco{-1}, latestDiscoAge{-1}, timeDisco{-1000};
  
  map<int, SVIOD> iods;
  void addGalileoWord(std::basic_string_view<uint8_t> page);
  
  bool completeIOD() const;
  uint16_t getIOD() const;
  SVIOD liveIOD() const;
  SVIOD& getEph(int i) { return iods[i]; }   // XXXX gps adaptor

  pair<int,SVIOD> prevIOD{-1, SVIOD()};
  void clearPrev()
  {
    prevIOD.first = -1;
  }
  void checkCompleteAndClean(int iod);
};


typedef std::map<SatID, SVStat> svstats_t;

// a vector of pairs of latidude,vector<longitude,numsats>
typedef vector<pair<double,vector<tuple<double, int, int, int, double, double, double, double, double, double, double, double, double> > > > covmap_t;
covmap_t emitCoverage(const vector<Point>& sats);
struct xDOP
{
  double gdop{-1};
  double pdop{-1};
  double tdop{-1};
  double hdop{-1};
  double vdop{-1};
};

xDOP getDOP(Point& us, vector<Point> sats);
