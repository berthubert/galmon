#include <stdio.h>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>
#include <map>
#include <bitset>
#include <curses.h>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <thread>
#include <signal.h>
#include "ext/powerblog/h2o-pp.hh"
#include "minicurl.hh"
#include <time.h>
#include "ubx.hh"
#include "bits.hh"

using namespace std;

struct EofException{};

uint8_t getUint8()
{
  int c;
  c = fgetc(stdin);
  if(c == -1)
    throw EofException();
  return (uint8_t) c;
}

uint16_t getUint16()
{
  uint16_t ret;
  int res = fread(&ret, 1, sizeof(ret), stdin);
  if(res != sizeof(ret))
    throw EofException();
  //  ret = ntohs(ret);
  return ret;
}


int g_tow, g_wn, g_dtLS{18};

/* inav schedule:
t   n   w
0   1:  2          wn % 30 == 0
2   2:  4          wn % 30 == 2
4   3:  6
6   4:  7/9
8   5:  8/10       
10  6:  0          WN/TOW
12  7:  0          WN/TOW
14  8:  0          WN/TOW
16  9:  0          WN/TOW
18  10: 0          WN/TOW
20  11: 1          
22  12: 3
24  13: 5
26  14: 0          WN/TOW
28  15: 0          WN/TOW
*/


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
  int16_t el{-1}, azi{-1}, db{-1};
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
      g_wn = wn = getbitu(&page[0], 96, 12);
      g_tow = tow = getbitu(&page[0], 108, 20);
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
    g_wn = wn = getbitu(&page[0], 73, 12);
    g_tow = tow = getbitu(&page[0], 85, 20);
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
    g_tow = tow = getbitu(&page[0], 105, 20);
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

void getCoordinates(int wn, int tow, const SVIOD& iod, double* x, double* y, double* z, bool quiet=true)
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
  *x = xprime * cos(Omega) - yprime * cos(i) * sin(Omega);
  *y = xprime * sin(Omega) + yprime * cos(i) * cos(Omega);
  *z = yprime * sin(i);

  if(!quiet)
    cout << sqrt( (*x)*(*x) + (*y)*(*y) + (*z)*(*z)) << " calculated r "<<endl;
  
}


std::map<int, SVStat> g_svstats;

uint64_t nanoTime(int wn, int tow)
{
  return 1000000000ULL*(935280000 + wn * 7*86400 + tow - g_dtLS); // Leap!!
}

