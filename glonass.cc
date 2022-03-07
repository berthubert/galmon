#include "glonass.hh"
#include <math.h>
#include <string.h>
#include <chrono>
#include <iostream>
using std::cout;
using std::endl;

static const double ae = 6378.136; // km		// IERS: 6378136.6
static const double mu = 398.6004418E3; // km3/s2	// IERS: 3.986004418
static const double J2 = 1082625.75E-9;                 // IERS: 1.0826359
static const double oe = 7.2921151467E-5; // rad/s	// IERS: 7.292115

// this strips out spare bits + parity, and leaves 10 clean 24 bit words
std::basic_string<uint8_t> getGlonassMessage(std::basic_string_view<uint8_t> payload)
{
  uint8_t buffer[4*4];

  for(int w = 0 ; w < 4; ++w) {
    setbitu(buffer, 32*w, 32, getbitu(&payload[0],  w*32, 32));
  }

  return std::basic_string<uint8_t>(buffer, 16);
  
}

// this does NOT turn it into unix time!!
uint32_t GlonassMessage::getGloTime() const
{
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = 96+4*(n4 -1);
  tm.tm_mon = 0;
  tm.tm_mday = 1;
  tm.tm_hour = -3; 
  tm.tm_min = 0;
  tm.tm_sec = 0;
  time_t t = timegm(&tm);
  //  cout<<" n4 "<<(int)gm.n4<<" start-of-4y "<< humanTime(t) <<" NT "<<(int)gm.NT;
  
  t += 86400 * (NT-1);
        
  t += 3600 * (hour) + 60 * minute + seconds;
  return t - 820368000; // this starts GLONASS time at 31st of december 1995, 00:00 UTC
}

// the 'referencetime' must reflect the time when the frame with Tb was received
uint32_t getGlonassT0e(time_t referencetime, int Tb)
{
  time_t now = referencetime + 3*3600; // this is so we get the Moscow day
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  gmtime_r(&now, &tm);
  tm.tm_hour = (Tb/4.0);
  tm.tm_min = (Tb % 4)*15;
  tm.tm_sec = 0;
  return timegm(&tm)-3*3600;           // and back to UTC
}

// y[0] .. y[2] --> X, Y, Z
// y[3] .. y[5] --> Vx, Vy, Vz
static void ff (const double A[3], const double y[6], double f[6])
{
  double X = y[0];
  double Y = y[1];
  double Z = y[2];
  double Vx = y[3];
  double Vy = y[4];
  double Vz = y[5];
  double R = sqrt (X*X + Y*Y + Z*Z);
  double C0 = pow(R, -3);
  double C1 = 1.5*J2*ae*ae*pow(R, -5);
  double C2 = 5*pow(Z/R, 2);
  double CXY = C0 + C1 * (1 - C2);
  double CZ = C0 + C1 * (3 - C2);
  f[0] = Vx;
  f[1] = Vy;
  f[2] = Vz;
  f[3] = (- mu*CXY + oe*oe)*X + 2*oe*Vy + A[0];
  f[4] = (- mu*CXY + oe*oe)*Y - 2*oe*Vx + A[1];
  f[5] = - mu*CZ*Z + A[2];
}

static void rk4step (const double A[3], double y[6], double h)
{
  double k1[6], k2[6], k3[6], k4[6], z[6];

  ff (A, y, k1);
  for (int j = 0; j < 6; j ++)
    z[j] = y[j] + 0.5*h*k1[j];

  ff (A, z, k2);
  for (int j = 0; j < 6; j ++)
    z[j] = y[j] + 0.5*h*k2[j];

  ff (A, z, k3);
  for (int j = 0; j < 6; j ++)
    z[j] = y[j] + h*k3[j];

  ff (A, z, k4);
  for (int j = 0; j < 6; j ++)
    y[j] = y[j] + h * (k1[j] + 2*(k2[j] + k3[j]) + k4[j]) / 6;
}


using Clock = std::chrono::steady_clock; 

static double passedMsec(const Clock::time_point& then, const Clock::time_point& now)
{
  return std::chrono::duration_cast<std::chrono::microseconds>(now - then).count()/1000.0;
}

#if 0
static double passedMsec(const Clock::time_point& then)
{
  return passedMsec(then, Clock::now());
}
#endif

double getCoordinates(double tow, const GlonassMessage& eph, Point* p)
{
  //  auto start = Clock::now();
  
  double y0[6] = {ldexp(eph.x, -11), ldexp(eph.y, -11), ldexp(eph.z, -11),
    ldexp(eph.dx, -20), ldexp(eph.dy, -20), ldexp(eph.dz, -20)};
  double A[3] = {ldexp(eph.ddx, -30), ldexp(eph.ddy, -30), ldexp(eph.ddz, -30)};

  // These all fake
  uint32_t glotime = eph.getGloTime();
  uint32_t gloT0e = getGlonassT0e(glotime + 820368000, eph.Tb);
  uint32_t ephtow =  (gloT0e - 820368000) % (7*86400);

  double DT = tow - ephtow;
  int n = abs (DT / 100) + 1; // integrate in roughly 100 second steps
  double h = DT / n;
  for (int j = 0; j < n; j ++)
    rk4step (A, y0, h);

  *p = Point (1E3*y0[0], 1E3*y0[1], 1E3*y0 [2]);
  //  static double total=0;
  //  cout<<"Took: "<<(total+=passedMsec(start))<<" ms" <<endl;
  return 0;
}
