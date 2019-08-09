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



struct SVIOD
{
  std::bitset<32> words;
  uint32_t t0e; 
  uint32_t e, sqrtA;
  int32_t m0, omega0, i0, omega, idot, omegadot, deltan;
  
  int16_t cuc{0}, cus{0}, crc{0}, crs{0}, cic{0}, cis{0};
  uint16_t t0c; // clock epoch
  int32_t af0, af1;
  int8_t af2;
  
  uint8_t sisa;

  uint32_t wn{0}, tow{0};
  bool complete() const
  {
    return words[1] && words[2] && words[3] && words[4]; 
  }
  void addWord(std::basic_string_view<uint8_t> page);
};

void SVIOD::addWord(std::basic_string_view<uint8_t> page)
{
  uint8_t wtype = getbitu(&page[0], 0, 6);
  words[wtype]=true;
  if(wtype == 1) {
    t0e = getbitu(&page[0],   16, 14);
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
  uint16_t wn;
  uint32_t tow; // last seen
};
  

struct SVStat
{
  uint8_t e5bhs{0}, e1bhs{0};
  uint16_t ai0{0};
  int16_t ai1{0}, ai2{0};
  bool sf1{0}, sf2{0}, sf3{0}, sf4{0}, sf5{0};
  int BGDE1E5a{0}, BGDE1E5b{0};
  bool e5bdvs{false}, e1bdvs{false};
  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  uint16_t wn{0};
  uint32_t tow{0}; // "last seen"
  int32_t a0{0}, a1{0}, t0t{0}, wn0t{0};
  int32_t a0g{0}, a1g{0}, t0g{0}, wn0g{0};
  int8_t dtLS{0}, dtLSF{0};
  uint16_t wnLSF{0};
  uint8_t dn; // leap second day number

  map<uint64_t, SVPerRecv> perrecv;

  map<int, SVIOD> iods;
  void addWord(std::basic_string_view<uint8_t> page);
  bool completeIOD() const;
  uint16_t getIOD() const;
  SVIOD liveIOD() const;

  pair<int,SVIOD> prevIOD{-1, SVIOD()};
  void clearPrev()
  {
    prevIOD.first = -1;
  }
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

void SVStat::addWord(std::basic_string_view<uint8_t> page)
{
  uint8_t wtype = getbitu(&page[0], 0, 6);

  if(wtype == 0) {
    if(getbitu(&page[0], 6,2) == 2) {
      wn = getbitu(&page[0], 96, 12);
      tow = getbitu(&page[0], 108, 20);
    }
  }
  else if(wtype >=1 && wtype <= 4) { // ephemeris 
    uint16_t iod = getbitu(&page[0], 6, 10);
    iods[iod].addWord(page);
    
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
    tow = getbitu(&page[0], 85, 20);
  }
  else if(wtype == 6) {
    a0 = getbits(&page[0], 6, 32);
    a1 = getbits(&page[0], 38, 24);
    dtLS = getbits(&page[0], 62, 8);

    t0t = getbitu(&page[0], 70, 8);
    wn0t = getbitu(&page[0], 78, 8);
    wnLSF = getbitu(&page[0], 86, 8);
    dn = getbitu(&page[0], 94, 3);
    dtLSF = getbits(&page[0], 97, 8);

    //    cout<<(int) dtLS << " " <<(int) wnLSF<<" " <<(int) dn <<" " <<(int) dtLSF<<endl;
    tow = getbitu(&page[0], 105, 20);
  }
  else if(wtype == 10) { // GSTT GPS
    a0g = getbits(&page[0], 86, 16);
    a1g = getbits(&page[0], 102, 12);
    t0g = getbitu(&page[0], 114, 8);
    wn0g = getbitu(&page[0], 122, 6);
  }
}

double todeg(double rad)
{
  return 360 * rad/(2*M_PI);
}

void getCoordinates(int wn, double tow, const SVIOD& iod, Point* p, bool quiet=true)
{
  // here goes

  constexpr double mu = 3.986004418 * pow(10.0, 14.0);
  constexpr double omegaE = 7.2921151467 * pow(10.0, -5);
  
  double sqrtA = 1.0*iod.sqrtA / (1ULL<<19);
  double deltan = M_PI * 1.0*iod.deltan / (1LL<<43);
  double t0e = 60.0*iod.t0e;
  double m0 = M_PI * 1.0*iod.m0 / (1LL<<31);
  double e = 1.0*iod.e / (1ULL<<33);
  double omega = M_PI * 1.0*iod.omega / (1LL<<31);

  double cuc = 1.0*iod.cuc / (1LL<<29);
  double cus = 1.0*iod.cus / (1LL<<29);

  double crc = 1.0*iod.crc / (1LL<<5);
  double crs = 1.0*iod.crs / (1LL<<5);

  double cic = 1.0*iod.cic / (1LL<<29);
  double cis = 1.0*iod.cis / (1LL<<29);

  double idot = M_PI * 1.0*iod.idot / (1LL<<43);
  double i0 = M_PI * 1.0*iod.i0 / (1LL << 31);

  double Omegadot = M_PI * 1.0*iod.omegadot / (1LL << 43);
  double Omega0 = M_PI * 1.0*iod.omega0 / (1LL << 31);
  
  // NO IOD BEYOND THIS POINT!
  if(!quiet) {
    cout << "sqrtA = "<< sqrtA << endl;
    cout << "deltan = "<< deltan << endl;
    cout << "t0e = "<< t0e << endl;
    cout << "m0 = "<< m0 << " ("<<todeg(m0)<<")"<<endl;
    cout << "e = "<< e << endl;
    cout << "omega = " << omega << " ("<<todeg(omega)<<")"<<endl;
    cout << "idot = " << idot <<endl;
    cout << "i0 = " << i0 << " ("<<todeg(i0)<<")"<<endl;
    
    cout << "cuc = " << cuc << endl;
    cout << "cus = " << cus << endl;
    
    cout << "crc = " << crc << endl;
    cout << "crs = " << crs << endl;
    
    cout << "cic = " << cic << endl;
    cout << "cis = " << cis << endl;

    cout << "Omega0 = " << Omega0 << " ("<<todeg(Omega0)<<")"<<endl;
    cout << "Omegadot = " << Omegadot << " ("<<todeg(Omegadot)<< ")"<<endl;
    
  }
  
  double A = pow(sqrtA, 2.0);
  double A3 = pow(sqrtA, 6.0);

  double n0 = sqrt(mu/A3);
  double tk = tow - t0e; // in seconds, ignores WN!

  double n = n0 + deltan;
  if(!quiet)
    cout <<"tk: "<<tk<<", n0: "<<n0<<", deltan: "<<deltan<<", n: "<<n<<endl;
  
  double M = m0 + n * tk;
  if(!quiet)
    cout << " M = m0 + n * tk = "<<m0 << " + " << n << " * " << tk <<endl;
  double E = M;
  for(int k =0 ; k < 10; ++k) {
    if(!quiet)
      cout<<"M = "<<M<<", E = "<< E << endl;
    E = M + e * sin(E);
  }

  // M = E  - e * sin(E)   -> E = M + e * sin(E)
  
  

  double nu2 = M + e*2*sin(M) +
    e *e * (5.0/4.0) * sin(2*M) -
    e*e*e * (0.25*sin(M) - (13.0/12.0)*sin(3*M));
  double corr = e*e*e*e * (103*sin(4*M) - 44*sin(2*M)) / 96.0 +
    e*e*e*e*e * (1097*sin(5*M) - 645*sin(3*M) + 50 *sin(M))/960.0 +
    e*e*e*e*e*e * (1223*sin(6*M) - 902*sin(4*M) + 85 *sin(2*M))/960.0;



  if(!quiet) {
    double nu1 = atan(   ((sqrt(1-e*e)  * sin(E)) / (1 - e * cos(E)) ) /
                        ((cos(E) - e)/ (1-e*cos(E)))
                        );
    
    double nu2 = atan(   (sqrt(1-e*e) * sin(E)) /
                       (cos(E) - e)
                     );

    double nu3 = 2* atan( sqrt((1+e)/(1-e)) * tan(E/2));
    cout << "e: "<<e<<", M: "<< M<<endl;
    cout <<"         nu sis: "<<nu1<< " / +pi = " << nu1 +M_PI << endl;
    cout <<"         nu ?: "<<nu2<< " / +pi = "  << nu2 +M_PI << endl;
    cout <<"         nu fourier/esa: "<<nu2<< " + " << corr <<" = " << nu2 + corr<<endl;
    cout <<"         nu wikipedia: "<<nu3<< " / +pi = " <<nu3 +M_PI << endl;
  }
  
  double nu = nu2 + corr;

  // https://en.wikipedia.org/wiki/True_anomaly is good
  
  double psi = nu + omega;
  if(!quiet) {
    cout<<"psi = nu + omega = " << nu <<" + "<<omega<< " = " << psi << "\n";
  }


  double deltau = cus * sin(2*psi) + cuc * cos(2*psi);
  double deltar = crs * sin(2*psi) + crc * cos(2*psi);
  double deltai = cis * sin(2*psi) + cic * cos(2*psi);

  double u = psi + deltau;

  double r = A * (1- e * cos(E)) + deltar;

  double xprime = r*cos(u), yprime = r*sin(u);
  if(!quiet) {
    cout<<"u = psi + deltau = "<< psi <<" + " << deltau << " = "<<u<<"\n";
    cout << "calculated r = "<< r << " (" << (r/1000.0) <<"km)"<<endl;
    cout << "xprime: "<<xprime<<", yprime: "<<yprime<<endl;
  }
  double Omega = Omega0 + (Omegadot - omegaE)*tk - omegaE * t0e;
  double i = i0 + deltai + idot * tk;
  p->x = xprime * cos(Omega) - yprime * cos(i) * sin(Omega);
  p->y = xprime * sin(Omega) + yprime * cos(i) * cos(Omega);
  p->z = yprime * sin(i);

  if(!quiet) {
    Point core(0.0, .0, .0);
    Vector radius(core, *p);
    cout << radius.length() << " calculated r "<<endl;
  }
  
}


void getSpeed(int wn, double tow, const SVIOD& iod, Vector* v)
{
  Point a, b;
  getCoordinates(wn, tow-0.5, iod, &a);
  getCoordinates(wn, tow+0.5, iod, &b);
  *v = Vector(a, b);
}

std::map<int, SVStat> g_svstats;

int latestWN()
{
  map<int, int> ages;
  for(const auto& s: g_svstats) 
    ages[7*s.second.wn*86400 + s.second.tow]= s.first;
  if(ages.empty())
    throw runtime_error("Asked for latest WN: we don't know it yet");
  return g_svstats[ages.rbegin()->second].wn;
}

int latestTow()
{
  map<int, int> ages;
  for(const auto& s: g_svstats) 
    ages[7*s.second.wn*86400 + s.second.tow]= s.first;
  if(ages.empty())
    throw runtime_error("Asked for latest WN: we don't know it yet");
  return g_svstats[ages.rbegin()->second].tow;
}



uint64_t nanoTime(int wn, int tow)
{
  return 1000000000ULL*(935280000 + wn * 7*86400 + tow - g_dtLS); // Leap!!
}

uint64_t utcFromGST(int wn, int tow)
{
  return (935280000 + wn * 7*86400 + tow - g_dtLS); // Leap!!
}

double utcFromGST(int wn, double tow)
{
  return (935280000.0 + wn * 7*86400 + tow - g_dtLS); // Leap!!
}


struct InfluxPusher
{
  explicit InfluxPusher(std::string_view dbname) : d_dbname(dbname)
  {
  }
  void addValue( const pair<int,SVStat>& ent, string_view name, auto value)
  {
    d_buffer+= string(name) +",sv=" +to_string(ent.first)+" value="+to_string(value)+
      " "+to_string(nanoTime(ent.second.wn, ent.second.tow))+"\n";
    checkSend();
  }
  
  void addValue(int sv, string_view name, auto value)
  {
    d_buffer+= string(name) +",sv=" +to_string(sv) + " value="+to_string(value)+" "+
      to_string(nanoTime(g_svstats[sv].wn, g_svstats[sv].tow))+"\n";
    checkSend();
  }

  void checkSend()
  {
    if(d_buffer.size() > 1000000 || (time(0) - d_lastsent) > 10) {
      string buffer;
      buffer.swap(d_buffer);
      //      thread t([buffer,this]() {
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

      map<int, int> utcstats, gpsgststats;
      for(const auto& s: g_svstats) {
        if(!s.second.wn) // this will suck in 20 years
          continue;
        int dw = (uint8_t)s.second.wn - s.second.wn0t;
        int age = dw * 7 * 86400 + s.second.tow - s.second.t0t * 3600;
        utcstats[age]=s.first;

        uint8_t wn0g = s.second.wn0t;
        int dwg = (((uint8_t)s.second.wn)&(1+2+4+8+16+32)) - wn0g;
        age = dwg*7*86400 + s.second.tow - s.second.t0g * 3600;
        gpsgststats[age]=s.first;

      }
      if(utcstats.empty()) {
        ret["utc-offset-ns"]=nullptr;
      }
      else {
        int sv = utcstats.begin()->second; // freshest SV
        long shift = g_svstats[sv].a0 * (1LL<<20) + g_svstats[sv].a1 * utcstats.begin()->first; // in 2^-50 seconds units
        //        cout<<"sv: "<<sv<<", shift: "<<shift<<" a0: "<< g_svstats[sv].a0 << ", a1: "<< g_svstats[sv].a1 <<", age: "<< utcstats.begin()->first<<endl;
        ret["utc-offset-ns"] = 1.073741824*ldexp(1.0*shift, -20);

        ret["leap-second-planned"] = (g_svstats[sv].dtLSF != g_svstats[sv].dtLS);
      }

      if(gpsgststats.empty()) {
        ret["gps-offset-ns"]=nullptr;
      }
      else {
        int sv = gpsgststats.begin()->second; // freshest SV
        long shift = g_svstats[sv].a0g * (1L<<16) + g_svstats[sv].a1g * gpsgststats.begin()->first; // in 2^-51 seconds units
        
        ret["gps-offset-ns"] = 1.073741824*ldexp(shift, -21);
      }

      
      return ret;
    });
  
  h2s.addHandler("/svs", [](auto handler, auto req) {
      nlohmann::json ret = nlohmann::json::object();
      for(const auto& s: g_svstats)
        if(s.second.completeIOD()) {
          nlohmann::json item  = nlohmann::json::object();
          
          item["iod"]=s.second.getIOD();
          item["sisa"]=humanSisa(s.second.liveIOD().sisa);
          item["a0"]=s.second.a0;
          item["a1"]=s.second.a1;
          item["dtLS"]=s.second.dtLS;
          item["a0g"]=s.second.a0g;
          item["a1g"]=s.second.a1g;
          item["e5bdvs"]=s.second.e5bdvs;
          item["e1bdvs"]=s.second.e1bdvs;
          item["e5bhs"]=s.second.e5bhs;
          item["e1bhs"]=s.second.e1bhs;                    

          nlohmann::json perrecv  = nlohmann::json::object();
          for(const auto& pr : s.second.perrecv) {
            nlohmann::json det  = nlohmann::json::object();
            det["elev"] = pr.second.el;
            det["db"] = pr.second.db;
            det["last-seen-s"] = (7*86400*(latestWN() - pr.second.wn) + latestTow() - (int)pr.second.tow);
            perrecv[to_string(pr.first)]=det;
          }
          item["perrecv"]=perrecv;
          item["eph-age-m"] = ephAge(s.second.tow, 60*s.second.liveIOD().t0e)/60.0;
          item["last-seen-s"] = s.second.tow ? (7*86400*(s.second.wn - s.second.wn) + latestTow() - (int)s.second.tow) : -1;

          item["af0"] = s.second.liveIOD().af0;
          item["af1"] = s.second.liveIOD().af1;
          item["af2"] = (int)s.second.liveIOD().af2;
          item["t0c"] = s.second.liveIOD().t0c;

          Point our = g_ourpos;
          Point p;
          Point core;

          // this should actually use local time!
          getCoordinates(latestWN(), latestTow(), s.second.liveIOD(), &p);

          Vector core2us(core, our);
          Vector dx(our, p); //  = x-ourx, dy = y-oury, dz = z-ourz;

          // https://ds9a.nl/articles/
          
          double elev = acos ( core2us.inner(dx) / (core2us.length() * dx.length()));
          double deg = 180.0* (elev/M_PI);
          item["elev"] = 90 - deg;
          

          item["x"]=p.x;
          item["y"]=p.y;
          item["z"]=p.z;
          item["wn"] = s.second.wn;
          item["tow"] = s.second.tow;
          ret[std::to_string(s.first)] = item;
        }
      return ret;
    });
  h2s.addDirectory("/", argc > 2 ? argv[2] : "./html/");

  int port = argc > 1 ? atoi(argv[1]) : 29599;
  std::thread ws([&h2s, port]() {
      auto actx = h2s.addContext();
      h2s.addListener(ComboAddress("::", port), actx);
      cout<<"Listening on port "<< port <<endl;
      h2s.runLoop();
    });
  ws.detach();

  
  ofstream csv("iod.csv");
  ofstream csv2("toe.csv");
  csv<<"timestamp sv iod sisa"<<endl;
  csv2<<"timestamp sv tow toe"<<endl;
  ofstream gstutc("gstutc.csv");
  gstutc << "timestamp sv tow t0t age rawshift nsecshift a0 a1" <<endl;
  ofstream gstgps("gstgps.csv");
  gstgps << "timestamp sv tow t0g age rawshift nsecshift a0g a1g" <<endl;

  ofstream sisacsv("sisa.csv");
  
  sisacsv << "timestamp sv sisa"<<endl;

  ofstream clockcsv("clock.csv");
  clockcsv <<"timestamp sv af0 af1 af2 t0c age offset"<<endl;

  ofstream dopplercsv("doppler.csv");
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
      int sv = nmm.rd().gnsssv();
      g_svstats[sv].perrecv[nmm.sourceid()].db = nmm.rd().db();
      g_svstats[sv].perrecv[nmm.sourceid()].el = nmm.rd().el();
      g_svstats[sv].perrecv[nmm.sourceid()].azi = nmm.rd().azi();
      
      //      idb.addValue(sv, "db", g_svstats[sv].db);
      // idb.addValue(sv, "elev", g_svstats[sv].el);
      // idb.addValue(sv, "azi", g_svstats[sv].azi);            
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      int sv = nmm.gi().gnsssv();
      g_svstats[sv].wn = nmm.gi().gnsswn();
      g_svstats[sv].tow = nmm.gi().gnsstow();

      g_svstats[sv].perrecv[nmm.sourceid()].wn = nmm.gi().gnsswn();
      g_svstats[sv].perrecv[nmm.sourceid()].tow = nmm.gi().gnsstow();
      
      //      cout<<"inav for "<<wtype<<" for sv "<<sv<<": ";
      //      for(auto& c : inav)
      //        fmt::printf("%02x ", c);
      
      unsigned int wtype = getbitu(&inav[0], 0, 6);
        
        g_svstats[sv].addWord(inav);
        if(g_svstats[sv].e1bhs || g_svstats[sv].e5bhs || g_svstats[sv].e1bdvs || g_svstats[sv].e5bdvs) {
          if(sv != 18 && sv != 14) 
            cout << "sv "<<sv<<" health: " << g_svstats[sv].e1bhs <<" " << g_svstats[sv].e5bhs <<" " << g_svstats[sv].e1bdvs <<" "<< g_svstats[sv].e5bdvs <<endl;
        }
        
        if(wtype >=1 && wtype <= 4) { // ephemeris 
          uint16_t iod = getbitu(&inav[0], 6, 10);          
          if(wtype == 3) {
            idb.addValue(sv, "sisa", g_svstats[sv].iods[iod].sisa);
          }
          else if(wtype == 4) {
            
            idb.addValue(sv, "af0", g_svstats[sv].iods[iod].af0);
            idb.addValue(sv, "af1", g_svstats[sv].iods[iod].af1);
            idb.addValue(sv, "af2", g_svstats[sv].iods[iod].af2);
            idb.addValue(sv, "t0c", g_svstats[sv].iods[iod].t0c);
            
            double age = ephAge(g_svstats[sv].tow, g_svstats[sv].iods[iod].t0c * 60);
            
            double offset = ldexp(1000.0*(1.0*g_svstats[sv].iods[iod].af0 + ldexp(age*g_svstats[sv].iods[iod].af1, -12)), -34);
            idb.addValue(sv, "atomic_offset_ns", 1000000.0*offset);
          }
          else
            ;
        }
        else if(wtype == 5) {
          idb.addValue(sv, "ai0", g_svstats[sv].ai0);
          idb.addValue(sv, "ai1", g_svstats[sv].ai1);
          idb.addValue(sv, "ai2", g_svstats[sv].ai2);
          
          idb.addValue(sv, "sf1", g_svstats[sv].sf1);
          idb.addValue(sv, "sf2", g_svstats[sv].sf2);
          idb.addValue(sv, "sf3", g_svstats[sv].sf3);
          idb.addValue(sv, "sf4", g_svstats[sv].sf4);
          idb.addValue(sv, "sf5", g_svstats[sv].sf5);
          
          idb.addValue(sv, "BGDE1E5a", g_svstats[sv].BGDE1E5a);
          idb.addValue(sv, "BGDE1E5b", g_svstats[sv].BGDE1E5b);
          
          idb.addValue(sv, "e1bhs", g_svstats[sv].e1bhs);
          idb.addValue(sv, "e5bhs", g_svstats[sv].e5bhs);
          idb.addValue(sv, "e5bdvs", g_svstats[sv].e5bdvs);
          idb.addValue(sv, "e1bdvs", g_svstats[sv].e1bdvs);
        }
        else if(wtype == 6) {  // GST-UTC
          idb.addValue(sv, "a0", g_svstats[sv].a0);
          idb.addValue(sv, "a1", g_svstats[sv].a1);
          
          g_dtLS = g_svstats[sv].dtLS;
        }
        else if(wtype == 10) { // GSTT GPS
          idb.addValue(sv, "a0g", g_svstats[sv].a0g);
          idb.addValue(sv, "a1g", g_svstats[sv].a1g);
          idb.addValue(sv, "t0g", g_svstats[sv].t0g);
        }

        for(auto& ent : g_svstats) {
          //        fmt::printf("%2d\t", ent.first);
          if(ent.second.completeIOD() && ent.second.prevIOD.first >= 0) {
            time_t t = utcFromGST((int)ent.second.wn, (int)ent.second.tow);
            sisacsv << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << endl;
            //          cout << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << "\n";
            
            double clockage = ephAge(ent.second.tow, ent.second.liveIOD().t0c * 60);
            double offset = 1.0*ent.second.liveIOD().af0/(1LL<<34) + clockage * ent.second.liveIOD().af1/(1LL<<46);
            clockcsv << t << " " << ent.first<<" " << ent.second.liveIOD().af0 << " " << ent.second.liveIOD().af1 <<" " << (int)ent.second.liveIOD().af2 <<" " << 935280000 + ent.second.wn *7*86400 + ent.second.liveIOD().t0c * 60 << " " << clockage << " " << offset<<endl;
            int ephage = ephAge(ent.second.tow, ent.second.prevIOD.second.t0e * 60);
            if(ent.second.liveIOD().sisa != ent.second.prevIOD.second.sisa) {
              
              cout<<humanTime(ent.second.wn, ent.second.tow)<<" sv "<<ent.first<<" changed sisa from "<<(unsigned int) ent.second.prevIOD.second.sisa<<" ("<<
                humanSisa(ent.second.prevIOD.second.sisa)<<") to " << (unsigned int)ent.second.liveIOD().sisa << " ("<<
                humanSisa(ent.second.liveIOD().sisa)<<"), lastseen = "<< (ephage/3600.0) <<"h"<<endl;
            }
            
            Point p, oldp;
            getCoordinates(ent.second.wn, ent.second.tow, ent.second.liveIOD(), &p);
            //            cout << ent.first << ": iod= "<<ent.second.getIOD()<<" "<< p.x/1000.0 << ", "<< p.y/1000.0 <<", "<<p.z/1000.0<<endl;
            
            //            cout<<"OLD: \n";
            getCoordinates(ent.second.wn, ent.second.tow, ent.second.prevIOD.second, &oldp);
            //            cout << ent.first << ": iod= "<<ent.second.prevIOD.first<<" "<< oldp.x/1000.0 << ", "<< oldp.y/1000.0 <<", "<<oldp.z/1000.0<<endl;
            
            double hours = ((ent.second.liveIOD().t0e - ent.second.prevIOD.second.t0e)/60.0);
            double disco = Vector(p, oldp).length();
            cout<<ent.first<<" discontinuity after "<< hours<<" hours: "<< disco <<endl;
            idb.addValue(sv, "iod-actual", ent.second.getIOD());
            idb.addValue(sv, "iod-hours", hours);
            
            
            if(hours < 4)
              idb.addValue(sv, "eph-disco", disco);
            
            
            if(0 && hours < 2) {
              ofstream orbitcsv("orbit."+to_string(ent.first)+"."+to_string(ent.second.prevIOD.first)+"-"+to_string(ent.second.getIOD())+".csv");
              
              orbitcsv << "timestamp x y z oldx oldy oldz\n";
              orbitcsv << fixed;
              for(int offset = -7200; offset < 7200; offset += 30) {
                int t = ent.second.liveIOD().t0e * 60 + offset;
                Point p, oldp;
                getCoordinates(ent.second.wn, t, ent.second.liveIOD(), &p);
                getCoordinates(ent.second.wn, t, ent.second.prevIOD.second, &oldp);
                time_t posix = utcFromGST(ent.second.wn, t);
                orbitcsv << posix <<" "
                         <<p.x<<" " <<p.y<<" "<<p.z<<" "
                         <<oldp.x<<" " <<oldp.y<<" "<<oldp.z<<"\n";
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
      if(nmm.rfd().gnssid() ==2 && g_svstats[sv].completeIOD()) {
        Point sat;
        Point us=g_ourpos;

        
        getCoordinates(g_svstats[sv].wn, nmm.rfd().rcvtow(), g_svstats[sv].liveIOD(), &sat);
          Point core;
          Vector us2sat(us, sat);
          Vector speed;
          getSpeed(g_svstats[sv].wn, nmm.rfd().rcvtow(), g_svstats[sv].liveIOD(), &speed);
          cout<<sv<<" radius: "<<Vector(core, sat).length()<<",  distance: "<<us2sat.length()<<", orbital velocity: "<<speed.length()/1000.0<<" km/s, ";

          Vector core2us(core, us);
          Vector dx(us, sat); //  = x-ourx, dy = y-oury, dz = z-ourz;
          //          double elev = acos ( core2us.inner(dx) / (core2us.length() * dx.length()));
          //double deg = 180.0* (elev/M_PI);
          //          cout <<"elev: "<<90 - deg<< " ("<<g_svstats[sv].el<<")\n";

          us2sat.norm();
          double radvel=us2sat.inner(speed);
          double c=299792458;
          double galileol1f = 1575.42 * 1000000; // frequency
          double preddop = -galileol1f*radvel/c;
          
          double ephage = ephAge(g_svstats[sv].tow, g_svstats[sv].liveIOD().t0e*60);
          cout<<"Radial velocity: "<< radvel<<", predicted doppler: "<< preddop << ", measured doppler: "<<nmm.rfd().doppler()<<endl;
          dopplercsv << std::fixed << utcFromGST(g_svstats[sv].wn, nmm.rfd().rcvtow()) <<" " << nmm.rfd().gnssid() <<" " <<sv<<" "<<nmm.rfd().pseudorange()<<" "<< nmm.rfd().carrierphase() <<" " << nmm.rfd().doppler()<<" " << preddop << " " << Vector(us, sat).length() << " " <<radvel <<" " << nmm.rfd().locktimems()<<" " <<ephage << " " << nmm.rfd().prstd() << " " << nmm.rfd().cpstd() <<" " << 
            nmm.rfd().dostd() << endl;
        }


      
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
