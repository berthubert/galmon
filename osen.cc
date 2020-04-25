#include <tuple>
#include <math.h>
#include "ephemeris.hh"

/* gratefully adopted from http://danceswithcode.net/engineeringnotes/geodetic_to_ecef/geodetic_to_ecef.html
   which in turn is based on the excellent work from 
https://hal.archives-ouvertes.fr/hal-01704943/document
*/

// lat, lon, height (rad, rad, meters)
std::tuple<double, double, double> ecefToWGS84(double x, double y, double z)
{
    constexpr double  a = 6378137.0;              //WGS-84 semi-major axis
    constexpr double e2 = 6.6943799901377997e-3;  //WGS-84 first eccentricity squared
    constexpr double a1 = 4.2697672707157535e+4;  //a1 = a*e2
    constexpr double a2 = 1.8230912546075455e+9;  //a2 = a1*a1
    constexpr double a3 = 1.4291722289812413e+2;  //a3 = a1*e2/2
    constexpr double a4 = 4.5577281365188637e+9;  //a4 = 2.5*a2
    constexpr double a5 = 4.2840589930055659e+4;  //a5 = a1+a3
    constexpr double a6 = 9.9330562000986220e-1;  //a6 = 1-e2
    double zp,w2,w,r2,r,s2,c2,s,c,ss;
    double g,rg,rf,u,v,m,f,p;

    std::tuple<double, double, double> geo;   //Results go here (Lat, Lon, Altitude)
    zp = abs( z );
    w2 = x*x + y*y;
    w = sqrt( w2 );
    r2 = w2 + z*z;
    r = sqrt( r2 );
    std::get<1>(geo) = atan2( y, x );       //Lon (final)
    s2 = z*z/r2;
    c2 = w2/r2;
    u = a2/r;
    v = a3 - a4/r;
    if( c2 > 0.3 ){
      s = ( zp/r )*( 1.0 + c2*( a1 + u + s2*v )/r );
      std::get<0>(geo) = asin( s );      //Lat
      ss = s*s;
      c = sqrt( 1.0 - ss );
    }
    else{
      c = ( w/r )*( 1.0 - s2*( a5 - u - c2*v )/r );
      std::get<0>(geo) = acos( c );      //Lat
      ss = 1.0 - c*c;
      s = sqrt( ss );
    }
    g = 1.0 - e2*ss;
    rg = a/sqrt( g );
    rf = a6*rg;
    u = w - rg*c;
    v = zp - rf*s;
    f = c*u + s*v;
    m = c*v - s*u;
    p = m/( rf/g + f );
    std::get<0>(geo) = std::get<0>(geo) + p;      //Lat
    std::get<2>(geo) = f + m*p/2.0;     //Altitude
    if( z < 0.0 ){
      std::get<0>(geo) *= -1.0;     //Lat
    }
    return( geo );    //Return Lat, Lon, Altitude in that order
}
