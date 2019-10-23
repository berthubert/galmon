#include "ephemeris.hh"
#include "minivec.hh"
/* |            t0e tow     |  - > tow - t0e, <3.5 days, so ok

   | t0e                tow |   -> tow - t0e > 3.5 days, so 
                                   7*86400 - tow + t0e

   |         tow t0e        |   -> 7*86400 - tow + t0e > 3.5 days, so
                                  tow - t0e (negative age)

   | tow               t0e  |   -> 7*86400 - tow + t0e < 3.5 days, ok
*/

// positive age = t0e in the past
int ephAge(int tow, int t0e)
{
  int diff = tow - t0e;
  if(diff > 3.5*86400)
    diff -= 604800;
  if(diff < -3.5*86400)
    diff += 604800;
  return diff;
}

// all axes start at earth center of gravity
// x-axis is on equator, 0 longitude
// y-axis is on equator, 90 longitude
// z-axis is straight up to the north pole
// https://en.wikipedia.org/wiki/ECEF#/media/File:Ecef.png
std::pair<double,double> getLongLat(double x, double y, double z)
{
  Point core{0,0,0};
  Point LatLon0{1,0,0};

  Point pos{x, y, z};
  
  Point proj{x, y, 0}; // in equatorial plane now
  Vector flat(core, proj);
  Vector toLatLon0{core, LatLon0};
  double longitude = acos( toLatLon0.inner(flat) / (toLatLon0.length() * flat.length()));
  if(y < 0)
    longitude *= -1;
  
  Vector toUs{core, pos};
  double inp = flat.inner(toUs) / (toUs.length() * flat.length());
  if(inp > 1.0 && inp < 1.0000001) // this happens because of rounding errors
    inp=1.0; 
  double latitude =  acos( inp);
  if(z < 0)
    latitude *= -1;

  return std::make_pair(longitude, latitude);
}


double getElevationDeg(const Point& sat, const Point& our)
{

  Point core{0,0,0};
  
  Vector core2us(core, our);
  Vector dx(our, sat); //  = x-ourx, dy = y-oury, dz = z-ourz;
  
  // https://ds9a.nl/articles/
  
  double elev = acos ( core2us.inner(dx) / (core2us.length() * dx.length()));
  double deg = 180.0* (elev/M_PI);
  return 90.0 - deg;
}

// https://gis.stackexchange.com/questions/58923/calculating-view-angle

double getAzimuthDeg(const Point& sat, const Point& our)
{
  Point core{0,0,0};

  Vector north{
    -our.z*our.x,
    -our.z*our.y,
    our.x*our.x + our.y * our.y};

  Vector east{-our.y, our.x, 0};
  
  Vector dx(our, sat); //  = x-ourx, dy = y-oury, dz = z-ourz;
  
  // https://ds9a.nl/articles/
  
  double azicos = ( north.inner(dx) / (north.length() * dx.length()));
  double azisin = ( east.inner(dx) / (east.length() * dx.length()));

  double azi = atan2(azisin, azicos);
  
  double deg = 180.0* (azi/M_PI);
  if(deg < 0)
    deg += 360;
  return deg;
}
