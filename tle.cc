#include "tle.hh"
#include "SGP4.h"
#include <iostream>
#include <fstream>

using namespace std;

static void trim(std::string& str)
{
  auto pos = str.find_first_of("\r\n");
  if(pos != string::npos)
    str.resize(pos);
  pos = str.find_last_not_of(" \t");
  if(pos != string::npos)
    str.resize(pos+1);
}

void TLERepo::parseFile(std::string_view fname)
{
  ifstream ifs(&fname[0]);
  string name, line1, line2;
  for(;;) {
    if(!getline(ifs, name) || !getline(ifs, line1) || !getline(ifs, line2))
      break;
    trim(name);
    trim(line1);
    trim(line2);
    Tle tle(line1, line2);
    /*    
    cout<<"name: "<<name<<endl;
    cout<<"line1: "<<line1<<endl;
    cout<<"line2: "<<line2<<endl;
    cout << tle.ToString() << endl;
    */
    auto sgp4 = std::make_unique<std::tuple<SGP4, Tle>>(SGP4(tle), tle);
    d_sgp4s[name] = std::move(sgp4);
  }
}

TLERepo::TLERepo()
{}

TLERepo::~TLERepo()
{}


static TLERepo::Match makeMatch(const DateTime& d, const SGP4& sgp4, const Tle& tle, string name, double distance)
{
  TLERepo::Match m;
  m.name=name;
  m.norad = tle.NoradNumber();
  m.internat = tle.IntDesignator();
  m.inclination = tle.Inclination(false);
  m.ran = tle.RightAscendingNode(false);
  m.e = tle.Eccentricity();
  m.distance = distance;

  auto eci = sgp4.FindPosition(d);

  double theta = -eci.GetDateTime().ToGreenwichSiderealTime();
  Vector rot = eci.Position();
  m.eciX = 1000.0*rot.x;
  m.eciY = 1000.0*rot.y;
  m.eciZ = 1000.0*rot.z;
  
  rot.x = eci.Position().x * cos(theta) - eci.Position().y * sin(theta);
  rot.y = eci.Position().x * sin(theta) + eci.Position().y * cos(theta);

  m.ecefX = 1000.0 * rot.x;
  m.ecefY = 1000.0 * rot.y;
  m.ecefZ = 1000.0 * rot.z;

  auto geod = eci.ToGeodetic();
  m.latitude = geod.latitude;
  m.longitude = geod.longitude;
  m.altitude = geod.altitude;
  return m;
}

TLERepo::Match TLERepo::getBestMatch(time_t now, double x, double y, double z, TLERepo::Match* secondbest)
{
  struct tm tm;
  gmtime_r(&now, &tm);
  DateTime d(1900 + tm.tm_year, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  
  Vector sat(x/1000.0, y/1000.0, z/1000.0);

  multimap<double, string> distances;
  for(const auto& sgp4 : d_sgp4s) {
    try {
      auto eci = get<0>(*sgp4.second).FindPosition(d);
      
      double theta = -eci.GetDateTime().ToGreenwichSiderealTime();
      Vector rot = eci.Position();
      rot.x = eci.Position().x * cos(theta) - eci.Position().y * sin(theta);
      rot.y = eci.Position().x * sin(theta) + eci.Position().y * cos(theta);
      
      distances.insert({1000.0*(rot - sat).Magnitude(),sgp4.first});
    }
    catch(SatelliteException& se) {
      //      cerr<<"TLE error: "<<se.what()<<endl;
      continue;
    }
    catch(DecayedException& se) {
      //      cerr<<"TLE error: "<<se.what()<<endl;
      continue;
    }

  }
  if(distances.empty())
    return TLERepo::Match();
  if(secondbest) {
    auto iter = distances.begin();
    if(iter != distances.end()) {
      ++iter;
      
      *secondbest = makeMatch(d, get<0>(*d_sgp4s[iter->second]), get<1>(*d_sgp4s[iter->second]), iter->second, iter->first);
    }
  }
    
  return makeMatch(d, get<0>(*d_sgp4s[distances.begin()->second]), get<1>(*d_sgp4s[distances.begin()->second]), distances.begin()->second, distances.begin()->first);

}
