#include <map>
#include "galileo.hh"
#include "minivec.hh"
#include "navmon.hh"
#include "navparse.hh"
#include "ephemeris.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>

using namespace std;
extern GetterSetter<map<int, GalileoMessage::Almanac>> g_galileoalmakeeper;
extern GetterSetter<svstats_t> g_statskeeper;

// a vector of pairs of latidude,vector<longitude,numsats>
typedef vector<pair<double,vector<pair<double, int> > > > covmap_t;

covmap_t emitCoverage()
{
  covmap_t ret;
  ofstream cmap("covmap.csv");
  cmap<<"latitude longitude count5 count10 count20"<<endl;
  map<int, Point> sats;
  auto galileoalma = g_galileoalmakeeper.get();
  auto svstats = g_statskeeper.get();
  auto pseudoTow = (time(0) - 820368000) % (7*86400);
  cout<<"pseudoTow "<<pseudoTow<<endl;
  for(const auto &g : galileoalma) {
    Point sat;
    getCoordinates(pseudoTow, g.second, &sat);

    if(g.first < 0)
      continue;
    if(svstats[{2,g.first,1}].completeIOD() && svstats[{2,g.first,1}].liveIOD().sisa == 255) {
      cout<<g.first<<" NAPA!"<<endl;
      continue;
    }
    sats[g.first]=sat;
  }
  cout<<"Have "<<sats.size()<<" SVs active"<<endl;
  double R = 6371000;
  for(int latitude = 90 ; latitude > -90; latitude-=1) {  // north-south
    double phi = M_PI* latitude / 180;
    double longsteps = 1 + 360.0 * cos(phi);
    double step = 360.0 / longsteps;
    cout<<"lat "<<latitude<<" Step size: "<<step<<endl;
    vector<pair<double, int>> latvect;
    for(double longitude = -180; longitude < 180; longitude += step) { // east - west
      cout<<" long "<<longitude<<endl;
      Point p;
      // phi = latitude, lambda = longitude

      double lambda = M_PI* longitude / 180;
      p.x = R * cos(phi) * cos(lambda);
      p.y = R * cos(phi) * sin(lambda);
      p.z = R * sin(phi);

      if(longitude == -180) {
        auto longlat = getLongLat(p.x, p.y, p.z);
        //        cout<<fmt::sprintf("%3.0f ", 180.0*longlat.second/M_PI);
      }
        
      
      int numsats5=0, numsats10=0, numsats20=0;
      for(const auto& s : sats) {
        //        double getElevationDeg(const Point& sat, const Point& our);
        double elev = getElevationDeg(s.second, p);
        if(elev > 5.0)
          numsats5++;
        if(elev > 10.0)
          numsats10++;
        if(elev > 20.0)
          numsats20++;
      }
      latvect.push_back(make_pair(longitude, numsats10));
      cmap << longitude <<" " <<latitude <<" " << numsats5 << " " <<numsats10<<" "<<numsats20<<endl;
    }
    ret.push_back(make_pair(latitude, latvect));
    cout<<endl;
  }
  return ret;
}
