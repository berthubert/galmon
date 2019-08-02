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

/* lovingly lifted from RTKLIB */
unsigned int getbitu(const unsigned char *buff, int pos, int len)
{
  unsigned int bits=0;
  int i;
  for (i=pos;i<pos+len;i++) bits=(bits<<1)+((buff[i/8]>>(7-i%8))&1u);
  return bits;
}

int getbits(const unsigned char *buff, int pos, int len)
{
    unsigned int bits=getbitu(buff,pos,len);
    if (len<=0||32<=len||!(bits&(1u<<(len-1)))) return (int)bits;
    return (int)(bits|(~0u<<len)); /* extend sign */
}

void setbitu(unsigned char *buff, int pos, int len, unsigned int data)
{
    unsigned int mask=1u<<(len-1);
    int i;
    if (len<=0||32<len) return;
    for (i=pos;i<pos+len;i++,mask>>=1) {
        if (data&mask) buff[i/8]|=1u<<(7-i%8); else buff[i/8]&=~(1u<<(7-i%8));
    }
}
static const unsigned int tbl_CRC24Q[]={
    0x000000,0x864CFB,0x8AD50D,0x0C99F6,0x93E6E1,0x15AA1A,0x1933EC,0x9F7F17,
    0xA18139,0x27CDC2,0x2B5434,0xAD18CF,0x3267D8,0xB42B23,0xB8B2D5,0x3EFE2E,
    0xC54E89,0x430272,0x4F9B84,0xC9D77F,0x56A868,0xD0E493,0xDC7D65,0x5A319E,
    0x64CFB0,0xE2834B,0xEE1ABD,0x685646,0xF72951,0x7165AA,0x7DFC5C,0xFBB0A7,
    0x0CD1E9,0x8A9D12,0x8604E4,0x00481F,0x9F3708,0x197BF3,0x15E205,0x93AEFE,
    0xAD50D0,0x2B1C2B,0x2785DD,0xA1C926,0x3EB631,0xB8FACA,0xB4633C,0x322FC7,
    0xC99F60,0x4FD39B,0x434A6D,0xC50696,0x5A7981,0xDC357A,0xD0AC8C,0x56E077,
    0x681E59,0xEE52A2,0xE2CB54,0x6487AF,0xFBF8B8,0x7DB443,0x712DB5,0xF7614E,
    0x19A3D2,0x9FEF29,0x9376DF,0x153A24,0x8A4533,0x0C09C8,0x00903E,0x86DCC5,
    0xB822EB,0x3E6E10,0x32F7E6,0xB4BB1D,0x2BC40A,0xAD88F1,0xA11107,0x275DFC,
    0xDCED5B,0x5AA1A0,0x563856,0xD074AD,0x4F0BBA,0xC94741,0xC5DEB7,0x43924C,
    0x7D6C62,0xFB2099,0xF7B96F,0x71F594,0xEE8A83,0x68C678,0x645F8E,0xE21375,
    0x15723B,0x933EC0,0x9FA736,0x19EBCD,0x8694DA,0x00D821,0x0C41D7,0x8A0D2C,
    0xB4F302,0x32BFF9,0x3E260F,0xB86AF4,0x2715E3,0xA15918,0xADC0EE,0x2B8C15,
    0xD03CB2,0x567049,0x5AE9BF,0xDCA544,0x43DA53,0xC596A8,0xC90F5E,0x4F43A5,
    0x71BD8B,0xF7F170,0xFB6886,0x7D247D,0xE25B6A,0x641791,0x688E67,0xEEC29C,
    0x3347A4,0xB50B5F,0xB992A9,0x3FDE52,0xA0A145,0x26EDBE,0x2A7448,0xAC38B3,
    0x92C69D,0x148A66,0x181390,0x9E5F6B,0x01207C,0x876C87,0x8BF571,0x0DB98A,
    0xF6092D,0x7045D6,0x7CDC20,0xFA90DB,0x65EFCC,0xE3A337,0xEF3AC1,0x69763A,
    0x578814,0xD1C4EF,0xDD5D19,0x5B11E2,0xC46EF5,0x42220E,0x4EBBF8,0xC8F703,
    0x3F964D,0xB9DAB6,0xB54340,0x330FBB,0xAC70AC,0x2A3C57,0x26A5A1,0xA0E95A,
    0x9E1774,0x185B8F,0x14C279,0x928E82,0x0DF195,0x8BBD6E,0x872498,0x016863,
    0xFAD8C4,0x7C943F,0x700DC9,0xF64132,0x693E25,0xEF72DE,0xE3EB28,0x65A7D3,
    0x5B59FD,0xDD1506,0xD18CF0,0x57C00B,0xC8BF1C,0x4EF3E7,0x426A11,0xC426EA,
    0x2AE476,0xACA88D,0xA0317B,0x267D80,0xB90297,0x3F4E6C,0x33D79A,0xB59B61,
    0x8B654F,0x0D29B4,0x01B042,0x87FCB9,0x1883AE,0x9ECF55,0x9256A3,0x141A58,
    0xEFAAFF,0x69E604,0x657FF2,0xE33309,0x7C4C1E,0xFA00E5,0xF69913,0x70D5E8,
    0x4E2BC6,0xC8673D,0xC4FECB,0x42B230,0xDDCD27,0x5B81DC,0x57182A,0xD154D1,
    0x26359F,0xA07964,0xACE092,0x2AAC69,0xB5D37E,0x339F85,0x3F0673,0xB94A88,
    0x87B4A6,0x01F85D,0x0D61AB,0x8B2D50,0x145247,0x921EBC,0x9E874A,0x18CBB1,
    0xE37B16,0x6537ED,0x69AE1B,0xEFE2E0,0x709DF7,0xF6D10C,0xFA48FA,0x7C0401,
    0x42FA2F,0xC4B6D4,0xC82F22,0x4E63D9,0xD11CCE,0x575035,0x5BC9C3,0xDD8538
};


