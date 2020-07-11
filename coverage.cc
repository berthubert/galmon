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

  auto [lambda, phi] = getLongLat(us.x, us.y, us.z);
  
  // https://gssc.esa.int/navipedia/index.php/Positioning_Error
  Eigen::Matrix3d Renu;
  Renu <<
    (-sin(lambda))      , (-sin(phi)*cos(lambda)) , (cos(phi)*cos(lambda)),
    (cos(lambda))       , (-sin(phi)*sin(lambda)) , (cos(phi)*sin(lambda)),
    (0.0)               , (cos(phi))              , (sin(phi)); 

  Eigen::Matrix3d Qxyz;
  for(int x=0; x<3; ++x) // feels like there should be a better way for this, but not sure
    for(int y=0; y<3; ++y)
      Qxyz(x,y) = Q(x,y);

  Eigen::Matrix3d Qenu = Renu.transpose()*Qxyz*Renu;
  //  if(Qenu(0,0) < 0 || Qenu(1,1) < 0 || Qenu(2,2) < 0)
  //    cout << "Original: \n"<<Qxyz<<"\nRotated: \n"<<Qenu<<endl;
  
  ret.pdop = sqrt(Q(0,0) + Q(1,1) + Q(2,2)); // already squared
  //  ret.pdop = sqrt(Qenu(0,0) + Qenu(1,1) + Qenu(2,2)); // already squared
  
  ret.tdop = sqrt(Q(3,3));
  ret.gdop = sqrt(ret.pdop*ret.pdop + ret.tdop*ret.tdop);
  if(Qenu(0,0) >=0 && Qenu(1,1) >=0)
    ret.hdop = sqrt(Qenu(0,0) + Qenu(1,1));
  if(Qenu(2,2)>=0)
    ret.vdop = sqrt(Qenu(2,2));
  return ret;
};

// in covmap:
// 0
// lon,
// 1           2            3
// numsats5, numsats10, numsats20
// 4       5        6
// pdop5, pdop10, pdop20
// hdop5, hdop10, hdop10
// vdop5, vdop10, vdop20
covmap_t emitCoverage(const vector<Point>& sats)
{
  covmap_t ret;
  //  ofstream cmap("covmap.csv");
  //  cmap<<"latitude longitude count5 count10 count20"<<endl;
  double R = 6371000;
  for(double latitude = 90 ; latitude > -90; latitude-=2) {  // north-south
    double phi = M_PI* latitude / 180;
    double longsteps = 1 + 360.0 * cos(phi);
    double step = 4*180.0 / longsteps;
    // this does sorta equi-distanced measurements
    vector<tuple<double, int, int, int, double, double, double, double, double, double,double, double, double>> latvect;
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
        double elev = getElevationDeg(s, p);
        if(elev > 5.0) {
          satposs5.push_back(s);
          numsats5++;
        }
        if(elev > 10.0) {
          satposs10.push_back(s);
          numsats10++;
        }
        if(elev > 20.0) {
          satposs20.push_back(s);
          numsats20++;
        }
      }
      latvect.push_back(make_tuple(longitude,
                                   numsats5, numsats10, numsats20,
                                   getDOP(p, satposs5).pdop,
                                   getDOP(p, satposs10).pdop, 
                                   getDOP(p, satposs20).pdop,
                                   getDOP(p, satposs5).hdop,
                                   getDOP(p, satposs10).hdop, 
                                   getDOP(p, satposs20).hdop,
                                   getDOP(p, satposs5).vdop,
                                   getDOP(p, satposs10).vdop, 
                                   getDOP(p, satposs20).vdop
                                   
                                   ));
      //      cmap << longitude <<" " <<latitude <<" " << numsats5 << " " <<numsats10<<" "<<numsats20<<endl;
    }
    ret.push_back(make_pair(latitude, latvect));
  }
  return ret;
}
