#include <stdio.h>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>
#include <map>
#include <bitset>
#include <vector>
#include <thread>
#include <signal.h>
#include "ext/powerblog/h2o-pp.hh"
#include "minicurl.hh"
#include <time.h>
#include "ubx.hh"
#include "bits.hh"
#include "minivec.hh"
#include "navmon.pb.h"
#include "ephemeris.hh"
#include "gps.hh"
#include <optional>
using namespace std;

struct EofException{};

Point g_ourpos(3922.505 * 1000,  290.116 * 1000, 5004.189 * 1000);

struct GNSSReceiver
{
  Point position; 
};


int g_dtLS{18};


string humanSisa(uint8_t sisa)
{
  unsigned int sval = sisa;
  if(sisa < 50)
    return std::to_string(sval)+" cm";
  if(sisa < 75)
    return std::to_string(50 + 2* (sval-50))+" cm";
  if(sisa < 100)
    return std::to_string(100 + 4*(sval-75))+" cm";
  if(sisa < 125)
    return std::to_string(200 + 16*(sval-100))+" cm";
  if(sisa < 255)
    return "SPARE";
  return "NO SIS AVAILABLE";
}

string humanUra(uint8_t ura)
{
  if(ura < 6)
    return fmt::sprintf("%d cm", (int)(100*pow(2.0, 1.0+1.0*ura/2.0)));
  else if(ura < 15)
    return fmt::sprintf("%d m", (int)(pow(2, ura-2)));
  return "NO URA AVAILABLE";
}


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
  int32_t af0   ,   af1;
  //     2^-59
  int8_t af2;
  
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
};

void SVIOD::addGalileoWord(std::basic_string_view<uint8_t> page)
{
  uint8_t wtype = getbitu(&page[0], 0, 6);
  words[wtype]=true;
  gnssid = 2;
  if(wtype == 1) {
    t0e = getbitu(&page[0],   16, 14) * 60;  // WE SCALE THIS FOR THE USER!
    m0 = getbits(&page[0],    30, 32);
    e = getbitu(&page[0],     62, 32);
    sqrtA = getbitu(&page[0], 94, 32);
  }
  else if(wtype == 2) {
    omega0 = getbits(&page[0], 16, 32);
    i0 = getbits(&page[0],     48, 32);
    omega = getbits(&page[0],  80, 32);
    idot = getbits(&page[0],   112, 14);
  }
  else if(wtype == 3) {
    omegadot = getbits(&page[0], 16, 24);
    deltan = getbits(&page[0],   40, 16);
    cuc = getbits(&page[0],      56, 16);
    cus = getbits(&page[0],      72, 16);
    crc = getbits(&page[0],      88, 16);
    crs = getbits(&page[0],     104, 16);
    sisa = getbitu(&page[0],    120, 8);
  }
  else if(wtype == 4) {
    cic = getbits(&page[0], 22, 16);
    cis = getbits(&page[0], 38, 16);

    t0c = getbitu(&page[0], 54, 14);
    af0 = getbits(&page[0], 68, 31);
    af1 = getbits(&page[0], 99, 21);
    af2 = getbits(&page[0], 120, 6);
    /*
    cout<<(int) t0c << " " <<(int) af0 <<" " <<(int) af1 <<" " <<(int) af2<<endl;
    cout<<(int) t0c*60 << " " << (((double) af0) / (1ULL<<34))*1000000  <<" usec " << (((double) af1)/(1ULL << 46))*1000000000000 <<" ps/s"<<endl;
    */
  }

}

