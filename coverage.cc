#include <map>
#include "galileo.hh"
#include "minivec.hh"
#include "navmon.hh"
#include "navparse.hh"
#include "ephemeris.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>

#include <eigen3/Eigen/Dense>
using Eigen::MatrixXd;


using namespace std;
extern GetterSetter<map<int, GalileoMessage::Almanac>> g_galileoalmakeeper;
extern GetterSetter<svstats_t> g_statskeeper;

xDOP getDOP(Point& us, vector<Point> sats)
{
  xDOP ret;
  if(sats.size() < 4) {
    return ret;
  }
  MatrixXd G(sats.size(), 4); // 4 columns

  // (x1 - x)/R1    (y1 -y)/R1    (z1 - z)/R1   -1

  for(size_t n = 0 ; n < sats.size(); ++n) {
    const auto& s = sats[n];
    auto R = Vector(us, s).length();

    G(n, 0) = (s.x - us.x)/R;
    G(n, 1) = (s.y - us.y)/R;
    G(n, 2) = (s.z - us.z)/R;
    G(n, 3) = -1;
  }

  //  cout<<"Matrix: "<<endl;
  //  cout<<G<<endl;
  MatrixXd Q = (G.transpose() * G).inverse();

  ret.pdop = sqrt(Q(0,0) + Q(1,1) + Q(2,2)); // already squared
  ret.tdop = sqrt(Q(3,3));
  ret.gdop = sqrt(ret.pdop*ret.pdop + ret.tdop*ret.tdop);
  return ret;
};

covmap_t emitCoverage()
{
  covmap_t ret;
  ofstream cmap("covmap.csv");
  cmap<<"latitude longitude count5 count10 count20"<<endl;
  map<int, Point> sats;
  auto galileoalma = g_galileoalmakeeper.get();
  auto svstats = g_statskeeper.get();
  auto pseudoTow = (time(0) - 820368000) % (7*86400);
  //  cout<<"pseudoTow "<<pseudoTow<<endl;
  for(const auto &g : galileoalma) {
    Point sat;
    getCoordinates(pseudoTow, g.second, &sat);

    if(g.first < 0)
      continue;
    if(svstats[{2,(uint32_t)g.first,1}].completeIOD() && svstats[{2,(uint32_t)g.first,1}].liveIOD().sisa == 255) {
      //      cout<<g.first<<" NAPA!"<<endl;
      continue;
    }
    sats[g.first]=sat;
  }
  double R = 6371000;
  for(double latitude = 90 ; latitude > -90; latitude-=0.5) {  // north-south
    double phi = M_PI* latitude / 180;
    double longsteps = 1 + 360.0 * cos(phi);
    double step = 180.0 / longsteps;
    vector<tuple<double, int, int, int, double, double, double>> latvect;
    for(double longitude = -180; longitude < 180; longitude += step) { // east - west
      Point p;
      // phi = latitude, lambda = longitude

      double lambda = M_PI* longitude / 180;
      p.x = R * cos(phi) * cos(lambda);
      p.y = R * cos(phi) * sin(lambda);
      p.z = R * sin(phi);

      if(longitude == -180) {
        //        auto longlat = getLongLat(p.x, p.y, p.z);
        //        cout<<fmt::sprintf("%3.0f ", 180.0*longlat.second/M_PI);
      }
        
      
      int numsats5=0, numsats10=0, numsats20=0;
      vector<Point> satposs5, satposs10, satposs20;
      for(const auto& s : sats) {
        //        double getElevationDeg(const Point& sat, const Point& our);
        double elev = getElevationDeg(s.second, p);
        if(elev > 5.0) {
          satposs5.push_back(s.second);
          numsats5++;
        }
        if(elev > 10.0) {
          satposs10.push_back(s.second);
          numsats10++;
        }
        if(elev > 20.0) {
          satposs20.push_back(s.second);
          numsats20++;
        }
      }
      latvect.push_back(make_tuple(longitude,
                                   numsats5, numsats10, numsats20,
                                   getDOP(p, satposs5).pdop,
                                   getDOP(p, satposs10).pdop, 
                                   getDOP(p, satposs20).pdop
                                   ));
      //      cmap << longitude <<" " <<latitude <<" " << numsats5 << " " <<numsats10<<" "<<numsats20<<endl;
    }
    ret.push_back(make_pair(latitude, latvect));
  }
  return ret;
}