/* crc-24q parity --------------------------------------------------------------
* compute crc-24q parity for sbas, rtcm3
* args   : unsigned char *buff I data
*          int    len    I      data length (bytes)
* return : crc-24Q parity
* notes  : see reference [2] A.4.3.3 Parity
*-----------------------------------------------------------------------------*/
unsigned int rtk_crc24q(const unsigned char *buff, int len)
{
    unsigned int crc=0;
    int i;
    
    for (i=0;i<len;i++) crc=((crc<<8)&0xFFFFFF)^tbl_CRC24Q[(crc>>16)^buff[i]];
    return crc;
}

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


uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str)
{
  uint8_t CK_A = 0, CK_B = 0;

  auto update = [&CK_A, &CK_B](uint8_t c) {
    CK_A = CK_A + c;
    CK_B = CK_B + CK_A;
  };
  update(ubxClass);
  update(ubxType);
  uint16_t len = str.size();
  update(((uint8_t*)&len)[0]);
  update(((uint8_t*)&len)[1]);
  for(unsigned int I=0; I < str.size(); I++) {
    update(str[I]);
  }
  return (CK_B << 8) + CK_A;
}

struct SVIOD
{
  std::bitset<32> words;
  uint32_t t0e; 
  uint32_t e, sqrtA;
  int32_t m0, omega0, i0, omega, idot, omegadot, deltan;
  
  int16_t cuc{0}, cus{0}, crc{0}, crs{0}, cic{0}, cis{0};
  
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
    /*
    t0c = getbitu(&page[0], 54, 14);
    af0 = getbitu(&page[0], 68, 31);
    af1 = getbitu(&page[0], 99, 21);
    af2 = getbitu(&page[0], 120, 6);
    */
  }

}