struct SVPerRecv
{
  int el{-1}, azi{-1}, db{-1};
  time_t t; // last seen
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
  int ura, af0, af1,  af2,   t0c; // GPS parameters that should not be here XXX
  map<uint64_t, SVPerRecv> perrecv;
  pair<uint32_t, double> deltaHz;
  double latestDisco{-1};
  
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

bool SVStat::completeIOD() const
{
  for(const auto& iod : iods)
    if(iod.second.complete())
      return true;
  return false;
}

uint16_t SVStat::getIOD() const
{
  for(const auto& iod : iods)
    if(iod.second.complete())
      return iod.first;
  throw std::runtime_error("Asked for unknown IOD");
}

SVIOD SVStat::liveIOD() const
{
  if(auto iter = iods.find(getIOD()); iter != iods.end())
    return iter->second;
  throw std::runtime_error("Asked for unknown IOD");
}

void SVStat::checkCompleteAndClean(int iod)
{
  if(iods[iod].complete()) {
    for(const auto& i : iods) {
      if(i.first != iod && i.second.complete())
        prevIOD=i;
    }
    SVIOD latest = iods[iod];
    iods.clear();
    iods[iod] = latest;
  }
}

void SVStat::addGalileoWord(std::basic_string_view<uint8_t> page)
{
  uint8_t wtype = getbitu(&page[0], 0, 6);
  if(wtype == 0) {
    if(getbitu(&page[0], 6,2) == 2) {
      wn = getbitu(&page[0], 96, 12);
      if(tow != getbitu(&page[0], 108, 20)) {
        cerr<<"wtype "<<wtype<<", was about to mis-set TOW, " <<tow<< " " <<getbitu(&page[0], 108, 20) <<endl;
      }

    }
  }
  else if(wtype >=1 && wtype <= 4) { // ephemeris 
    uint16_t iod = getbitu(&page[0], 6, 10);
    iods[iod].addGalileoWord(page);
    checkCompleteAndClean(iod);
  }
  else if(wtype==5) { // disturbance, health, time
    ai0 = getbitu(&page[0], 6, 11);
    ai1 = getbits(&page[0], 17, 11); // ai1 & 2 are signed, 0 not
    ai2 = getbits(&page[0], 28, 14);
    
    sf1 = getbitu(&page[0], 42, 1);
    sf2 = getbitu(&page[0], 43, 1);
    sf3 = getbitu(&page[0], 44, 1);
    sf4 = getbitu(&page[0], 45, 1);
    sf5 = getbitu(&page[0], 46, 1);
    BGDE1E5a = getbits(&page[0], 47, 10);
    BGDE1E5b = getbits(&page[0], 57, 10);
    
    e5bhs = getbitu(&page[0], 67, 2);
    e1bhs = getbitu(&page[0], 69, 2);
    e5bdvs = getbitu(&page[0], 71, 1);
    e1bdvs = getbitu(&page[0], 72, 1);
    wn = getbitu(&page[0], 73, 12);
    if(tow != getbitu(&page[0], 85, 20))
    {
      cerr<<"wtype "<<wtype<<", was about to mis-set TOW"<<endl;
    }
  }
  else if(wtype == 6) {

    a0 = getbits(&page[0], 6, 32);
    a1 = getbits(&page[0], 38, 24);
    dtLS = getbits(&page[0], 62, 8);
    t0t = getbitu(&page[0], 70, 8) * 3600; // WE SCALE THIS FOR THE USER
    wn0t = getbitu(&page[0], 78, 8);
    wnLSF = getbitu(&page[0], 86, 8);
    dn = getbitu(&page[0], 94, 3);
    dtLSF = getbits(&page[0], 97, 8);

    //    cout<<(int) dtLS << " " <<(int) wnLSF<<" " <<(int) dn <<" " <<(int) dtLSF<<endl;
    if(tow != getbitu(&page[0], 105, 20)) {
      cerr<<"wtype "<<wtype<<", was about to mis-set TOW"<<endl;
    }
  }
  else if(wtype == 10) { // GSTT GPS
    a0g = getbits(&page[0], 86, 16);
    a1g = getbits(&page[0], 102, 12);
    t0g = getbitu(&page[0], 114, 8);
    wn0g = getbitu(&page[0], 122, 6);
  }
}

void getSpeed(int wn, double tow, const SVIOD& iod, Vector* v)
{
  Point a, b;
  getCoordinates(wn, tow-0.5, iod, &a);
  getCoordinates(wn, tow+0.5, iod, &b);
  *v = Vector(a, b);
}

std::map<pair<int,int>, SVStat> g_svstats;

int latestWN(int gnssid)
{
  map<int, pair<int,int>> ages;
  for(const auto& s: g_svstats)
    if(s.first.first == gnssid)
      ages[7*s.second.wn*86400 + s.second.tow]= s.first;
  if(ages.empty())
    throw runtime_error("Asked for latest WN for "+to_string(gnssid)+": we don't know it yet");
  return g_svstats[ages.rbegin()->second].wn;
}

int latestTow(int gnssid)
{
  map<int, pair<int,int>> ages;
  for(const auto& s: g_svstats)
    if(s.first.first == gnssid)
      ages[7*s.second.wn*86400 + s.second.tow]= s.first;
  if(ages.empty())
    throw runtime_error("Asked for latest WN: we don't know it yet");
  return g_svstats[ages.rbegin()->second].tow;
}



uint64_t nanoTime(int gnssid, int wn, int tow)
{
  int offset =  (gnssid == 0) ? 315964800 : 935280000;
  
  return 1000000000ULL*(offset + wn * 7*86400 + tow - g_dtLS); // Leap!!
}

uint64_t utcFromGST(int wn, int tow)
{
  return (935280000 + wn * 7*86400 + tow - g_dtLS); 
}

double utcFromGST(int wn, double tow)
{
  return (935280000.0 + wn * 7*86400 + tow - g_dtLS); 
}

double utcFromGPS(int wn, double tow)
{
  return (315964800 + wn * 7*86400 + tow - g_dtLS); 
}



struct InfluxPusher
{
  explicit InfluxPusher(std::string_view dbname) : d_dbname(dbname)
  {
  }
  template <typename T>
  void addValue( const pair<pair<int,int>,SVStat>& ent, string_view name, T value)
  {
    d_buffer+= string(name)+",gnssid="+to_string(ent.first.first)+ +",sv=" +to_string(ent.first.second)+" value="+to_string(value)+
      " "+to_string(nanoTime(ent.first.first, ent.second.wn, ent.second.tow))+"\n";
    checkSend();
  }
  