uint64_t utcFromGST(int wn, int tow)
{
  return (935280000 + wn * 7*86400 + tow - g_dtLS); // Leap!!
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
  
  // note: this version trusts the system-wide g_wn, g_tow ovre the local one
  void addValue(int sv, string_view name, auto value)
  {
    d_buffer+= string(name) +",sv=" +to_string(sv) + " value="+to_string(value)+" "+
      to_string(nanoTime(g_wn, g_tow))+"\n";
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

      map<int, int> utcstats;
      for(const auto& s: g_svstats) {
        int dw = (uint8_t)g_wn - s.second.wn0t;
        int age = dw * 7 * 86400 + g_tow - s.second.t0t * 3600;
        utcstats[age]=s.first;
      }
      if(utcstats.empty()) {
        ret["utc-offset"]=nullptr;
      }
      else {
        int sv = utcstats.begin()->second; // freshest SV
        long shift = g_svstats[sv].a0 * (1LL<<20) + g_svstats[sv].a1 * utcstats.begin()->first; // in 2^-50 seconds units
        ret["utc-offset-ns"] = 1.073741824*ldexp(shift, -20);

        ret["leap-second-planned"] = (g_svstats[sv].dtLSF != g_svstats[sv].dtLS);
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
          item["elev"]=s.second.el;
          item["db"]=s.second.db;
          item["eph-age-m"] = ephAge(g_tow, 60*s.second.liveIOD().t0e)/60.0;
          item["last-seen-s"] = s.second.tow ? (7*86400*(g_wn - s.second.wn) + (int)g_tow - (int)s.second.tow) : -1;

          item["af0"] = s.second.liveIOD().af0;
          item["af1"] = s.second.liveIOD().af1;
          item["af2"] = (int)s.second.liveIOD().af2;
          item["t0c"] = s.second.liveIOD().t0c;

          /* Our location:
X : 3922.505   km
Y : 290.116   km
Z : 5004.189   km
          */

          double ourx = 3922.505 * 1000;
          double oury = 290.116 * 1000;
          double ourz = 5004.189 * 1000;
          double x, y, z;
          //          cout<<"For sv " <<s.first<<" at "<<humanTime(g_wn, g_tow)<<": \n";
          getCoordinates(g_wn, g_tow, s.second.liveIOD(), &x, &y, &z);

          double dx = x-ourx, dy = y-oury, dz = z-ourz;
          
          /* https://gis.stackexchange.com/questions/58923/calculating-view-angle
             to calculate elevation:
             Cos(elevation) = (x*dx + y*dy + z*dz) / Sqrt((x^2+y^2+z^2)*(dx^2+dy^2+dz^2))
             Obtain its principal inverse cosine. Subtract this from 90 degrees if you want 
             the angle of view relative to a nominal horizon. This is the "elevation."
             NOTE! x = you on the ground!
          */
          double elev = acos ( (ourx*dx + oury*dy + ourz*dz) / (sqrt(ourx*ourx + oury*oury + ourz*ourz) * sqrt(dx*dx + dy*dy + dz*dz)));

          double deg = 180.0* (elev/M_PI);
          
          item["calc-elev"] = 90 - deg;
          /*
          cout<<s.first<<" calc elev radians "<< elev << ", deg "<< deg <<" calc-elev "<<(90-deg)<<", from ublox "<< s.second.el<<endl <<std::fixed<<" (" << ourx << ", "<< oury <<", "<<ourz<<") -> ("
              << x << ", "<< y <<", "<< z<<") " << sqrt(ourx*ourx + oury*oury + ourz*ourz) <<" - " << sqrt(x*x+y*y+z*z) <<endl;
          */
          item["x"]=x;
          item["y"]=y;
          item["z"]=z;
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

  string line;
  time_t lastDisplay = time(0);
  try {
  for(;;) {
    auto c = getUint8();
    if(c != 0xb5) {
      line.append(1, c);
      //      cout << (char)c;
      /*
             msgs num  svs   sv el az  db sv el az  db sv el az db  sv el az  db gnssid
      $GAGSV,3,   1,   09,   02,31,073,34,07,84,296,42,08,40,081,37,12,03,340,  ,0*7A
      $GAGSV,3,   2,   09,   13,07,191,16,25,11,023,  ,26,36,235,33,30,21,129,38,0*75
      $GAGSV,3,   3,   09,   33,34,300,38,0*42
      */
      if(c=='\n') {
        if(line.rfind("$GAGSV", 0)==0) {
          vector<string> strs;
          boost::split(strs,line,boost::is_any_of(","));
          for(unsigned int n=4; n + 4 < strs.size(); n += 4) {
            int sv = atoi(strs[n].c_str());

            if(sv < 37) {            
              g_svstats[sv].el = atoi(strs[n+1].c_str());
              g_svstats[sv].azi = atoi(strs[n+2].c_str());            
              if(g_svstats[sv].el >= 0)
                g_svstats[sv].db = atoi(strs[n+3].c_str());
              else
                g_svstats[sv].db = -1;

              idb.addValue(sv, "db", g_svstats[sv].db);
              idb.addValue(sv, "elev", g_svstats[sv].el);
              idb.addValue(sv, "azi", g_svstats[sv].azi);            
            }
          }
        }
        line.clear();
      }
      continue;
    }
    c = getUint8();
    if(c != 0x62) {
      ungetc(c, stdin); // might be 0xb5
      continue;
    }
    if(lastDisplay != time(0)) {
      lastDisplay = time(0);
      if(g_wn && g_tow) {
        cout<<"UTC from satellite: "<<humanTime(g_wn, g_tow)<<", delta: "<<lastDisplay - utcFromGST(g_wn, g_tow) << endl;
      }
    }
    // if we are here, just had ubx header

    uint8_t ubxClass = getUint8();
    uint8_t ubxType = getUint8();
    uint16_t msgLen = getUint16();
    
    //    cout <<"Had an ubx message of class "<<(int) ubxClass <<" and type "<< (int) ubxType << " of " << msgLen <<" bytes"<<endl;

    std::basic_string<uint8_t> msg;
    msg.reserve(msgLen);
    for(int n=0; n < msgLen; ++n)
      msg.append(1, getUint8());

    uint16_t ubxChecksum = getUint16();
    if(ubxChecksum != calcUbxChecksum(ubxClass, ubxType, msg)) {
      cout<<humanTime(g_wn, g_tow)<<": len = "<<msgLen<<", sv ? "<<(int)msg[2]<<", checksum: "<<ubxChecksum<< ", calculated: "<<
        calcUbxChecksum(ubxClass, ubxType, msg)<<"\n";
    }

    if(ubxClass == 2 && ubxType == 89) { // SAR
      string hexstring;
      for(int n = 0; n < 15; ++n)
        hexstring+=fmt::format("%x", (int)getbitu(msg.c_str(), 36 + 4*n, 4));
      
      //      int sv = (int)msg[2];
      //      wk.emitLine(sv, "SAR "+hexstring);
      //      cout<<"SAR: sv = "<< (int)msg[2] <<" ";
      //      for(int n=4; n < 12; ++n)
      //        fmt::printf("%02x", (int)msg[n]);

      //      for(int n = 0; n < 15; ++n)
      //        fmt::printf("%x", (int)getbitu(msg.c_str(), 36 + 4*n, 4));
      
      //      cout << " Type: "<< (int) msg[12] <<"\n";
      //      cout<<"Parameter: (len = "<<msg.length()<<") ";
      //      for(unsigned int n = 13; n < msg.length(); ++n)
      //        fmt::printf("%02x ", (int)msg[n]);
      //      cout<<"\n";
    }
    if(ubxClass == 2 && ubxType == 19) { //UBX-RXM-SFRBX
      //      cout<<"SFRBX GNSSID "<< (int)msg[0]<<", SV "<< (int)msg[1];
      //      cout<<" words "<< (int)msg[4]<<", version "<< (int)msg[6];
      if(msg[0] == 2) {
        //        cout<<" word "<< (int)(msg[11] & (~(64+128)));
      }
      //      cout<<"\n";
      if(msg[0] != 2) // galileo
        continue;

      unsigned int sv = (int)msg[1];
      //      cout << "Word type "<< (int)(msg[11] & (~(64+128))) <<" SV " << (int)msg[1]<<"\n";
      //      for(unsigned int n = 8; n < msg.size() ; ++n) {
      //        fmt::printf("%02x ", msg[n]);
      //      }
      //      cout<<"\n";
      std::basic_string<uint8_t> payload;
      for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
        for(int j=1; j <= 4; ++j)
          payload.append(1, msg[8 + (i+1) * 4 -j]);


      /* test crc (4(pad) + 114 + 82 bits) */
      unsigned char crc_buff[26]={0};
      unsigned int i,j;
      for (i=0,j=  4;i<15;i++,j+=8) setbitu(crc_buff,j,8,getbitu(payload.c_str()   ,i*8,8));
      for (i=0,j=118;i<11;i++,j+=8) setbitu(crc_buff,j,8,getbitu(payload.c_str()+16,i*8,8));
      if (rtk_crc24q(crc_buff,25) != getbitu(payload.c_str()+16,82,24)) {
        cout << "CRC mismatch, " << rtk_crc24q(crc_buff, 25) << " != " << getbitu(payload.c_str()+16,82,24) <<endl;
        continue;
      }
      
      //      for(auto& c : payload)
      //        fmt::printf("%02x ", c);
            
      //      cout<<"\n";

      std::basic_string<uint8_t> inav;

      for (i=0,j=2; i<14; i++, j+=8)
        inav.append(1, (unsigned char)getbitu(payload.c_str()   ,j,8));
      for (i=0,j=2; i< 2; i++, j+=8)
        inav.append(1, (unsigned char)getbitu(payload.c_str()+16,j,8));

      //      cout<<"inav for "<<wtype<<" for sv "<<sv<<": ";
      //      for(auto& c : inav)
      //        fmt::printf("%02x ", c);

      unsigned int wtype = getbitu(&inav[0], 0, 6);
       
      g_svstats[sv].addWord(inav);
      if(g_svstats[sv].e1bhs || g_svstats[sv].e5bhs || g_svstats[sv].e1bdvs || g_svstats[sv].e5bdvs) {
        if(sv != 18 && sv != 14) 
          cout << "sv "<<sv<<" health: " << g_svstats[sv].e1bhs <<" " << g_svstats[sv].e5bhs <<" " << g_svstats[sv].e1bdvs <<" "<< g_svstats[sv].e5bdvs <<endl;
      }
      if(!wtype) {
        if(getbitu(&inav[0], 6,2) == 2) {
          //          wn = getbitu(&inav[0], 96, 12);
          //          tow = getbitu(&inav[0], 108, 20);
        }
      }
      else if(wtype >=1 && wtype <= 4) { // ephemeris 
        uint16_t iod = getbitu(&inav[0], 6, 10);          
        if(wtype == 1 && g_tow) {
          //          int t0e = 60*getbitu(&inav[0], 16, 14);
          //          int age = (tow - t0e)/60;
          //          uint32_t e = getbitu(&inav[0], 6+10+14+32, 32);
        }
        else if(wtype == 3) {
          //          unsigned int sisa = getbitu(&inav[0], 120, 8);
          idb.addValue(sv, "sisa", g_svstats[sv].iods[iod].sisa);
        }
        else if(wtype == 4) {

          idb.addValue(sv, "af0", g_svstats[sv].iods[iod].af0);
          idb.addValue(sv, "af1", g_svstats[sv].iods[iod].af1);
          idb.addValue(sv, "af2", g_svstats[sv].iods[iod].af2);
          idb.addValue(sv, "t0c", g_svstats[sv].iods[iod].t0c);

          double age = ephAge(g_tow, g_svstats[sv].iods[iod].t0c * 60);
          /*
          cout<<"Atomic age "<<age<<", g_tow: "<<g_tow<<", t0c*60 = "<<g_svstats[sv].iods[iod].t0c * 60<<endl;
          cout<<"af0: "<<g_svstats[sv].iods[iod].af0<<", af1: "<<g_svstats[sv].iods[iod].af1 << ", af2: "<<(int)g_svstats[sv].iods[iod].af2<<endl;
          */
          
          double offset = ldexp(1000.0*(1.0*g_svstats[sv].iods[iod].af0 + ldexp(age*g_svstats[sv].iods[iod].af1, -12)), -34);
          // XXX what units is this in then? milliseconds?
          
          /*
          cout <<"Atomic offset: "<<offset<<endl;
          cout << 1.0*g_svstats[sv].iods[iod].af0/(1LL<<34) << endl;
          cout << age * (g_svstats[sv].iods[iod].af1/(1LL<<46)) << endl;
          */
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
        
        int dw = (uint8_t)g_wn - g_svstats[sv].wn0t;

        if(g_tow && g_wn) {
          
          int age = dw * 7 * 86400 + g_tow - g_svstats[sv].t0t * 3600;
          // a0 = 2^-30 s, a1 = 2^-50 s/s
          
          long shift = g_svstats[sv].a0 * (1LL<<20) + g_svstats[sv].a1 * age; // in 2^-50 seconds units
          time_t t = utcFromGST(g_wn, g_tow);
          gstutc << t << " " << sv <<" " << g_tow << " " << (g_svstats[sv].t0t * 3600) << " "<< age <<" " <<shift << " " << shift * pow(2, -50) * 1000000000 << " " << g_svstats[sv].a0 <<" " << g_svstats[sv].a1 << "\n";
          //                                               2^-30
          idb.addValue(sv, "utc_diff_ns", 1.073741824*ldexp(shift, -20));
          //          cout<<humanTime(g_wn, g_tow)<<" sv "<<sv<< " GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1)+", age="+to_string(age/3600)+"h, dw="+to_string(dw)
          //          +", wn0t="+to_string(wn0t)+", wn8="+to_string(g_wn&0xff)+"\n";

        }
      }
      else if(wtype == 10) { // GSTT GPS
        idb.addValue(sv, "a0g", g_svstats[sv].a0g);
        idb.addValue(sv, "a1g", g_svstats[sv].a1g);

        int a0g = getbits(&inav[0], 86, 16);
        int a1g = getbits(&inav[0], 102, 12);
        int t0g = getbitu(&inav[0], 114, 8);
        uint8_t wn0g = getbitu(&inav[0], 122, 6);
        int dw = (((uint8_t)g_wn)&(1+2+4+8+16+32)) - wn0g;
        
        if(g_tow && g_wn) {
          
          int age = g_tow - t0g * 3600;
          // a0g = 2^-32 s, a1 = 2^-50 s/s
          
          int shift = a0g * (1U<<16) + a1g * age; // in 2^-51 seconds units
          time_t t = utcFromGST(g_wn, g_tow);
          gstgps << t << " " << sv <<" " << g_tow << " " << (t0g * 3600) <<" " << age <<" " <<shift << " " << shift * pow(2, -50) * 1000000000 << " " << a0g <<" " << a1g << "\n";
          
          //          cout<<humanTime(g_wn, g_tow)<<" sv "<<sv<< " GST-GPS, a0g="+to_string(a0g)+", a1g="+to_string(a1g)+", t0g="+to_string(t0g)+", age="+to_string(g_tow/3600-t0g)+"h, dw="+to_string(dw)
          //        +", wn0g="+to_string(wn0g)+", wn6="+to_string(g_wn&(1+2+4+8+16+32))+"\n";
        }
      }

      for(auto& ent : g_svstats) {
        //        fmt::printf("%2d\t", ent.first);
        if(ent.second.completeIOD() && ent.second.prevIOD.first >= 0) {
          time_t t = utcFromGST(g_wn, g_tow);
          sisacsv << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << endl;
          //          cout << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << "\n";

          double clockage = ephAge(g_tow, ent.second.liveIOD().t0c * 60);
          double offset = 1.0*ent.second.liveIOD().af0/(1LL<<34) + clockage * ent.second.liveIOD().af1/(1LL<<46);
          clockcsv << t << " " << ent.first<<" " << ent.second.liveIOD().af0 << " " << ent.second.liveIOD().af1 <<" " << (int)ent.second.liveIOD().af2 <<" " << 935280000 + g_wn *7*86400 + ent.second.liveIOD().t0c * 60 << " " << clockage << " " << offset<<endl;
          int ephage = ephAge(g_tow, ent.second.prevIOD.second.t0e * 60);
          if(ent.second.liveIOD().sisa != ent.second.prevIOD.second.sisa) {

            cout<<humanTime(g_wn, g_tow)<<" sv "<<ent.first<<" changed sisa from "<<(unsigned int) ent.second.prevIOD.second.sisa<<" ("<<
              humanSisa(ent.second.prevIOD.second.sisa)<<") to " << (unsigned int)ent.second.liveIOD().sisa << " ("<<
              humanSisa(ent.second.liveIOD().sisa)<<"), lastseen = "<< (ephage/3600.0) <<"h"<<endl;
          }

          double x, y, z;
          double oldx, oldy, oldz;
          getCoordinates(g_wn, g_tow, ent.second.liveIOD(), &x, &y, &z);
          cout << ent.first << ": iod= "<<ent.second.getIOD()<<" "<< x/1000.0 << ", "<< y/1000.0 <<", "<<z/1000.0<<endl;

          cout<<"OLD: \n";
          getCoordinates(g_wn, g_tow, ent.second.prevIOD.second, &oldx, &oldy, &oldz);
          cout << ent.first << ": iod= "<<ent.second.prevIOD.first<<" "<< oldx/1000.0 << ", "<< oldy/1000.0 <<", "<<oldz/1000.0<<endl;

          double hours = ((ent.second.liveIOD().t0e - ent.second.prevIOD.second.t0e)/60.0);
          double disco = sqrt((x-oldx)*(x-oldx) + (y-oldy)*(y-oldy) + (z-oldz)*(z-oldz));
          cout<<ent.first<<" discontinuity "<< hours<<"h old: "<< disco <<endl;
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
              getCoordinates(g_wn, t, ent.second.liveIOD(), &x, &y, &z, false);
              getCoordinates(g_wn, t, ent.second.prevIOD.second, &oldx, &oldy, &oldz);
              time_t posix = utcFromGST(g_wn, t);
              orbitcsv << posix <<" "
                       <<x<<" " <<y<<" "<<z<<" "
                       <<oldx<<" " <<oldy<<" "<<oldz<<"\n";
            }
          }
          ent.second.clearPrev();          
        }
      }
    }
    else if (ubxClass == 1 && ubxType == 0x35) { // UBX-NAV-SAT
      cout<< "Info for "<<(int) msg[5]<<" svs: \n";
      for(unsigned int n = 0 ; n < msg[5]; ++n) {
        cout << "  "<<(msg[8+12*n] ? 'E' : 'G') << (int)msg[9+12*n] <<" db=";
        cout << (int)msg[10+12*n]<<" elev="<<(int)(char)msg[11+12*n]<<" azi=";
        cout << ((int)msg[13+12*n]*256 + msg[12+12*n])<<" pr="<< *((int16_t*)(msg.c_str()+ 14 +12*n)) *0.1 << " signal="<< ((int)(msg[16+12*n])&7) << " used="<<  (msg[16+12*n]&8);
          
        fmt::printf(" | %02x %02x %02x %02x", (int)msg[16+12*n], (int)msg[17+12*n],
                    (int)msg[18+12*n], (int)msg[19+12*n]);
        cout<<"\n";
      }
      cout<<endl;
    }
    else if (ubxClass == 4) { // info
      fmt::printf("Log level %d: %.*s\n", (int)ubxType, msg.size(), (char*)msg.c_str());
    }
    else if (ubxClass == 6 && ubxType == 1) { // msg rate
      fmt::printf("Rate for class %02x type %02x: %d %d %d %d %d %d\n",
                  (int)msg[0], (int)msg[1], (int) msg[2], (int) msg[3],(int) msg[4],
                  (int) msg[5],(int) msg[6],(int) msg[7]);
    }

    else
      cout << "ubxClass: "<<(unsigned int)ubxClass<<", ubxType: "<<(unsigned int)ubxType<<", size="<<msg.size()<<endl;
  }
  }
  catch(EofException& e)
    {}
}
catch(std::exception& e)
{
  cerr<<"Exiting because of fatal error "<<e.what()<<endl;
}