struct SVStat
{
  uint8_t e5bhs{0}, e1bhs{0};
  bool e5bdvs{false}, e1bdvs{false};
  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  uint16_t wn{0};
  uint32_t tow{0}; // "last seen"
  uint32_t a0{0}, a1{0}, t0t{0}, wn0t{0};
  int16_t el{-1}, db{-1};
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
    e5bhs = getbitu(&page[0], 67, 2);
    e1bhs = getbitu(&page[0], 69, 2);
    e5bdvs = getbitu(&page[0], 71, 1);
    e1bdvs = getbitu(&page[0], 72, 1);
    wn = getbitu(&page[0], 73, 12);
    tow = getbitu(&page[0], 85, 20);
    //    cout<<"Setting tow to "<<tow<<endl;
  }
  else if(wtype == 6) {
    a0 = getbits(&page[0], 6, 32);
    a1 = getbits(&page[0], 38, 24);
    t0t = getbitu(&page[0], 70, 8);
    wn0t = getbits(&page[0], 78, 8);

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
  double deltan = M_PI * 1.0*iod.deltan / (1ULL<<43);
  double t0e = 60.0*iod.t0e;
  double m0 = M_PI * 1.0*iod.m0 / (1ULL<<31);
  double e = 1.0*iod.e / (1ULL<<33);
  double omega = M_PI * 1.0*iod.omega / (1ULL<<31);

  double cuc = 1.0*iod.cuc / (1ULL<<29);
  double cus = 1.0*iod.cus / (1ULL<<29);

  double crc = 1.0*iod.crc / (1ULL<<5);
  double crs = 1.0*iod.crs / (1ULL<<5);

  double cic = 1.0*iod.cic / (1ULL<<29);
  double cis = 1.0*iod.cis / (1ULL<<29);

  double idot = M_PI * 1.0*iod.idot / (1ULL<<43);
  double i0 = M_PI * 1.0*iod.i0 / (1ULL << 31);

  double Omegadot = M_PI * 1.0*iod.omegadot / (1ULL << 43);
  double Omega0 = M_PI * 1.0*iod.omega0 / (1ULL << 31);
  
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
  
  
  double nu = atan(   ((sqrt(1-e*e)  * sin(E)) / (1 - e * cos(E)) ) /
                        ((cos(E) - e)/ (1-e*cos(E)))
                        );
  
  double nu1 = atan(   (sqrt(1-e*e) * sin(E)) /
                       (cos(E) - e)
                     );

  double nu2 = M + e*2*sin(M) +
    e *e * (5.0/4.0) * sin(2*M) -
    e*e*e * (0.25*sin(M) - (13.0/12.0)*sin(3*M));

  double nu3 = 2* atan( sqrt((1+e)/(1-e)) * tan(E/2));

  if(!quiet) {
    cout <<"         nu sis: "<<nu<< " / +pi = " << nu +M_PI << endl;
    cout <<"         nu ?: "<<nu1<< " / +pi = "  << nu1 +M_PI << endl;
    cout <<"         nu fourier/esa: "<<nu2<< " / +pi = "<<nu2+M_PI << endl;
    cout <<"         nu wikipedia: "<<nu3<< " / +pi = " <<nu3 +M_PI << endl;
  }
  
  nu = nu2;

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

std::string humanTime(int wn, int tow)
{
  time_t t = 935280000 + wn * 7*86400 + tow;
  struct tm tm;
  gmtime_r(&t, &tm);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", &tm);
  return buffer;
}

int* g_tow, *g_wn;

int main(int argc, char** argv)
try
{
  signal(SIGPIPE, SIG_IGN);
  H2OWebserver h2s("galmon");

  int tow=0, wn=0;
  g_tow = &tow;
  g_wn = &wn;
  
  h2s.addHandler("/svs", [](auto handler, auto req) {
      nlohmann::json ret = nlohmann::json::object();
      for(const auto& s: g_svstats)
        if(s.second.completeIOD()) {
          nlohmann::json item  = nlohmann::json::object();
          
          item["iod"]=s.second.getIOD();
          item["sisa"]=humanSisa(s.second.liveIOD().sisa);
          item["a0"]=s.second.a0;
          item["a1"]=s.second.a1;
          item["e5bdvs"]=s.second.e5bdvs;
          item["e1bdvs"]=s.second.e1bdvs;
          item["e5bhs"]=s.second.e5bhs;
          item["e1bhs"]=s.second.e1bhs;                    
          item["elev"]=s.second.el;
          item["db"]=s.second.db;
          item["eph-age-m"] = (*g_tow - 60*s.second.liveIOD().t0e)/60.0;
          item["last-seen-s"] = s.second.tow ? (*g_tow - s.second.tow) : -1;

          /* Our location:
X : 3922.505   km
Y : 290.116   km
Z : 5004.189   km
          */

          double ourx = 3922.505 * 1000;
          double oury = 290.116 * 1000;
          double ourz = 5004.189 * 1000;
          double x, y, z;
          //          cout<<"For sv " <<s.first<<" at "<<humanTime(*g_wn, *g_tow)<<": \n";
          getCoordinates(*g_wn, *g_tow, s.second.liveIOD(), &x, &y, &z);

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
          item["wn"]=*g_wn;
          item["tow"]=*g_tow;
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

  
  //  unsigned int tow=0, wn=0;
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
  

  string line;
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
            g_svstats[atoi(strs[n].c_str())].el = atoi(strs[n+1].c_str());
            if(g_svstats[atoi(strs[n].c_str())].el >= 0)
              g_svstats[atoi(strs[n].c_str())].db = atoi(strs[n+3].c_str());
            else
              g_svstats[atoi(strs[n].c_str())].db = -1;
            
            /*
            cout<<"sv "<<atoi(strs[n].c_str())
                << " el "<< atoi(strs[n+1].c_str())
                << " db " << atoi(strs[n+3].c_str()) 
                <<endl;
            */
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
      cout<<"Checksum: "<<ubxChecksum<< ", calculated: "<<
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
      //      cout<<"\n";
      if(msg[0] != 2) // version field
        continue;
      // 7 is reserved
      
      //      cout << ((msg[8]&128) ? "Even " : "Odd ");
      //      cout << ((msg[8]&64) ? "Alert " : "Nominal ");

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
      if(!wtype) {
        if(getbitu(&inav[0], 6,2) == 2) {
          wn = getbitu(&inav[0], 96, 12);
          tow = getbitu(&inav[0], 108, 20);
        }
      }
      else if(wtype >=1 && wtype <= 4) { // ephemeris 
        //        uint16_t iod = getbitu(&inav[0], 6, 10);
        if(wtype == 1 && tow) {
          //          int t0e = 60*getbitu(&inav[0], 16, 14);
          //          int age = (tow - t0e)/60;
          //          uint32_t e = getbitu(&inav[0], 6+10+14+32, 32);
        }
        else if(wtype == 3) {
          //          unsigned int sisa = getbitu(&inav[0], 120, 8);
        }
        else
          ;
      }
      else if(wtype == 5) {
        tow = getbitu(&inav[0], 85, 20);
        wn = getbitu(&inav[0], 73, 12);
      }
      else if(wtype == 6) {  // GST-UTC
        int a0 = getbits(&inav[0], 6, 32);
        int a1 = getbits(&inav[0], 38, 24);
        int t0t = getbitu(&inav[0], 70, 8);
        uint8_t wn0t = getbits(&inav[0], 78, 8);
        int dw = (uint8_t)wn - wn0t;

        if(tow && wn) {
          
          int age = dw * 7 * 86400 + tow - t0t * 3600;
          // a0 = 2^-30 s, a1 = 2^-50 s/s
          
          int shift = a0 * (1U<<20) + a1 * age; // in 2^-50 seconds units
          time_t t = 935280000 + wn * 7*86400 + tow;
          gstutc << t << " " << sv <<" " << tow << " " << (t0t * 3600) << " "<< age <<" " <<shift << " " << shift * pow(2, -50) * 1000000000 << " " << a0 <<" " << a1 << "\n";

          cout<<humanTime(wn, tow)<<" sv "<<sv<< " GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1)+", age="+to_string(age/3600)+"h, dw="+to_string(dw)
                      +", wn0t="+to_string(wn0t)+", wn8="+to_string(wn&0xff)+"\n";

        }

        
        /*
        else
          wk.emitLine(sv, "GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1));
        */

      }
      else if(wtype == 10) { // GSTT GPS
        int a0g = getbits(&inav[0], 86, 16);
        int a1g = getbits(&inav[0], 102, 12);
        int t0g = getbitu(&inav[0], 114, 8);
        uint8_t wn0g = getbitu(&inav[0], 122, 6);
        int dw = (((uint8_t)wn)&(1+2+4+8+16+32)) - wn0g;
        
        if(tow && wn) {
          
          int age = tow - t0g * 3600;
          // a0g = 2^-32 s, a1 = 2^-50 s/s
          
          int shift = a0g * (1U<<16) + a1g * age; // in 2^-51 seconds units
          time_t t = 935280000 + wn * 7*86400 + tow;
          gstgps << t << " " << sv <<" " << tow << " " << (t0g * 3600) <<" " << age <<" " <<shift << " " << shift * pow(2, -50) * 1000000000 << " " << a0g <<" " << a1g << "\n";
          
          cout<<humanTime(wn, tow)<<" sv "<<sv<< " GST-GPS, a0g="+to_string(a0g)+", a1g="+to_string(a1g)+", t0g="+to_string(t0g)+", age="+to_string(tow/3600-t0g)+"h, dw="+to_string(dw)
            +", wn0g="+to_string(wn0g)+", wn6="+to_string(wn&(1+2+4+8+16+32))+"\n";
        }
      }

      for(auto& ent : g_svstats) {
        //        fmt::printf("%2d\t", ent.first);
        if(ent.second.completeIOD() && ent.second.prevIOD.first >= 0) {
          time_t t = 935280000 + wn * 7*86400 + tow;
          sisacsv << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << "\n";
          cout << t <<" " << ent.first << " " << (unsigned int) ent.second.liveIOD().sisa << "\n";
          if(ent.second.liveIOD().sisa != ent.second.prevIOD.second.sisa) {
            int age = tow - ent.second.prevIOD.second.t0e * 60;
            cout<<humanTime(wn, tow)<<" sv "<<ent.first<<" changed sisa from "<<(unsigned int) ent.second.prevIOD.second.sisa<<" ("<<
              humanSisa(ent.second.prevIOD.second.sisa)<<") to " << (unsigned int)ent.second.liveIOD().sisa << " ("<<
              humanSisa(ent.second.liveIOD().sisa)<<"), lastseen = "<< (age/3600.0) <<"h"<<endl;
          }
          ent.second.clearPrev();          
        }
        
        if(0 && ent.second.completeIOD() && ent.second.prevIOD.first >= 0) {
          double x, y, z;
          double oldx, oldy, oldz;
          getCoordinates(wn, tow, ent.second.liveIOD(), &x, &y, &z);
          cout << ent.first << ": iod= "<<ent.second.getIOD()<<" "<< x/1000.0 << ", "<< y/1000.0 <<", "<<z/1000.0<<endl;

          cout<<"OLD: \n";
          getCoordinates(wn, tow, ent.second.prevIOD.second, &oldx, &oldy, &oldz);
          cout << ent.first << ": iod= "<<ent.second.prevIOD.first<<" "<< oldx/1000.0 << ", "<< oldy/1000.0 <<", "<<oldz/1000.0<<endl;

          double hours = ((ent.second.liveIOD().t0e - ent.second.prevIOD.second.t0e)/60.0);
          cout<<ent.first<<" discontinuity "<< hours<<"h old: "<<sqrt((x-oldx)*(x-oldx) + (y-oldy)*(y-oldy) + (z-oldz)*(z-oldz)) <<endl;


          if(hours < 2) {
            ofstream orbitcsv("orbit."+to_string(ent.first)+"."+to_string(ent.second.prevIOD.first)+"-"+to_string(ent.second.getIOD())+".csv");

            orbitcsv << "timestamp x y z oldx oldy oldz\n";
            orbitcsv << fixed;
            for(int offset = -7200; offset < 7200; offset += 30) {
              int t = ent.second.liveIOD().t0e * 60 + offset;
              getCoordinates(wn, t, ent.second.liveIOD(), &x, &y, &z, false);
              getCoordinates(wn, t, ent.second.prevIOD.second, &oldx, &oldy, &oldz);
              time_t posix = 935280000 + wn * 7*86400 + t;
              orbitcsv << posix <<" "
                       <<x<<" " <<y<<" "<<z<<" "
                       <<oldx<<" " <<oldy<<" "<<oldz<<"\n";
            }
          }
          ent.second.clearPrev();          
        }
        
      }
      
      //        time_t t = 935280000 + wn * 7*86400 + tow;
      /*
      for(const auto& ent : g_svstats) {
        // 12  iod   t0e
        fmt::printf("%2d\t", ent.first);
        if(ent.second.completeIOD()) {
          cout << ent.second.getIOD() << "\t" << ( ent.second.tow - ent.second.liveIOD().t0e ) << "\t" << (unsigned int)ent.second.liveIOD().sisa;
        }
        cout<<"\n";
      }
      cout<<"\n";
      */
    }
  }
 }
 catch(EofException& e)
   {}