  template <typename T>
  void addValue(pair<int,int> id, string_view name, T value)
  {
    if(g_svstats[id].wn ==0 && g_svstats[id].tow == 0)
      return;
    d_buffer+= string(name) +",gnssid="+to_string(id.first)+",sv=" +to_string(id.second) + " value="+to_string(value)+" "+
      to_string(nanoTime(id.first, g_svstats[id].wn, g_svstats[id].tow))+"\n";

    checkSend();
  }

  void checkSend()
  {
    if(d_buffer.size() > 1000000 || (time(0) - d_lastsent) > 10) {
      string buffer;
      buffer.swap(d_buffer);
      //      thread t([buffer,this]() {
      if(d_dbname != "null")
          doSend(buffer);
          //        });
          //      t.detach();
      d_lastsent=time(0);
    }
  }
  
  void doSend(std::string buffer)
  {
    MiniCurl mc;
    MiniCurl::MiniCurlHeaders mch;
    if(!buffer.empty()) {
      mc.postURL("http://127.0.0.1:8086/write?db="+d_dbname, buffer, mch);
    }
  }

  ~InfluxPusher()
  {
    doSend(d_buffer);
  }
  
  std::string d_buffer;
  time_t d_lastsent{0};
  string d_dbname;
};

/* The GST start epoch is defined as 13 seconds before midnight between 21st August and
22nd August 1999, i.e. GST was equal to 13 seconds at 22nd August 1999 00:00:00 UTC.  */

std::string humanTime(int wn, int tow)
{
  time_t t = utcFromGST(wn, tow);
  struct tm tm;
  gmtime_r(&t, &tm);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", &tm);
  return buffer;
}

/* |            t0e tow     |  - > tow - t0e, <3.5 days, so ok

   | t0e                tow |   -> tow - t0e > 3.5 days, so 
                                   7*86400 - tow + t0e

   |         tow t0e        |   -> 7*86400 - tow + t0e > 3.5 days, so
                                  tow - t0e (negative age)

   | tow               t0e  |   -> 7*86400 - tow + t0e < 3.5 days, ok
*/

int ephAge(int tow, int t0e)
{
  unsigned int diff;
  unsigned int halfweek = 0.5*7*86400;
  if(t0e < tow) {
    diff = tow - t0e;
    if(diff < halfweek)
      return diff;
    else
      return (7*86400 - tow) + t0e;
  }
  else { // "t0e in future"
    diff = 7*86400 - t0e + tow;
    if(diff < halfweek)
      return diff;
    else
      return tow - t0e; // in the future, negative age
  }
}

std::optional<double> getHzCorrection(time_t now)
{
  int galcount{0}, gpscount{0}, allcount{0};
  double galtot{0}, gpstot{0}, alltot{0};

  for(const auto& s: g_svstats) {
    if(now - s.second.deltaHz.first < 60) {
      alltot+=s.second.deltaHz.second;
      allcount++;
      if(s.first.first == 0) {
        gpstot+=s.second.deltaHz.second;
        gpscount++;
      }
      else if(s.first.first == 2) {
        galtot+=s.second.deltaHz.second;
        galcount++;
      }
    }
  }
  std::optional<double> galHzCorr, gpsHzCorr, allHzCorr;
  if(galcount > 3)
    galHzCorr = galtot/galcount;
  if(gpscount > 3)
    gpsHzCorr = gpstot/gpscount;
  if(allcount > 3)
    allHzCorr = alltot/allcount;

  if(galHzCorr)
    return galHzCorr;
  return allHzCorr;
}

int main(int argc, char** argv)
try
{
  signal(SIGPIPE, SIG_IGN);
  InfluxPusher idb(argc > 3 ? argv[3] : "galileo");
  MiniCurl::init();
  
  H2OWebserver h2s("galmon");

  
  h2s.addHandler("/global", [](auto handler, auto req) {
      nlohmann::json ret = nlohmann::json::object();
      ret["leap-seconds"] = g_dtLS;
      try {
        ret["last-seen"]=utcFromGST(latestWN(2), latestTow(2));
      }
      catch(...)
        {}
      
      map<int, int> utcstats, gpsgststats;
      for(const auto& s: g_svstats) {
        if(!s.second.wn) // this will suck in 20 years
          continue;
        if(s.first.first != 2) // Galileo only
          continue;
        int dw = (uint8_t)s.second.wn - s.second.wn0t;
        int age = dw * 7 * 86400 + s.second.tow - s.second.t0t; // t0t is pre-scaled
        utcstats[age]=s.first.second;

        uint8_t wn0g = s.second.wn0t;
        int dwg = (((uint8_t)s.second.wn)&(1+2+4+8+16+32)) - wn0g;
        age = dwg*7*86400 + s.second.tow - s.second.t0g * 3600;
        gpsgststats[age]=s.first.second;

      }
      if(utcstats.empty()) {
        ret["utc-offset-ns"]=nullptr;
      }
      else {
        int sv = utcstats.begin()->second; // freshest SV
        long shift = g_svstats[{2,sv}].a0 * (1LL<<20) + g_svstats[{2,sv}].a1 * utcstats.begin()->first; // in 2^-50 seconds units
        ret["utc-offset-ns"] = 1.073741824*ldexp(1.0*shift, -20);
        ret["leap-second-planned"] = (g_svstats[{2,sv}].dtLSF != g_svstats[{2,sv}].dtLS);
      }

      if(gpsgststats.empty()) {
        ret["gps-offset-ns"]=nullptr;
      }
      else {
        int sv = gpsgststats.begin()->second; // freshest SV
        long shift = g_svstats[{2,sv}].a0g * (1L<<16) + g_svstats[{2,sv}].a1g * gpsgststats.begin()->first; // in 2^-51 seconds units
        
        ret["gps-offset-ns"] = 1.073741824*ldexp(shift, -21);
      }

      
      return ret;
    });
  
  h2s.addHandler("/svs", [](auto handler, auto req) {
      nlohmann::json ret = nlohmann::json::object();



      auto hzCorrection = getHzCorrection(time(0));
      
      for(const auto& s: g_svstats) {
        nlohmann::json item  = nlohmann::json::object();
        if(!s.second.tow) // I know, I know, will suck briefly
          continue;
        if(s.second.completeIOD()) {
          item["iod"]=s.second.getIOD();
          if(s.first.first ==0) {
            item["sisa"]=humanUra(s.second.ura);
            //            cout<<"Asked to convert "<<s.second.ura<<" for sv "<<s.first.first<<","<<s.first.second<<endl;
          }
          else
            item["sisa"]=humanSisa(s.second.liveIOD().sisa);
          item["eph-age-m"] = ephAge(s.second.tow, s.second.liveIOD().t0e)/60.0;
          item["af0"] = s.second.liveIOD().af0;
          item["af1"] = s.second.liveIOD().af1;
          item["af2"] = (int)s.second.liveIOD().af2;
          item["t0c"] = s.second.liveIOD().t0c;
          
          Point our = g_ourpos;
          Point p;
          Point core;
          
          // this should actually use local time!
          getCoordinates(latestWN(2), latestTow(2), s.second.liveIOD(), &p);
          
          Vector core2us(core, our);
          Vector dx(our, p); //  = x-ourx, dy = y-oury, dz = z-ourz;
          
          // https://ds9a.nl/articles/
          
          double elev = acos ( core2us.inner(dx) / (core2us.length() * dx.length()));
          double deg = 180.0* (elev/M_PI);
          item["elev"] = 90 - deg;
          
          
          item["x"]=p.x;
          item["y"]=p.y;
          item["z"]=p.z;

          if(time(0) - s.second.deltaHz.first < 60) {
            item["delta_hz"] = s.second.deltaHz.second;
            if(hzCorrection)
              item["delta_hz_corr"] = s.second.deltaHz.second - *hzCorrection;
          }
          
        }

        item["a0"]=s.second.a0;
        item["a1"]=s.second.a1;
        item["dtLS"]=s.second.dtLS;
        if(s.first.first == 2) {  // galileo
          item["a0g"]=s.second.a0g;
          item["a1g"]=s.second.a1g;
          item["e5bdvs"]=s.second.e5bdvs;
          item["e1bdvs"]=s.second.e1bdvs;
          item["e5bhs"]=s.second.e5bhs;
          item["e1bhs"]=s.second.e1bhs;
        }
        else
          item["gpshealth"]=s.second.gpshealth;
        
        nlohmann::json perrecv  = nlohmann::json::object();
        for(const auto& pr : s.second.perrecv) {
          nlohmann::json det  = nlohmann::json::object();
          det["elev"] = pr.second.el;
          det["db"] = pr.second.db;
          det["last-seen-s"] = time(0) - pr.second.t;
          perrecv[to_string(pr.first)]=det;
        }
        item["perrecv"]=perrecv;

        item["last-seen-s"] = s.second.tow ? (7*86400*(latestWN(s.first.first) - s.second.wn) + latestTow(s.first.first) - (int)s.second.tow) : -1;
        if(s.second.latestDisco >=0) {
          item["latest-disco"]= s.second.latestDisco;
        }

        
        item["wn"] = s.second.wn;
        item["tow"] = s.second.tow;
        ret[fmt::sprintf("%c%02d", s.first.first ? 'E' : 'G', s.first.second)] = item;
      }
      return ret;
    });
  h2s.addDirectory("/", argc > 2 ? argv[2] : "./html/");

  const char *address = argc > 1 ? argv[1] : "127.0.0.1:29599";
  std::thread ws([&h2s, address]() {
      auto actx = h2s.addContext();
      h2s.addListener(ComboAddress(address), actx);
      cout<<"Listening on "<< address <<endl;
      h2s.runLoop();
    });
  ws.detach();

  ofstream dopplercsv("doppler."+to_string(getpid())+".csv");
  dopplercsv<<"timestamp gnssid sv prmes cpmes doppler preddop distance radvel locktimems iod_age prstd cpstd dostd"<<endl;

  
  try {
  for(;;) {
    char bert[4];
    if(read(0, bert, 4) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"EOF or bad magic"<<endl;
      break;
    }
    
    uint16_t len;
    if(read(0, &len, 2) != 2)
      break;
    len = htons(len);
    char buffer[len];
    if(read(0, buffer, len) != len)
      break;
    
    NavMonMessage nmm;
    nmm.ParseFromString(string(buffer, len));
    if(nmm.type() == NavMonMessage::ReceptionDataType) {
      int gnssid = nmm.rd().gnssid();
      int sv = nmm.rd().gnsssv();
      pair<int,int> id{gnssid, sv};
      g_svstats[id].perrecv[nmm.sourceid()].db = nmm.rd().db();
      g_svstats[id].perrecv[nmm.sourceid()].el = nmm.rd().el();
      g_svstats[id].perrecv[nmm.sourceid()].azi = nmm.rd().azi();

      

      // THIS HAS TO SPLIT OUT PER SOURCE
      idb.addValue(id, "db", nmm.rd().db());
      if(nmm.rd().el() <= 90 && nmm.rd().el() > 0)
        idb.addValue(id, "elev", nmm.rd().el());
      idb.addValue(id, "azi", nmm.rd().azi());            
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      int sv = nmm.gi().gnsssv();
      pair<int, int> id={2,sv};
      g_svstats[id].wn = nmm.gi().gnsswn();

      unsigned int wtype = getbitu(&inav[0], 0, 6);
      if(1) {
        //        cout<<sv <<"\t" << wtype << "\t" << nmm.gi().gnsstow() << "\t"<< nmm.sourceid() << endl;
        /*        if(g_svstats[id].tow > nmm.gi().gnsstow()) {
          cout<<"  wtype "<<wtype<<", was about to set tow backwards for "<<sv<<", "<<g_svstats[id].tow << " > "<<nmm.gi().gnsstow()<<", " << ((signed)g_svstats[id].tow - (signed)nmm.gi().gnsstow()) << ", source "<<nmm.sourceid()<<endl;
          }*/
      }
      g_svstats[id].tow = nmm.gi().gnsstow();

      //      g_svstats[id].perrecv[nmm.sourceid()].wn = nmm.gi().gnsswn();
      //      g_svstats[id].perrecv[nmm.sourceid()].tow = nmm.gi().gnsstow();
      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      //      cout<<"inav for "<<wtype<<" for sv "<<sv<<": ";
      //      for(auto& c : inav)
      //        fmt::printf("%02x ", c);
      //      cout<<"About to add word for galileo sv "<<id.first<<","<<id.second<<": word = "<<wtype<<endl;
        g_svstats[id].addGalileoWord(inav);
        if(g_svstats[id].e1bhs || g_svstats[id].e5bhs || g_svstats[id].e1bdvs || g_svstats[id].e5bdvs) {
          if(sv != 18 && sv != 14) 
            cout << "sv "<<sv<<" health: " << g_svstats[id].e1bhs <<" " << g_svstats[id].e5bhs <<" " << g_svstats[id].e1bdvs <<" "<< g_svstats[id].e5bdvs <<endl;
        }
        
        if(wtype >=1 && wtype <= 4) { // ephemeris 
          uint16_t iod = getbitu(&inav[0], 6, 10);          
          if(wtype == 3) {
            idb.addValue(id, "sisa", g_svstats[id].iods[iod].sisa);
          }
          else if(wtype == 4) {
            
            idb.addValue(id, "af0", g_svstats[id].iods[iod].af0);
            idb.addValue(id, "af1", g_svstats[id].iods[iod].af1);
            idb.addValue(id, "af2", g_svstats[id].iods[iod].af2);
            idb.addValue(id, "t0c", g_svstats[id].iods[iod].t0c * 60);
            
            double age = ephAge(g_svstats[id].tow, g_svstats[id].iods[iod].t0c * 60);
            
            double offset = ldexp(1000.0*(1.0*g_svstats[id].iods[iod].af0 + ldexp(age*g_svstats[id].iods[iod].af1, -12)), -34);
            idb.addValue(id, "atomic_offset_ns", 1000000.0*offset);
          }
          else
            ;
        }
        else if(wtype == 5) {
          idb.addValue(id, "ai0", g_svstats[id].ai0);
          idb.addValue(id, "ai1", g_svstats[id].ai1);
          idb.addValue(id, "ai2", g_svstats[id].ai2);
          
          idb.addValue(id, "sf1", g_svstats[id].sf1);
          idb.addValue(id, "sf2", g_svstats[id].sf2);
          idb.addValue(id, "sf3", g_svstats[id].sf3);
          idb.addValue(id, "sf4", g_svstats[id].sf4);
          idb.addValue(id, "sf5", g_svstats[id].sf5);
          
          idb.addValue(id, "BGDE1E5a", g_svstats[id].BGDE1E5a);
          idb.addValue(id, "BGDE1E5b", g_svstats[id].BGDE1E5b);
          
          idb.addValue(id, "e1bhs", g_svstats[id].e1bhs);
          idb.addValue(id, "e5bhs", g_svstats[id].e5bhs);
          idb.addValue(id, "e5bdvs", g_svstats[id].e5bdvs);
          idb.addValue(id, "e1bdvs", g_svstats[id].e1bdvs);
        }
        else if(wtype == 6) {  // GST-UTC
          idb.addValue(id, "a0", g_svstats[id].a0);
          idb.addValue(id, "a1", g_svstats[id].a1);
          int dw = (uint8_t)g_svstats[id].wn - g_svstats[id].wn0t;
          int age = dw * 7 * 86400 + g_svstats[id].tow - g_svstats[id].t0t; // t0t is PRESCALED
          
          long shift = g_svstats[id].a0 * (1LL<<20) + g_svstats[id].a1 * age; // in 2^-50 seconds units
          idb.addValue(id, "utc_diff_ns", 1.073741824*ldexp(1.0*shift, -20));
          
          g_dtLS = g_svstats[id].dtLS;
        }
        else if(wtype == 10) { // GSTT GPS
          idb.addValue(id, "a0g", g_svstats[id].a0g);
          idb.addValue(id, "a1g", g_svstats[id].a1g);
          idb.addValue(id, "t0g", g_svstats[id].t0g);
          uint8_t wn0g = g_svstats[id].wn0t;
          int dwg = (((uint8_t)g_svstats[id].wn)&(1+2+4+8+16+32)) - wn0g;
          int age = dwg*7*86400 + g_svstats[id].tow - g_svstats[id].t0g * 3600;

          long shift = g_svstats[id].a0g * (1L<<16) + g_svstats[id].a1g * age; // in 2^-51 seconds units
          
          idb.addValue(id, "gps_gst_offset_ns", 1.073741824*ldexp(shift, -21));

          
        }

        for(auto& ent : g_svstats) {
          //        fmt::printf("%2d\t", ent.first);
          id=ent.first;
          if(ent.second.completeIOD() && ent.second.prevIOD.first >= 0) {
            //            time_t t = utcFromGST((int)ent.second.wn, (int)ent.second.tow);
            //          cout << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << "\n";
            
            //            double clockage = ephAge(ent.second.tow, ent.second.liveIOD().t0c * 60);
            //            double offset = 1.0*ent.second.liveIOD().af0/(1LL<<34) + clockage * ent.second.liveIOD().af1/(1LL<<46);

            int ephage = ephAge(ent.second.tow, ent.second.prevIOD.second.t0e);
            if(ent.second.liveIOD().sisa != ent.second.prevIOD.second.sisa) {
              
              cout<<humanTime(ent.second.wn, ent.second.tow)<<" gnssid "<<ent.first.first<<" sv "<<ent.first.second<<" changed sisa from "<<(unsigned int) ent.second.prevIOD.second.sisa<<" ("<<
                humanSisa(ent.second.prevIOD.second.sisa)<<") to " << (unsigned int)ent.second.liveIOD().sisa << " ("<<
                humanSisa(ent.second.liveIOD().sisa)<<"), lastseen = "<< (ephage/3600.0) <<"h"<<endl;
            }
            
            Point p, oldp;
            getCoordinates(ent.second.wn, ent.second.tow, ent.second.liveIOD(), &p);
            //            cout << ent.first << ": iod= "<<ent.second.getIOD()<<" "<< p.x/1000.0 << ", "<< p.y/1000.0 <<", "<<p.z/1000.0<<endl;
            
            //            cout<<"OLD: \n";
            getCoordinates(ent.second.wn, ent.second.tow, ent.second.prevIOD.second, &oldp);
            //            cout << ent.first << ": iod= "<<ent.second.prevIOD.first<<" "<< oldp.x/1000.0 << ", "<< oldp.y/1000.0 <<", "<<oldp.z/1000.0<<endl;

            if(ent.second.prevIOD.second.t0e < ent.second.liveIOD().t0e) {
              double hours = ((ent.second.liveIOD().t0e - ent.second.prevIOD.second.t0e)/3600.0);
              double disco = Vector(p, oldp).length();
              cout<<id.first<<","<<id.second<<" discontinuity after "<< hours<<" hours: "<< disco <<endl;
              idb.addValue(id, "iod-actual", ent.second.getIOD());
              idb.addValue(id, "iod-hours", hours);
              
              
              if(hours < 4) {
                idb.addValue(id, "eph-disco", disco);
                g_svstats[id].latestDisco= disco;
              }
              else
                g_svstats[id].latestDisco= -1;

            
              
              if(0 && hours < 2) {
                ofstream orbitcsv("orbit."+to_string(id.first)+"."+to_string(id.second)+"."+to_string(ent.second.prevIOD.first)+"-"+to_string(ent.second.getIOD())+".csv");
                
                orbitcsv << "timestamp x y z oldx oldy oldz\n";
                orbitcsv << fixed;
                for(int offset = -7200; offset < 7200; offset += 30) {
                  int t = ent.second.liveIOD().t0e + offset;
                  Point p, oldp;
                  getCoordinates(ent.second.wn, t, ent.second.liveIOD(), &p);
                  getCoordinates(ent.second.wn, t, ent.second.prevIOD.second, &oldp);
                  time_t posix = utcFromGST(ent.second.wn, t);
                  orbitcsv << posix <<" "
                           <<p.x<<" " <<p.y<<" "<<p.z<<" "
                           <<oldp.x<<" " <<oldp.y<<" "<<oldp.z<<"\n";
                }
              }
            }
            ent.second.clearPrev();          
          }
        }
    }
    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      // XXX!! this has to deal with source id!
      g_ourpos.x = nmm.op().x();
      g_ourpos.y = nmm.op().y();
      g_ourpos.z = nmm.op().z();
    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      int sv = nmm.rfd().gnsssv();
      pair<int,int> id{nmm.rfd().gnssid(), nmm.rfd().gnsssv()};
      if(g_svstats[id].completeIOD()) {
        Point sat;
        Point us=g_ourpos;

        // be careful with time here - we need to evaluate at the timestamp of this RFDataType update
        // which might be newer than .tow in g_svstats
        getCoordinates(nmm.rfd().rcvwn(), nmm.rfd().rcvtow(), g_svstats[id].liveIOD(), &sat);
        Point core;
        Vector us2sat(us, sat);
        Vector speed;
        getSpeed(nmm.rfd().rcvwn(), nmm.rfd().rcvtow(), g_svstats[id].liveIOD(), &speed);
        //        cout<<sv<<" radius: "<<Vector(core, sat).length()<<",  distance: "<<us2sat.length()<<", orbital velocity: "<<speed.length()/1000.0<<" km/s, ";
        
        Vector core2us(core, us);
        Vector dx(us, sat); //  = x-ourx, dy = y-oury, dz = z-ourz;
        //          double elev = acos ( core2us.inner(dx) / (core2us.length() * dx.length()));
        //double deg = 180.0* (elev/M_PI);
        //          cout <<"elev: "<<90 - deg<< " ("<<g_svstats[id].el<<")\n";
        
        us2sat.norm();
        double radvel=us2sat.inner(speed);
        double c=299792458;
        double freq = 1575.42 * 1000000; // frequency
        double preddop = -freq*radvel/c;
        
        // be careful with time here - 
        double ephage = ephAge(nmm.rfd().rcvtow(), g_svstats[id].liveIOD().t0e);
        //        cout<<"Radial velocity: "<< radvel<<", predicted doppler: "<< preddop << ", measured doppler: "<<nmm.rfd().doppler()<<endl;
        time_t t = utcFromGPS(nmm.rfd().rcvwn(), nmm.rfd().rcvtow());
        dopplercsv << std::fixed << t <<" " << nmm.rfd().gnssid() <<" " <<sv<<" "<<nmm.rfd().pseudorange()<<" "<< nmm.rfd().carrierphase() <<" " << nmm.rfd().doppler()<<" " << preddop << " " << Vector(us, sat).length() << " " <<radvel <<" " << nmm.rfd().locktimems()<<" " <<ephage << " " << nmm.rfd().prstd() << " " << nmm.rfd().cpstd() <<" " << 
          nmm.rfd().dostd() << endl;

        if(t - g_svstats[id].deltaHz.first > 10) {
          g_svstats[id].deltaHz = {t, nmm.rfd().doppler() -  preddop};
          idb.addValue(id, "delta_hz", nmm.rfd().doppler() -  preddop);
          auto corr = getHzCorrection(t);
          if(corr) {
            idb.addValue(id, "delta_hz_cor", nmm.rfd().doppler() -  preddop - *corr);
          }
        }
        
        //        cout<<"Had doppler for "<<id.first<<", "<<id.second<<endl;
      }
    }
    else if(nmm.type()== NavMonMessage::GPSInavType) {
      auto cond = getCondensedGPSMessage(std::basic_string<uint8_t>((uint8_t*)nmm.gpsi().contents().c_str(), nmm.gpsi().contents().size()));
      pair<int,int> id{nmm.gpsi().gnssid(), nmm.gpsi().gnsssv()};

      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      
      auto& svstat = g_svstats[id];
      uint8_t page;
      int frame=parseGPSMessage(cond, svstat, &page);
      if(frame == 1) {
        idb.addValue(id, "af0", 8* svstat.af0); // scaled to galileo units - native gps: 2^-31
        idb.addValue(id, "af1", 8* svstat.af1); // scaled to galileo units - native gps: 2^-43
        idb.addValue(id, "af2", 16* svstat.af2); // scaled to galileo units
        idb.addValue(id, "t0c", 16 * svstat.t0c);
        //        cout<<"Got ura "<<svstat.ura<<" for sv "<<id.first<<","<<id.second<<endl;
        idb.addValue(id, "ura", svstat.ura);

        double age = ephAge(g_svstats[id].tow, g_svstats[id].t0c * 16);
        
        double offset = ldexp(1000.0*(1.0*g_svstats[id].af0 + ldexp(age*g_svstats[id].af1, -12)), -31);
        idb.addValue(id, "atomic_offset_ns", 1000000.0*offset);
      }
      else if(frame==2) {
        idb.addValue(id, "gpshealth", g_svstats[id].gpshealth);
      }
      else if(frame==4 && page==18) {
        idb.addValue(id, "a0", g_svstats[id].a0);
        idb.addValue(id, "a1", g_svstats[id].a1);
        int dw = (uint8_t)g_svstats[id].wn - g_svstats[id].wn0t;
        int age = dw * 7 * 86400 + g_svstats[id].tow - g_svstats[id].t0t; // t0t is PRESCALED
          
        long shift = g_svstats[id].a0 * (1LL<<20) + g_svstats[id].a1 * age; // in 2^-50 seconds units
        idb.addValue(id, "utc_diff_ns", 1.073741824*ldexp(1.0*shift, -20));
      }
      
      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      g_svstats[id].tow = nmm.gpsi().gnsstow();
      g_svstats[id].wn = nmm.gpsi().gnsswn();
      if(g_svstats[id].wn < 512)
        g_svstats[id].wn += 2048;
    }
    else {
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
    
  }
  }
  catch(EofException& e)
    {}
}
catch(std::exception& e)
{
  cerr<<"Exiting because of fatal error "<<e.what()<<endl;
}
